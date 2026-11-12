// ---- Portal ----
DNSServer dnsServer;
bool portalActive = false;
bool dnsServerStarted = false;
String scannedNetworksJson = "[]";
QueueHandle_t portalConnectQueue = nullptr;
bool scanInProgress = false;
bool stationConnecting = false;
bool stationWasOnline = false;
unsigned long stationConnectStartedAt = 0;
unsigned long lastScanStartedAt = 0;
unsigned long nextStationRetryAt = 0;
char candidateSsid[33] = "";
char candidatePass[65] = "";

struct WifiConnectRequest {
  char ssid[33];
  char password[65];
  char apPassword[32];
  bool updateApPassword;
};

void startScan() {
  if (scanInProgress) return;
  WiFi.scanDelete();
  int result = WiFi.scanNetworks(true, false);
  if (result == WIFI_SCAN_RUNNING) {
    scanInProgress = true;
    lastScanStartedAt = millis();
  } else {
    Serial.printf("[WiFi] scan start failed: %d\n", result);
  }
}

void collectScanResults() {
  if (!scanInProgress) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scanInProgress = false;
  if (n < 0) {
    Serial.printf("[WiFi] scan failed: %d\n", n);
    WiFi.scanDelete();
    return;
  }
  struct Net { String ssid; int32_t rssi; bool secure; };
  Net nets[32]; int count = 0;
  for (int i = 0; i < n && count < 32; i++) {
    String ssid = WiFi.SSID(i), clean;
    for (unsigned j = 0; j < ssid.length(); j++) if (ssid[j] >= 32 && ssid[j] < 127) clean += ssid[j];
    ssid = clean; ssid.trim(); if (!ssid.length()) continue;
    int32_t rssi = WiFi.RSSI(i); bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    int found = -1;
    for (int j = 0; j < count; j++) if (nets[j].ssid == ssid) { found = j; break; }
    if (found >= 0) { if (rssi > nets[found].rssi) { nets[found].rssi = rssi; nets[found].secure = secure; } }
    else nets[count++] = {ssid, rssi, secure};
  }
  for (int i = 0; i < count; i++) for (int j = i + 1; j < count; j++)
    if (nets[j].rssi > nets[i].rssi) { Net t = nets[i]; nets[i] = nets[j]; nets[j] = t; }
  JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < count; i++) {
    if (nets[i].rssi < -90) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = nets[i].ssid; o["rssi"] = nets[i].rssi; o["secure"] = nets[i].secure;
  }
  String result;
  serializeJson(doc, result);
  {
    StateGuard guard;
    scannedNetworksJson = result;
  }
  WiFi.scanDelete();
}

bool initializePortalQueue() {
  portalConnectQueue = xQueueCreate(1, sizeof(WifiConnectRequest));
  return portalConnectQueue != nullptr;
}

