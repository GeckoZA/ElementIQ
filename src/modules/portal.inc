// ---- Portal ----
DNSServer dnsServer;
bool portalActive = false;
bool dnsServerStarted = false;
String scannedNetworksJson = "[]";
QueueHandle_t portalConnectQueue = nullptr;
bool scanInProgress = false;
bool scanRequested = false;
bool stationConnecting = false;
bool stationWasOnline = false;
bool setupFinishPending = false;
unsigned long apShutdownAt = 0;
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
  bool finishSetup;     // first-time wizard: mark setup complete once Wi-Fi is up
};

// Blocking scan is far more reliable on ESP32 in AP+STA when STA is not
// connected. Async scan frequently returns WIFI_SCAN_FAILED (-2) in that case.
void runBlockingScan() {
  // Never scan (or disconnect) while a station connection is in progress –
  // that was aborting WiFi.begin() and causing connect timeouts.
  if (stationConnecting || scanInProgress) return;
  if (millis() - lastScanStartedAt < 800) return;  // radio settle

  scanInProgress = true;
  lastScanStartedAt = millis();
  Serial.println("[WiFi] starting scan...");

  // Only clear STA state when idle (not connecting / not connected)
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(false);  // keep AP up
  }

  // Blocking scan (typical 1–3 s). Acceptable during captive-portal setup.
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);

  scanInProgress = false;

  if (n < 0) {
    Serial.printf("[WiFi] scan failed: %d\n", n);
    WiFi.scanDelete();
    // Retry sooner on failure
    lastScanStartedAt = millis() - WIFI_SCAN_INTERVAL_MS + 4000;
    return;
  }

  struct Net { String ssid; int32_t rssi; bool secure; };
  Net nets[32];
  int count = 0;
  for (int i = 0; i < n && count < 32; i++) {
    String ssid = WiFi.SSID(i), clean;
    for (unsigned j = 0; j < ssid.length(); j++)
      if (ssid[j] >= 32 && ssid[j] < 127) clean += ssid[j];
    ssid = clean;
    ssid.trim();
    if (!ssid.length()) continue;
    int32_t rssi = WiFi.RSSI(i);
    bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    int found = -1;
    for (int j = 0; j < count; j++)
      if (nets[j].ssid == ssid) { found = j; break; }
    if (found >= 0) {
      if (rssi > nets[found].rssi) {
        nets[found].rssi = rssi;
        nets[found].secure = secure;
      }
    } else {
      nets[count++] = {ssid, rssi, secure};
    }
  }

  // Sort by RSSI descending
  for (int i = 0; i < count; i++)
    for (int j = i + 1; j < count; j++)
      if (nets[j].rssi > nets[i].rssi) {
        Net t = nets[i];
        nets[i] = nets[j];
        nets[j] = t;
      }

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < count; i++) {
    if (nets[i].rssi < -90) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = nets[i].ssid;
    o["rssi"] = nets[i].rssi;
    o["secure"] = nets[i].secure;
  }

  String result;
  serializeJson(doc, result);
  {
    StateGuard guard;
    scannedNetworksJson = result;
  }
  Serial.printf("[WiFi] scan complete – %d network(s)\n", count);
  WiFi.scanDelete();
}

// Keep old names so the rest of the file compiles; they now call the blocking path.
void startScan() { runBlockingScan(); }
void collectScanResults() { /* results are produced inside runBlockingScan() */ }

bool initializePortalQueue() {
  portalConnectQueue = xQueueCreate(1, sizeof(WifiConnectRequest));
  return portalConnectQueue != nullptr;
}

// Portal HTML is gzipped in generated/web_gz.inc (included via web_ui.inc)
static void sendPortalHtml(AsyncWebServerRequest *req) {
  AsyncWebServerResponse *res = req->beginResponse(200, "text/html", GZ_PAGE_PORTAL, GZ_PAGE_PORTAL_LEN);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}


// Captive-portal routes (/scan, /connect, /portal-status) are open ONLY while the
// first-time setup wizard has not been completed; afterwards they need a login
// (see requirePortalAccess() in api.inc).

