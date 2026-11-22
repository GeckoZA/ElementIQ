#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "control_logic.h"
#include <esp_system.h>
#if __has_include(<esp_random.h>)
#include <esp_random.h>
#endif
#include <mbedtls/md.h>
#include <Preferences.h>

// Implemented in system.inc (included later in the same translation unit)
void safeRestart();
void otaGuardArm();

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
Mode mode = MODE_OFF;            // safe default: never heat until configured / user chooses Auto
Preset preset = PRESET_CUSTOM;
bool heaterOn = false;
bool sensorFault = false;
enum SensorFaultCode : uint8_t {
  SF_OK = 0, SF_ADS_MISSING, SF_OPEN, SF_SHORT, SF_STUCK,
  SF_JUMP, SF_OUT_OF_RANGE, SF_INVALID
};
SensorFaultCode sensorFaultCode = SF_OK;
const char* sensorFaultName() {
  switch (sensorFaultCode) {
    case SF_ADS_MISSING: return "ads_missing";
    case SF_OPEN: return "open_circuit";
    case SF_SHORT: return "short_circuit";
    case SF_STUCK: return "stuck_reading";
    case SF_JUMP: return "implausible_jump";
    case SF_OUT_OF_RANGE: return "out_of_range";
    case SF_INVALID: return "invalid";
    default: return "ok";
  }
}
int16_t lastAdcRaw = 0;
uint8_t stuckAdcCount = 0;
float lastRawTempSample = NAN;

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

// Runtime (seconds) — persisted on LittleFS (survives reboot + OTA)
uint32_t runtimeTotalSec = 0;
uint32_t runtimeTodaySec = 0;
uint32_t runtimeWeekSec = 0;
uint32_t runtimeMonthSec = 0;
int lastDayOfYear = -1;
int lastWeekNum = -1;
int lastMonthNum = -1;
int lastYearNum = -1;

// Temperature history ring buffer (7 days @ 5 min), packed + persisted
struct HistSample {
  uint32_t epoch;
  int16_t temp10;  // °C * 10
};
static_assert(sizeof(HistSample) == 8 || sizeof(HistSample) == 6 || sizeof(HistSample) >= 6,
              "HistSample size unexpected");
HistSample histBuf[HISTORY_POINTS];
uint16_t histHead = 0, histCount = 0;
uint8_t histDirtyCount = 0;  // samples since last flash save

char mqtt_host[40] = MQTT_HOST_DEFAULT;
char mqtt_port[6] = "1883";
char mqtt_user[32] = MQTT_USER_DEFAULT;
char mqtt_pass[32] = MQTT_PASS_DEFAULT;
bool hideAp = false, apEnabled = true, mqttEnabled = true;
bool webAuthEnabled = false;
char webAuthUser[24] = WEB_AUTH_USER_DEFAULT;
// Admin password is never stored in clear: 8-byte random salt + iterated SHA-256 (hex).
char webAuthSalt[17] = "";
char webAuthHash[65] = "";
bool setupDone = false;          // first-time setup wizard completed
bool setupPortalHold = false;    // keep captive portal routes open after STA join (wizard success page)
bool overTempFault = false;      // software hard trip (latched)
bool otaRebootPending = false;   // image written, waiting to restart: heater stays off
bool otaPendingVerify = false;   // freshly updated image not yet confirmed healthy

bool pendingMqttReconnect = false;
bool pendingStatePublish = false;
bool pendingApReconfigure = false;
bool pendingTimeReconfigure = false;
bool otaInProgress = false;
bool otaBusy = false;
int otaProgressPercent = -1;  // -1 = idle/unknown, 0-100 during transfer
bool arduinoOtaReady = false;
bool pendingFactoryReset = false;
bool pendingReboot = false;
bool pendingWifiReset = false;
char apName[32] = AP_NAME;
char friendlyName[40] = "ElementIQ";
char apPass[32] = AP_PASSWORD;
uint8_t themeId = 0;
int8_t tzOffsetHours = 2;
String timeStr = "--:--";
extern char deviceSerialBuf[24];
extern const char* deviceSerial;
bool elementFault = false;
unsigned long elementHeatStartedAt = 0;
float elementHeatStartTemp = NAN;


char wifi_ssid[33] = "";
char wifi_pass[65] = "";
char githubOwner[40] = GITHUB_OWNER;
char githubRepo[40] = GITHUB_REPO;
bool otaAutoCheck = false;
String lastOtaMsg = "Idle";

// ---- Web console log (ring buffer, also mirrors to Serial) ----
#ifndef EQ_LOG_LINES
#define EQ_LOG_LINES 100
#endif
#ifndef EQ_LOG_LINE_LEN
#define EQ_LOG_LINE_LEN 160
#endif
static char eqLogLines[EQ_LOG_LINES][EQ_LOG_LINE_LEN];
static uint16_t eqLogHead = 0;   // next write index
static uint16_t eqLogCount = 0;
static uint32_t eqLogSeq = 0;    // monotonic sequence for clients

