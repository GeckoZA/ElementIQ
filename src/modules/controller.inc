#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "control_logic.h"

Adafruit_ADS1115 ads;
bool adsReady = false;
bool eepromReady = false;
bool settingsSaveOk = true;
SemaphoreHandle_t stateMutex = nullptr;
AsyncWebServer server(HTTP_PORT);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

using Mode = elementiq::Mode;
constexpr Mode MODE_OFF = Mode::off;
constexpr Mode MODE_AUTO = Mode::automatic;
constexpr Mode MODE_BOOST = Mode::boost;
enum Preset : uint8_t { PRESET_CUSTOM = 0, PRESET_HOME, PRESET_AWAY, PRESET_ECO };
const char* MODE_NAMES[] = { "off", "auto", "boost" };
const char* PRESET_NAMES[] = { "custom", "home", "away", "eco" };

float currentTemp = NAN;
float setpoint = DEFAULT_SETPOINT;
float boostTemp = DEFAULT_BOOST_TEMP;
float hysteresis = DEFAULT_HYSTERESIS;
float homeTemp = DEFAULT_HOME_TEMP;
float awayTemp = DEFAULT_AWAY_TEMP;
float ecoTemp = DEFAULT_ECO_TEMP;
float boostMinTemp = DEFAULT_BOOST_MIN; // 0 = disabled
uint32_t minOnMs = DEFAULT_MIN_ON_MS;
uint32_t minOffMs = DEFAULT_MIN_OFF_MS;
Mode mode = MODE_AUTO;
Preset preset = PRESET_CUSTOM;
bool heaterOn = false;
bool sensorFault = false;
bool scheduleEnabled = false;
// Per-day schedule: index 0=Mon ... 6=Sun, up to SCHED_SLOTS windows each
static const uint8_t SCHED_SLOTS = 3;
struct DayWindow { uint8_t enabled; uint8_t on_h, on_m, off_h, off_m; };
DayWindow daySchedule[7][3]; // enabled=0 means empty slot
void initDefaultSchedule() {
  for (int d = 0; d < 7; d++) {
    daySchedule[d][0] = {1, 5, 0, 22, 0};
    for (int s = 1; s < 3; s++) daySchedule[d][s] = {0, 0, 0, 0, 0};
  }
}

unsigned long lastRead = 0, heaterChangedAt = 0, boostStartedAt = 0;
unsigned long lastMqttAttempt = 0, lastDiscovery = 0, lastLedCheck = 0;
unsigned long lastAdsRetry = 0;
unsigned long badSensorSince = 0;
unsigned long lastHistoryAt = 0;
unsigned long heaterOnAccumMs = 0; // session segment
unsigned long lastHeaterTick = 0;

// Runtime (seconds)
uint32_t runtimeTotalSec = 0;
uint32_t runtimeTodaySec = 0;
uint32_t runtimeWeekSec = 0;
int lastDayOfYear = -1;
int lastWeekNum = -1;

// Temperature history ring buffer
float histTemp[HISTORY_POINTS];
uint32_t histEpoch[HISTORY_POINTS];
uint8_t histHead = 0, histCount = 0;

char mqtt_host[40] = MQTT_HOST_DEFAULT;
char mqtt_port[6] = "1883";
char mqtt_user[32] = MQTT_USER_DEFAULT;
char mqtt_pass[32] = MQTT_PASS_DEFAULT;
bool hideAp = false, apEnabled = true, mqttEnabled = true;
bool pendingMqttReconnect = false;
bool pendingStatePublish = false;
bool pendingApReconfigure = false;
bool pendingTimeReconfigure = false;
bool otaInProgress = false;
bool otaBusy = false;
bool arduinoOtaReady = false;
bool pendingFactoryReset = false;
bool pendingWifiReset = false;
char apName[32] = AP_NAME;
char friendlyName[40] = "ElementIQ";
char apPass[32] = AP_PASSWORD;
uint8_t themeId = 0;
int8_t tzOffsetHours = 2;
String timeStr = "--:--";
char wifi_ssid[33] = "";
char wifi_pass[65] = "";
char githubOwner[40] = GITHUB_OWNER;
char githubRepo[40] = GITHUB_REPO;
bool otaAutoCheck = false;
String lastOtaMsg = "Idle";
String deviceId, topicBase, topicState, topicCmd, topicAvail;
bool mqttIsConnected = false;