const char PORTAL_HTML[] PROGMEM = R"portal(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Setup</title><style>
body{font-family:system-ui;background:#05080f;color:#e8f0fa;margin:0;padding:20px}
.card{background:#0a1628;border-radius:16px;padding:24px;max-width:400px;margin:0 auto}
h1{color:#3b9eff;text-align:center}label{display:block;font-size:.72rem;color:#6b8aad;margin:14px 0 6px}
input,select{width:100%;padding:12px;border-radius:10px;border:1px solid #123050;background:#0e243f;color:#e8f0fa;box-sizing:border-box}
button{width:100%;margin-top:20px;padding:14px;border:none;border-radius:12px;background:#1e6bb8;color:#fff;font-weight:700}
.hint{font-size:.75rem;color:#6b8aad;text-align:center;margin-top:8px}
</style></head><body><div class="card"><h1>ElementIQ</h1>
<label>Network</label><select id="ssid"><option>Scanning...</option></select>
<label>Password</label><input type="password" id="pass">
<label>Device AP password (optional)</label><input type="password" id="ap_pass" maxlength="31" placeholder="Blank = open AP">
<p class="hint">Leave blank for an open AP, or use at least 8 characters.</p>
<button id="btn" onclick="save()">Connect</button><p class="hint" id="hint"></p></div>
<script>
async function load(){try{const arr=await(await fetch('/scan')).json();const s=document.getElementById('ssid');s.innerHTML='';
arr.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid;s.appendChild(o)});
document.getElementById('hint').textContent=arr.length+' networks'}catch(e){}}
async function save(){const ssid=document.getElementById('ssid').value,pass=document.getElementById('pass').value;
const ap_pass=document.getElementById('ap_pass').value;
if(ap_pass.length>0&&ap_pass.length<8){document.getElementById('hint').textContent='AP password must be blank or at least 8 characters';return}
document.getElementById('btn').disabled=true;
const r=await fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:new URLSearchParams({ssid,pass,ap_pass})});
if(!r.ok){document.getElementById('hint').textContent=await r.text();document.getElementById('btn').disabled=false;return}
document.body.innerHTML='<div class="card"><h1>Connecting...</h1></div>'}
load();setInterval(load,15000);
</script></body></html>
)portal";

void setupPortalRoutes() {
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
    String networks;
    {
      StateGuard guard;
      networks = scannedNetworksJson;
    }
    req->send(200, "application/json", networks);
  });
  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("ssid", true)) {
      req->send(400, "text/plain", "Missing network name");
      return;
    }
    String ssid = req->getParam("ssid", true)->value();
    String password = req->hasParam("pass", true)
      ? req->getParam("pass", true)->value() : "";
    String apPassword = req->hasParam("ap_pass", true)
      ? req->getParam("ap_pass", true)->value() : "";
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64 ||
        apPassword.length() > 31 ||
        (apPassword.length() > 0 && apPassword.length() < 8)) {
      req->send(400, "text/plain", "Invalid network or AP password");
      return;
    }
    WifiConnectRequest command = {};
    strlcpy(command.ssid, ssid.c_str(), sizeof(command.ssid));
    strlcpy(command.password, password.c_str(), sizeof(command.password));
    strlcpy(command.apPassword, apPassword.c_str(), sizeof(command.apPassword));
    command.updateApPassword = req->hasParam("ap_pass", true);
    if (!portalConnectQueue ||
        xQueueSend(portalConnectQueue, &command, 0) != pdTRUE) {
      req->send(503, "text/plain", "Connection request is already pending");
      return;
    }
    req->send(202, "text/plain", "Connecting");
  });
  server.onNotFound([](AsyncWebServerRequest *req) {
    bool captive;
    {
      StateGuard guard;
      captive = portalActive;
    }
    if (captive) req->redirect("http://192.168.4.1/");
    else req->send(404);
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    bool captive;
    {
      StateGuard guard;
      captive = portalActive;
    }
    if (captive) req->send(200, "text/html", PORTAL_HTML);
    else sendPage(req, "home", INDEX_BODY);
  });
}

void configureAccessPoint(bool forceForSetup) {
  char name[sizeof(apName)];
  char password[sizeof(apPass)];
  bool enabled;
  {
    StateGuard guard;
    strlcpy(name, apName, sizeof(name));
    strlcpy(password, apPass, sizeof(password));
    enabled = apEnabled && !hideAp;
  }
  bool stationUp = WiFi.status() == WL_CONNECTED;
  if (!enabled && stationUp && !forceForSetup) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    return;
  }
  if (!name[0]) strlcpy(name, AP_NAME, sizeof(name));
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPdisconnect(true);
  if (!WiFi.softAP(name, password[0] ? password : nullptr))
    Serial.println("[WiFi] access point start failed");
}

void beginStationConnection(const char* ssid, const char* password) {
  if (!ssid || !ssid[0]) return;
  if (scanInProgress) {
    WiFi.scanDelete();
    scanInProgress = false;
  }
  strlcpy(candidateSsid, ssid, sizeof(candidateSsid));
  strlcpy(candidatePass, password ? password : "", sizeof(candidatePass));
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.begin(candidateSsid, candidatePass);
  stationConnecting = true;
  stationConnectStartedAt = millis();
  {
    StateGuard guard;
    portalActive = true;
  }
  if (!dnsServerStarted) {
    dnsServer.start(53, "*", WiFi.softAPIP());
    dnsServerStarted = true;
  }
}

void startCaptivePortal() {
  {
    StateGuard guard;
    portalActive = true;
  }
  configureAccessPoint(true);
  if (!dnsServerStarted) {
    dnsServer.start(53, "*", WiFi.softAPIP());
    dnsServerStarted = true;
  }
  startScan();
}

void setupWiFi() {
  WiFi.setAutoReconnect(false);
  startCaptivePortal();
  char ssid[sizeof(wifi_ssid)];
  char password[sizeof(wifi_pass)];
  {
    StateGuard guard;
    strlcpy(ssid, wifi_ssid, sizeof(ssid));
    strlcpy(password, wifi_pass, sizeof(password));
  }
  if (ssid[0]) beginStationConnection(ssid, password);
}

