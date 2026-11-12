// ---- API helpers ----
void copyStr(char *dst, size_t n, JsonVariantConst v) {
  if (v.isNull()) return;
  const char *s = v.as<const char*>();
  if (!s) return;
  strncpy(dst, s, n - 1); dst[n - 1] = 0;
}

constexpr size_t MAX_SETTINGS_BODY = 2048;
struct SettingsBody {
  String value;
  size_t expectedLength = 0;
};
SettingsBody rejectedSettingsBody;

void bodyAccum(AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
  if (index == 0) {
    if (req->_tempObject && req->_tempObject != &rejectedSettingsBody)
      delete static_cast<SettingsBody*>(req->_tempObject);
    req->_tempObject = nullptr;
    if (total > MAX_SETTINGS_BODY) {
      req->_tempObject = &rejectedSettingsBody;
      req->send(413, "application/json", "{\"error\":\"body too large\"}");
      return;
    }
    SettingsBody *body = new (std::nothrow) SettingsBody;
    if (!body) {
      req->_tempObject = &rejectedSettingsBody;
      req->send(503, "application/json", "{\"error\":\"insufficient memory\"}");
      return;
    }
    body->expectedLength = total;
    if (!body->value.reserve(total + 1)) {
      delete body;
      req->_tempObject = &rejectedSettingsBody;
      req->send(503, "application/json", "{\"error\":\"insufficient memory\"}");
      return;
    }
    req->_tempObject = body;
  }
  if (!req->_tempObject || req->_tempObject == &rejectedSettingsBody) return;
  SettingsBody *body = static_cast<SettingsBody*>(req->_tempObject);
  if (total != body->expectedLength || index != body->value.length() ||
      index > body->expectedLength ||
      len > body->expectedLength - index ||
      !body->value.concat(reinterpret_cast<const char*>(data), len)) {
    delete body;
    req->_tempObject = &rejectedSettingsBody;
    req->send(400, "application/json", "{\"error\":\"invalid body\"}");
  }
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
  if (!doc["sched_on_h"].isNull()) scheduleOnH = constrain(doc["sched_on_h"].as<int>(), 0, 23);
  if (!doc["sched_on_m"].isNull()) scheduleOnM = constrain(doc["sched_on_m"].as<int>(), 0, 59);
  if (!doc["sched_off_h"].isNull()) scheduleOffH = constrain(doc["sched_off_h"].as<int>(), 0, 23);
  if (!doc["sched_off_m"].isNull()) scheduleOffM = constrain(doc["sched_off_m"].as<int>(), 0, 59);
  copyStr(githubOwner, sizeof(githubOwner), doc["github_owner"]);
  copyStr(githubRepo, sizeof(githubRepo), doc["github_repo"]);
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
    doc["sched_on_h"] = scheduleOnH; doc["sched_on_m"] = scheduleOnM;
    doc["sched_off_h"] = scheduleOffH; doc["sched_off_m"] = scheduleOffM;
    doc["schedule_active"] = scheduleEnabled && scheduleAllowsHeat();
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
      if (req->_tempObject == &rejectedSettingsBody) {
        req->_tempObject = nullptr;
        return;
      }
      SettingsBody *body = static_cast<SettingsBody*>(req->_tempObject);
      if (!body) { req->send(400, "application/json", "{\"error\":\"no body\"}"); return; }
      JsonDocument doc;
      if (body->value.length() != body->expectedLength ||
          deserializeJson(doc, body->value)) {
        delete body; req->_tempObject = nullptr;
        req->send(400, "application/json", "{\"error\":\"bad json\"}"); return;
      }
      delete body; req->_tempObject = nullptr;
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
    }, NULL, bodyAccum);

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