class StateGuard {
 public:
  StateGuard() : locked(stateMutex &&
      xSemaphoreTakeRecursive(stateMutex, portMAX_DELAY) == pdTRUE) {}
  ~StateGuard() {
    if (locked) xSemaphoreGiveRecursive(stateMutex);
  }
  StateGuard(const StateGuard&) = delete;
  StateGuard& operator=(const StateGuard&) = delete;
 private:
  bool locked;
};

bool initializeStateMutex() {
  stateMutex = xSemaphoreCreateRecursiveMutex();
  return stateMutex != nullptr;
}

struct Settings {
  float setpoint; uint8_t mode;
  char mqtt_host[40]; char mqtt_port[6]; char mqtt_user[32]; char mqtt_pass[32];
  char wifi_ssid[33]; char wifi_pass[65];
  uint8_t hide_ap, ap_enabled;
  char ap_name[32]; char ap_pass[32];
  uint8_t mqtt_enabled; int8_t tz_offset_hours; uint8_t theme;
  char friendly_name[40];
  float boost_temp, hysteresis;
  uint32_t min_on_ms, min_off_ms;
  char github_owner[40]; char github_repo[40]; uint8_t ota_auto;
  float home_temp, away_temp, eco_temp, boost_min_temp;
  uint8_t preset;
  uint8_t schedule_enabled;
  uint8_t sched_on_h, sched_on_m, sched_off_h, sched_off_m; // legacy v1
  uint8_t day_enabled[7];
  uint8_t day_on_h[7], day_on_m[7], day_off_h[7], day_off_m[7]; // legacy v2 single slot
  // v3: up to 3 slots/day
  uint8_t day_en3[7][3];
  uint8_t day_on_h3[7][3], day_on_m3[7][3], day_off_h3[7][3], day_off_m3[7][3];
  uint32_t runtime_total;
  uint32_t magic;
  uint32_t schema_tag;
  uint16_t schema_version;
  uint16_t reserved;
  uint32_t checksum;
};
#define EEPROM_MAGIC 0x47455958
#define EEPROM_SIZE sizeof(Settings)
constexpr uint32_t SETTINGS_SCHEMA_TAG = 0x45514951;
constexpr uint16_t SETTINGS_SCHEMA_VERSION = 3;
static_assert(offsetof(Settings, schema_tag) == offsetof(Settings, magic) + sizeof(uint32_t),
              "Settings schema fields must remain appended after the legacy layout");

bool saveSettings();
void setHeater(bool on, bool force = false);
void setMode(Mode m);
void publishState();
void factoryReset();

uint32_t settingsChecksum(const Settings& settings) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&settings);
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < offsetof(Settings, checksum); i++) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
  }
  return ~crc;
}

bool tempValid(float t) {
  return elementiq::temperatureValid(t, TEMP_VALID_MIN, TEMP_VALID_MAX);
}

