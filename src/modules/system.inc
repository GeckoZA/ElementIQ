// ---- System: watchdog, safe restart, OTA boot guard (rollback), status RGB LED ----
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

// ============================================================
//  Watchdog
// ============================================================
// loop() feeds the task watchdog. If loop() stalls (deadlock, blocked I/O, bad
// firmware) the chip resets; setup() drives the relay LOW first thing, and
// heaterInterlock() keeps it off for BOOT_HEATER_HOLD_MS after every boot.
void watchdogBegin() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WDT_TIMEOUT_S * 1000UL;
  cfg.idle_core_mask = 0;          // only watch loopTask (C3 is single-core)
  cfg.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(nullptr);       // subscribe the Arduino loop task
}
static inline void watchdogFeed() { esp_task_wdt_reset(); }

// ============================================================
//  Safe restart: relay is forced LOW before every reboot
// ============================================================
void safeRestart() {
  setHeater(false, true);
  digitalWrite(RELAY_PIN, LOW);
  delay(100);
  ESP.restart();
}

// ============================================================
//  OTA boot guard (rollback)
// ============================================================
// Native app rollback needs bootloader support that the stock Arduino build may
// not enable, so this works on its own with a small NVS record:
//   - after a successful OTA write we set pend=1, crash=0
//   - on the next boots, a crash/WDT/brownout reset while pend=1 increments crash
//   - crash >= OTA_MAX_CRASH_BOOTS  -> boot the previous slot again
//   - healthy for OTA_CONFIRM_MS    -> pend=0, image confirmed
// While pend=1 the relay stays off (heaterInterlock "update_probation").
// If the bootloader *does* support native rollback, we also mark the image valid.
extern "C" bool verifyRollbackLater() { return true; }   // we confirm ourselves after OTA_CONFIRM_MS

static bool resetWasCrash() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: case ESP_RST_BROWNOUT:
      return true;
    default:
      return false;
  }
}

void otaGuardArm() {
  Preferences p;
  p.begin("eiq-boot", false);
  p.putUChar("pend", 1);
  p.putUChar("crash", 0);
  p.end();
}

void bootGuardBegin() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  if (run) esp_ota_get_state_partition(run, &st);
  Preferences p;
  p.begin("eiq-boot", false);
  otaPendingVerify = p.getUChar("pend", 0) != 0;
  uint8_t crash = p.getUChar("crash", 0);
  eqLogf("[BOOT] slot=%s imgState=%d reset=%d pending=%d crashes=%u\n",
         run ? run->label : "?", (int)st, (int)esp_reset_reason(), otaPendingVerify, crash);
  if (otaPendingVerify && resetWasCrash()) {
    crash++;
    p.putUChar("crash", crash);
    if (crash >= OTA_MAX_CRASH_BOOTS) {
      // Inactive slot is the previous image when we are running the new one.
      const esp_partition_t* prev = esp_ota_get_next_update_partition(nullptr);
      if (prev && prev != run && esp_ota_set_boot_partition(prev) == ESP_OK) {
        p.putUChar("pend", 0);
        p.putUChar("crash", 0);
        p.end();
        eqLogf("[BOOT] new firmware unstable (%u crashes) - rolling back to %s\n", crash, prev->label);
        safeRestart();
        return;
      }
      eqLog("[BOOT] rollback needed but previous slot is not bootable; staying");
    }
  }
  p.end();
}

void bootGuardLoop() {
  static bool confirmed = false;
  if (confirmed || millis() < OTA_CONFIRM_MS) return;
  // Confirm after the probation window if we are still running (no crash/WDT).
  // Do NOT require a valid temperature sensor — a missing/faulty NTC must not
  // prevent the new image from being kept, otherwise every clean reboot (or the
  // native bootloader rollback path) reverts to the previous firmware.
  // Heater safety is already enforced by heaterInterlock() independently.
  confirmed = true;
  if (otaPendingVerify) {
    Preferences p;
    p.begin("eiq-boot", false);
    p.putUChar("pend", 0);
    p.putUChar("crash", 0);
    p.end();
    otaPendingVerify = false;
    eqLog("[BOOT] new firmware confirmed healthy");
  }
  esp_ota_mark_app_valid_cancel_rollback();   // cancels native bootloader rollback when enabled
}

// ============================================================
//  Status RGB LED (analog RGB on three PWM pins)
// ============================================================
//  Boot (first 2 s)            white
//  Firmware update / rebooting blue, fast blink
//  Over-temp / element fault   red, fast blink
//  Sensor fault                red, slow blink
//  First-time setup waiting    magenta, breathing
//  Wi-Fi connecting            blue, blink
//  Wi-Fi lost / setup AP up    amber, slow blink
//  Heating                     orange, solid
//  Mode OFF                    dim white
//  Idle, all good              green, breathing
extern bool portalActive;
extern bool stationConnecting;

