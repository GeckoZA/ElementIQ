// ---- MQTT ----
void publishState() {
  StateGuard guard;
  if (!mqtt.connected()) return;
  JsonDocument doc;
  if (tempValid(currentTemp))
    doc["temperature"] = (float)(roundf(currentTemp * 10.0f) / 10.0f);
  else
    doc["temperature"] = nullptr;
  float shown = (mode == MODE_BOOST) ? boostTemp : setpoint;
  doc["target_temp"] = (float)(roundf(shown * 10.0f) / 10.0f);
  doc["setpoint"] = (float)(roundf(setpoint * 10.0f) / 10.0f);
  doc["boost_temp"] = (float)(roundf(boostTemp * 10.0f) / 10.0f);
  doc["hysteresis"] = (float)(roundf(hysteresis * 10.0f) / 10.0f);
  doc["mode"] = MODE_NAMES[static_cast<uint8_t>(mode)];
  doc["preset"] = PRESET_NAMES[preset];
  doc["action"] = heaterOn ? "heating" : "idle";
  doc["heater"] = heaterOn;
  doc["sensor_fault"] = sensorFault;
  doc["schedule_active"] = scheduleEnabled && scheduleAllowsHeat();
  doc["runtime_total_h"] = (float)(roundf(runtimeTotalSec / 3600.0f * 10) / 10);
  doc["runtime_today_h"] = (float)(roundf(runtimeTodaySec / 3600.0f * 100) / 100);
  doc["runtime_week_h"] = (float)(roundf(runtimeWeekSec / 3600.0f * 100) / 100);
  char buf[512];
  serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(topicState.c_str(), (const uint8_t*)buf, strlen(buf), true);
}

void publishDiscovery() {
  StateGuard guard;
  if (!mqtt.connected()) return;
  auto pub = [&](const String& topic, JsonDocument& doc) {
    char buf[1400]; size_t n = serializeJson(doc, buf, sizeof(buf));
    mqtt.publish(topic.c_str(), (const uint8_t*)buf, n, true);
  };
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/climate/" + deviceId + "/config";
    JsonDocument doc;
    doc["name"] = friendlyName;
    doc["unique_id"] = deviceId + "_climate_v5";
    doc["icon"] = "mdi:water-boiler";
    JsonArray modes = doc["modes"].to<JsonArray>();
    modes.add("off"); modes.add("auto");
    JsonArray presets = doc["preset_modes"].to<JsonArray>();
    presets.add("none"); presets.add("boost");
    doc["mode_command_topic"] = topicCmd + "/mode";
    doc["mode_state_topic"] = topicState;
    doc["mode_state_template"] = "{% if value_json.mode == 'off' %}off{% else %}auto{% endif %}";
    doc["preset_mode_command_topic"] = topicCmd + "/preset";
    doc["preset_mode_state_topic"] = topicState;
    doc["preset_mode_value_template"] = "{% if value_json.mode == 'boost' %}boost{% else %}none{% endif %}";
    doc["current_temperature_topic"] = topicState;
    doc["current_temperature_template"] = "{{ value_json.temperature }}";
    doc["temperature_command_topic"] = topicCmd + "/setpoint";
    doc["temperature_state_topic"] = topicState;
    doc["temperature_state_template"] = "{{ value_json.target_temp }}";
    doc["action_topic"] = topicState;
    doc["action_template"] = "{{ value_json.action }}";
    doc["min_temp"] = (int)MIN_SETPOINT; doc["max_temp"] = (int)MAX_SETPOINT;
    doc["temp_step"] = 1; doc["temperature_unit"] = "C";
    doc["availability_topic"] = topicAvail;
    doc["payload_available"] = "online"; doc["payload_not_available"] = "offline";
    doc["device"]["identifiers"][0] = String("elementiq_") + deviceId;
    doc["device"]["name"] = friendlyName;
    doc["device"]["sw_version"] = FIRMWARE_VERSION;
    pub(t, doc);
  }
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + deviceId + "_fault/config";
    JsonDocument doc;
    doc["name"] = String(friendlyName) + " Sensor Fault";
    doc["unique_id"] = deviceId + "_fault_v5";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ 'ON' if value_json.sensor_fault else 'OFF' }}";
    doc["device_class"] = "problem";
    doc["availability_topic"] = topicAvail;
    doc["payload_available"] = "online"; doc["payload_not_available"] = "offline";
    doc["device"]["identifiers"][0] = String("elementiq_") + deviceId;
    pub(t, doc);
  }
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_runtime/config";
    JsonDocument doc;
    doc["name"] = String(friendlyName) + " Heater Hours Today";
    doc["unique_id"] = deviceId + "_rt_today_v5";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.runtime_today_h }}";
    doc["unit_of_measurement"] = "h";
    doc["state_class"] = "total_increasing";
    doc["availability_topic"] = topicAvail;
    doc["payload_available"] = "online"; doc["payload_not_available"] = "offline";
    doc["device"]["identifiers"][0] = String("elementiq_") + deviceId;
    pub(t, doc);
  }
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/number/" + deviceId + "_boost/config";
    JsonDocument doc;
    doc["name"] = String(friendlyName) + " Boost Temp";
    doc["unique_id"] = deviceId + "_boost_v5";
    doc["command_topic"] = topicCmd + "/boost_temp";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.boost_temp }}";
    doc["min"] = (int)MIN_SETPOINT; doc["max"] = (int)MAX_SETPOINT; doc["step"] = 1;
    doc["unit_of_measurement"] = "°C";
    doc["availability_topic"] = topicAvail;
    doc["payload_available"] = "online"; doc["payload_not_available"] = "offline";
    doc["device"]["identifiers"][0] = String("elementiq_") + deviceId;
    pub(t, doc);
  }
  mqtt.publish(topicAvail.c_str(), "online", true);
}

