#pragma once

// ============================================================
//  Hardware pins (ESP32-C3-WROOM-02-N4)
// ============================================================
#define RELAY_PIN           7
#define I2C_SDA             4
#define I2C_SCL             6
#define LED_PIN             8     // Common onboard LED / RGB data (active LOW on many boards)
#define FACTORY_RESET_PIN   9     // BOOT button (hold LOW at boot ~5s to factory reset)

// ============================================================
//  Thermistor (raw Steinhart–Hart, then calibration table)
// ============================================================
#define FIXED_RESISTOR      10980.0f
#define NTC_R25             10000.0f
#define NTC_BETA            3950.0f
#define VCC                 3.3f

// Piecewise linear: NTC reading (°C) → calibrated probe (°C)
// From your comparison table
#define CAL_POINTS          15
// EMA filter: 0 = off, 0.15–0.35 typical (higher = more smoothing, slower response)
#define TEMP_EMA_ALPHA      0.20f
// Reject single samples that jump more than this vs filtered value (°C)
#define TEMP_JUMP_REJECT    6.0f

// ============================================================
//  Thermostat defaults
// ============================================================
#define DEFAULT_SETPOINT    55.0f
#define DEFAULT_HYSTERESIS  5.0f
#define DEFAULT_BOOST_TEMP  65.0f
#define DEFAULT_HOME_TEMP   55.0f
#define DEFAULT_AWAY_TEMP   40.0f
#define DEFAULT_ECO_TEMP    50.0f
#define DEFAULT_BOOST_MIN   0.0f
#define MIN_SETPOINT        30.0f
#define MAX_SETPOINT        100.0f
#define TEMP_VALID_MIN      5.0f
#define TEMP_VALID_MAX      110.0f
#define BOOST_SAFETY_MS     (90UL * 60UL * 1000UL)
#define ELEMENT_SAFETY_MS   (10UL * 60UL * 1000UL)  /* no rise while heating */
#define ELEMENT_RISE_LOW    3.0f   /* expected °C rise near 40°C in window */
#define ELEMENT_RISE_HIGH   0.8f   /* expected °C rise near 70°C+ in window */
#define SENSOR_FAULT_MS     (3UL * 60UL * 1000UL)

// ============================================================
//  Timing
// ============================================================
#define READ_INTERVAL_MS    2000
#define DEFAULT_MIN_ON_MS   30000
#define DEFAULT_MIN_OFF_MS  30000
#define MQTT_RECONNECT_MS   5000
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define WIFI_RETRY_INTERVAL_MS  15000
#define WIFI_SCAN_INTERVAL_MS   8000
#define WIFI_LED_UPDATE_MS  1000
// Temperature history: 7 days @ 5 min → 2016 samples (~12 KB), persisted on LittleFS
#define HISTORY_INTERVAL_MS 300000UL   // 5 minutes
#define HISTORY_DAYS        7
#define HISTORY_SAMPLES_PER_DAY  288   // 24*60/5
#define HISTORY_POINTS      (HISTORY_SAMPLES_PER_DAY * HISTORY_DAYS)
#define HISTORY_SAVE_EVERY  6          // flush to flash every N new samples (~30 min)
#define HISTORY_FILE        "/hist.bin"
#define RUNTIME_FILE        "/runtime.bin"

// ============================================================
//  WiFi / MQTT / Web
// ============================================================
#define AP_NAME             "ElementIQ-Setup"
#define AP_PASSWORD         ""
#define MQTT_HOST_DEFAULT   ""
#define MQTT_PORT_DEFAULT   1883
#define MQTT_USER_DEFAULT   ""
#define MQTT_PASS_DEFAULT   ""
#define MQTT_BASE_TOPIC     "elementiq"
#define HA_DISCOVERY_PREFIX "homeassistant"
#define HTTP_PORT           80
#define FIRMWARE_VERSION    "v1.0.1.16"
// Must appear in every official .bin — web/GitHub OTA rejects images without it
#define OTA_PRODUCT_MARKER  "ElementIQ-FW"
// ESP image chip_id for ESP32-C3 (see esp_image_format.h)
#define OTA_EXPECT_CHIP_ID  5
#define HARDWARE_VERSION    "v1.0.1"
#define GITHUB_OWNER        "GeckoZA"
#define GITHUB_REPO         "ElementIQ"
#define GITHUB_ASSET_NAME   "firmware.bin"

// Device identity (change per unit)
#define DEVICE_SERIAL       "2026/EQ/0001"


// ============================================================
//  Web UI authentication (empty password = auth disabled)
// ============================================================
#define WEB_AUTH_USER_DEFAULT   "admin"
#define WEB_AUTH_PASS_DEFAULT   ""
// 1 = require valid NTP before schedule can allow Auto heat
#define SCHEDULE_REQUIRE_TIME   1
// TLS: 0 = setInsecure (lab only), 1 = use cert bundle when available
#define OTA_TLS_INSECURE        0
