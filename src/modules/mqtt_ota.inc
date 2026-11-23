#include "ota_marker.h"
// ---- MQTT ----
void publishUpdateState();   // defined below; called from publishState

// OTA worker queue (worker body defined later, after checkGitHubUpdate)
enum OtaCommand : uint8_t { OTA_CHECK = 1, OTA_INSTALL = 2 };
QueueHandle_t otaQueue = nullptr;
void otaWorker(void*);  // defined later

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
  otaProgressPercent = 0;
  lastOtaMsg = install ? "Firmware updating..." : "Checking for updates...";
  if (install) {
    otaInProgress = true;
    setHeater(false, true);
  }
  if (xQueueSend(otaQueue, &command, 0) != pdTRUE) {
    otaBusy = false;
    otaInProgress = false;
    otaProgressPercent = -1;
    return false;
  }
  return true;
}

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
  // Split HA selects: Control (off/auto/boost) and Preset (home/away/eco/none)
  doc["control"] = MODE_NAMES[static_cast<uint8_t>(mode)];
  if (preset == PRESET_HOME) doc["preset_select"] = "home";
  else if (preset == PRESET_AWAY) doc["preset_select"] = "away";
  else if (preset == PRESET_ECO) doc["preset_select"] = "eco";
  else doc["preset_select"] = "none";
  doc["action"] = heaterOn ? "heating" : "idle";
  doc["heater"] = heaterOn;
  doc["sensor_fault"] = sensorFault;
  doc["sensor_status"] = sensorFaultName();
  doc["element_fault"] = elementFault;
  doc["element_status"] = elementFault ? "fault" : "ok";
  doc["device_status"] = (sensorFault || elementFault) ? "fault" : (WiFi.status()==WL_CONNECTED ? "online" : "offline");
  doc["schedule_active"] = scheduleEnabled && scheduleAllowsHeat();
  doc["schedule_enabled"] = scheduleEnabled;
  doc["heat_block_reason"] = heatBlockReason();
  doc["wifi_rssi"] = WiFi.status()==WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["firmware"] = FIRMWARE_VERSION;
  doc["update_available"] = updateAvailable;
  if (latestFirmware[0]) doc["latest_firmware"] = latestFirmware;
  else doc["latest_firmware"] = FIRMWARE_VERSION;
  doc["runtime_total_h"] = (float)(roundf(runtimeTotalSec / 3600.0f * 10) / 10);
  doc["runtime_today_h"] = (float)(roundf(runtimeTodaySec / 3600.0f * 100) / 100);
  doc["runtime_week_h"] = (float)(roundf(runtimeWeekSec / 3600.0f * 100) / 100);
  doc["runtime_month_h"] = (float)(roundf(runtimeMonthSec / 3600.0f * 100) / 100);
  char buf[640];
  serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(topicState.c_str(), (const uint8_t*)buf, strlen(buf), true);
  publishUpdateState();
}

// Home Assistant MQTT Update entity state (shows on Settings → Updates)
void publishUpdateState() {
  if (!mqtt.connected()) return;
  JsonDocument doc;
  doc["installed_version"] = FIRMWARE_VERSION;
  doc["latest_version"] = latestFirmware[0] ? latestFirmware : FIRMWARE_VERSION;
  doc["title"] = "ElementIQ Firmware";
  if (latestReleaseUrl[0]) doc["release_url"] = latestReleaseUrl;
  if (updateAvailable)
    doc["release_summary"] = String("Update available: ") + latestFirmware;
  else
    doc["release_summary"] = "Up to date";
  char buf[384];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n > 0 && n < sizeof(buf))
    mqtt.publish((topicBase + "/update").c_str(), (const uint8_t*)buf, n, true);
}