bool topicEndsWith(const char* topic, const char* suffix) {
  size_t topicLength = strlen(topic);
  size_t suffixLength = strlen(suffix);
  return topicLength >= suffixLength &&
         strcmp(topic + topicLength - suffixLength, suffix) == 0;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  StateGuard guard;
  constexpr size_t MAX_MQTT_COMMAND_LENGTH = 64;
  if (!topic || length >= MAX_MQTT_COMMAND_LENGTH) {
    Serial.println("[MQTT] command rejected: invalid topic or oversized payload");
    return;
  }

  char msg[MAX_MQTT_COMMAND_LENGTH];
  memcpy(msg, payload, length);
  msg[length] = '\0';
  char* first = msg;
  while (*first && isspace(static_cast<unsigned char>(*first))) first++;
  char* end = first + strlen(first);
  while (end > first && isspace(static_cast<unsigned char>(end[-1]))) *--end = '\0';
  Serial.printf("[MQTT] %s → %s\n", topic, first);

  if (topicEndsWith(topic, "/setpoint") ||
      topicEndsWith(topic, "/boost_temp") ||
      topicEndsWith(topic, "/hysteresis")) {
    char* parsedEnd = nullptr;
    float value = strtof(first, &parsedEnd);
    if (parsedEnd == first || *parsedEnd != '\0' || !isfinite(value)) return;
    if (topicEndsWith(topic, "/setpoint") &&
        value >= MIN_SETPOINT && value <= MAX_SETPOINT) {
      setpoint = value; preset = PRESET_CUSTOM; saveSettings();
    } else if (topicEndsWith(topic, "/boost_temp") &&
               value >= MIN_SETPOINT && value <= MAX_SETPOINT) {
      boostTemp = value; saveSettings();
    } else if (topicEndsWith(topic, "/hysteresis") &&
               value >= 0.5f && value <= 20.0f) {
      hysteresis = value; saveSettings();
    }
  } else if (topicEndsWith(topic, "/mode")) {
    if (!strcmp(first, "off")) setMode(MODE_OFF);
    else if (!strcmp(first, "auto") || !strcmp(first, "heat") || !strcmp(first, "on"))
      setMode(MODE_AUTO);
  } else if (topicEndsWith(topic, "/preset")) {
    if (!strcmp(first, "boost")) setMode(MODE_BOOST);
    else if (!strcmp(first, "none") && mode == MODE_BOOST) setMode(MODE_AUTO);
    else if (!strcmp(first, "home")) applyPreset(PRESET_HOME);
    else if (!strcmp(first, "away")) applyPreset(PRESET_AWAY);
    else if (!strcmp(first, "eco")) applyPreset(PRESET_ECO);
  } else if (topicEndsWith(topic, "/heater")) {
    if (!strcmp(first, "ON") || !strcmp(first, "on") || !strcmp(first, "1"))
      setMode(MODE_AUTO);
    else setMode(MODE_OFF);
  }
  publishState();
}

bool mqttConnect() {
  char host[sizeof(mqtt_host)];
  char portText[sizeof(mqtt_port)];
  char user[sizeof(mqtt_user)];
  char password[sizeof(mqtt_pass)];
  {
    StateGuard guard;
    strlcpy(host, mqtt_host, sizeof(host));
    strlcpy(portText, mqtt_port, sizeof(portText));
    strlcpy(user, mqtt_user, sizeof(user));
    strlcpy(password, mqtt_pass, sizeof(password));
  }
  if (strlen(host) < 2) return false;
  mqtt.setServer(host, atoi(portText));
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(2048);
  mqtt.setSocketTimeout(5);
  mqtt.setKeepAlive(60);
  String clientId = "elementiq-" + deviceId;
  bool ok = (strlen(user) > 0)
    ? mqtt.connect(clientId.c_str(), user, password, topicAvail.c_str(), 0, true, "offline")
    : mqtt.connect(clientId.c_str(), topicAvail.c_str(), 0, true, "offline");
  {
    StateGuard guard;
    mqttIsConnected = ok;
  }
  if (ok) {
    mqtt.subscribe((topicCmd + "/#").c_str());
    publishDiscovery();
    publishState();
  }
  return ok;
}