void setupPortalRoutes() {
  // Lightweight status for the captive-portal connect screen
  server.on("/portal-status", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!requirePortalAccess(req)) return;
    wl_status_t st = WiFi.status();
    String staIp = WiFi.localIP().toString();
    bool joined = (st == WL_CONNECTED) && staIp.length() && staIp != "0.0.0.0" &&
                  !staIp.startsWith("192.168.4.");
    JsonDocument doc;
    doc["wifi_connecting"] = stationConnecting;
    doc["wifi_status"] = (int)st;
    doc["ip"] = staIp;
    doc["ap_ip"] = WiFi.softAPIP().toString();
    doc["ssid"] = candidateSsid[0] ? candidateSsid : wifi_ssid;
    doc["mdns"] = "ElementIQ.local";
    doc["rssi"] = WiFi.RSSI();
    doc["joined"] = joined;
    doc["setup_done"] = setupDone;
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!requirePortalAccess(req)) return;
    String networks;
    {
      StateGuard guard;
      networks = scannedNetworksJson;
    }
    // If we still have no results and nothing is running, ask for a scan
    if (networks == "[]" && !scanInProgress) {
      scanRequested = true;
    }
    req->send(200, "application/json", networks);
  });
  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!requirePortalAccess(req)) return;
    if (!req->hasParam("ssid", true)) {
      req->send(400, "text/plain", "Missing network name");
      return;
    }
    String ssid = req->getParam("ssid", true)->value();
    String password = req->hasParam("pass", true)
      ? req->getParam("pass", true)->value() : "";
    String apPassword = req->hasParam("ap_pass", true)
      ? req->getParam("ap_pass", true)->value() : "";
    bool wizard = !setupDone;
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64 ||
        apPassword.length() > 31 ||
        (apPassword.length() > 0 && apPassword.length() < PASSWORD_MIN_LEN)) {
      req->send(400, "text/plain", "Invalid network or hotspot password");
      return;
    }
    if (!wizard && !requireDangerAuth(req)) return;   // changing Wi-Fi on a password-less unit needs the BOOT button
    if (wizard) {
      // First-time setup: password protection is ON unless the user explicitly opted out ("secure=0").
      bool secure = !(req->hasParam("secure", true) && req->getParam("secure", true)->value() == "0");
      String adminUser = req->hasParam("admin_user", true) ? req->getParam("admin_user", true)->value() : "admin";
      String adminPass = req->hasParam("admin_pass", true) ? req->getParam("admin_pass", true)->value() : "";
      String tzText = req->hasParam("tz", true) ? req->getParam("tz", true)->value() : "";
      adminUser.trim();
      if (secure && (adminUser.length() == 0 || adminUser.length() >= sizeof(webAuthUser))) {
        req->send(400, "text/plain", "Choose a username (max 23 characters)");
        return;
      }
      if (secure && (adminPass.length() < PASSWORD_MIN_LEN || adminPass.length() > 31)) {
        req->send(400, "text/plain", "Device password must be 8-31 characters");
        return;
      }
      if (apPassword.length() < PASSWORD_MIN_LEN) {
        req->send(400, "text/plain", "Hotspot password must be 8-31 characters");
        return;
      }
      {
        StateGuard guard;
        if (secure) {
          strlcpy(webAuthUser, adminUser.c_str(), sizeof(webAuthUser));
          setAdminPassword(adminPass.c_str());
          saveWebAuth();
        } else {
          webAuthHash[0] = 0;                  // open mode: no login (CSRF + BOOT-button guards remain)
          webAuthSalt[0] = 0;
          LittleFS.remove("/webauth.txt");
          eqLog("[SETUP] password protection skipped by user (open mode)");
        }
        if (tzText.length()) tzOffsetHours = constrain(tzText.toInt(), -12, 14);
        bool keepAp = req->hasParam("ap_keep", true) && req->getParam("ap_keep", true)->value() == "1";
        apEnabled = keepAp;
        hideAp = !keepAp;
        saveSettings();
      }
    }
    WifiConnectRequest command = {};
    command.finishSetup = wizard;
    strlcpy(command.ssid, ssid.c_str(), sizeof(command.ssid));
    strlcpy(command.password, password.c_str(), sizeof(command.password));
    strlcpy(command.apPassword, apPassword.c_str(), sizeof(command.apPassword));
    command.updateApPassword = req->hasParam("ap_pass", true) && apPassword.length() > 0;
    if (!portalConnectQueue ||
        xQueueSend(portalConnectQueue, &command, 0) != pdTRUE) {
      req->send(503, "text/plain", "Connection request is already pending");
      return;
    }
    req->send(202, "text/plain", "Connecting");
  });
  // Captive-portal probe URLs used by Android / Windows / iOS / macOS / Chrome OS.
  // Strategy: 302 → http://192.168.4.1/ for connectivity checks (triggers OS "Sign in"
  // banner reliably), and always serve the portal HTML for unknown hosts via onNotFound.
  auto captiveRedirect = [](AsyncWebServerRequest *req) {
    if (!setupDone) {
      // Absolute URL so the client leaves the probe hostname and hits our AP IP.
      AsyncWebServerResponse *res = req->beginResponse(302, "text/plain", "");
      res->addHeader("Location", "http://192.168.4.1/");
      res->addHeader("Cache-Control", "no-store");
      req->send(res);
    } else {
      req->send(204);
    }
  };

  server.on("/generate_204", HTTP_GET, captiveRedirect);                // Android
  server.on("/gen_204", HTTP_GET, captiveRedirect);                     // Android
  server.on("/generate204", HTTP_GET, captiveRedirect);
  server.on("/hotspot-detect.html", HTTP_GET, captiveRedirect);         // iOS / macOS
  server.on("/library/test/success.html", HTTP_GET, captiveRedirect);
  server.on("/ncsi.txt", HTTP_GET, captiveRedirect);                    // Windows NCSI
  server.on("/connecttest.txt", HTTP_GET, captiveRedirect);             // Windows
  server.on("/redirect", HTTP_GET, captiveRedirect);
  server.on("/canonical.html", HTTP_GET, captiveRedirect);
  server.on("/success.txt", HTTP_GET, captiveRedirect);
  server.on("/check_network_status.txt", HTTP_GET, captiveRedirect);    // Huawei / others
  server.on("/kindle-wifi/wifiredirect.html", HTTP_GET, captiveRedirect);

  // Any other path/host (DNS wildcard → 192.168.4.1) serves the portal during setup.
  server.onNotFound([](AsyncWebServerRequest *req) {
    if (!setupDone) {
      // Prefer serving the page directly – more reliable than redirect alone
      sendPortalHtml(req);
    } else {
      req->send(404);
    }
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!setupDone) {
      sendPortalHtml(req);
    } else {
      sendPage(req, "home", nullptr);
    }
  });
}