void eqLog(const char* line) {
  if (!line) return;
  // Copy into ring (truncate, strip trailing CR/LF)
  char* slot = eqLogLines[eqLogHead];
  size_t n = 0;
  while (line[n] && line[n] != '\r' && line[n] != '\n' && n < EQ_LOG_LINE_LEN - 1) {
    slot[n] = line[n];
    n++;
  }
  slot[n] = 0;
  eqLogHead = (eqLogHead + 1) % EQ_LOG_LINES;
  if (eqLogCount < EQ_LOG_LINES) eqLogCount++;
  eqLogSeq++;
  Serial.println(slot);
}

void eqLog(const String& line) {
  eqLog(line.c_str());
}

void eqLogf(const char* fmt, ...) {
  char buf[EQ_LOG_LINE_LEN];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  eqLog(buf);
}

// Fill JSON array with lines newer than sinceSeq; updates *outSeq to latest
void eqLogExportSince(JsonArray arr, uint32_t sinceSeq, uint32_t* outSeq) {
  if (outSeq) *outSeq = eqLogSeq;
  if (eqLogCount == 0) return;
  uint32_t firstSeq = eqLogSeq - eqLogCount + 1;
  if (sinceSeq < firstSeq) sinceSeq = firstSeq - 1;
  for (uint32_t s = sinceSeq + 1; s <= eqLogSeq; s++) {
    uint16_t idx = (uint16_t)((eqLogHead + EQ_LOG_LINES - (eqLogSeq - s) - 1) % EQ_LOG_LINES);
    // idx: sequence s is at position relative to head
    uint32_t age = eqLogSeq - s; // 0 = newest
    idx = (uint16_t)((eqLogHead + EQ_LOG_LINES - 1 - age) % EQ_LOG_LINES);
    arr.add(eqLogLines[idx]);
  }
}

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
const char* heaterInterlock();
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


// ---- Admin password: salted, iterated SHA-256 ----
static void toHex(const uint8_t* in, size_t n, char* out) {
  static const char* H = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[i*2] = H[in[i] >> 4]; out[i*2+1] = H[in[i] & 15]; }
  out[n*2] = 0;
}

static void hashPassword(const char* salt, const char* pass, char outHex[65]) {
  uint8_t buf[32 + 16 + 64];
  uint8_t h[32] = {0};
  size_t sl = strnlen(salt, 16), pl = strnlen(pass, 63);
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  for (int i = 0; i < 2000; i++) {
    memcpy(buf, h, 32);
    memcpy(buf + 32, salt, sl);
    memcpy(buf + 32 + sl, pass, pl);
    mbedtls_md(md, buf, 32 + sl + pl, h);
  }
  toHex(h, 32, outHex);
}