void loadSettings() {
  eepromReady = EEPROM.begin(EEPROM_SIZE);
  if (!eepromReady) {
    Serial.println("[EEPROM] init failed; using defaults");
    initDefaultSchedule();
    return;
  }
  settingsSaveOk = true;
  Settings s;
  EEPROM.get(0, s);
  if (s.magic != EEPROM_MAGIC) { initDefaultSchedule(); return; }
  bool needsMigration = false;
  if (s.schema_tag == SETTINGS_SCHEMA_TAG) {
    if (s.schema_version == 1 || s.schema_version == 2) {
      needsMigration = true; // upgrade schedule layout
    } else if (s.schema_version != SETTINGS_SCHEMA_VERSION ||
               s.checksum != settingsChecksum(s)) {
      Serial.println("[EEPROM] unsupported or corrupt settings; using defaults");
      settingsSaveOk = false;
      initDefaultSchedule();
      return;
    }
  } else if (s.schema_tag == 0 || s.schema_tag == UINT32_MAX) {
    needsMigration = true;
  } else {
    Serial.println("[EEPROM] unknown settings format; using defaults");
    settingsSaveOk = false;
    return;
  }
  if (!isfinite(s.setpoint) || s.setpoint < MIN_SETPOINT ||
      s.setpoint > MAX_SETPOINT || s.mode > static_cast<uint8_t>(MODE_BOOST)) {
    Serial.println("[EEPROM] invalid setpoint or mode; using defaults");
    initDefaultSchedule();
    return;
  }
  setpoint = s.setpoint;
  mode = (Mode)s.mode;
  strncpy(mqtt_host, s.mqtt_host, sizeof(mqtt_host)-1);
  strncpy(mqtt_port, s.mqtt_port, sizeof(mqtt_port)-1);
  strncpy(mqtt_user, s.mqtt_user, sizeof(mqtt_user)-1);
  strncpy(mqtt_pass, s.mqtt_pass, sizeof(mqtt_pass)-1);
  strncpy(wifi_ssid, s.wifi_ssid, sizeof(wifi_ssid)-1);
  strncpy(wifi_pass, s.wifi_pass, sizeof(wifi_pass)-1);
  hideAp = s.hide_ap != 0;
  apEnabled = s.ap_enabled != 0;
  if (s.ap_name[0]) strncpy(apName, s.ap_name, sizeof(apName)-1);
  if (s.ap_pass[0]) {
    size_t passwordLength = strnlen(s.ap_pass, sizeof(s.ap_pass));
    if (passwordLength >= 8 && passwordLength < sizeof(apPass))
      strncpy(apPass, s.ap_pass, sizeof(apPass)-1);
    else {
      apPass[0] = '\0';
      Serial.println("[EEPROM] invalid AP password cleared");
    }
  }
  mqttEnabled = s.mqtt_enabled != 0;
  tzOffsetHours = constrain(s.tz_offset_hours, -12, 14);
  themeId = (s.theme <= 2) ? s.theme : 0;
  if (s.friendly_name[0]) strncpy(friendlyName, s.friendly_name, sizeof(friendlyName)-1);
  if (s.boost_temp >= MIN_SETPOINT && s.boost_temp <= MAX_SETPOINT) boostTemp = s.boost_temp;
  if (s.hysteresis >= 0.5f && s.hysteresis <= 20.0f) hysteresis = s.hysteresis;
  if (s.min_on_ms >= 5000 && s.min_on_ms <= 600000) minOnMs = s.min_on_ms;
  if (s.min_off_ms >= 5000 && s.min_off_ms <= 600000) minOffMs = s.min_off_ms;
  if (s.github_owner[0]) strncpy(githubOwner, s.github_owner, sizeof(githubOwner)-1);
  if (s.github_repo[0]) strncpy(githubRepo, s.github_repo, sizeof(githubRepo)-1);
  otaAutoCheck = s.ota_auto != 0;
  if (s.home_temp >= MIN_SETPOINT && s.home_temp <= MAX_SETPOINT) homeTemp = s.home_temp;
  if (s.away_temp >= MIN_SETPOINT && s.away_temp <= MAX_SETPOINT) awayTemp = s.away_temp;
  if (s.eco_temp >= MIN_SETPOINT && s.eco_temp <= MAX_SETPOINT) ecoTemp = s.eco_temp;
  if (s.boost_min_temp >= 0 && s.boost_min_temp <= MAX_SETPOINT) boostMinTemp = s.boost_min_temp;
  if (s.preset <= PRESET_ECO) preset = (Preset)s.preset;
  scheduleEnabled = s.schedule_enabled != 0;
  initDefaultSchedule();
  bool anySlot = false;
  // v3 multi-slot
  for (int d = 0; d < 7; d++) {
    for (int sidx = 0; sidx < 3; sidx++) {
      if (s.day_en3[d][sidx] || (s.day_on_h3[d][sidx] || s.day_off_h3[d][sidx])) {
        daySchedule[d][sidx].enabled = s.day_en3[d][sidx] ? 1 : 0;
        daySchedule[d][sidx].on_h = s.day_on_h3[d][sidx] < 24 ? s.day_on_h3[d][sidx] : 0;
        daySchedule[d][sidx].on_m = s.day_on_m3[d][sidx] < 60 ? s.day_on_m3[d][sidx] : 0;
        daySchedule[d][sidx].off_h = s.day_off_h3[d][sidx] < 24 ? s.day_off_h3[d][sidx] : 0;
        daySchedule[d][sidx].off_m = s.day_off_m3[d][sidx] < 60 ? s.day_off_m3[d][sidx] : 0;
        if (daySchedule[d][sidx].enabled) anySlot = true;
      }
    }
  }
  // v2 single slot migration
  if (!anySlot) {
    for (int i = 0; i < 7; i++) {
      if (s.day_on_h[i] < 24 && s.day_off_h[i] < 24) {
        daySchedule[i][0] = {
          (uint8_t)(s.day_enabled[i] ? 1 : 0),
          s.day_on_h[i],
          (uint8_t)(s.day_on_m[i] < 60 ? s.day_on_m[i] : 0),
          s.day_off_h[i],
          (uint8_t)(s.day_off_m[i] < 60 ? s.day_off_m[i] : 0)
        };
        anySlot = true;
      }
    }
  }
  // v1 single global window
  if (!anySlot) {
    uint8_t oh = s.sched_on_h < 24 ? s.sched_on_h : 5;
    uint8_t om = s.sched_on_m < 60 ? s.sched_on_m : 0;
    uint8_t fh = s.sched_off_h < 24 ? s.sched_off_h : 22;
    uint8_t fm = s.sched_off_m < 60 ? s.sched_off_m : 0;
    for (int i = 0; i < 7; i++) daySchedule[i][0] = {1, oh, om, fh, fm};
  }
  runtimeTotalSec = s.runtime_total;
  char *portEnd = nullptr;
  unsigned long portValue = strtoul(mqtt_port, &portEnd, 10);
  if (portEnd == mqtt_port || *portEnd != '\0' || portValue < 1 || portValue > 65535)
    strncpy(mqtt_port, "1883", sizeof(mqtt_port)-1);
  if (hideAp) apEnabled = false;
  Serial.printf("[EEPROM] set=%.1f boost=%.1f mode=%s sched=%d\n",
                setpoint, boostTemp, MODE_NAMES[static_cast<uint8_t>(mode)], scheduleEnabled);
  if (needsMigration) {
    Serial.println("[EEPROM] migrating legacy settings");
    settingsSaveOk = saveSettings();
  }
}

