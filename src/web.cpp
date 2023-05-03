#include "web.h"

#include <list>
#include <tuple>

#include "debug.h"

static const std::list<std::tuple<const char *, const char *, const char *>>
    assets = {
        {"/", "/index.html", "text/html"},
        {"/index.html", "/index.html", "text/html"},
        {"/lib/bootstrap.min.js", "/lib/bootstrap.min.jsgz", "text/css"},
        {"/lib/bootstrap.min.css", "/lib/bootstrap.min.cssgz", "text/css"},
        {"/lib/bootstrap.bundle.min.js", "/lib/bootstrap.bundle.min.jsgz",
         "text/javascript"},
        {"/lib/jquery.min.js", "/lib/jquery.min.jsgz", "text/javascript"},
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
static bool _ws_connected = false;
static StaticJsonDocument<256> rxdoc;

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
      client->ping();
      _ws_connected = true;
      break;
    case WS_EVT_DISCONNECT:
      LOGF("WebSocket client %s:%u disconnected\n", server->url(),
           client->id());
      _ws_connected = false;
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      String text = "";

      for (size_t i = 0; i < len; ++i) text += (char)data[i];

      if (text == "__ping__") {
        LOGF("Websocket client %s:%u ping\n", server->url(), client->id());
      } else {
        LOGF("Websocket client %s:%u data: %s\n", server->url(), client->id(),
             text.c_str());
        deserializeJson(rxdoc, text);
        ws_data_callback(rxdoc);
      }
      break;
    }
    case WS_EVT_ERROR:
      LOGF("WebSocket error %s:%u %s\n", server->url(), client->id(),
           len ? (char *)data : "");
      break;
    case WS_EVT_PONG:
      break;
  }
}

bool ws_connected() { return _ws_connected; }