static bool constTimeEq(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  uint8_t d = (la != lb);
  size_t n = la < lb ? la : lb;
  for (size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0;
}

bool adminPasswordSet() { return webAuthHash[0] != 0; }

bool verifyAdminLogin(const char* user, const char* pass) {
  if (!adminPasswordSet()) return false;
  char h[65];
  hashPassword(webAuthSalt, pass ? pass : "", h);
  bool userOk = constTimeEq(user ? user : "", webAuthUser);
  bool passOk = constTimeEq(h, webAuthHash);
  return userOk && passOk;
}

void setAdminPassword(const char* pass) {
  uint8_t r[8];
  esp_fill_random(r, sizeof(r));
  toHex(r, sizeof(r), webAuthSalt);
  hashPassword(webAuthSalt, pass, webAuthHash);
}

void saveWebAuth() {
  File f = LittleFS.open("/webauth.txt", "w");
  if (!f) return;
  // v2 format: 2|user|salt|hash
  f.printf("2|%s|%s|%s", webAuthUser, webAuthSalt, webAuthHash);
  f.close();
}

void loadWebAuth() {
  if (!LittleFS.exists("/webauth.txt")) return;
  File f = LittleFS.open("/webauth.txt", "r");
  if (!f) return;
  String line = f.readString();
  f.close();
  line.trim();
  if (line.startsWith("2|")) {
    int p1 = line.indexOf('|', 2), p2 = line.indexOf('|', p1 + 1);
    if (p1 < 0 || p2 < 0) return;
    String u = line.substring(2, p1), salt = line.substring(p1 + 1, p2), hash = line.substring(p2 + 1);
    if (u.length() > 0 && u.length() < (int)sizeof(webAuthUser)) strlcpy(webAuthUser, u.c_str(), sizeof(webAuthUser));
    if (salt.length() == 16 && hash.length() == 64) {
      strlcpy(webAuthSalt, salt.c_str(), sizeof(webAuthSalt));
      strlcpy(webAuthHash, hash.c_str(), sizeof(webAuthHash));
    }
  } else {
    // Legacy v1 "enabled|user|plaintext": migrate to a hash and rewrite the file.
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    if (p1 < 0 || p2 < 0) return;
    bool wasEnabled = line.substring(0, p1).toInt() != 0;
    String u = line.substring(p1 + 1, p2), p = line.substring(p2 + 1);
    u.trim(); p.trim();
    if (u.length() > 0 && u.length() < (int)sizeof(webAuthUser)) strlcpy(webAuthUser, u.c_str(), sizeof(webAuthUser));
    if (wasEnabled && p.length() > 0 && p.length() < 32) {
      setAdminPassword(p.c_str());
      saveWebAuth();
      eqLog("[AUTH] legacy plaintext password migrated to salted hash");
    } else {
      LittleFS.remove("/webauth.txt");   // auth was off: nothing to keep
    }
  }
  eqLogf("[AUTH] admin login %s user=%s\n", adminPasswordSet() ? "ON" : "not set", webAuthUser);
}

// ---- First-time setup flag (NVS, survives OTA, cleared by factory reset) ----
void loadSetupDone() {
  Preferences p;
  p.begin("eiq", false);
  bool known = p.isKey("setup");
  setupDone = p.getUChar("setup", 0) != 0;
  p.end();
  if (!known && wifi_ssid[0]) {
    // Unit configured before the wizard existed: treat as already set up.
    setupDone = true;
    Preferences w; w.begin("eiq", false); w.putUChar("setup", 1); w.end();
  }
}

void markSetupDone() {
  setupDone = true;
  Preferences p; p.begin("eiq", false); p.putUChar("setup", 1); p.end();
}

void ensureDeviceSerial() {
  // Persist S/N in LittleFS so OTA version bumps never rewrite it.
  if (LittleFS.exists("/serial.txt")) {
    File f = LittleFS.open("/serial.txt", "r");
    if (f) {
      String s = f.readString();
      f.close();
      s.trim();
      if (s.length() > 0 && s.length() < (int)sizeof(deviceSerialBuf)) {
        strncpy(deviceSerialBuf, s.c_str(), sizeof(deviceSerialBuf) - 1);
        deviceSerialBuf[sizeof(deviceSerialBuf) - 1] = 0;
        eqLogf("[SN] loaded %s\n", deviceSerialBuf);
        return;
      }
    }
  }
  strncpy(deviceSerialBuf, DEVICE_SERIAL, sizeof(deviceSerialBuf) - 1);
  deviceSerialBuf[sizeof(deviceSerialBuf) - 1] = 0;
  File f = LittleFS.open("/serial.txt", "w");
  if (f) {
    f.print(deviceSerialBuf);
    f.close();
    eqLogf("[SN] first flash stored %s\n", deviceSerialBuf);
  } else {
    eqLogf("[SN] using %s (could not persist)\n", deviceSerialBuf);
  }
}

void clearElementFault() {
  elementFault = false;
  overTempFault = false;
  elementHeatStartedAt = 0;
  elementHeatStartTemp = NAN;
}

float elementMinRiseForTemp(float t) {
  // Higher tank temp → slower rise. Linear from ELEMENT_RISE_LOW @ 40°C to ELEMENT_RISE_HIGH @ 70°C
  if (!isfinite(t)) return ELEMENT_RISE_LOW;
  float u = (t - 40.0f) / 30.0f;
  if (u < 0) u = 0;
  if (u > 1) u = 1;
  return ELEMENT_RISE_LOW + u * (ELEMENT_RISE_HIGH - ELEMENT_RISE_LOW);
}

void updateElementSafety() {
  if (!heaterOn) {
    elementHeatStartedAt = 0;
    elementHeatStartTemp = NAN;
    return;
  }
  if (!tempValid(currentTemp)) return;
  if (elementHeatStartedAt == 0) {
    elementHeatStartedAt = millis();
    elementHeatStartTemp = currentTemp;
    return;
  }
  if (millis() - elementHeatStartedAt < ELEMENT_SAFETY_MS) return;
  float rise = currentTemp - elementHeatStartTemp;
  float need = elementMinRiseForTemp(elementHeatStartTemp);
  if (rise < need) {
    elementFault = true;
    eqLogf("[ELEMENT] FAULT – rise %.2f < need %.2f over 10 min (start %.1f). Element may be bad.\n",
                  rise, need, elementHeatStartTemp);
    setHeater(false, true);
    // Hold in OFF so it does not keep retrying
    mode = MODE_OFF;
    saveSettings();
  } else {
    // Rolling window: restart baseline so long heat cycles still monitored
    elementHeatStartedAt = millis();
    elementHeatStartTemp = currentTemp;
  }
}


void loadSettings() {
  eepromReady = EEPROM.begin(EEPROM_SIZE);
  if (!eepromReady) {
    eqLog("[EEPROM] init failed; using defaults");
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
      eqLog("[EEPROM] unsupported or corrupt settings; using defaults");
      settingsSaveOk = false;
      initDefaultSchedule();
      return;
    }
  } else if (s.schema_tag == 0 || s.schema_tag == UINT32_MAX) {
    needsMigration = true;
  } else {
    eqLog("[EEPROM] unknown settings format; using defaults");
    settingsSaveOk = false;
    return;
  }
  if (!isfinite(s.setpoint) || s.setpoint < MIN_SETPOINT ||
      s.setpoint > 100.0f || s.mode > static_cast<uint8_t>(MODE_BOOST)) {
    eqLog("[EEPROM] invalid setpoint or mode; using defaults");
    initDefaultSchedule();
    return;
  }
  // MAX_SETPOINT was lowered from 100 to 80 °C: clamp old saved values instead
  // of discarding the whole record (which would also wipe Wi-Fi credentials).
  setpoint = constrain(s.setpoint, MIN_SETPOINT, MAX_SETPOINT);
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
      eqLog("[EEPROM] invalid AP password cleared");
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
  // Always use fixed repo from config.h
  strncpy(githubOwner, GITHUB_OWNER, sizeof(githubOwner)-1); githubOwner[sizeof(githubOwner)-1]=0;
  strncpy(githubRepo, GITHUB_REPO, sizeof(githubRepo)-1); githubRepo[sizeof(githubRepo)-1]=0;
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
  eqLogf("[EEPROM] set=%.1f boost=%.1f mode=%s sched=%d\n",
                setpoint, boostTemp, MODE_NAMES[static_cast<uint8_t>(mode)], scheduleEnabled);
  if (needsMigration) {
    eqLog("[EEPROM] migrating legacy settings");
    settingsSaveOk = saveSettings();
  }
}