void publishDiscovery() {
  StateGuard guard;
  if (!mqtt.connected()) return;

  // Shared device block so every entity groups under one HA device
  // named after the ElementIQ friendly name (not "MQTT").
  auto fillDevice = [&](JsonDocument& doc) {
    doc["device"]["identifiers"][0] = String("elementiq_") + deviceId;
    doc["device"]["name"] = friendlyName;
    doc["device"]["manufacturer"] = "ElementIQ";
    doc["device"]["model"] = "ElementIQ Heater Controller";
    doc["device"]["sw_version"] = FIRMWARE_VERSION;
    if (deviceSerial && deviceSerial[0])
      doc["device"]["serial_number"] = deviceSerial;
    doc["availability_topic"] = topicAvail;
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";
  };

  auto pub = [&](const String& topic, JsonDocument& doc) {
    char buf[1600];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
      eqLogf("[MQTT] discovery payload too large for %s\n", topic.c_str());
      return;
    }
    mqtt.publish(topic.c_str(), (const uint8_t*)buf, n, true);
  };

  // --- Climate (main heater control in HA) ---
  // HA has no separate "heater" domain; climate with heat/off is the standard
  // water-heater / thermostat pattern and shows temperature + mode controls.
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/climate/" + deviceId + "/config";
    JsonDocument doc;
    doc["name"] = friendlyName;
    doc["unique_id"] = deviceId + "_climate_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_heater";
    doc["icon"] = "mdi:water-boiler";
    JsonArray modes = doc["modes"].to<JsonArray>();
    modes.add("off");
    modes.add("heat");
    JsonArray presets = doc["preset_modes"].to<JsonArray>();
    presets.add("none");
    presets.add("home");
    presets.add("away");
    presets.add("eco");
    doc["mode_command_topic"] = topicCmd + "/mode";
    doc["mode_state_topic"] = topicState;
    // Climate HVAC modes: off / heat (boost still available via Control select)
    doc["mode_state_template"] =
        "{% if value_json.mode == 'off' %}off{% else %}heat{% endif %}";
    doc["preset_mode_command_topic"] = topicCmd + "/preset";
    doc["preset_mode_state_topic"] = topicState;
    doc["preset_mode_value_template"] =
        "{% if value_json.preset == 'home' %}home"
        "{% elif value_json.preset == 'away' %}away"
        "{% elif value_json.preset == 'eco' %}eco"
        "{% else %}none{% endif %}";
    doc["current_temperature_topic"] = topicState;
    doc["current_temperature_template"] = "{{ value_json.temperature }}";
    doc["temperature_command_topic"] = topicCmd + "/setpoint";
    doc["temperature_state_topic"] = topicState;
    doc["temperature_state_template"] = "{{ value_json.target_temp }}";
    doc["action_topic"] = topicState;
    doc["action_template"] = "{{ value_json.action }}";
    doc["min_temp"] = (int)MIN_SETPOINT;
    doc["max_temp"] = (int)MAX_SETPOINT;
    doc["temp_step"] = 1;
    doc["temperature_unit"] = "C";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Control select: Off / Auto / Boost ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/select/" + deviceId + "_control/config";
    JsonDocument doc;
    doc["name"] = "Control";
    doc["unique_id"] = deviceId + "_control_v1";
    doc["object_id"] = String("elementiq_") + deviceId + "_control";
    doc["command_topic"] = topicCmd + "/control";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.control }}";
    JsonArray opts = doc["options"].to<JsonArray>();
    opts.add("off");
    opts.add("auto");
    opts.add("boost");
    doc["icon"] = "mdi:water-boiler";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Preset select: Home / Away / Eco (+ none) ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/select/" + deviceId + "_preset/config";
    JsonDocument doc;
    doc["name"] = "Preset";
    doc["unique_id"] = deviceId + "_preset_v1";
    doc["object_id"] = String("elementiq_") + deviceId + "_preset";
    doc["command_topic"] = topicCmd + "/preset";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.preset_select }}";
    JsonArray opts = doc["options"].to<JsonArray>();
    opts.add("none");
    opts.add("home");
    opts.add("away");
    opts.add("eco");
    doc["icon"] = "mdi:home-thermometer";
    fillDevice(doc);
    pub(t, doc);
  }

  // Clear legacy combined Operation entity (HA keeps stale discovery otherwise)
  mqtt.publish((String(HA_DISCOVERY_PREFIX) + "/select/" + deviceId + "_operation/config").c_str(),
               "", true);

  // --- Current temperature (sensor) ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_temp/config";
    JsonDocument doc;
    doc["name"] = "Temperature";
    doc["unique_id"] = deviceId + "_temp_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_temperature";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.temperature }}";
    doc["unit_of_measurement"] = "°C";
    doc["device_class"] = "temperature";
    doc["state_class"] = "measurement";
    doc["icon"] = "mdi:thermometer";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Target / setpoint (number) ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/number/" + deviceId + "_setpoint/config";
    JsonDocument doc;
    doc["name"] = "Setpoint";
    doc["unique_id"] = deviceId + "_setpoint_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_setpoint";
    doc["command_topic"] = topicCmd + "/setpoint";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.setpoint }}";
    doc["min"] = (int)MIN_SETPOINT;
    doc["max"] = (int)MAX_SETPOINT;
    doc["step"] = 1;
    doc["unit_of_measurement"] = "°C";
    doc["device_class"] = "temperature";
    doc["mode"] = "box";
    doc["icon"] = "mdi:thermometer-lines";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Boost temperature ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/number/" + deviceId + "_boost/config";
    JsonDocument doc;
    doc["name"] = "Boost Temp";
    doc["unique_id"] = deviceId + "_boost_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_boost_temp";
    doc["command_topic"] = topicCmd + "/boost_temp";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.boost_temp }}";
    doc["min"] = (int)MIN_SETPOINT;
    doc["max"] = (int)MAX_SETPOINT;
    doc["step"] = 1;
    doc["unit_of_measurement"] = "°C";
    doc["device_class"] = "temperature";
    doc["mode"] = "box";
    doc["icon"] = "mdi:thermometer-chevron-up";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Hysteresis ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/number/" + deviceId + "_hyst/config";
    JsonDocument doc;
    doc["name"] = "Hysteresis";
    doc["unique_id"] = deviceId + "_hyst_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_hysteresis";
    doc["command_topic"] = topicCmd + "/hysteresis";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.hysteresis }}";
    doc["min"] = 0.5;
    doc["max"] = 20;
    doc["step"] = 0.5;
    doc["unit_of_measurement"] = "°C";
    doc["mode"] = "box";
    doc["icon"] = "mdi:arrow-expand-vertical";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Heater on/off switch (simple control) ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/switch/" + deviceId + "_heater/config";
    JsonDocument doc;
    doc["name"] = "Heater";
    doc["unique_id"] = deviceId + "_heater_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_heater";
    doc["command_topic"] = topicCmd + "/heater";
    doc["state_topic"] = topicState;
    doc["value_template"] =
        "{% if value_json.mode == 'off' %}OFF{% else %}ON{% endif %}";
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["icon"] = "mdi:water-boiler";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Heater actively heating (binary sensor) ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + deviceId + "_heating/config";
    JsonDocument doc;
    doc["name"] = "Heating";
    doc["unique_id"] = deviceId + "_heating_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_heating";
    doc["state_topic"] = topicState;
    doc["value_template"] =
        "{{ 'ON' if value_json.heater else 'OFF' }}";
    doc["device_class"] = "heat";
    doc["icon"] = "mdi:fire";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Sensor fault ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + deviceId + "_fault/config";
    JsonDocument doc;
    doc["name"] = "Sensor Fault";
    doc["unique_id"] = deviceId + "_fault_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_sensor_fault";
    doc["state_topic"] = topicState;
    doc["value_template"] =
        "{{ 'ON' if value_json.sensor_fault else 'OFF' }}";
    doc["device_class"] = "problem";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Element fault ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + deviceId + "_element/config";
    JsonDocument doc;
    doc["name"] = "Element Fault";
    doc["unique_id"] = deviceId + "_element_v6";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ 'ON' if value_json.element_fault else 'OFF' }}";
    doc["device_class"] = "problem";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Heat block reason ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_block/config";
    JsonDocument doc;
    doc["name"] = "Heat Block Reason";
    doc["unique_id"] = deviceId + "_block_v6";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.heat_block_reason }}";
    doc["icon"] = "mdi:information";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- WiFi RSSI ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_rssi/config";
    JsonDocument doc;
    doc["name"] = "WiFi Signal";
    doc["unique_id"] = deviceId + "_rssi_v6";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.wifi_rssi }}";
    doc["unit_of_measurement"] = "dBm";
    doc["device_class"] = "signal_strength";
    doc["state_class"] = "measurement";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Runtime month ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_rt_month/config";
    JsonDocument doc;
    doc["name"] = "Heating Runtime Month";
    doc["unique_id"] = deviceId + "_rt_month_v6";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.runtime_month_h }}";
    doc["unit_of_measurement"] = "h";
    doc["state_class"] = "total_increasing";
    doc["icon"] = "mdi:timer";
    fillDevice(doc);
    pub(t, doc);
  }


  // --- Schedule active ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + deviceId + "_sched/config";
    JsonDocument doc;
    doc["name"] = "Schedule Active";
    doc["unique_id"] = deviceId + "_sched_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_schedule_active";
    doc["state_topic"] = topicState;
    doc["value_template"] =
        "{{ 'ON' if value_json.schedule_active else 'OFF' }}";
    doc["icon"] = "mdi:calendar-clock";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Runtime today ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_rt_today/config";
    JsonDocument doc;
    doc["name"] = "Heater Hours Today";
    doc["unique_id"] = deviceId + "_rt_today_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_runtime_today";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.runtime_today_h }}";
    doc["unit_of_measurement"] = "h";
    doc["state_class"] = "total_increasing";
    doc["icon"] = "mdi:timer-outline";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Runtime week ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_rt_week/config";
    JsonDocument doc;
    doc["name"] = "Heater Hours This Week";
    doc["unique_id"] = deviceId + "_rt_week_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_runtime_week";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.runtime_week_h }}";
    doc["unit_of_measurement"] = "h";
    doc["state_class"] = "total_increasing";
    doc["icon"] = "mdi:timer-outline";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Runtime total ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_rt_total/config";
    JsonDocument doc;
    doc["name"] = "Heater Hours Total";
    doc["unique_id"] = deviceId + "_rt_total_v6";
    doc["object_id"] = String("elementiq_") + deviceId + "_runtime_total";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.runtime_total_h }}";
    doc["unit_of_measurement"] = "h";
    doc["state_class"] = "total_increasing";
    doc["icon"] = "mdi:timer";
    fillDevice(doc);
    pub(t, doc);
  }

  // Clear legacy discovery topic that used a different path (old runtime sensor)
  mqtt.publish((String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_runtime/config").c_str(),
               (const uint8_t*)"", 0, true);

  // --- Firmware version (sensor) ---
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_fw/config";
    JsonDocument doc;
    doc["name"] = "Firmware Version";
    doc["unique_id"] = deviceId + "_fw_v1";
    doc["object_id"] = String("elementiq_") + deviceId + "_firmware";
    doc["state_topic"] = topicState;
    doc["value_template"] = "{{ value_json.firmware }}";
    doc["icon"] = "mdi:chip";
    doc["entity_category"] = "diagnostic";
    fillDevice(doc);
    pub(t, doc);
  }

  // --- Firmware update (HA Update entity → Settings → Updates) ---
  // https://www.home-assistant.io/integrations/update.mqtt/
  {
    String t = String(HA_DISCOVERY_PREFIX) + "/update/" + deviceId + "_fw/config";
    JsonDocument doc;
    doc["name"] = "Firmware";
    doc["unique_id"] = deviceId + "_update_v1";
    doc["object_id"] = String("elementiq_") + deviceId + "_firmware";
    doc["state_topic"] = topicBase + "/update";
    doc["command_topic"] = topicCmd + "/update";
    doc["payload_install"] = "install";
    doc["device_class"] = "firmware";
    doc["entity_category"] = "config";
    fillDevice(doc);
    pub(t, doc);
  }

  mqtt.publish(topicAvail.c_str(), "online", true);
  publishUpdateState();
  eqLogf("[MQTT] discovery published for device '%s'\n", friendlyName);
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
    eqLog("[MQTT] command rejected: invalid topic or oversized payload");
    return;
  }

  char msg[MAX_MQTT_COMMAND_LENGTH];
  memcpy(msg, payload, length);
  msg[length] = '\0';
  char* first = msg;
  while (*first && isspace(static_cast<unsigned char>(*first))) first++;
  char* end = first + strlen(first);
  while (end > first && isspace(static_cast<unsigned char>(end[-1]))) *--end = '\0';
  eqLogf("[MQTT] %s → %s\n", topic, first);

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
  } else if (topicEndsWith(topic, "/mode") || topicEndsWith(topic, "/control")) {
    // Control: Off / Auto / Boost
    if (!strcmp(first, "off")) setMode(MODE_OFF);
    else if (!strcmp(first, "auto") || !strcmp(first, "heat") || !strcmp(first, "on"))
      setMode(MODE_AUTO);
    else if (!strcmp(first, "boost")) setMode(MODE_BOOST);
  } else if (topicEndsWith(topic, "/preset")) {
    // Presets only: Home / Away / Eco / none (boost is Control, not a preset)
    if (!strcmp(first, "none") || !strcmp(first, "custom")) {
      preset = PRESET_CUSTOM;
      saveSettings();
    } else if (!strcmp(first, "home")) {
      if (mode == MODE_OFF) setMode(MODE_AUTO);
      applyPreset(PRESET_HOME);
    } else if (!strcmp(first, "away")) {
      if (mode == MODE_OFF) setMode(MODE_AUTO);
      applyPreset(PRESET_AWAY);
    } else if (!strcmp(first, "eco")) {
      if (mode == MODE_OFF) setMode(MODE_AUTO);
      applyPreset(PRESET_ECO);
    } else if (!strcmp(first, "boost")) {
      // Legacy: older HA climate sent boost as preset
      setMode(MODE_BOOST);
    }
  } else if (topicEndsWith(topic, "/operation")) {
    // Legacy combined topic — still accepted for old automations
    if (!strcmp(first, "off")) setMode(MODE_OFF);
    else if (!strcmp(first, "auto")) setMode(MODE_AUTO);
    else if (!strcmp(first, "boost")) setMode(MODE_BOOST);
    else if (!strcmp(first, "home")) {
      if (mode == MODE_OFF) setMode(MODE_AUTO);
      applyPreset(PRESET_HOME);
    } else if (!strcmp(first, "away")) {
      if (mode == MODE_OFF) setMode(MODE_AUTO);
      applyPreset(PRESET_AWAY);
    } else if (!strcmp(first, "eco")) {
      if (mode == MODE_OFF) setMode(MODE_AUTO);
      applyPreset(PRESET_ECO);
    }
  } else if (topicEndsWith(topic, "/heater")) {
    if (!strcmp(first, "ON") || !strcmp(first, "on") || !strcmp(first, "1"))
      setMode(MODE_AUTO);
    else setMode(MODE_OFF);
  } else if (topicEndsWith(topic, "/update") || topicEndsWith(topic, "/ota/install")) {
    // HA Update entity sends "install"; also accept explicit ota/install
    if (!strcmp(first, "install") || !strcmp(first, "INSTALL") ||
        !strcmp(first, "update") || !strcmp(first, "1")) {
      if (!updateAvailable && latestFirmware[0] == '\0') {
        // No cached release info yet — queue a check-then-install via install path
        eqLog("[MQTT] firmware install requested");
      }
      if (!queueOtaOperation(true))
        eqLog("[MQTT] OTA install queue failed (busy?)");
    } else if (!strcmp(first, "check") || !strcmp(first, "CHECK")) {
      if (!queueOtaOperation(false))
        eqLog("[MQTT] OTA check queue failed (busy?)");
    }
  } else if (topicEndsWith(topic, "/ota/check")) {
    if (!queueOtaOperation(false))
      eqLog("[MQTT] OTA check queue failed (busy?)");
  }
  publishState();
}