static void restartDnsServer() {
  if (dnsServerStarted) {
    dnsServer.stop();
    dnsServerStarted = false;
  }
  IPAddress apIP = WiFi.softAPIP();
  if (apIP == IPAddress(0, 0, 0, 0)) {
    apIP = IPAddress(192, 168, 4, 1);
  }
  // TTL 0 so clients do not cache a bad answer while the AP is settling.
  dnsServer.setTTL(0);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);
  dnsServerStarted = true;
  Serial.printf("[WiFi] DNS captive server on %s:53\n", apIP.toString().c_str());
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
    if (dnsServerStarted) {
      dnsServer.stop();
      dnsServerStarted = false;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    return;
  }
  if (!name[0]) strlcpy(name, AP_NAME, sizeof(name));

  WiFi.setSleep(false);  // better beacon reliability / visibility
  WiFi.persistent(false);

  // During first-time setup / captive portal, pure AP mode is more reliable than
  // AP+STA (fewer radio races, DHCP and DNS answer faster, softAPIP is stable).
  // Switch to AP_STA only when the user starts a station connection.
  const bool pureAp = forceForSetup && !stationConnecting && WiFi.status() != WL_CONNECTED;
  if (pureAp) {
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, false);
    delay(50);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);
  } else {
    WiFi.mode(WIFI_AP_STA);
    if (forceForSetup && WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect(false);
    }
    WiFi.softAPdisconnect(true);
    delay(80);
  }

  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  // softAPConfig before softAP is the documented order on ESP32.
  WiFi.softAPConfig(apIP, gateway, subnet);

  // Channel 1 is often quieter than 6 on crowded 2.4 GHz; both are universally supported.
  // Prefer channel 6 (widely tested with Android captive detection).
  const int channel = 6;
  const int maxClients = 4;
  bool ok = false;
  if (password[0] && strlen(password) >= 8) {
    ok = WiFi.softAP(name, password, channel, /*ssid_hidden=*/0, maxClients);
  } else {
    ok = WiFi.softAP(name, nullptr, channel, /*ssid_hidden=*/0, maxClients);
  }

  // Give the radio / DHCP server time before clients associate.
  delay(200);

  // Re-apply config after softAP — some ESP32-C3 cores ignore the first call.
  WiFi.softAPConfig(apIP, gateway, subnet);
  delay(50);

  if (!ok) {
    Serial.println("[WiFi] access point start failed");
  } else {
    IPAddress got = WiFi.softAPIP();
    Serial.printf("[WiFi] AP \"%s\" up on channel %d  IP %s  mode=%s\n",
                  name, channel, got.toString().c_str(),
                  pureAp ? "AP" : "AP_STA");
    if (got != apIP) {
      Serial.println("[WiFi] softAPIP mismatch – retrying softAPConfig");
      WiFi.softAPConfig(apIP, gateway, subnet);
      delay(50);
      Serial.printf("[WiFi] softAPIP now %s\n", WiFi.softAPIP().toString().c_str());
    }
  }