bool saveSettings() {
  if (!eepromReady) {
    Serial.println("[EEPROM] save skipped; storage unavailable");
    settingsSaveOk = false;
    return false;
  }
  Settings s;
  memset(&s, 0, sizeof(s));
  s.setpoint = setpoint; s.mode = static_cast<uint8_t>(mode);
  strncpy(s.mqtt_host, mqtt_host, sizeof(s.mqtt_host)-1);
  strncpy(s.mqtt_port, mqtt_port, sizeof(s.mqtt_port)-1);
  strncpy(s.mqtt_user, mqtt_user, sizeof(s.mqtt_user)-1);
  strncpy(s.mqtt_pass, mqtt_pass, sizeof(s.mqtt_pass)-1);
  strncpy(s.wifi_ssid, wifi_ssid, sizeof(s.wifi_ssid)-1);
  strncpy(s.wifi_pass, wifi_pass, sizeof(s.wifi_pass)-1);
  s.hide_ap = hideAp ? 1 : 0; s.ap_enabled = apEnabled ? 1 : 0;
  strncpy(s.ap_name, apName, sizeof(s.ap_name)-1);
  strncpy(s.ap_pass, apPass, sizeof(s.ap_pass)-1);
  s.mqtt_enabled = mqttEnabled ? 1 : 0;
  s.tz_offset_hours = tzOffsetHours; s.theme = themeId;
  strncpy(s.friendly_name, friendlyName, sizeof(s.friendly_name)-1);
  s.boost_temp = boostTemp; s.hysteresis = hysteresis;
  s.min_on_ms = minOnMs; s.min_off_ms = minOffMs;
  strncpy(s.github_owner, githubOwner, sizeof(s.github_owner)-1);
  strncpy(s.github_repo, githubRepo, sizeof(s.github_repo)-1);
  s.ota_auto = otaAutoCheck ? 1 : 0;
  s.home_temp = homeTemp; s.away_temp = awayTemp; s.eco_temp = ecoTemp;
  s.boost_min_temp = boostMinTemp; s.preset = preset;
  s.schedule_enabled = scheduleEnabled ? 1 : 0;
  s.sched_on_h = daySchedule[0][0].on_h; s.sched_on_m = daySchedule[0][0].on_m;
  s.sched_off_h = daySchedule[0][0].off_h; s.sched_off_m = daySchedule[0][0].off_m;
  for (int i = 0; i < 7; i++) {
    s.day_enabled[i] = daySchedule[i][0].enabled ? 1 : 0;
    s.day_on_h[i] = daySchedule[i][0].on_h;
    s.day_on_m[i] = daySchedule[i][0].on_m;
    s.day_off_h[i] = daySchedule[i][0].off_h;
    s.day_off_m[i] = daySchedule[i][0].off_m;
    for (int sidx = 0; sidx < 3; sidx++) {
      s.day_en3[i][sidx] = daySchedule[i][sidx].enabled ? 1 : 0;
      s.day_on_h3[i][sidx] = daySchedule[i][sidx].on_h;
      s.day_on_m3[i][sidx] = daySchedule[i][sidx].on_m;
      s.day_off_h3[i][sidx] = daySchedule[i][sidx].off_h;
      s.day_off_m3[i][sidx] = daySchedule[i][sidx].off_m;
    }
  }
  s.runtime_total = runtimeTotalSec;
  s.magic = EEPROM_MAGIC;
  s.schema_tag = SETTINGS_SCHEMA_TAG;
  s.schema_version = SETTINGS_SCHEMA_VERSION;
  s.checksum = settingsChecksum(s);
  EEPROM.put(0, s);
  settingsSaveOk = EEPROM.commit();
  if (!settingsSaveOk) Serial.println("[EEPROM] commit failed");
  return settingsSaveOk;
}

