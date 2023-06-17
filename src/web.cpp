#include "web.h"

#include <list>
#include <tuple>

#include "debug.h"

static const std::list<std::tuple<const char *, const char *, const char *>>
    assets = {
        {"/", "/index.html", "text/html"},
        {"/index.html", "/index.html", "text/html"},
        {"/lib/bootstrap.min.css", "/lib/bootstrap.min.cssgz", "text/css"},
        {"/lib/pure-min.css", "/lib/pure-min.cssgz", "text/css"},
        {"/lib/grids-responsive-min.css", "/lib/grids-responsive-min.cssgz",
         "text/css"},
        {"/lib/bootstrap.bundle.min.js", "/lib/bootstrap.bundle.min.jsgz",
         "text/javascript"},
        {"/lib/zepto.min.js", "/lib/zepto.min.jsgz", "text/javascript"},
        {"/favicon.ico", "/favicon.ico", "image/x-icon"},
        {"/favicon-16x16.png", "/favicon-32x32.pnggz", "image/png"},
        {"/favicon-32x32.png", "/favicon-32x32.pnggz", "image/png"},
        {"/bike192x192.png", "/bike192x192.pnggz", "image/png"},
        {"/bike512x512.png", "/bike512x512.pnggz", "image/png"},
        {"/apple-touch-icon.png", "/apple-touch-icon.pnggz", "image/png"},
        {"/site.webmanifest", "/site.webmanifest", "text/plain"},
};

static void (*ws_data_callback)(StaticJsonDocument<256> doc) = NULL;
static StaticJsonDocument<256> rxdoc;
static char ws_data_buf[256];

void init_fs() {
  LOGF("Initializing LittleFS\n");
  if (!(LittleFS.begin())) {
    LOGF("Error initializing LittleFS\n");
    return;
  }
}

void init_wifi(DNSServer *dns_server) {
  esp_wifi_set_ps(WIFI_PS_NONE);

#if WIFI_SOFT_AP == 1
  LOGF("Configuring WiFi Soft AP\n");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SOFT_AP_SSID);
  LOGF("WiFi AP SSID: %s\nSoft AP IP: %s\n", WIFI_SOFT_AP_SSID,
       WiFi.softAPIP());

  dns_server->start(53, "*", WiFi.softAPIP());
  LOGF("Captive portal DNS server started\n");
#else
  LOGF("Connecting to %s\n", WIFI_STA_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PSK);
  while (WiFi.status() != WL_CONNECTED) {
    LOGF(".");
    delay(100);
  }
  LOGF("\nLocal IP: %s\n", WiFi.localIP().toString());
  ArduinoOTA.begin();
  LOGF("OTA updater started\n");
#endif

  MDNS.begin(MDNS_NAME);
  LOGF("mDNS responder started: http://%s.local\n", MDNS_NAME);
}
void init_webserver(AsyncWebServer *server, AsyncWebSocket *socket,
                    void (*cb)(StaticJsonDocument<256> rxdoc)) {
  for (auto const &it : assets) {
    server->on(std::get<0>(it), HTTP_GET, [it](AsyncWebServerRequest *req) {
      AsyncWebServerResponse *resp =
          req->beginResponse(LittleFS, std::get<1>(it), std::get<2>(it));
      char *gz = strrchr(std::get<1>(it), 'g');
      if (gz && !strcmp(gz, "gz")) {
        resp->addHeader("Content-Encoding", "gzip");
      }
      req->send(resp);
    });
    LOGF("Serving asset %s on %s\n", std::get<1>(it), std::get<0>(it));
  }

  ws_data_callback = cb;
  socket->onEvent(ws_event_handler);
  server->addHandler(socket);
  LOGF("Starting webserver\n");
  server->begin();
}

void ws_event_handler(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      LOGF("WebSocket client %s:%u connected from %s\n", server->url(),
           client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      LOGF("WebSocket client %s:%u disconnected\n", server->url(),
           client->id());
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      if (len > 255) len = 255;
      for (size_t i = 0; i < len; ++i) ws_data_buf[i] = (char)data[i];
      ws_data_buf[len] = '\0';

      if (!strcmp(ws_data_buf, "__ping__")) {
        LOGF("Websocket client %s:%u ping\n", server->url(), client->id());
        client->text("__pong__");
      } else {
        LOGF("Websocket client %s:%u data: %s\n", server->url(), client->id(),
             ws_data_buf);
        deserializeJson(rxdoc, ws_data_buf);
        ws_data_callback(rxdoc);
      }
      break;
    }
    case WS_EVT_ERROR:
      LOGF("WebSocket error %s:%u\n", server->url(), client->id());
      break;
  }
}