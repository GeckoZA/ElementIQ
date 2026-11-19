// ---- API helpers ----
void copyStr(char *dst, size_t n, JsonVariantConst v) {
  if (v.isNull()) return;
  const char *s = v.as<const char*>();
  if (!s) return;
  strncpy(dst, s, n - 1); dst[n - 1] = 0;
}

constexpr size_t MAX_SETTINGS_BODY = 2048;

static bool readRequestJsonBody(AsyncWebServerRequest* req, JsonDocument& doc) {
  String body = req->arg("plain");
  if (body.length() == 0) {
    return false;
  }
  if (body.length() > MAX_SETTINGS_BODY) {
    req->send(413, "application/json", "{\"error\":\"body too large\"}");
    return false;
  }
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    req->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return false;
  }
  return true;
}

bool applySettingsJson(JsonDocument& doc) {
  copyStr(mqtt_host, sizeof(mqtt_host), doc["mqtt_host"]);
  if (!doc["mqtt_port"].isNull()) {
    const char *portText = doc["mqtt_port"].as<const char*>();
    if (portText && portText[0]) {
      char *end = nullptr;
      unsigned long port = strtoul(portText, &end, 10);
      if (end != portText && *end == '\0' && port >= 1 && port <= 65535) {
        snprintf(mqtt_port, sizeof(mqtt_port), "%lu", port);
      }
    }
  }
  copyStr(mqtt_user, sizeof(mqtt_user), doc["mqtt_user"]);
  if (!doc["mqtt_pass"].isNull()) {
    const char *pw = doc["mqtt_pass"].as<const char*>();
    if (pw && strlen(pw) > 0) copyStr(mqtt_pass, sizeof(mqtt_pass), doc["mqtt_pass"]);
  }
  if (!doc["mqtt_enabled"].isNull()) mqttEnabled = doc["mqtt_enabled"].as<bool>();
  if (strlen(mqtt_port) == 0) strncpy(mqtt_port, "1883", sizeof(mqtt_port)-1);
  if (!doc["ap_enabled"].isNull()) { apEnabled = doc["ap_enabled"].as<bool>(); hideAp = !apEnabled; }
  copyStr(apName, sizeof(apName), doc["ap_name"]);
  if (!doc["ap_pass"].isNull()) {
    const char *pw = doc["ap_pass"].as<const char*>();
    if (pw && (strlen(pw) == 0 || strlen(pw) >= 8)) copyStr(apPass, sizeof(apPass), doc["ap_pass"]);
  }
  if (!doc["tz"].isNull()) tzOffsetHours = constrain(doc["tz"].as<int>(), -12, 14);
  if (!doc["theme"].isNull()) themeId = (uint8_t)constrain(doc["theme"].as<int>(), 0, 2);
  if (!doc["friendly_name"].isNull()) {
    copyStr(friendlyName, sizeof(friendlyName), doc["friendly_name"]);
    if (strlen(friendlyName) < 1) strncpy(friendlyName, "ElementIQ", sizeof(friendlyName)-1);
  }
  auto clampF = [](float v, float lo, float hi) { return isfinite(v) ? constrain(v, lo, hi) : v; };
  if (!doc["setpoint"].isNull()) {
    float v = doc["setpoint"].as<float>();
    if (isfinite(v)) { setpoint = clampF(v, MIN_SETPOINT, MAX_SETPOINT); preset = PRESET_CUSTOM; }
  }
  if (!doc["boost_temp"].isNull()) {
    float v = doc["boost_temp"].as<float>();
    if (isfinite(v)) boostTemp = clampF(v, MIN_SETPOINT, MAX_SETPOINT);
  }
  if (!doc["hysteresis"].isNull()) {
    float v = doc["hysteresis"].as<float>();
    if (isfinite(v)) hysteresis = clampF(v, 0.5f, 20.0f);
  }
  if (!doc["min_on_ms"].isNull()) { uint32_t v = doc["min_on_ms"].as<uint32_t>(); if (v >= 5000 && v <= 600000) minOnMs = v; }
  if (!doc["min_off_ms"].isNull()) { uint32_t v = doc["min_off_ms"].as<uint32_t>(); if (v >= 5000 && v <= 600000) minOffMs = v; }
  if (!doc["home_temp"].isNull()) {
    float v = doc["home_temp"].as<float>();
    if (isfinite(v)) homeTemp = clampF(v, MIN_SETPOINT, MAX_SETPOINT);
  }
  if (!doc["away_temp"].isNull()) {
    float v = doc["away_temp"].as<float>();
    if (isfinite(v)) awayTemp = clampF(v, MIN_SETPOINT, MAX_SETPOINT);
  }
  if (!doc["eco_temp"].isNull()) {
    float v = doc["eco_temp"].as<float>();
    if (isfinite(v)) ecoTemp = clampF(v, MIN_SETPOINT, MAX_SETPOINT);
  }
  if (!doc["boost_min_temp"].isNull()) {
    float v = doc["boost_min_temp"].as<float>();
    if (isfinite(v)) boostMinTemp = clampF(v, 0.0f, MAX_SETPOINT);
  }
  if (!doc["schedule_enabled"].isNull()) scheduleEnabled = doc["schedule_enabled"].as<bool>();
  if (doc["schedule"].is<JsonArray>()) {
    JsonArray arr = doc["schedule"].as<JsonArray>();
    // Clear all slots first
    for (int d = 0; d < 7; d++)
      for (int s = 0; s < 3; s++) daySchedule[d][s] = {0, 0, 0, 0, 0};
    for (int i = 0; i < (int)arr.size(); i++) {
      JsonObject o = arr[i].as<JsonObject>();
      if (o.isNull()) continue;
      int day = o["day"].isNull() ? i : o["day"].as<int>();
      if (day < 0 || day > 6) continue;
      int slot = 0;
      while (slot < 3 && daySchedule[day][slot].enabled) slot++;
      if (slot >= 3) continue;
      daySchedule[day][slot].enabled = o["enabled"].isNull() ? 1 : (o["enabled"].as<bool>() ? 1 : 0);
      if (!o["on"].isNull()) {
        const char* s = o["on"]; int h=0,m=0;
        if (s && sscanf(s, "%d:%d", &h, &m) == 2) {
          daySchedule[day][slot].on_h = constrain(h, 0, 23);
          daySchedule[day][slot].on_m = constrain(m, 0, 59);
        }
      }
      if (!o["off"].isNull()) {
        const char* s = o["off"]; int h=0,m=0;
        if (s && sscanf(s, "%d:%d", &h, &m) == 2) {
          daySchedule[day][slot].off_h = constrain(h, 0, 23);
          daySchedule[day][slot].off_m = constrain(m, 0, 59);
        }
      }
    }
  }
  // GitHub owner/repo are fixed in config.h (not user-editable)
  if (!doc["ota_auto"].isNull()) otaAutoCheck = doc["ota_auto"].as<bool>();
  if (!doc["preset"].isNull()) {
    const char* p = doc["preset"].as<const char*>();
    if (p) {
      if (!strcmp(p, "home")) applyPreset(PRESET_HOME);
      else if (!strcmp(p, "away")) applyPreset(PRESET_AWAY);
      else if (!strcmp(p, "eco")) applyPreset(PRESET_ECO);
      else preset = PRESET_CUSTOM;
    }
  }
  bool saved = saveSettings();
  pendingApReconfigure = true;
  pendingTimeReconfigure = true;
  pendingMqttReconnect = true;
  return saved;
}