void factoryReset() {
  Serial.println("[RESET] Factory reset");
  setHeater(false, true);
  if (!eepromReady) eepromReady = EEPROM.begin(EEPROM_SIZE);
  if (eepromReady) {
    Settings blank; memset(&blank, 0, sizeof(blank));
    EEPROM.put(0, blank);
    if (!EEPROM.commit()) {
      settingsSaveOk = false;
      Serial.println("[EEPROM] reset commit failed");
    } else {
      settingsSaveOk = true;
    }
  } else {
    settingsSaveOk = false;
    Serial.println("[EEPROM] reset skipped; storage unavailable");
  }
  WiFi.disconnect(true);
  delay(300);
  ESP.restart();
}

// Calibration table: raw NTC °C → true (calibrated probe) °C
// Interpolated from your probe comparison
// Measured probe pairs + high-end extension (40–100 °C focus)
// Above 75 °C: extrapolated from last measured segment — re-check with probe at 85/95 if possible
static const float CAL_NTC[CAL_POINTS] = {
  26.0f, 28.7f, 38.2f, 42.9f, 49.6f, 53.2f, 60.2f, 66.5f, 71.9f, 78.0f,
  85.0f, 92.0f, 99.0f, 106.0f, 113.0f
};
static const float CAL_TRUE[CAL_POINTS] = {
  26.8f, 30.0f, 40.0f, 45.0f, 50.0f, 55.0f, 60.0f, 65.0f, 70.0f, 75.0f,
  80.7f, 86.5f, 92.2f, 98.0f, 103.7f
};

float applyCalibration(float ntcC) {
  if (isnan(ntcC)) return NAN;
  // Below first point: linear extend using first segment
  if (ntcC <= CAL_NTC[0]) {
    float dx = CAL_NTC[1] - CAL_NTC[0];
    if (fabsf(dx) < 1e-6f) return CAL_TRUE[0];
    float t = (ntcC - CAL_NTC[0]) / dx;
    return CAL_TRUE[0] + t * (CAL_TRUE[1] - CAL_TRUE[0]);
  }
  for (int i = 0; i < CAL_POINTS - 1; i++) {
    if (ntcC <= CAL_NTC[i + 1]) {
      float dx = CAL_NTC[i + 1] - CAL_NTC[i];
      float t = (ntcC - CAL_NTC[i]) / dx;
      return CAL_TRUE[i] + t * (CAL_TRUE[i + 1] - CAL_TRUE[i]);
    }
  }
  // Above last point: extend last segment
  int i = CAL_POINTS - 2;
  float dx = CAL_NTC[i + 1] - CAL_NTC[i];
  float t = (ntcC - CAL_NTC[i]) / dx;
  return CAL_TRUE[i] + t * (CAL_TRUE[i + 1] - CAL_TRUE[i]);
}

float readTemperatureRaw() {
  if (!adsReady) return NAN;
  // 8 samples — solar can push >75 °C; more averaging stabilises high end
  int32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += ads.readADC_SingleEnded(0);
    delay(2);
  }
  float adc = sum / 8.0f;
  float voltage = adc * 0.125f / 1000.0f;
  // Allow closer to 3.3 V so ~100 °C remains measurable
  if (voltage < 0.05f || voltage > 3.28f) return NAN;
  float rNtc = FIXED_RESISTOR * ((VCC - voltage) / voltage);
  float steinhart = logf(rNtc / NTC_R25) / NTC_BETA;
  steinhart += 1.0f / 298.15f;
  return (1.0f / steinhart) - 273.15f;
}

