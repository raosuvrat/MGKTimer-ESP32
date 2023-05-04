#ifndef WEB_H
#define WEB_H

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <AsyncWebSocket.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "debug.h"

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
#define WIFI_STA_SSID "SSID"
#define WIFI_STA_PSK "PASS"
#endif

#define WIFI_SOFT_AP 0
#define WIFI_SOFT_AP_SSID "MGK Lap Timer"
#define MDNS_NAME "mgktimer"
#define HTTP_PORT 80
#define WEBSOCKET_NAME "/ws"

void init_fs();

void init_wifi(DNSServer *dns_server);
void init_webserver(AsyncWebServer *server, AsyncWebSocket *socket,
                    void (*cb)(StaticJsonDocument<256> doc));
bool ws_connected();
void ws_event_handler(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len);

#endif