void mqttLoop() {
  bool enabled;
  bool publishPending;
  char host[sizeof(mqtt_host)];
  {
    StateGuard guard;
    enabled = mqttEnabled;
    strlcpy(host, mqtt_host, sizeof(host));
    publishPending = pendingStatePublish;
    pendingStatePublish = false;
  }
  if (!enabled || strlen(host) < 2) {
    if (mqtt.connected()) mqtt.disconnect();
    StateGuard guard;
    mqttIsConnected = false;
    return;
  }
  if (!mqtt.connected()) {
    {
      StateGuard guard;
      mqttIsConnected = false;
    }
    if (millis() - lastMqttAttempt > MQTT_RECONNECT_MS) {
      lastMqttAttempt = millis();
      mqttConnect();
    }
  } else {
    mqtt.loop();
    {
      StateGuard guard;
      mqttIsConnected = mqtt.connected();
    }
    if (millis() - lastDiscovery > 60000UL) {
      lastDiscovery = millis();
      mqtt.publish(topicAvail.c_str(), "online", true);
    }
    if (publishPending) publishState();
  }
}

void setupArduinoOTA() {
  ArduinoOTA.setHostname("ElementIQ");
  ArduinoOTA.onStart([]() {
    StateGuard guard;
    otaInProgress = true;
    setHeater(false, true);
  });
  ArduinoOTA.begin();
}

bool checkGitHubUpdate(bool install, const char* owner, const char* repo, String& result) {
  if (!owner[0] || !repo[0]) { result = "GitHub owner/repo not set"; return false; }
  if (WiFi.status() != WL_CONNECTED) { result = "WiFi not connected"; return false; }
  String api = String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http; http.setTimeout(15000); http.setUserAgent("ElementIQ");
  if (!http.begin(client, api)) { result = "HTTP begin failed"; return false; }
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code == 404) {
      result = String("Repo/release not found: ") + owner + "/" + repo +
               " (create a public GitHub Release with firmware.bin)";
    } else if (code < 0) {
      result = String("GitHub request failed (") + code + ")";
    } else {
      result = String("GitHub API HTTP ") + code;
    }
    return false;
  }
  String body = http.getString(); http.end();
  JsonDocument doc;
  if (deserializeJson(doc, body)) { result = "JSON error"; return false; }
  const char* tag = doc["tag_name"];
  if (!tag) { result = "No tag"; return false; }
  String remoteVer = tag;
  if (remoteVer.startsWith("v") || remoteVer.startsWith("V")) remoteVer = remoteVer.substring(1);
  String localVer = FIRMWARE_VERSION;
  if (localVer.startsWith("v") || localVer.startsWith("V")) localVer = localVer.substring(1);
  if (remoteVer == localVer) { result = String("Up to date (") + FIRMWARE_VERSION + ")"; return false; }
  String binUrl;
  for (JsonObject a : doc["assets"].as<JsonArray>()) {
    const char* name = a["name"];
    if (name && String(name) == GITHUB_ASSET_NAME) { binUrl = a["browser_download_url"].as<String>(); break; }
  }
  if (!binUrl.length()) { result = "No firmware.bin asset"; return false; }
  result = String("Update available: ") + tag;
  if (!install) return true;
  HTTPUpdate updater;
  updater.rebootOnUpdate(true);
  t_httpUpdate_return ret = updater.update(client, binUrl);
  if (ret == HTTP_UPDATE_FAILED) result = updater.getLastErrorString();
  return ret == HTTP_UPDATE_OK;
}

enum OtaCommand : uint8_t { OTA_CHECK = 1, OTA_INSTALL = 2 };
QueueHandle_t otaQueue = nullptr;

void otaWorker(void*) {
  for (;;) {
    OtaCommand command;
    if (xQueueReceive(otaQueue, &command, portMAX_DELAY) != pdTRUE) continue;
    char owner[sizeof(githubOwner)];
    char repo[sizeof(githubRepo)];
    {
      StateGuard guard;
      strlcpy(owner, githubOwner, sizeof(owner));
      strlcpy(repo, githubRepo, sizeof(repo));
    }
    String message;
    checkGitHubUpdate(command == OTA_INSTALL, owner, repo, message);
    {
      StateGuard guard;
      lastOtaMsg = message;
      otaBusy = false;
      otaInProgress = false;
    }
  }
}

bool initializeOtaWorker() {
  otaQueue = xQueueCreate(1, sizeof(OtaCommand));
  if (!otaQueue) return false;
  return xTaskCreate(otaWorker, "elementiq_ota", 10240, nullptr, 1, nullptr) == pdPASS;
}

bool queueOtaOperation(bool install) {
  StateGuard guard;
  if (!otaQueue || otaBusy) return false;
  OtaCommand command = install ? OTA_INSTALL : OTA_CHECK;
  otaBusy = true;
  if (install) {
    otaInProgress = true;
    setHeater(false, true);
  }
  if (xQueueSend(otaQueue, &command, 0) != pdTRUE) {
    otaBusy = false;
    otaInProgress = false;
    return false;
  }
  return true;
}