bool saveSettings() {
  if (!eepromReady) {
    eqLog("[EEPROM] save skipped; storage unavailable");
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
  if (!settingsSaveOk) eqLog("[EEPROM] commit failed");
  return settingsSaveOk;
}

void factoryReset() {
  eqLog("[RESET] Factory reset");
  mode = MODE_OFF;                 // hard-safe default for the next boot
  setHeater(false, true);
  digitalWrite(RELAY_PIN, LOW);    // pin LOW even if setHeater short-circuits
  if (!eepromReady) eepromReady = EEPROM.begin(EEPROM_SIZE);
  if (eepromReady) {
    Settings blank; memset(&blank, 0, sizeof(blank));
    // Explicit OFF so a partially-written or legacy reader cannot leave AUTO on
    blank.mode = static_cast<uint8_t>(MODE_OFF);
    EEPROM.put(0, blank);
    if (!EEPROM.commit()) {
      settingsSaveOk = false;
      eqLog("[EEPROM] reset commit failed");
    } else {
      settingsSaveOk = true;
    }
  } else {
    settingsSaveOk = false;
    eqLog("[EEPROM] reset skipped; storage unavailable");
  }
  LittleFS.remove(HISTORY_FILE);
  LittleFS.remove(RUNTIME_FILE);
  LittleFS.remove("/webauth.txt");           // admin login must not survive a factory reset
  { Preferences p; p.begin("eiq", false); p.clear(); p.end(); }   // setup-complete flag
  // Erase stored STA credentials (second arg) so the next boot does not auto-join
  // the old network and interfere with the captive-portal AP / 192.168.4.1.
  WiFi.disconnect(true, true);
  delay(300);
  safeRestart();
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

void scanI2cBus() {
  eqLogf("[I2C] scan SDA=GPIO%d SCL=GPIO%d\n", I2C_SDA, I2C_SCL);
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      eqLogf("[I2C] device at 0x%02X\n", addr);
      found++;
    }
  }
  if (!found) eqLog("[I2C] no devices found — check wiring / pull-ups / power");
}

bool initializeAds() {
  scanI2cBus();
  // ADDR→GND = 0x48, ADDR→VDD = 0x49, ADDR→SDA = 0x4A, ADDR→SCL = 0x4B
  const uint8_t candidates[] = {0x48, 0x49, 0x4A, 0x4B};
  for (uint8_t a : candidates) {
    eqLogf("[ADS] try begin(0x%02X)...\n", a);
    if (ads.begin(a)) {
      ads.setGain(GAIN_ONE);  // ±4.096 V, 0.125 mV/LSB
      // Sanity read on A0
      int16_t sample = ads.readADC_SingleEnded(0);
      float mv = sample * 0.125f;
      eqLogf("[ADS] OK at 0x%02X  gain=ONE  A0 raw=%d (%.2f mV)\n", a, sample, mv);
      if (sample < 50) {
        eqLog("[ADS] WARN A0 near 0 — open circuit or probe shorted to GND?");
      } else if (sample > 26000) {
        eqLog("[ADS] WARN A0 near full-scale — open to VCC or wrong divider?");
      }
      return true;
    }
  }
  eqLog("[ADS] begin failed on 0x48-0x4B");
  return false;
}

void retryAdsInitialization() {
  if (adsReady || millis() - lastAdsRetry < 5000) return;
  lastAdsRetry = millis();
  eqLog("[ADS] retry init...");
  adsReady = initializeAds();
  if (adsReady) eqLog("[ADS] recovered");
  else eqLog("[ADS] still not found");
}