#if defined(WIFI_POWER_19_5dBm)
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // max TX where supported
#endif

  // Always (re)start the captive DNS after the AP is up so the answer IP is correct.
  if (forceForSetup || portalActive) {
    restartDnsServer();
  }
}

void beginStationConnection(const char* ssid, const char* password) {
  if (!ssid || !ssid[0]) return;

  // Stop any in-flight scan so it cannot call disconnect() under us
  if (scanInProgress) {
    WiFi.scanDelete();
    scanInProgress = false;
  }
  scanRequested = false;

  strlcpy(candidateSsid, ssid, sizeof(candidateSsid));
  strlcpy(candidatePass, password ? password : "", sizeof(candidatePass));

  // Need AP+STA to keep the setup hotspot while joining home Wi-Fi.
  WiFi.setSleep(false);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  delay(50);

  // Clean slate for STA, keep AP running
  WiFi.disconnect(false);
  delay(100);

  Serial.printf("[WiFi] connecting to \"%s\" (pass len %u)...\n",
                candidateSsid, (unsigned)strlen(candidatePass));
  wl_status_t beginStatus = WiFi.begin(candidateSsid, candidatePass);
  Serial.printf("[WiFi] WiFi.begin -> %d\n", (int)beginStatus);

  stationConnecting = true;
  stationConnectStartedAt = millis();
  {
    StateGuard guard;
    portalActive = true;
  }
  // Keep captive DNS alive so the phone can still reach the portal during connect.
  restartDnsServer();
}

void startCaptivePortal() {
  {
    StateGuard guard;
    portalActive = true;
  }
  configureAccessPoint(true);   // also (re)starts DNS
  // Defer the first scan so clients can finish DHCP + captive probes first.
  // Scanning the 2.4 GHz band briefly drops AP beacons and is a common reason
  // phones show "connected, no internet" with no portal popup.
  // First automatic scan ~4 s after AP is up (interval is 8 s thereafter).
  lastScanStartedAt = millis() - (WIFI_SCAN_INTERVAL_MS > 4000 ? WIFI_SCAN_INTERVAL_MS - 4000 : 0);
  scanRequested = false;
  Serial.println("[WiFi] captive portal started (scan deferred ~4s)");
}

