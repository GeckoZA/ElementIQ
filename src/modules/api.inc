// ---- API helpers ----
bool requireWebAuth(AsyncWebServerRequest* req) {
  if (!webAuthEnabled || !webAuthPass[0]) return true;
  if (req->authenticate(webAuthUser, webAuthPass)) return true;
  req->requestAuthentication("ElementIQ");
  return false;
}


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
  if (!doc["web_auth_enabled"].isNull()) webAuthEnabled = doc["web_auth_enabled"].as<bool>();
  copyStr(webAuthUser, sizeof(webAuthUser), doc["web_auth_user"]);
  if (!doc["web_auth_pass"].isNull()) {
    const char* p = doc["web_auth_pass"].as<const char*>();
    if (p && strlen(p) > 0) copyStr(webAuthPass, sizeof(webAuthPass), doc["web_auth_pass"]);
  }
  if (!webAuthPass[0]) webAuthEnabled = false;

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
  saveWebAuth();
  bool saved = saveSettings();
  pendingApReconfigure = true;
  pendingTimeReconfigure = true;
  pendingMqttReconnect = true;
  return saved;
}

void setupAPI() {

  server.on("/api/console", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!requireWebAuth(req)) return;
    uint32_t since = 0;
    if (req->hasParam("since")) since = (uint32_t)req->getParam("since")->value().toInt();
    JsonDocument doc;
    doc["seq"] = eqLogSeq;
    JsonArray lines = doc["lines"].to<JsonArray>();
    eqLogExportSince(lines, since, nullptr);
    doc["seq"] = eqLogSeq;
    AsyncResponseStream *res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!requireWebAuth(req)) return;
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
    doc["element_fault"] = elementFault;
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
    doc["runtime_month_sec"] = runtimeMonthSec;
    doc["history_count"] = histCount;
    doc["history_days"] = HISTORY_DAYS;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    doc["version"] = FIRMWARE_VERSION;
    doc["heat_block_reason"] = heatBlockReason();
    doc["sensor_fault_code"] = sensorFaultName();
    doc["element_fault"] = elementFault;
    doc["web_auth_enabled"] = webAuthEnabled;
    doc["web_auth_user"] = webAuthUser;
    doc["hardware_version"] = HARDWARE_VERSION;
    doc["serial_number"] = deviceSerial;
    if (mode == MODE_BOOST) {
      unsigned long elapsed = millis() - boostStartedAt;
      unsigned long left = elapsed >= BOOST_SAFETY_MS ? 0 : BOOST_SAFETY_MS - elapsed;
      doc["boost_remaining"] = left / 1000;
    }
    doc["time"] = timeStr;
    doc["mqtt_host"] = mqtt_host; doc["mqtt_port"] = mqtt_port;
    doc["mqtt_user"] = mqtt_user; doc["mqtt_connected"] = mqttIsConnected;
    doc["mqtt_password_configured"] = mqtt_pass[0] != '\0';
    doc["ap_enabled"] = apEnabled; doc["ap_name"] = apName;
    doc["ap_password_configured"] = apPass[0] != '\0';
    doc["mqtt_enabled"] = mqttEnabled; doc["tz"] = tzOffsetHours;
    doc["settings_saved"] = settingsSaveOk;
    doc["theme"] = themeId; doc["friendly_name"] = friendlyName;
    doc["mdns"] = "ElementIQ.local";
    doc["github_owner"] = githubOwner; doc["github_repo"] = githubRepo;
    doc["ota_auto"] = otaAutoCheck; doc["ota_msg"] = lastOtaMsg; doc["ota_busy"] = otaBusy;
    doc["ota_progress"] = otaProgressPercent;
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!requireWebAuth(req)) return;
    StateGuard guard;
    // Optional ?days=0,1,2 where 0=today (local), 1=yesterday, ... up to HISTORY_DAYS-1
    bool filterDays = false;
    bool daySel[HISTORY_DAYS] = {};
    if (req->hasParam("days")) {
      String raw = req->getParam("days")->value();
      filterDays = true;
      int start = 0;
      while (start < (int)raw.length()) {
        int comma = raw.indexOf(',', start);
        if (comma < 0) comma = raw.length();
        int d = raw.substring(start, comma).toInt();
        if (d >= 0 && d < HISTORY_DAYS) daySel[d] = true;
        start = comma + 1;
      }
    }

    // Local midnight for "today"
    time_t now = time(nullptr);
    uint32_t todayStart = 0;
    if (now > 100000) {
      struct tm t;
      if (localtime_r(&now, &t)) {
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        todayStart = (uint32_t)mktime(&t);
      }
    }

    JsonDocument doc;
    doc["interval_sec"] = (int)(HISTORY_INTERVAL_MS / 1000);
    doc["days_available"] = HISTORY_DAYS;
    doc["count_total"] = histCount;
    JsonArray temps = doc["temps"].to<JsonArray>();
    JsonArray epochs = doc["epochs"].to<JsonArray>();
    uint16_t outCount = 0;
    for (uint16_t i = 0; i < histCount; i++) {
      uint16_t idx = (histHead + HISTORY_POINTS - histCount + i) % HISTORY_POINTS;
      uint32_t ep = histBuf[idx].epoch;
      if (filterDays && todayStart > 0) {
        // Which day offset is this sample on?
        if (ep < todayStart - (HISTORY_DAYS - 1) * 86400UL) continue;
        int dayOffset;
        if (ep >= todayStart) dayOffset = 0;
        else dayOffset = (int)((todayStart - 1 - ep) / 86400UL) + 1;
        if (dayOffset < 0 || dayOffset >= HISTORY_DAYS || !daySel[dayOffset]) continue;
      }
      temps.add((float)histBuf[idx].temp10 / 10.0f);
      epochs.add(ep);
      outCount++;
    }
    doc["count"] = outCount;
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/api/setpoint", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      if (!requireWebAuth(req)) return;
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
    if (!requireWebAuth(req)) return;
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
    if (!requireWebAuth(req)) return;
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
      // Response is sent from the body handler below.
    },
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!requireWebAuth(req)) return;
      static String body;
      if (index == 0) body = "";
      body.concat(reinterpret_cast<const char*>(data), len);
      if (index + len < total) return;

      if (body.length() == 0) {
        req->send(400, "application/json", "{\"error\":\"empty body\"}");
        body = "";
        return;
      }
      if (body.length() > MAX_SETTINGS_BODY) {
        req->send(413, "application/json", "{\"error\":\"body too large\"}");
        body = "";
        return;
      }
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, body);
      body = "";
      if (err) {
        req->send(400, "application/json", "{\"error\":\"invalid json\"}");
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
    if (!requireWebAuth(req)) return;
    if (!queueOtaOperation(false)) {
      req->send(409, "application/json", "{\"error\":\"OTA worker busy\"}");
      return;
    }
    req->send(202, "application/json", "{\"queued\":true}");
  });
  server.on("/api/ota/install", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!requireWebAuth(req)) return;
    if (!queueOtaOperation(true)) {
      req->send(409, "application/json", "{\"error\":\"OTA worker busy\"}");
      return;
    }
    req->send(202, "application/json", "{\"queued\":true}");
  });
  // Browser firmware upload: POST raw .bin body to /api/ota/upload
  // begin(UPDATE_SIZE_UNKNOWN) avoids "Bad Size Given" when Content-Length is
  // missing or wrong. Update.end() runs only in the request-complete handler.
  server.on(
    "/api/ota/upload", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      if (!requireWebAuth(req)) return;
      bool started = false;
      {
        StateGuard guard;
        started = otaInProgress;
      }
      bool ok = false;
      if (started && !Update.hasError()) {
        ok = Update.end(true);
        if (!ok)
          Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
        else
          Serial.println("[OTA] Web upload image OK");
      } else if (started) {
        Serial.printf("[OTA] Web upload had error: %s\n", Update.errorString());
      }
      {
        StateGuard guard;
        otaBusy = false;
        otaInProgress = false;
        if (ok) {
          markOtaSuccess(FIRMWARE_VERSION);
          lastOtaMsg = "Update complete — rebooting";
          otaProgressPercent = 100;
        } else if (started) {
          lastOtaMsg = String("Web upload failed: ") + Update.errorString();
          otaProgressPercent = -1;
        }
        // if !started, lastOtaMsg already set by body handler (too large / begin failed)
      }
      if (ok) {
        req->send(200, "text/plain", "OK");
        delay(500);
        ESP.restart();
      } else {
        if (started) Update.abort();
        req->send(500, "text/plain", lastOtaMsg.c_str());
      }
    },
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!requireWebAuth(req)) return;
      static bool hdrOk = false;
      static bool markerOk = false;
      static size_t scanWindow = 0;
      static uint8_t headBuf[24];
      static size_t headLen = 0;

      if (index == 0) {
        hdrOk = false;
        markerOk = false;
        scanWindow = 0;
        headLen = 0;
        {
          StateGuard guard;
          if (otaBusy) {
            Serial.println("[OTA] Web upload rejected: already busy");
            return;
          }
          otaBusy = true;
          otaInProgress = true;
          otaProgressPercent = 0;
          lastOtaMsg = "Firmware updating... 0%";
          setHeater(false, true);
        }
        size_t freeSketch = ESP.getFreeSketchSpace();
        Serial.printf("[OTA] Web upload start: Content-Length=%u freeOTA=%u\n",
                      (unsigned)total, (unsigned)freeSketch);
        if (total > 0 && total > freeSketch) {
          StateGuard guard;
          lastOtaMsg = String("Firmware too large (") + (total / 1024) + " KB > " +
                       (freeSketch / 1024) + " KB free OTA)";
          otaBusy = false;
          otaInProgress = false;
          otaProgressPercent = -1;
          Serial.println(lastOtaMsg);
          return;
        }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          StateGuard guard;
          lastOtaMsg = String("Update.begin failed: ") + Update.errorString();
          otaBusy = false;
          otaInProgress = false;
          otaProgressPercent = -1;
          Serial.println(lastOtaMsg);
          Update.abort();
          return;
        }
      }
      if (Update.hasError()) return;
      if (len) {
        // ESP image header validation (magic 0xE9 + chip id)
        if (!hdrOk) {
          size_t need = sizeof(headBuf) - headLen;
          size_t take = len < need ? len : need;
          memcpy(headBuf + headLen, data, take);
          headLen += take;
          if (headLen >= 24) {
            if (headBuf[0] != 0xE9) {
              StateGuard guard;
              lastOtaMsg = "Rejected: not a valid ESP firmware image";
              otaBusy = false;
              otaInProgress = false;
              otaProgressPercent = -1;
              Serial.println(lastOtaMsg);
              Update.abort();
              return;
            }
            uint16_t chipId = (uint16_t)headBuf[12] | ((uint16_t)headBuf[13] << 8);
            if (chipId != (uint16_t)OTA_EXPECT_CHIP_ID) {
              StateGuard guard;
              lastOtaMsg = String("Rejected: wrong chip id ") + chipId + " (need ESP32-C3)";
              otaBusy = false;
              otaInProgress = false;
              otaProgressPercent = -1;
              Serial.println(lastOtaMsg);
              Update.abort();
              return;
            }
            hdrOk = true;
            Serial.printf("[OTA] header OK magic=0xE9 chip_id=%u\n", (unsigned)chipId);
          }
        }
        // Require official ElementIQ product marker in the image
        if (!markerOk && scanWindow < 256000) {
          const char* marker = OTA_PRODUCT_MARKER;
          const size_t mlen = strlen(marker);
          if (mlen > 0 && len >= mlen) {
            for (size_t i = 0; i + mlen <= len; i++) {
              if (memcmp(data + i, marker, mlen) == 0) {
                markerOk = true;
                Serial.println("[OTA] product marker found");
                break;
              }
            }
          }
          scanWindow += len;
        }

        if (Update.write(data, len) != len) {
          StateGuard guard;
          lastOtaMsg = String("Write failed: ") + Update.errorString();
          otaProgressPercent = -1;
          Serial.println(lastOtaMsg);
          Update.abort();
          return;
        }
      }
      // At end of body, require marker (header already enforced)
      if (total > 0 && index + len >= total) {
        if (!hdrOk) {
          StateGuard guard;
          lastOtaMsg = "Rejected: incomplete or invalid firmware header";
          otaProgressPercent = -1;
          Serial.println(lastOtaMsg);
          Update.abort();
          return;
        }
        if (!markerOk) {
          StateGuard guard;
          lastOtaMsg = "Rejected: not an official ElementIQ firmware image";
          otaProgressPercent = -1;
          Serial.println(lastOtaMsg);
          Update.abort();
          return;
        }
      }
      if (total > 0) {
        int pct = (int)(((index + len) * 100UL) / total);
        if (pct > 100) pct = 100;
        StateGuard guard;
        otaProgressPercent = pct;
        lastOtaMsg = String("Firmware updating... ") + pct + "%";
      } else if (index + len > 0) {
        StateGuard guard;
        lastOtaMsg = String("Firmware updating... ") + ((index + len) / 1024) + " KB";
      }
    });

  server.on("/api/element/ack", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!requireWebAuth(req)) return;
    StateGuard guard;
    if (!elementFault) {
      req->send(200, "application/json", "{\"ok\":true,\"cleared\":false}");
      return;
    }
    // Only clear after user ack; do not clear if sensor still bad
    if (sensorFault || !tempValid(currentTemp) || !adsReady) {
      req->send(409, "application/json", "{\"error\":\"sensor still unhealthy — cannot clear element fault\"}");
      return;
    }
    clearElementFault();
    Serial.println("[ELEMENT] fault acknowledged and cleared");
    pendingStatePublish = true;
    req->send(200, "application/json", "{\"ok\":true,\"cleared\":true}");
  });

  server.on("/api/factoryreset", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!requireWebAuth(req)) return;
    {
      StateGuard guard;
      pendingFactoryReset = true;
    }
    req->send(202, "text/plain", "Factory reset queued");
  });
  server.on("/api/resetwifi", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!requireWebAuth(req)) return;
    {
      StateGuard guard;
      pendingWifiReset = true;
    }
    req->send(202, "text/plain", "WiFi reset queued");
  });
}