float readTemperatureRaw() {
  if (!adsReady) {
    sensorFaultCode = SF_ADS_MISSING;
    eqLog("[NTC] skip — ADS not ready");
    return NAN;
  }
  int32_t sum = 0;
  int16_t first = 0, last = 0;
  for (int i = 0; i < 8; i++) {
    int16_t s = ads.readADC_SingleEnded(0);
    if (i == 0) first = s;
    last = s;
    sum += s;
    delay(2);
  }
  float adc = sum / 8.0f;
  int16_t adcI = (int16_t)lroundf(adc);
  float voltage = adc * 0.125f / 1000.0f;

  // Stuck ADC detection (identical average for many cycles)
  if (adcI == lastAdcRaw) {
    if (stuckAdcCount < 250) stuckAdcCount++;
  } else {
    stuckAdcCount = 0;
    lastAdcRaw = adcI;
  }

  float rNtc = NAN;
  float steinhartC = NAN;
  SensorFaultCode code = SF_OK;
  const char* reject = nullptr;

  if (stuckAdcCount >= 30) {
    code = SF_STUCK;
    reject = "stuck ADC reading";
  } else if (adc < -100) {
    code = SF_INVALID;
    reject = "negative ADC";
  } else if (voltage < 0.05f) {
    // Near 0 V: open NTC (pull-down only) or A0 shorted to GND
    code = SF_OPEN;
    reject = "open/short-to-GND (V low)";
  } else if (voltage > 3.28f) {
    // Near VCC: shorted NTC across VCC or open bottom resistor
    code = SF_SHORT;
    reject = "short/open-bottom (V high)";
  }

  if (!reject) {
    rNtc = FIXED_RESISTOR * ((VCC - voltage) / voltage);
    if (rNtc <= 1.0f) {
      code = SF_INVALID;
      reject = "Rntc invalid";
    } else {
      float steinhart = logf(rNtc / NTC_R25) / NTC_BETA;
      steinhart += 1.0f / 298.15f;
      steinhartC = (1.0f / steinhart) - 273.15f;
      if (!isfinite(steinhartC) || steinhartC < TEMP_VALID_MIN || steinhartC > TEMP_VALID_MAX) {
        code = SF_OUT_OF_RANGE;
        reject = "out of calibrated range";
        steinhartC = NAN;
      } else if (isfinite(lastRawTempSample) &&
                 fabsf(steinhartC - lastRawTempSample) > 25.0f) {
        // Single-sample implausible jump — report but still return value for EMA soft path
        code = SF_JUMP;
        reject = "implausible jump";
      }
    }
  }

  if (reject) {
    sensorFaultCode = code;
  } else {
    sensorFaultCode = SF_OK;
    lastRawTempSample = steinhartC;
  }

  eqLogf("[NTC] A0 avg=%.1f V=%.4f R=%.0f T=%.2f code=%s%s%s\n",
                adc, voltage,
                isnan(rNtc) ? -1.0f : rNtc,
                isnan(steinhartC) ? -999.0f : steinhartC,
                sensorFaultName(),
                reject ? " REJECT: " : "",
                reject ? reject : "");

  if (code == SF_JUMP && isfinite(steinhartC)) return steinhartC; // soft: allow EMA to damp
  if (reject) return NAN;
  return steinhartC;
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

void saveRuntimeStats();
void loadRuntimeStats();
void saveHistoryToFlash();
void loadHistoryFromFlash();

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
      runtimeMonthSec += sec;
      // persist occasionally (~every 60s of heater time)
      sinceSave += sec;
      if (sinceSave >= 60) {
        sinceSave = 0;
        saveSettings();
        saveRuntimeStats();
      }
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
  int mon = t.tm_mon;
  int year = t.tm_year;
  bool changed = false;
  if (lastDayOfYear >= 0 && doy != lastDayOfYear) {
    runtimeTodaySec = 0;
    changed = true;
  }
  if (lastWeekNum >= 0 && w != lastWeekNum) {
    runtimeWeekSec = 0;
    changed = true;
  }
  if (lastMonthNum >= 0 && (mon != lastMonthNum || year != lastYearNum)) {
    runtimeMonthSec = 0;
    changed = true;
  }
  lastDayOfYear = doy;
  lastWeekNum = w;
  lastMonthNum = mon;
  lastYearNum = year;
  if (changed) saveRuntimeStats();
}

static bool scheduleWindowActive(const DayWindow& d, int nowM) {
  if (!d.enabled) return false;
  int onM = d.on_h * 60 + d.on_m;
  int offM = d.off_h * 60 + d.off_m;
  if (onM == offM) return true; // treat as all-day
  if (onM < offM) return nowM >= onM && nowM < offM;
  // Overnight window (e.g. 22:00 → 02:00): on THIS day only the evening part
  // (>= on) counts. The after-midnight tail belongs to the NEXT day and is
  // handled via the "previous day" check in scheduleAllowsHeat().
  return nowM >= onM;
}

