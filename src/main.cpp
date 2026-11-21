/*
 * ElementIQ Controller – ESP32-C3-WROOM-02-N4 + ADS1115 + NTC + Relay
 * Modes: OFF · AUTO · BOOST
 * Features: schedule, presets, runtime stats, temp graph, sensor fault,
 *           soft-min boost, factory reset, OTA, multi-page web UI
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <new>
#include "config.h"

// Kept in the linked image so OTA validation can detect official builds
static const char ELEMENTIQ_OTA_MARKER[] __attribute__((used)) = OTA_PRODUCT_MARKER;

// Editable per-device serial number (also in config.h DEVICE_SERIAL)
char deviceSerialBuf[24] = "";
const char* deviceSerial = deviceSerialBuf;

// Use native USB CDC on ESP32-C3-DevKitM-1 (ARDUINO_USB_CDC_ON_BOOT=1).
// Do NOT redefine Serial to Serial0 — that silences the USB monitor.

#include "modules/controller.inc"
#include "modules/mqtt_ota.inc"
#include "modules/api.inc"
#include "modules/web_ui.inc"
#include "modules/portal.inc"

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  delay(800);   // allow USB CDC to enumerate before first prints
#endif
  eqLogf("=== ElementIQ Controller %s  S/N %s ===", FIRMWARE_VERSION, deviceSerial);
  pinMode(RELAY_PIN, OUTPUT); digitalWrite(RELAY_PIN, LOW);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
  if (!initializeStateMutex()) {
    Serial.println("[STATE] mutex allocation failed; rebooting");
    ESP.restart();
    return;
  }
  checkFactoryResetPin();

  if (!LittleFS.begin(true)) Serial.println("[FS] mount/format failed");
  else {
    checkOtaSuccessBanner();
    ensureDeviceSerial();
    loadWebAuth();
    loadRuntimeStats();
    loadHistoryFromFlash();
  }
  Serial.printf("[I2C] Wire.begin SDA=%d SCL=%d\n", I2C_SDA, I2C_SCL);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);  // 100 kHz — more reliable with longer probe leads
  adsReady = initializeAds();
  if (adsReady) {
    Serial.println("[ADS] ready — NTC on A0, expect divider 3V3-NTC-A0-Rfixed-GND");
  } else {
    Serial.println("[ADS] init failed; heater locked out until sensor recovers");
  }

  deviceId = String((uint32_t)(ESP.getEfuseMac() >> 24), HEX);
  topicBase = String(MQTT_BASE_TOPIC) + "/" + deviceId;
  topicState = topicBase + "/state";
  topicCmd = topicBase + "/cmd";
  topicAvail = topicBase + "/status";

  loadSettings();
  if (!initializePortalQueue() || !initializeOtaWorker()) {
    Serial.println("[INIT] worker queue/task allocation failed; rebooting");
    ESP.restart();
    return;
  }
  setupAPI();
  setupWebUI();
  setupPortalRoutes();
  setupWiFi();
  server.begin();
}

void loop() {
  processWifiManager();
  processPendingNetworkChanges();
  processPendingSystemActions();
  if (arduinoOtaReady) ArduinoOTA.handle();
  mqttLoop();
  retryAdsInitialization();
  {
    StateGuard guard;
    updateRuntimeTick();
  }

  if (millis() - lastLedCheck >= WIFI_LED_UPDATE_MS) {
    lastLedCheck = millis();
    updateWifiLed();
  }

  // Refresh clock often so the UI is not stuck on --:-- waiting for sensor interval
  {
    static uint32_t lastTimeUi = 0;
    if (millis() - lastTimeUi >= 1000) {
      lastTimeUi = millis();
      StateGuard guard;
      updateTimeString();
    }
  }

  if (millis() - lastRead >= READ_INTERVAL_MS) {
    lastRead = millis();
    {
      StateGuard guard;
      currentTemp = readTemperature();
      updateThermostat();
      updateTimeString();
      updateDayWeekCounters();
      if (millis() - lastHistoryAt >= HISTORY_INTERVAL_MS) {
        lastHistoryAt = millis();
        pushHistory(currentTemp);
      }
      publishState();
      Serial.printf("T=%.1f Set=%.1f Mode=%s H=%s Fault=%d\n",
                    currentTemp, setpoint, MODE_NAMES[static_cast<uint8_t>(mode)],
                    heaterOn ? "ON" : "OFF", sensorFault);
    }
  }
}