bool mqttConnect() {
  if (WiFi.status() != WL_CONNECTED) {
    eqLog("[MQTT] skip connect: WiFi not connected");
    return false;
  }

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
  // Trim trailing whitespace that sometimes sneaks in from the UI
  auto trimRight = [](char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
      s[--n] = '\0';
    }
  };
  trimRight(host);
  trimRight(portText);
  trimRight(user);
  trimRight(password);

  if (strlen(host) < 2) {
    eqLog("[MQTT] skip connect: host empty");
    return false;
  }

  int port = atoi(portText);
  if (port < 1 || port > 65535) port = 1883;

  // Network context – critical for diagnosing state=-2
  IPAddress localIp = WiFi.localIP();
  IPAddress gateway = WiFi.gatewayIP();
  IPAddress subnet = WiFi.subnetMask();
  eqLogf("[MQTT] net: ip=%s gw=%s mask=%s ssid=%s rssi=%d\n",
                localIp.toString().c_str(),
                gateway.toString().c_str(),
                subnet.toString().c_str(),
                WiFi.SSID().c_str(),
                WiFi.RSSI());

  // Quick TCP reachability probe (isolates pure network vs MQTT protocol)
  {
    WiFiClient probe;
    probe.setTimeout(3);
    eqLogf("[MQTT] TCP probe %s:%d ... ", host, port);
    if (probe.connect(host, port)) {
      eqLog("OK");
      probe.stop();
    } else {
      eqLog("FAILED (host unreachable or port closed)");
      eqLog("[MQTT] check: same subnet? broker listening on 1883? firewall?");
      {
        StateGuard guard;
        mqttIsConnected = false;
      }
      return false;
    }
  }

  // Clean any half-open PubSubClient state before retrying
  if (mqtt.connected()) mqtt.disconnect();
  wifiClient.stop();

  mqtt.setServer(host, port);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(2048);
  mqtt.setSocketTimeout(5);
  mqtt.setKeepAlive(60);

  String clientId = "elementiq-" + deviceId;
  eqLogf("[MQTT] connecting to %s:%d as '%s' (user='%s', pass=%s)\n",
                host, port, clientId.c_str(), user,
                password[0] ? "set" : "empty");

  bool ok = (strlen(user) > 0)
    ? mqtt.connect(clientId.c_str(), user, password, topicAvail.c_str(), 0, true, "offline")
    : mqtt.connect(clientId.c_str(), topicAvail.c_str(), 0, true, "offline");

  {
    StateGuard guard;
    mqttIsConnected = ok;
  }

  if (ok) {
    eqLog("[MQTT] connected");
    mqtt.subscribe((topicCmd + "/#").c_str());
    publishDiscovery();
    publishState();
  } else {
    int st = mqtt.state();
    const char* reason = "unknown";
    switch (st) {
      case -4: reason = "connection timeout"; break;
      case -3: reason = "connection lost"; break;
      case -2: reason = "connect failed (network)"; break;
      case -1: reason = "disconnected"; break;
      case  1: reason = "bad protocol"; break;
      case  2: reason = "bad client id"; break;
      case  3: reason = "server unavailable"; break;
      case  4: reason = "bad credentials"; break;
      case  5: reason = "unauthorized"; break;
      default: break;
    }
    eqLogf("[MQTT] connect failed, state=%d (%s)\n", st, reason);
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
  // Don't hammer the broker while STA is down
  if (WiFi.status() != WL_CONNECTED) {
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

// Periodic GitHub release check when ota_auto is enabled (default interval: 10 days).
// Survives reboot via NVS timestamp. Check-only — never installs automatically.
void processOtaAutoCheck() {
  bool enabled;
  {
    StateGuard guard;
    enabled = otaAutoCheck;
  }
  if (!enabled) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (otaBusy || otaInProgress) return;

  time_t now = time(nullptr);
  if (now < 100000) return;  // need valid NTP time for interval

  uint32_t last = 0;
  {
    StateGuard guard;
    last = lastOtaCheckEpoch;
  }
  // Load from NVS once if RAM is still zero (after boot)
  static bool nvsLoaded = false;
  if (!nvsLoaded) {
    nvsLoaded = true;
    Preferences p;
    if (p.begin("eiq", true)) {
      last = p.getUInt("otaChk", 0);
      p.end();
      StateGuard guard;
      lastOtaCheckEpoch = last;
    }
  }

  if (last != 0 && (uint32_t)now >= last &&
      ((uint32_t)now - last) < OTA_AUTO_CHECK_INTERVAL_S)
    return;

  // Avoid hammering GitHub on repeated failures: at most one attempt per hour
  // until a check records a new lastOtaCheckEpoch.
  static uint32_t lastAttemptMs = 0;
  if (lastAttemptMs && (millis() - lastAttemptMs) < 3600000UL) return;
  lastAttemptMs = millis();

  eqLog("[OTA] scheduled GitHub check (10-day interval)");
  queueOtaOperation(false);
}

#if ENABLE_ARDUINO_OTA
// Development only: ArduinoOTA on the LAN has no authentication here.
void setupArduinoOTA() {
  ArduinoOTA.setHostname("ElementIQ");
  ArduinoOTA.onStart([]() {
    StateGuard guard;
    otaInProgress = true;
    setHeater(false, true);
  });
  ArduinoOTA.onEnd([]() {
    StateGuard guard;
    otaRebootPending = true;
    setHeater(false, true);
    otaGuardArm();
  });
  ArduinoOTA.begin();
}
#endif


// Download a firmware image over HTTPS, following redirects (required for GitHub assets).
// HTTPUpdate alone often returns "HTTP error: Wrong HTTP code" on GitHub 302 → CDN.
bool installFirmwareFromUrl(const String& url, String& result) {
  WiFiClientSecure client;
  #if OTA_TLS_INSECURE
  client.setInsecure();
#else
  // Production intent: verify TLS. Until a pinned CA is embedded, log loudly.
  // Prefer building with OTA_TLS_INSECURE 0 and adding setCACert(github_root_pem).
  client.setInsecure();
  eqLog("[OTA] TLS not pinned — enable setCACert for production");
#endif
  client.setTimeout(30);

  HTTPClient http;
  http.setTimeout(60000);  // max uint16_t is 65535 ms
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setUserAgent("ElementIQ");
  http.addHeader("Accept", "application/octet-stream");

  {
    StateGuard guard;
    lastOtaMsg = "Downloading firmware...";
    otaProgressPercent = 0;
  }

  if (!http.begin(client, url)) {
    result = "Firmware download begin failed";
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    result = String("Firmware download HTTP ") + code +
             " (GitHub asset redirects must be followed)";
    http.end();
    return false;
  }

  int contentLen = http.getSize();
  WiFiClient& stream = http.getStream();

  if (!Update.begin(contentLen > 0 ? (size_t)contentLen : UPDATE_SIZE_UNKNOWN)) {
    result = String("Update.begin failed: ") + Update.errorString();
    http.end();
    return false;
  }

  {
    StateGuard guard;
    lastOtaMsg = "Firmware updating...";
    otaProgressPercent = 0;
  }

  // Chunked write so we can report progress to the web UI
  uint8_t buf[1024];
  size_t written = 0;
  int lastPct = -1;
  bool hdrOk = false;
  MarkerScanner marker;            // scans the WHOLE image, across chunk boundaries
  marker.begin(OTA_PRODUCT_MARKER);
  while (http.connected() && (contentLen <= 0 || (int)written < contentLen)) {
    size_t avail = stream.available();
    if (!avail) {
      delay(1);
      if (!stream.connected()) break;
      continue;
    }
    size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
    if (contentLen > 0 && written + toRead > (size_t)contentLen)
      toRead = (size_t)contentLen - written;
    int n = stream.readBytes(buf, toRead);
    if (n <= 0) break;
    if (!hdrOk && written == 0 && n > 0) {
      if ((uint8_t)buf[0] != 0xE9) {
        result = "Rejected: not a valid ESP firmware image";
        Update.abort(); http.end(); return false;
      }
      if (n >= 14) {
        uint16_t chipId = (uint16_t)buf[12] | ((uint16_t)buf[13] << 8);
        if (chipId != (uint16_t)OTA_EXPECT_CHIP_ID) {
          result = String("Rejected: wrong chip id ") + chipId;
          Update.abort(); http.end(); return false;
        }
      }
      hdrOk = true;
    }
    marker.feed(buf, (size_t)n);
    if (Update.write(buf, n) != (size_t)n) {
      result = String("Write failed: ") + Update.errorString();
      Update.abort();
      http.end();
      return false;
    }
    written += n;
    if (contentLen > 0) {
      int pct = (int)((written * 100UL) / (size_t)contentLen);
      if (pct > 100) pct = 100;
      if (pct != lastPct) {
        lastPct = pct;
        StateGuard guard;
        otaProgressPercent = pct;
        lastOtaMsg = String("Firmware updating... ") + pct + "%";
      }
    }
  }

  if (!marker.found) {
    result = "Rejected: not an official ElementIQ firmware image";
    Update.abort();
    http.end();
    return false;
  }
  if (contentLen > 0 && written != (size_t)contentLen) {
    result = String("Incomplete write ") + written + "/" + contentLen;
    Update.abort();
    http.end();
    return false;
  }
  if (written == 0) {
    result = "No firmware data received";
    Update.abort();
    http.end();
    return false;
  }

  if (!Update.end()) {
    result = String("Update.end failed: ") + Update.errorString();
    http.end();
    return false;
  }

  http.end();
  {
    StateGuard guard;
    otaProgressPercent = 100;
    lastOtaMsg = "Update complete — rebooting";
  }
  result = "Update complete — rebooting";
  return true;
}


// Compare dotted version strings (optional leading 'v'). Returns <0 if a<b, 0 if equal, >0 if a>b.
// Non-numeric suffixes are ignored segment-wise; missing segments treated as 0.
static int compareFirmwareVersions(const char* a, const char* b) {
  auto skipV = [](const char* s) -> const char* {
    if (!s) return "";
    if (*s == 'v' || *s == 'V') ++s;
    return s;
  };
  const char* pa = skipV(a);
  const char* pb = skipV(b);
  while (*pa || *pb) {
    long va = 0, vb = 0;
    if (*pa) {
      char* end = nullptr;
      va = strtol(pa, &end, 10);
      pa = end;
      if (*pa == '.' || *pa == '-' || *pa == '_') ++pa;
      else { while (*pa && *pa != '.' && *pa != '-' && *pa != '_') ++pa; if (*pa) ++pa; }
    }
    if (*pb) {
      char* end = nullptr;
      vb = strtol(pb, &end, 10);
      pb = end;
      if (*pb == '.' || *pb == '-' || *pb == '_') ++pb;
      else { while (*pb && *pb != '.' && *pb != '-' && *pb != '_') ++pb; if (*pb) ++pb; }
    }
    if (va < vb) return -1;
    if (va > vb) return 1;
  }
  return 0;
}

bool checkGitHubUpdate(bool install, const char* owner, const char* repo, String& result) {
  otaCheckContactOk = false;
  if (!owner[0] || !repo[0]) { result = "GitHub owner/repo not set"; return false; }
  if (WiFi.status() != WL_CONNECTED) { result = "WiFi not connected"; return false; }

  String api = String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest";
  WiFiClientSecure client;
  #if OTA_TLS_INSECURE
  client.setInsecure();
#else
  // Production intent: verify TLS. Until a pinned CA is embedded, log loudly.
  // Prefer building with OTA_TLS_INSECURE 0 and adding setCACert(github_root_pem).
  client.setInsecure();
  eqLog("[OTA] TLS not pinned — enable setCACert for production");
#endif
  client.setTimeout(20);

  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setUserAgent("ElementIQ");
  http.addHeader("Accept", "application/vnd.github+json");

  if (!http.begin(client, api)) { result = "HTTP begin failed"; return false; }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
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
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) { result = "JSON error"; return false; }
  const char* tag = doc["tag_name"];
  if (!tag) { result = "No tag"; return false; }

  String remoteVer = tag;
  if (remoteVer.startsWith("v") || remoteVer.startsWith("V")) remoteVer = remoteVer.substring(1);
  String localVer = FIRMWARE_VERSION;
  if (localVer.startsWith("v") || localVer.startsWith("V")) localVer = localVer.substring(1);

  // Always record the remote tag for the HA Update entity
  otaCheckContactOk = true;
  {
    StateGuard guard;
    strlcpy(latestFirmware, tag, sizeof(latestFirmware));
    const char* htmlUrl = doc["html_url"];
    if (htmlUrl && htmlUrl[0])
      strlcpy(latestReleaseUrl, htmlUrl, sizeof(latestReleaseUrl));
    else
      snprintf(latestReleaseUrl, sizeof(latestReleaseUrl),
               "https://github.com/%s/%s/releases/tag/%s", owner, repo, tag);
  }

  int cmp = compareFirmwareVersions(remoteVer.c_str(), localVer.c_str());
  if (cmp < 0) {
    {
      StateGuard guard;
      updateAvailable = false;
    }
    result = String("No update (GitHub ") + tag + " is older than installed " + FIRMWARE_VERSION + ")";
    return false;
  }
  if (cmp == 0) {
    {
      StateGuard guard;
      updateAvailable = false;
      strlcpy(latestFirmware, FIRMWARE_VERSION, sizeof(latestFirmware));
    }
    result = String("Up to date (") + FIRMWARE_VERSION + ")";
    return false;
  }

  String binUrl;
  for (JsonObject a : doc["assets"].as<JsonArray>()) {
    const char* name = a["name"];
    if (name && String(name) == GITHUB_ASSET_NAME) {
      binUrl = a["browser_download_url"].as<String>();
      break;
    }
  }
  if (!binUrl.length()) {
    result = "No firmware.bin asset in latest release";
    return false;
  }

  {
    StateGuard guard;
    updateAvailable = true;
  }
  result = String("Update available: ") + tag + " (installed " + FIRMWARE_VERSION + ")";
  if (!install) return true;

  // Important: use a fresh TLS client for the asset download (do not reuse API client)
  return installFirmwareFromUrl(binUrl, result);
}

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
    bool ok = checkGitHubUpdate(command == OTA_INSTALL, owner, repo, message);
    if (ok && command == OTA_INSTALL) {
      // Image is written: keep the relay off from here until the new firmware has proven itself.
      StateGuard guard;
      otaRebootPending = true;
      setHeater(false, true);
      otaGuardArm();
    }
    {
      StateGuard guard;
      lastOtaMsg = message;
      otaBusy = false;
      otaInProgress = false;
      if (!ok) otaProgressPercent = -1;
      else if (command == OTA_CHECK) otaProgressPercent = -1;
      // Record successful contact with GitHub only when the API returned a release
      if (otaCheckContactOk) {
        time_t now = time(nullptr);
        if (now > 100000) {
          lastOtaCheckEpoch = (uint32_t)now;
          Preferences p;
          if (p.begin("eiq", false)) {
            p.putUInt("otaChk", lastOtaCheckEpoch);
            p.end();
          }
        }
      }
    }
    // Refresh HA Update entity + state after every check/install attempt
    {
      StateGuard guard;
      publishState();
    }
    // Reboot only after a successful install (check-only must not reboot)
    if (ok && command == OTA_INSTALL) {
      markOtaSuccess(FIRMWARE_VERSION);
      delay(800);
      safeRestart();
    }
  }
}