bool scheduleAllowsHeat() {
  if (!scheduleEnabled) return true;
  time_t now = time(nullptr);
  // Fail-safe: without valid time, Auto schedule must NOT allow heat.
  // Boost remains a separate deliberate path (not gated here).
  if (now < 100000) {
#if SCHEDULE_REQUIRE_TIME
    return false;
#else
    return true;
#endif
  }
  struct tm t;
  if (!localtime_r(&now, &t)) {
#if SCHEDULE_REQUIRE_TIME
    return false;
#else
    return true;
#endif
  }
  // tm_wday: 0=Sun..6=Sat → index 0=Mon..6=Sun
  int idx = (t.tm_wday == 0) ? 6 : (t.tm_wday - 1);
  if (idx < 0 || idx > 6) return false;
  int nowM = t.tm_hour * 60 + t.tm_min;

  // Today's windows (including overnight that started today)
  for (int sidx = 0; sidx < 3; sidx++) {
    if (scheduleWindowActive(daySchedule[idx][sidx], nowM)) return true;
  }

  // Previous day's overnight windows that spill past midnight into today
  int prev = (idx + 6) % 7;
  for (int sidx = 0; sidx < 3; sidx++) {
    const DayWindow& d = daySchedule[prev][sidx];
    if (!d.enabled) continue;
    int onM = d.on_h * 60 + d.on_m;
    int offM = d.off_h * 60 + d.off_m;
    if (onM > offM && nowM < offM) return true; // still in yesterday's overnight tail
  }
  return false;
}

// Human-readable reason Auto heat is blocked (for MQTT / UI)
String heatBlockReason() {
  if (const char* why = heaterInterlock()) return why;
  if (elementFault) return "element_fault";
  if (sensorFault) return "sensor_fault";
  if (!tempValid(currentTemp)) return "sensor_invalid";
  if (mode == MODE_OFF) return "manual_off";
  if (mode == MODE_AUTO && scheduleEnabled && !scheduleAllowsHeat()) {
    time_t now = time(nullptr);
    if (now < 100000) return "time_unavailable";
    return "outside_schedule";
  }
  if (mode != MODE_OFF && tempValid(currentTemp)) {
    float target = (mode == MODE_BOOST) ? boostTemp : setpoint;
    if (currentTemp >= target) return "target_reached";
    if (!heaterOn && (millis() - heaterChangedAt) < minOffMs) return "min_off_time";
  }
  if (heaterOn) return "heating";
  return "idle";
}

// Single place that decides whether the relay may be energised right now.
// Returns nullptr when heating is allowed, else a short reason (also used for MQTT/UI).
const char* heaterInterlock() {
  if (otaInProgress || otaRebootPending) return "ota";
  if (overTempFault) return "over_temperature";
  if (millis() < BOOT_HEATER_HOLD_MS) return "boot_hold";       // relay off after every (re)boot
  if (otaPendingVerify) return "update_probation";             // new firmware not yet confirmed healthy
  // Never heat during first-time setup / after factory reset until the wizard finishes.
  // Default mode is AUTO with a 55 °C setpoint; a valid cold reading would otherwise
  // energise the relay as soon as the 20 s boot hold expires — unsafe on an unconfigured unit.
  if (!setupDone) return "setup";
  return nullptr;
}

void setHeater(bool on, bool force) {
  if (on) {
    const char* why = heaterInterlock();
    if (why) {
      on = false;
      force = true;
    }
    // Never turn on with bad sensor or failed element
    if (!on) {
      // interlock already forced off
    } else if (!tempValid(currentTemp) || sensorFault || elementFault) {
      eqLog(elementFault ? "[HEATER] blocked – element fault" : "[HEATER] blocked – sensor fault/invalid");
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
  if (on) {
    heaterOnAccumMs = 0;
    elementHeatStartedAt = millis();
    elementHeatStartTemp = currentTemp;
  } else {
    elementHeatStartedAt = 0;
    elementHeatStartTemp = NAN;
  }
  eqLogf("[HEATER] %s%s%s\n", on ? "ON" : "OFF", force ? " (forced)" : "",
                elementFault ? " [element fault]" : "");
}

void updateThermostat() {
  if (otaInProgress) {
    setHeater(false, true);
    return;
  }
  bool sampleValid = tempValid(currentTemp);
  if (sampleValid && currentTemp >= TEMP_HARD_TRIP && !overTempFault) {
    overTempFault = true;
    elementFault = true;          // reuses the existing "fault, heater locked off" UI/MQTT path
    setHeater(false, true);
    mode = MODE_OFF;
    saveSettings();
    eqLogf("[SAFETY] OVER-TEMPERATURE %.1f >= %.1f - heater locked off until acknowledged\n",
           currentTemp, (double)TEMP_HARD_TRIP);
    publishState();
  }
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
      eqLogf("[SENSOR] FAULT (%s) – heater forced off\n", sensorFaultName());
      setHeater(false, true);
      publishState();
    } else {
      sensorFaultCode = SF_OK;
      eqLog("[SENSOR] recovered");
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
        eqLog("[MODE] BOOST done → AUTO");
      } else {
        eqLog("[MODE] BOOST safety → AUTO");
      }
    }
  }

  float target = (mode == MODE_BOOST) ? boostTemp : setpoint;
  elementiq::HeaterDecision decision = elementiq::evaluateThermostat(
      mode, heaterOn, sampleValid, sensorFault || elementFault, currentTemp, target,
      hysteresis, scheduleAllowsHeat());
  if (decision.forceOff || elementFault) setHeater(false, true);
  else setHeater(decision.requestedOn);
  updateElementSafety();
}

