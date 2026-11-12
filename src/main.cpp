/*
 * ElementIQ Controller v1.0.1 – ESP32-C3-WROOM-02-N4 + ADS1115 + NTC + Relay
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
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <new>
#include "config.h"

// ESP32-C3: core may not expose USB Serial in this TU — use UART0.
#include <HardwareSerial.h>
#ifndef Serial
#define Serial Serial0
#endif

#include "modules/controller.inc"
#include "modules/mqtt_ota.inc"
#include "modules/api.inc"
#include "modules/web_ui.inc"
#include "modules/portal.inc"

void setup() {
  Serial.begin(115200); delay(200);
  Serial.printf("\n=== ElementIQ Controller %s ===\n", FIRMWARE_VERSION);
  pinMode(RELAY_PIN, OUTPUT); digitalWrite(RELAY_PIN, LOW);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
  if (!initializeStateMutex()) {
    Serial.println("[STATE] mutex allocation failed; rebooting");
    ESP.restart();
    return;
  }
  checkFactoryResetPin();

  if (!LittleFS.begin()) Serial.println("[FS] fail");
  Wire.begin(I2C_SDA, I2C_SCL);
  adsReady = initializeAds();
  if (adsReady) {
    Serial.println("[ADS] ok");
  } else {
    Serial.println("[ADS] init failed; heater remains locked out until sensor reads valid");
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
  server.begin();
  setupWiFi();
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