void processWifiManager() {
  bool captive;
  {
    StateGuard guard;
    captive = portalActive;
  }
  if (captive) dnsServer.processNextRequest();
  collectScanResults();

  WifiConnectRequest command;
  if (portalConnectQueue &&
      xQueueReceive(portalConnectQueue, &command, 0) == pdTRUE) {
    if (command.updateApPassword) {
      {
        StateGuard guard;
        strlcpy(apPass, command.apPassword, sizeof(apPass));
        saveSettings();
      }
      configureAccessPoint(true);
    }
    beginStationConnection(command.ssid, command.password);
  }

  wl_status_t status = WiFi.status();
  if (stationConnecting && status == WL_CONNECTED) {
    stationConnecting = false;
    stationWasOnline = true;
    {
      StateGuard guard;
      strlcpy(wifi_ssid, candidateSsid, sizeof(wifi_ssid));
      strlcpy(wifi_pass, candidatePass, sizeof(wifi_pass));
      portalActive = false;
      saveSettings();
    }
    Serial.printf("[WiFi] Connected  IP: %s  SSID: %s\n",
                  WiFi.localIP().toString().c_str(), candidateSsid);
    if (dnsServerStarted) {
      dnsServer.stop();
      dnsServerStarted = false;
    }
    setupNTP();
    setupMDNS();
    configureAccessPoint(false);
    if (!arduinoOtaReady) {
      setupArduinoOTA();
      arduinoOtaReady = true;
    }
    updateWifiLed();
    bool autoUpdate;
    {
      StateGuard guard;
      autoUpdate = otaAutoCheck;
    }
    if (autoUpdate) queueOtaOperation(true);
  } else if (stationConnecting &&
             millis() - stationConnectStartedAt >= WIFI_CONNECT_TIMEOUT_MS) {
    stationConnecting = false;
    WiFi.disconnect(false, false);
    nextStationRetryAt = millis() + WIFI_RETRY_INTERVAL_MS;
    Serial.println("[WiFi] connection timed out; setup portal remains available");
    startCaptivePortal();
  } else if (!stationConnecting && stationWasOnline && status != WL_CONNECTED) {
    stationWasOnline = false;
    nextStationRetryAt = millis() + WIFI_RETRY_INTERVAL_MS;
    Serial.println("[WiFi] disconnected; setup portal started");
    startCaptivePortal();
  } else if (!stationConnecting && !stationWasOnline &&
             candidateSsid[0] && (int32_t)(millis() - nextStationRetryAt) >= 0) {
    beginStationConnection(candidateSsid, candidatePass);
  }

  {
    StateGuard guard;
    captive = portalActive;
  }
  if (captive && !scanInProgress &&
      millis() - lastScanStartedAt >= WIFI_SCAN_INTERVAL_MS)
    startScan();
}

void processPendingNetworkChanges() {
  bool reconfigureAp;
  bool reconfigureTime;
  bool reconnectMqtt;
  int8_t timezone;
  {
    StateGuard guard;
    reconfigureAp = pendingApReconfigure;
    reconfigureTime = pendingTimeReconfigure;
    reconnectMqtt = pendingMqttReconnect;
    pendingApReconfigure = pendingTimeReconfigure = pendingMqttReconnect = false;
    timezone = tzOffsetHours;
  }
  if (reconfigureAp) configureAccessPoint(false);
  if (reconfigureTime) configTime(timezone * 3600, 0, "pool.ntp.org", "time.nist.gov");
  if (reconnectMqtt && mqtt.connected()) mqtt.disconnect();
}

void processPendingSystemActions() {
  bool resetSettings;
  bool resetWifi;
  {
    StateGuard guard;
    resetSettings = pendingFactoryReset;
    resetWifi = pendingWifiReset;
    pendingFactoryReset = pendingWifiReset = false;
  }
  if (resetSettings) {
    factoryReset();
    return;
  }
  if (resetWifi) {
    setHeater(false, true);
    WiFi.disconnect(true);
    ESP.restart();
  }
}

void checkFactoryResetPin() {
  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
  if (digitalRead(FACTORY_RESET_PIN) != LOW) return;
  Serial.println("[RESET] Hold BOOT 5s for factory reset...");
  unsigned long t0 = millis();
  while (digitalRead(FACTORY_RESET_PIN) == LOW) {
    if (millis() - t0 > 5000) { factoryReset(); return; }
    delay(50);
  }
}