void setMode(Mode m) {
  if (m == MODE_BOOST) {
    // Soft min boost: only if below threshold (when threshold > 0)
    if (boostMinTemp > 0.5f && tempValid(currentTemp) && currentTemp >= boostMinTemp) {
      eqLogf("[MODE] Boost skipped – temp %.1f >= min %.1f\n", currentTemp, boostMinTemp);
      return;
    }
    if (!tempValid(currentTemp) || sensorFault) {
      eqLog("[MODE] Boost blocked – sensor");
      return;
    }
  }
  if (m == mode) return;
  // Element fault is NOT cleared by mode changes — requires explicit acknowledge.
  mode = m;
  if (m == MODE_BOOST) boostStartedAt = millis();
  saveSettings();
  eqLogf("[MODE] → %s\n", MODE_NAMES[static_cast<uint8_t>(mode)]);
  if (m == MODE_OFF) setHeater(false, true);
}

void applyPreset(Preset p) {
  preset = p;
  if (p == PRESET_HOME) setpoint = homeTemp;
  else if (p == PRESET_AWAY) setpoint = awayTemp;
  else if (p == PRESET_ECO) setpoint = ecoTemp;
  saveSettings();
}

// ---- Persistent runtime stats (LittleFS) ----
struct RuntimeFile {
  uint32_t magic;
  uint32_t total;
  uint32_t today;
  uint32_t week;
  uint32_t month;
  int32_t last_doy;
  int32_t last_week;
  int32_t last_month;
  int32_t last_year;
  uint32_t crc;
};
#define RUNTIME_MAGIC 0x52554E54u  // 'RUNT'

static uint32_t simpleCrc(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int b = 0; b < 8; b++)
      c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int)(c & 1));
  }
  return ~c;
}

void saveRuntimeStats() {
  RuntimeFile f{};
  f.magic = RUNTIME_MAGIC;
  f.total = runtimeTotalSec;
  f.today = runtimeTodaySec;
  f.week = runtimeWeekSec;
  f.month = runtimeMonthSec;
  f.last_doy = lastDayOfYear;
  f.last_week = lastWeekNum;
  f.last_month = lastMonthNum;
  f.last_year = lastYearNum;
  f.crc = simpleCrc(reinterpret_cast<const uint8_t*>(&f), offsetof(RuntimeFile, crc));
  File file = LittleFS.open(RUNTIME_FILE, "w");
  if (!file) {
    eqLog("[HIST] runtime save failed");
    return;
  }
  file.write(reinterpret_cast<const uint8_t*>(&f), sizeof(f));
  file.close();
}

void loadRuntimeStats() {
  if (!LittleFS.exists(RUNTIME_FILE)) return;
  File file = LittleFS.open(RUNTIME_FILE, "r");
  if (!file) return;
  RuntimeFile f{};
  if (file.read(reinterpret_cast<uint8_t*>(&f), sizeof(f)) != sizeof(f)) {
    file.close();
    return;
  }
  file.close();
  if (f.magic != RUNTIME_MAGIC) return;
  if (f.crc != simpleCrc(reinterpret_cast<const uint8_t*>(&f), offsetof(RuntimeFile, crc))) {
    eqLog("[HIST] runtime file CRC bad");
    return;
  }
  // Prefer EEPROM total if larger (legacy), else file
  if (f.total > runtimeTotalSec) runtimeTotalSec = f.total;
  runtimeTodaySec = f.today;
  runtimeWeekSec = f.week;
  runtimeMonthSec = f.month;
  lastDayOfYear = f.last_doy;
  lastWeekNum = f.last_week;
  lastMonthNum = f.last_month;
  lastYearNum = f.last_year;
  eqLogf("[HIST] runtime loaded today=%us week=%us month=%us total=%us\n",
                runtimeTodaySec, runtimeWeekSec, runtimeMonthSec, runtimeTotalSec);
}

// ---- Persistent temperature history (LittleFS ring) ----
struct HistFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t capacity;
  uint16_t head;
  uint16_t count;
  uint32_t crc;  // of samples only is expensive; store header integrity lightly
};
#define HIST_MAGIC 0x48495354u  // 'HIST'
#define HIST_VERSION 1

void saveHistoryToFlash() {
  HistFileHeader hdr{};
  hdr.magic = HIST_MAGIC;
  hdr.version = HIST_VERSION;
  hdr.capacity = HISTORY_POINTS;
  hdr.head = histHead;
  hdr.count = histCount;
  hdr.crc = histCount ^ histHead ^ HISTORY_POINTS;
  File file = LittleFS.open(HISTORY_FILE, "w");
  if (!file) {
    eqLog("[HIST] history save failed");
    return;
  }
  file.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
  // Write ring in logical order (oldest → newest) for simpler load
  for (uint16_t i = 0; i < histCount; i++) {
    uint16_t idx = (histHead + HISTORY_POINTS - histCount + i) % HISTORY_POINTS;
    file.write(reinterpret_cast<const uint8_t*>(&histBuf[idx]), sizeof(HistSample));
  }
  file.close();
  histDirtyCount = 0;
  eqLogf("[HIST] saved %u samples to flash\n", histCount);
}