void setupWiFi() {
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);  // never write STA credentials from this firmware path
  // After factory reset (or first boot) ensure no residual STA association fights the AP.
  if (!setupDone) {
    WiFi.disconnect(true, true);
    delay(100);
  }
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
  // Process several DNS queries per loop – phones fire many probes at once when
  // joining the AP; a single processNextRequest() drops the rest.
  if (captive && dnsServerStarted) {
    for (int i = 0; i < 8; i++) dnsServer.processNextRequest();
  }
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
      // During first-time setup do NOT restart the hotspot now: that would drop the
      // phone mid-wizard. The new password takes effect the next time the AP starts.
      if (setupDone) configureAccessPoint(true);
    }
    setupFinishPending = command.finishSetup;
    beginStationConnection(command.ssid, command.password);
  }

  if (apShutdownAt && (int32_t)(millis() - apShutdownAt) >= 0) {
    apShutdownAt = 0;
    setupPortalHold = false;   // portal routes now require normal auth
    {
      StateGuard guard;
      portalActive = false;
    }
    if (dnsServerStarted) {
      dnsServer.stop();
      dnsServerStarted = false;
    }
    if (WiFi.status() == WL_CONNECTED) configureAccessPoint(false);
    eqLog("[SETUP] wizard grace ended — setup AP stopped");
  }

  wl_status_t status = WiFi.status();
  if (stationConnecting && status == WL_CONNECTED) {
    stationConnecting = false;
    stationWasOnline = true;
    {
      StateGuard guard;
      strlcpy(wifi_ssid, candidateSsid, sizeof(wifi_ssid));
      strlcpy(wifi_pass, candidatePass, sizeof(wifi_pass));
      saveSettings();
    }
    Serial.printf("[WiFi] Connected  IP: %s  SSID: %s\n",
                  WiFi.localIP().toString().c_str(), candidateSsid);
    setupNTP();
    setupMDNS();
    if (setupFinishPending) {
      // First-time wizard: STA is up. Mark setup complete, but keep the setup AP,
      // captive DNS, and unauthenticated portal routes alive so the phone (still on
      // ElementIQ-Setup) can poll /portal-status and show the success screen.
      // Previously markSetupDone() immediately forced /portal-status to require a
      // login session → 401 → UI "Couldn't join Wi-Fi" despite a successful join.
      setupFinishPending = false;
      markSetupDone();
      setupPortalHold = true;   // requirePortalAccess stays open during grace
      {
        StateGuard guard;
        portalActive = true;
      }
      if (!dnsServerStarted) restartDnsServer();
      apShutdownAt = millis() + 60000UL;  // 60 s for the success page
      eqLog("[SETUP] first-time setup complete — AP held for wizard success page");
    } else {
      {
        StateGuard guard;
        portalActive = false;
      }
      if (dnsServerStarted) {
        dnsServer.stop();
        dnsServerStarted = false;
      }
      configureAccessPoint(false);
    }
#if ENABLE_ARDUINO_OTA
    if (!arduinoOtaReady) {
      setupArduinoOTA();
      arduinoOtaReady = true;
    }
#endif
    // ota_auto: periodic GitHub *check* (10 days), not silent install on every join
    bool autoUpdate;
    {
      StateGuard guard;
      autoUpdate = otaAutoCheck;
    }
    if (autoUpdate) queueOtaOperation(false);
  } else if (stationConnecting &&
             millis() - stationConnectStartedAt >= WIFI_CONNECT_TIMEOUT_MS) {
    stationConnecting = false;
    Serial.printf("[WiFi] connection timed out (status=%d) to \"%s\"; portal stays up\n",
                  (int)status, candidateSsid);
    WiFi.disconnect(false, false);
    nextStationRetryAt = millis() + WIFI_RETRY_INTERVAL_MS;
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
  // Do not start scans while a station connection attempt is running
  if (captive && !stationConnecting) {
    if (scanRequested ||
        (!scanInProgress && millis() - lastScanStartedAt >= WIFI_SCAN_INTERVAL_MS)) {
      scanRequested = false;
      startScan();
    }
  }
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
  if (reconfigureTime) setupNTP();
  if (reconnectMqtt) {
    if (mqtt.connected()) mqtt.disconnect();
    lastMqttAttempt = 0;  // force immediate reconnect attempt in mqttLoop
  }
}

void processPendingSystemActions() {
  bool resetSettings;
  bool resetWifi;
  bool reboot;
  {
    StateGuard guard;
    resetSettings = pendingFactoryReset;
    resetWifi = pendingWifiReset;
    reboot = pendingReboot;
    pendingFactoryReset = pendingWifiReset = pendingReboot = false;
  }
  if (resetSettings) {
    factoryReset();
    return;
  }
  if (resetWifi) {
    setHeater(false, true);
    WiFi.disconnect(true, true);  // erase stored credentials
    safeRestart();
  }
  if (reboot) {
    // Heater off first, then let the HTTP response flush before restarting.
    setHeater(false, true);
    saveRuntimeStats();
    delay(1000);          // let the HTTP response (e.g. after a firmware upload) reach the browser
    safeRestart();
  }
}

void checkFactoryResetPin() {
  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
  if (digitalRead(FACTORY_RESET_PIN) != LOW) return;
  Serial.println("[RESET] Hold BOOT 5s for factory reset...");
  unsigned long t0 = millis();
  while (digitalRead(FACTORY_RESET_PIN) == LOW) {
    if (millis() - t0 > 5000) {
      // Wait for release so the post-reset boot does not immediately re-trigger
      // (which would leave the device stuck in a reset loop with no AP).
      Serial.println("[RESET] Release BOOT button to complete factory reset...");
      while (digitalRead(FACTORY_RESET_PIN) == LOW) {
        delay(50);
      }
      delay(100);  // debounce
      factoryReset();
      return;
    }
    delay(50);
  }
}