bool initializeAds() {
  if (!ads.begin(0x48)) return false;
  ads.setGain(GAIN_ONE);
  return true;
}

void retryAdsInitialization() {
  if (adsReady || millis() - lastAdsRetry < 5000) return;
  lastAdsRetry = millis();
  adsReady = initializeAds();
  if (adsReady) Serial.println("[ADS] recovered");
}

static float filteredTemp = NAN;

float readTemperature() {
  float raw = readTemperatureRaw();
  if (isnan(raw)) {
    filteredTemp = NAN;
    return NAN;
  }
  float cal = applyCalibration(raw);

  // Spike reject: ignore one-off jumps far from filter (ADC glitch)
  if (!isnan(filteredTemp) && fabsf(cal - filteredTemp) > TEMP_JUMP_REJECT) {
    // Soften: pull filter halfway toward spike once, don't trust full jump
    cal = filteredTemp + 0.3f * (cal - filteredTemp);
  }

  // Exponential moving average
  if (isnan(filteredTemp)) {
    filteredTemp = cal;
  } else {
    filteredTemp = TEMP_EMA_ALPHA * cal + (1.0f - TEMP_EMA_ALPHA) * filteredTemp;
  }
  return filteredTemp;
}

void updateRuntimeTick() {
  static uint32_t sinceSave = 0;
  unsigned long now = millis();
  if (heaterOn) {
    if (lastHeaterTick == 0) lastHeaterTick = now;
    uint32_t d = now - lastHeaterTick;
    lastHeaterTick = now;
    heaterOnAccumMs += d;
    uint32_t sec = heaterOnAccumMs / 1000;
    heaterOnAccumMs %= 1000;
    if (sec > 0) {
      runtimeTotalSec += sec;
      runtimeTodaySec += sec;
      runtimeWeekSec += sec;
      // persist total occasionally (~every 60s of heater time)
      sinceSave += sec;
      if (sinceSave >= 60) { sinceSave = 0; saveSettings(); }
    }
  } else {
    lastHeaterTick = 0;
    heaterOnAccumMs = 0;
  }
}

void updateDayWeekCounters() {
  time_t now = time(nullptr);
  if (now < 100000) return;
  struct tm t;
  if (!localtime_r(&now, &t)) return;
  int doy = t.tm_yday;
  int w = t.tm_yday / 7;
  if (lastDayOfYear >= 0 && doy != lastDayOfYear) runtimeTodaySec = 0;
  if (lastWeekNum >= 0 && w != lastWeekNum) runtimeWeekSec = 0;
  lastDayOfYear = doy;
  lastWeekNum = w;
}

bool scheduleAllowsHeat() {
  if (!scheduleEnabled) return true;
  time_t now = time(nullptr);
  if (now < 100000) return true; // no time → allow
  struct tm t;
  if (!localtime_r(&now, &t)) return true;
  // tm_wday: 0=Sun..6=Sat → our index 0=Mon..6=Sun
  int idx = (t.tm_wday == 0) ? 6 : (t.tm_wday - 1);
  if (idx < 0 || idx > 6) return true;
  int nowM = t.tm_hour * 60 + t.tm_min;
  bool anyEnabled = false;
  for (int sidx = 0; sidx < 3; sidx++) {
    const DayWindow& d = daySchedule[idx][sidx];
    if (!d.enabled) continue;
    anyEnabled = true;
    int onM = d.on_h * 60 + d.on_m;
    int offM = d.off_h * 60 + d.off_m;
    if (onM == offM) return true; // all day
    if (onM < offM) {
      if (nowM >= onM && nowM < offM) return true;
    } else {
      if (nowM >= onM || nowM < offM) return true; // overnight
    }
  }
  // no enabled slots for today → block; if had enabled but none matched → block
  return !anyEnabled ? false : false;
}

void setHeater(bool on, bool force) {
  if (on) {
    // Never turn on with bad sensor
    if (!tempValid(currentTemp) || sensorFault) {
      Serial.println("[HEATER] blocked – sensor fault/invalid");
      on = false;
      force = true;
    }
  }
  if (on == heaterOn) return;
  if (!force) {
    unsigned long elapsed = millis() - heaterChangedAt;
    if (on && elapsed < minOffMs) return;
    if (!on && elapsed < minOnMs) return;
  }
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
  heaterOn = on;
  heaterChangedAt = millis();
  lastHeaterTick = on ? millis() : 0;
  if (on) heaterOnAccumMs = 0;
  Serial.printf("[HEATER] %s%s\n", on ? "ON" : "OFF", force ? " (forced)" : "");
}