void loadHistoryFromFlash() {
  if (!LittleFS.exists(HISTORY_FILE)) {
    eqLog("[HIST] no history file yet");
    return;
  }
  File file = LittleFS.open(HISTORY_FILE, "r");
  if (!file) return;
  HistFileHeader hdr{};
  if (file.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr) ||
      hdr.magic != HIST_MAGIC || hdr.version != HIST_VERSION ||
      hdr.capacity != HISTORY_POINTS || hdr.count > HISTORY_POINTS) {
    file.close();
    eqLog("[HIST] history header invalid");
    return;
  }
  histCount = 0;
  histHead = 0;
  for (uint16_t i = 0; i < hdr.count; i++) {
    HistSample s{};
    if (file.read(reinterpret_cast<uint8_t*>(&s), sizeof(s)) != sizeof(s)) break;
    histBuf[histHead] = s;
    histHead = (histHead + 1) % HISTORY_POINTS;
    histCount++;
  }
  file.close();
  // Drop samples older than HISTORY_DAYS (in-place, no large stack buffer)
  time_t now = time(nullptr);
  if (now > 100000 && histCount > 0) {
    uint32_t cutoff = (uint32_t)now - (uint32_t)HISTORY_DAYS * 86400UL;
    while (histCount > 0) {
      uint16_t oldest = (histHead + HISTORY_POINTS - histCount) % HISTORY_POINTS;
      if (histBuf[oldest].epoch >= cutoff) break;
      histCount--;
    }
  }
  eqLogf("[HIST] loaded %u samples from flash\n", histCount);
}

void pushHistory(float t) {
  if (!tempValid(t)) return;
  time_t now = time(nullptr);
  uint32_t epoch = (now > 100000) ? (uint32_t)now : (millis() / 1000);
  // Avoid duplicates closer than ~half the interval
  if (histCount > 0) {
    uint16_t lastIdx = (histHead + HISTORY_POINTS - 1) % HISTORY_POINTS;
    if (epoch >= histBuf[lastIdx].epoch &&
        epoch - histBuf[lastIdx].epoch < (HISTORY_INTERVAL_MS / 1000) / 2)
      return;
  }
  HistSample s;
  s.epoch = epoch;
  s.temp10 = (int16_t)lroundf(t * 10.0f);
  histBuf[histHead] = s;
  histHead = (histHead + 1) % HISTORY_POINTS;
  if (histCount < HISTORY_POINTS) histCount++;
  // Trim anything older than 7 days when full
  if (now > 100000 && histCount > 0) {
    uint32_t cutoff = (uint32_t)now - (uint32_t)HISTORY_DAYS * 86400UL;
    while (histCount > 0) {
      uint16_t oldest = (histHead + HISTORY_POINTS - histCount) % HISTORY_POINTS;
      if (histBuf[oldest].epoch >= cutoff) break;
      histCount--;
    }
  }
  histDirtyCount++;
  if (histDirtyCount >= HISTORY_SAVE_EVERY) saveHistoryToFlash();
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
  // POSIX TZ: std offset where offset is hours WEST of UTC.
  // SAST (UTC+2) → "UTC-2". Build from tzOffsetHours.
  char tz[16];
  if (timezone >= 0)
    snprintf(tz, sizeof(tz), "UTC-%d", (int)timezone);
  else
    snprintf(tz, sizeof(tz), "UTC+%d", (int)(-timezone));
  // NOTE: configTime() internally resets TZ to UTC ("<+00>0"), so it MUST be
  // called first. Set our TZ afterwards, otherwise localtime_r() returns UTC.
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  setenv("TZ", tz, 1);
  tzset();
  eqLogf("[NTP] TZ=%s (offset %+d h)\n", tz, (int)timezone);
}

bool ntpSynced() {
  time_t now = time(nullptr);
  return now > 100000;
}

void updateTimeString() {
  time_t now = time(nullptr);
  if (now < 100000) {
    static uint32_t lastLog = 0;
    if (WiFi.status() == WL_CONNECTED && millis() - lastLog > 15000) {
      lastLog = millis();
      eqLog("[NTP] waiting for sync...");
    }
    timeStr = "--:--";
    return;
  }
  struct tm t;
  if (!localtime_r(&now, &t)) { timeStr = "--:--"; return; }
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  static bool loggedSync = false;
  if (!loggedSync) {
    loggedSync = true;
    eqLogf("[NTP] synced %04d-%02d-%02d %s\n",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, buf);
  }
  timeStr = buf;
}

// Survives soft restart only if we write EEPROM; use a one-shot file in LittleFS
void markOtaSuccess(const char* ver) {
  File f = LittleFS.open("/ota_ok.txt", "w");
  if (f) {
    f.print(ver ? ver : FIRMWARE_VERSION);
    f.close();
  }
}

void checkOtaSuccessBanner() {
  if (!LittleFS.exists("/ota_ok.txt")) return;
  File f = LittleFS.open("/ota_ok.txt", "r");
  String prev = f ? f.readString() : String();
  if (f) f.close();
  LittleFS.remove("/ota_ok.txt");
  lastOtaMsg = String("Firmware update complete — now running ") + FIRMWARE_VERSION;
  if (prev.length()) lastOtaMsg += String(" (from ") + prev + ")";
  eqLog(lastOtaMsg);
}