static void ledOut(uint8_t r, uint8_t g, uint8_t b) {
  r = (uint16_t)r * STATUS_LED_BRIGHTNESS / 255;
  g = (uint16_t)g * STATUS_LED_BRIGHTNESS / 255;
  b = (uint16_t)b * STATUS_LED_BRIGHTNESS / 255;
#if STATUS_LED_COMMON_ANODE
  r = 255 - r; g = 255 - g; b = 255 - b;
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(STATUS_LED_R_PIN, r);
  ledcWrite(STATUS_LED_G_PIN, g);
  ledcWrite(STATUS_LED_B_PIN, b);
#else
  ledcWrite(0, r); ledcWrite(1, g); ledcWrite(2, b);
#endif
}

// Hold the BOOT button (GPIO9) for 3 s to arm dangerous web actions for 60 s on a
// unit that has no admin password (see requireDangerAuth() in api.inc).
void dangerButtonPoll() {
  static uint32_t downSince = 0;
  if (digitalRead(FACTORY_RESET_PIN) == LOW) {
    if (!downSince) downSince = millis();
    else if (millis() - downSince >= 3000 && !(dangerArmed && (int32_t)(millis() - dangerArmedUntil) < 0)) {
      dangerArmed = true;
      dangerArmedUntil = millis() + 60000UL;
      eqLog("[SECURITY] BOOT button held: firmware / factory-reset / Wi-Fi changes allowed for 60 s");
    }
  } else {
    downSince = 0;
  }
}

void statusLedBegin() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(STATUS_LED_R_PIN, 5000, 8);
  ledcAttach(STATUS_LED_G_PIN, 5000, 8);
  ledcAttach(STATUS_LED_B_PIN, 5000, 8);
#else
  ledcSetup(0, 5000, 8); ledcAttachPin(STATUS_LED_R_PIN, 0);
  ledcSetup(1, 5000, 8); ledcAttachPin(STATUS_LED_G_PIN, 1);
  ledcSetup(2, 5000, 8); ledcAttachPin(STATUS_LED_B_PIN, 2);
#endif
  ledOut(255, 255, 255);
}

static uint8_t breathe(uint32_t periodMs, uint8_t lo, uint8_t hi) {
  uint32_t t = millis() % periodMs;
  uint32_t half = periodMs / 2;
  uint32_t x = t < half ? t : periodMs - t;          // triangle wave
  return lo + (uint32_t)(hi - lo) * x / half;
}

void statusLedUpdate() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 25) return;
  last = now;

  bool blinkFast = (now / 150) & 1;
  bool blinkMed  = (now / 500) & 1;
  bool blinkSlow = (now / 1000) & 1;

  bool otaBusyNow, faultHard, faultSensor, wifiUp, heating, modeOff, setupMode, connecting, portal;
  {
    StateGuard guard;
    otaBusyNow  = otaInProgress || otaRebootPending;
    faultHard   = overTempFault || elementFault;
    faultSensor = sensorFault || (now > 15000 && !tempValid(currentTemp));
    heating     = heaterOn;
    modeOff     = (mode == MODE_OFF);
    setupMode   = !setupDone;
    connecting  = stationConnecting;
    portal      = portalActive;
  }
  wifiUp = WiFi.status() == WL_CONNECTED;

  if (now < 2000)               { ledOut(255, 255, 255); return; }
  if (dangerArmed && (int32_t)(now - dangerArmedUntil) < 0 && !adminPasswordSet()) {
    ledOut(blinkMed ? 255 : 0, blinkMed ? 255 : 0, blinkMed ? 255 : 0);   // white blink = confirmation window open
    return;
  }
  if (otaBusyNow)               { ledOut(0, 0, blinkFast ? 255 : 0); return; }
  if (faultHard)                { ledOut(blinkFast ? 255 : 0, 0, 0); return; }
  if (faultSensor)              { ledOut(blinkSlow ? 255 : 0, 0, 0); return; }
  if (setupMode)                { uint8_t v = breathe(2500, 20, 255); ledOut(v, 0, v); return; }
  if (connecting)               { ledOut(0, 0, blinkMed ? 255 : 0); return; }
  if (!wifiUp || portal)        { ledOut(blinkSlow ? 255 : 0, blinkSlow ? 120 : 0, 0); return; }
  if (heating)                  { ledOut(255, 90, 0); return; }
  if (modeOff)                  { ledOut(40, 40, 40); return; }
  uint8_t g = breathe(4000, 30, 200);
  ledOut(0, g, 0);
}