void setupAPI() {
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    StateGuard guard;
    JsonDocument doc;
    if (tempValid(currentTemp)) doc["temperature"] = roundf(currentTemp * 10.0f) / 10.0f;
    else doc["temperature"] = nullptr;
    doc["setpoint"] = roundf(setpoint * 10.0f) / 10.0f;
    doc["boost_temp"] = roundf(boostTemp * 10.0f) / 10.0f;
    doc["hysteresis"] = roundf(hysteresis * 10.0f) / 10.0f;
    doc["home_temp"] = roundf(homeTemp * 10.0f) / 10.0f;
    doc["away_temp"] = roundf(awayTemp * 10.0f) / 10.0f;
    doc["eco_temp"] = roundf(ecoTemp * 10.0f) / 10.0f;
    doc["boost_min_temp"] = roundf(boostMinTemp * 10.0f) / 10.0f;
    doc["min_on_ms"] = minOnMs; doc["min_off_ms"] = minOffMs;
    doc["mode"] = MODE_NAMES[static_cast<uint8_t>(mode)];
    doc["preset"] = PRESET_NAMES[preset];
    doc["heater"] = heaterOn;
    doc["action"] = heaterOn ? "heating" : "idle";
    doc["sensor_fault"] = sensorFault;
    doc["schedule_enabled"] = scheduleEnabled;
    doc["schedule_active"] = scheduleEnabled && scheduleAllowsHeat();
    {
      JsonArray arr = doc["schedule"].to<JsonArray>();
      static const char* dayNames[] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
      for (int d = 0; d < 7; d++) {
        for (int s = 0; s < 3; s++) {
          if (!daySchedule[d][s].enabled && s > 0) continue;
          // always emit slot 0 so UI has a row; extra slots only if enabled
          if (s > 0 && !daySchedule[d][s].enabled) continue;
          JsonObject o = arr.add<JsonObject>();
          o["day"] = d;
          o["day_name"] = dayNames[d];
          o["slot"] = s;
          o["enabled"] = daySchedule[d][s].enabled != 0;
          char onBuf[8], offBuf[8];
          snprintf(onBuf, sizeof(onBuf), "%02u:%02u", daySchedule[d][s].on_h, daySchedule[d][s].on_m);
          snprintf(offBuf, sizeof(offBuf), "%02u:%02u", daySchedule[d][s].off_h, daySchedule[d][s].off_m);
          o["on"] = onBuf;
          o["off"] = offBuf;
        }
      }
    }
    doc["runtime_total_sec"] = runtimeTotalSec;
    doc["runtime_today_sec"] = runtimeTodaySec;
    doc["runtime_week_sec"] = runtimeWeekSec;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    doc["version"] = FIRMWARE_VERSION;
    if (mode == MODE_BOOST) {
      unsigned long elapsed = millis() - boostStartedAt;
      unsigned long left = elapsed >= BOOST_SAFETY_MS ? 0 : BOOST_SAFETY_MS - elapsed;
      doc["boost_remaining"] = left / 1000;
    }
    doc["time"] = timeStr;
    doc["mqtt_host"] = mqtt_host; doc["mqtt_port"] = mqtt_port;
    doc["mqtt_user"] = mqtt_user; doc["mqtt_connected"] = mqttIsConnected;
    doc["ap_enabled"] = apEnabled; doc["ap_name"] = apName;
    doc["ap_password_configured"] = apPass[0] != '\0';
    doc["mqtt_enabled"] = mqttEnabled; doc["tz"] = tzOffsetHours;
    doc["settings_saved"] = settingsSaveOk;
    doc["theme"] = themeId; doc["friendly_name"] = friendlyName;
    doc["mdns"] = "ElementIQ.local";
    doc["github_owner"] = githubOwner; doc["github_repo"] = githubRepo;
    doc["ota_auto"] = otaAutoCheck; doc["ota_msg"] = lastOtaMsg; doc["ota_busy"] = otaBusy;
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *req) {
    StateGuard guard;
    JsonDocument doc;
    JsonArray temps = doc["temps"].to<JsonArray>();
    JsonArray epochs = doc["epochs"].to<JsonArray>();
    for (uint8_t i = 0; i < histCount; i++) {
      uint8_t idx = (histHead + HISTORY_POINTS - histCount + i) % HISTORY_POINTS;
      temps.add(roundf(histTemp[idx] * 10.0f) / 10.0f);
      epochs.add(histEpoch[idx]);
    }
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/setpoint", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      StateGuard guard;
      JsonDocument doc;
      if (deserializeJson(doc, data, len) || !doc["setpoint"].is<float>() ||
          !isfinite(doc["setpoint"].as<float>())) {
        req->send(400, "application/json", "{\"error\":\"bad request\"}"); return;
      }
      setpoint = constrain(doc["setpoint"].as<float>(), MIN_SETPOINT, MAX_SETPOINT);
      preset = PRESET_CUSTOM;
      if (!saveSettings()) {
        req->send(500, "application/json", "{\"error\":\"settings save failed\"}");
        return;
      }
      pendingStatePublish = true;
      req->send(200, "application/json", "{\"ok\":true}");
    });

  server.on("/api/mode", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      StateGuard guard;
      JsonDocument doc;
      if (deserializeJson(doc, data, len) || !doc["mode"].is<const char*>()) {
        req->send(400, "application/json", "{\"error\":\"bad request\"}"); return;
      }
      const char* m = doc["mode"];
      if (!strcmp(m, "off")) setMode(MODE_OFF);
      else if (!strcmp(m, "auto")) setMode(MODE_AUTO);
      else if (!strcmp(m, "boost")) setMode(MODE_BOOST);
      else { req->send(400, "application/json", "{\"error\":\"unknown mode\"}"); return; }
      if (!settingsSaveOk) {
        req->send(500, "application/json", "{\"error\":\"settings save failed\"}");
        return;
      }
      pendingStatePublish = true;
      req->send(200, "application/json", "{\"ok\":true}");
    });

  server.on("/api/preset", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      StateGuard guard;
      JsonDocument doc;
      if (deserializeJson(doc, data, len) || !doc["preset"].is<const char*>()) {
        req->send(400, "application/json", "{\"error\":\"bad request\"}"); return;
      }
      const char* p = doc["preset"];
      if (!strcmp(p, "home")) applyPreset(PRESET_HOME);
      else if (!strcmp(p, "away")) applyPreset(PRESET_AWAY);
      else if (!strcmp(p, "eco")) applyPreset(PRESET_ECO);
      else { req->send(400, "application/json", "{\"error\":\"unknown\"}"); return; }
      if (!settingsSaveOk) {
        req->send(500, "application/json", "{\"error\":\"settings save failed\"}");
        return;
      }
      pendingStatePublish = true;
      req->send(200, "application/json", "{\"ok\":true}");
    });

  server.on("/api/settings", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      JsonDocument doc;
      if (!readRequestJsonBody(req, doc)) {
        return;
      }
      StateGuard guard;
      if (!doc["ap_pass"].isNull()) {
        const char* password = doc["ap_pass"].as<const char*>();
        size_t length = password ? strlen(password) : 0;
        if (!password || length > 31 || (length > 0 && length < 8)) {
          req->send(400, "application/json", "{\"error\":\"AP password must be blank or 8-31 characters\"}");
          return;
        }
      }
      if (!applySettingsJson(doc)) {
        req->send(500, "application/json", "{\"error\":\"settings save failed\"}");
        return;
      }
      pendingStatePublish = true;
      req->send(200, "application/json", "{\"ok\":true}");
    });

  server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!queueOtaOperation(false)) {
      req->send(409, "application/json", "{\"error\":\"OTA worker busy\"}");
      return;
    }
    req->send(202, "application/json", "{\"queued\":true}");
  });
  server.on("/api/ota/install", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!queueOtaOperation(true)) {
      req->send(409, "application/json", "{\"error\":\"OTA worker busy\"}");
      return;
    }
    req->send(202, "application/json", "{\"queued\":true}");
  });
  // Browser firmware upload: POST raw .bin body to /api/ota/upload
  server.on(
    "/api/ota/upload", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      bool ok = !Update.hasError();
      {
        StateGuard guard;
        otaBusy = false;
        otaInProgress = false;
        lastOtaMsg = ok ? "Web upload OK — rebooting"
                        : String("Web upload failed: ") + Update.errorString();
      }
      if (ok) {
        req->send(200, "text/plain", "OK");
        delay(500);
        ESP.restart();
      } else {
        req->send(500, "text/plain", Update.errorString());
      }
    },
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        {
          StateGuard guard;
          if (otaBusy) return;
          otaBusy = true;
          otaInProgress = true;
          lastOtaMsg = "Web upload in progress...";
          setHeater(false, true);
        }
        Serial.printf("[OTA] Web upload start, %u bytes\n", (unsigned)total);
        if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN)) {
          StateGuard guard;
          lastOtaMsg = String("Update.begin failed: ") + Update.errorString();
          otaBusy = false;
          otaInProgress = false;
          Serial.println(lastOtaMsg);
        }
      }
      if (Update.hasError()) return;
      if (len && Update.write(data, len) != len) {
        StateGuard guard;
        lastOtaMsg = String("Write failed: ") + Update.errorString();
        Serial.println(lastOtaMsg);
      }
      if (!Update.hasError() && total > 0 && index + len >= total) {
        if (!Update.end(true)) {
          StateGuard guard;
          lastOtaMsg = String("Update.end failed: ") + Update.errorString();
          Serial.println(lastOtaMsg);
        } else {
          Serial.println("[OTA] Web upload image OK");
        }
      }
    });

  server.on("/api/factoryreset", HTTP_POST, [](AsyncWebServerRequest *req) {
    {
      StateGuard guard;
      pendingFactoryReset = true;
    }
    req->send(202, "text/plain", "Factory reset queued");
  });
  server.on("/api/resetwifi", HTTP_GET, [](AsyncWebServerRequest *req) {
    {
      StateGuard guard;
      pendingWifiReset = true;
    }
    req->send(202, "text/plain", "WiFi reset queued");
  });
}

