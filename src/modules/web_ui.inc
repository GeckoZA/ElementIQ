// ---- Web UI (full-page gzip in flash) ----
#include "generated/web_gz.inc"

static void sendGzPage(AsyncWebServerRequest *req, const uint8_t *data, size_t len) {
  if (!requireWebAuth(req)) return;
  AsyncWebServerResponse *res = req->beginResponse(200, "text/html", data, len);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

// portal.inc calls sendPage(req, "home", ...) for "/"
void sendPage(AsyncWebServerRequest *req, const char * /*active*/, const char * /*body*/) {
  sendGzPage(req, GZ_PAGE_HOME, GZ_PAGE_HOME_LEN);
}

void setupWebUI() {
  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/logo.png")) {
      AsyncWebServerResponse *res = req->beginResponse(LittleFS, "/logo.png", "image/png");
      res->addHeader("Cache-Control", "public, max-age=86400");
      req->send(res);
    } else {
      req->send(404);
    }
  });

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!setupDone || !adminPasswordSet()) { req->redirect("/"); return; }
    AsyncWebServerResponse *res = req->beginResponse(200, "text/html", GZ_PAGE_LOGIN, GZ_PAGE_LOGIN_LEN);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  server.on("/connections", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendGzPage(req, GZ_PAGE_CONNECTIONS, GZ_PAGE_CONNECTIONS_LEN);
  });
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendGzPage(req, GZ_PAGE_SETTINGS, GZ_PAGE_SETTINGS_LEN);
  });
  server.on("/schedule", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendGzPage(req, GZ_PAGE_SCHEDULE, GZ_PAGE_SCHEDULE_LEN);
  });
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendGzPage(req, GZ_PAGE_UPDATE, GZ_PAGE_UPDATE_LEN);
  });
  server.on("/about", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendGzPage(req, GZ_PAGE_ABOUT, GZ_PAGE_ABOUT_LEN);
  });
  server.on("/console", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendGzPage(req, GZ_PAGE_CONSOLE, GZ_PAGE_CONSOLE_LEN);
  });

  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("/settings"); });
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("/settings"); });
  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("/"); });
}