void updateThermostat() {
  if (otaInProgress) {
    setHeater(false, true);
    return;
  }
  bool sampleValid = tempValid(currentTemp);
  if (!sampleValid) {
    if (badSensorSince == 0) badSensorSince = millis();
  } else {
    badSensorSince = 0;
  }

  bool nextSensorFault = elementiq::updateSensorFault(
      sensorFault, sampleValid,
      sampleValid ? 0 : millis() - badSensorSince, SENSOR_FAULT_MS);
  if (nextSensorFault != sensorFault) {
    sensorFault = nextSensorFault;
    if (sensorFault) {
      Serial.println("[SENSOR] FAULT – heater forced off");
      setHeater(false, true);
      publishState();
    } else {
      Serial.println("[SENSOR] recovered");
    }
  }

  if (mode == MODE_BOOST) {
    Mode nextMode = elementiq::advanceBoostMode(
        mode, sampleValid, currentTemp, boostTemp,
        millis() - boostStartedAt, BOOST_SAFETY_MS);
    if (nextMode != mode) {
      bool reachedTarget = sampleValid && currentTemp >= boostTemp;
      mode = nextMode;
      saveSettings();
      if (reachedTarget) {
        setHeater(false);
        Serial.println("[MODE] BOOST done → AUTO");
      } else {
        Serial.println("[MODE] BOOST safety → AUTO");
      }
    }
  }

  float target = (mode == MODE_BOOST) ? boostTemp : setpoint;
  elementiq::HeaterDecision decision = elementiq::evaluateThermostat(
      mode, heaterOn, sampleValid, sensorFault, currentTemp, target,
      hysteresis, scheduleAllowsHeat());
  if (decision.forceOff) setHeater(false, true);
  else setHeater(decision.requestedOn);
}

void setMode(Mode m) {
  if (m == MODE_BOOST) {
    // Soft min boost: only if below threshold (when threshold > 0)
    if (boostMinTemp > 0.5f && tempValid(currentTemp) && currentTemp >= boostMinTemp) {
      Serial.printf("[MODE] Boost skipped – temp %.1f >= min %.1f\n", currentTemp, boostMinTemp);
      return;
    }
    if (!tempValid(currentTemp) || sensorFault) {
      Serial.println("[MODE] Boost blocked – sensor");
      return;
    }
  }
  if (m == mode) return;
  mode = m;
  if (m == MODE_BOOST) boostStartedAt = millis();
  saveSettings();
  Serial.printf("[MODE] → %s\n", MODE_NAMES[static_cast<uint8_t>(mode)]);
  if (m == MODE_OFF) setHeater(false, true);
}

void applyPreset(Preset p) {
  preset = p;
  if (p == PRESET_HOME) setpoint = homeTemp;
  else if (p == PRESET_AWAY) setpoint = awayTemp;
  else if (p == PRESET_ECO) setpoint = ecoTemp;
  saveSettings();
}

void pushHistory(float t) {
  if (!tempValid(t)) return;
  time_t now = time(nullptr);
  histTemp[histHead] = t;
  histEpoch[histHead] = (now > 100000) ? (uint32_t)now : (millis() / 1000);
  histHead = (histHead + 1) % HISTORY_POINTS;
  if (histCount < HISTORY_POINTS) histCount++;
}

void updateWifiLed() {
  digitalWrite(LED_PIN, (WiFi.status() == WL_CONNECTED) ? LOW : HIGH);
}

void setupMDNS() {
  WiFi.setHostname("ElementIQ");
  if (MDNS.begin("ElementIQ")) MDNS.addService("http", "tcp", 80);
}

void setupNTP() {
  int8_t timezone;
  {
    StateGuard guard;
    timezone = tzOffsetHours;
  }
  configTime(timezone * 3600, 0, "pool.ntp.org", "time.nist.gov");
}

void updateTimeString() {
  time_t now = time(nullptr);
  if (now < 100000) { timeStr = "--:--"; return; }
  struct tm t;
  if (!localtime_r(&now, &t)) { timeStr = "--:--"; return; }
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  timeStr = buf;
}

