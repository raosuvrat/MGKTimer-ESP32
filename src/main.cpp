#include <Arduino.h>
#include <Preferences.h>

#include "beam.h"
#include "debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "md_max.h"
#include "pins.h"
#include "web.h"

#define TOUCH_STRIP_TIMEOUT_US 250000

void ws_data_handler(StaticJsonDocument<256> doc);
void update_clients(const char *msg = NULL);
void IRAM_ATTR ISR_touch_strip();
void app_task(void *pvParameters);
void handle_touch();

static beam_t beam;
static TaskHandle_t app_task_handle = NULL;
static uint8_t intensity = 0;
static Preferences prefs;
static MD_Parola md_max =
    MD_Parola(MD_MAX72XX::FC16_HW, MD_CS_PIN, MD_MAX_DEVICES);
static DNSServer dns_server;
static AsyncWebServer server(HTTP_PORT);
static AsyncWebSocket socket(WEBSOCKET_NAME);
static StaticJsonDocument<1024> doc;
static char buf[1024];
static volatile bool touch_strip_touched = false;

void setup() {
#if DEBUG == 1
  Serial.begin(960000);
  LOGF("Serial debugging enabled\n");
#endif

  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(PHOTOTRANS_100000_OHM_LOAD_PIN, INPUT);
  // digitalWrite(PHOTOTRANS_100000_OHM_LOAD_PIN, LOW);
  pinMode(LASER_PIN, OUTPUT);
  pinMode(LASER_2_PIN, OUTPUT);
  pinMode(IR_RECV_PIN, INPUT);
  pinMode(PHOTOTRANS_PIN, INPUT_PULLUP);
  pinMode(PHOTOTRANS_2_PIN, INPUT_PULLUP);
  pinMode(TOUCH_STRIP_PIN, INPUT);

  touchAttachInterrupt(TOUCH_STRIP_PIN, ISR_touch_strip, 40);
  analogWrite(LASER_2_PIN, 100);

  init_fs();
  init_wifi(&dns_server);
  init_webserver(&server, &socket, ws_data_handler);
  init_display(&md_max);

  prefs.begin("MGKTimer", false);
  // prefs.putUInt("mode", LASER_PHOTOTRANS_ADC);
  intensity = prefs.getUInt("intensity", 4);
  beam.mode = static_cast<detection_mode_t>(prefs.getUInt("mode"));
  beam.crossings = prefs.getUInt("crossings", 4);
  beam.adc_threshold = prefs.getUInt("adc_threshold", 512);
  set_display_intensity(intensity);
}

void app_task(void *pvParameters) {
  display_print("Ready");

  while (true) {
    reset_beam();
    while (!beam.counter) {
      update_clients("ready");
      vTaskDelay(100);
    }
    while (!beam.finish_time) {
      update_clients("running");
      display_time(beam.start_time, micros());
      vTaskDelay(100);
    }
    display_time(beam.start_time, beam.finish_time);
    update_clients("finish");
  }
}

bool check_beam_stability() {
  bool check_beam_stable = true;
  init_beam(&beam);
  delay(500);
  char buf[33];
  for (int i = 0; i < 32; ++i) {
    update_clients("Checking beam stability");
    check_beam_stable &= beam.state == RECEIVED && !beam.counter;
    buf[i] = ';';
    buf[i + 1] = '\0';
    display_print(buf, 0);
    delay(2000 / 32);
  }
  return check_beam_stable;
}

void loop() {
#if WIFI_SOFT_AP == 0
  ArduinoOTA.handle();
#endif
  handle_touch();
  if (!app_task_handle) {
    if (check_beam_stability()) {
      display_print("Locked");
      delay(1000);
      xTaskCreatePinnedToCore(app_task, "app_task", 10000, NULL, 10,
                              &app_task_handle, 0);
    } else {
      display_print("No lock");
    }
  }
}

void update_clients(const char *msg) {
  if (socket.availableForWriteAll()) {
    doc["msg"] = msg ? msg : "";
    doc["mode"] = detection_mode_to_str(beam.mode);
    doc["state"] = beam.state == RECEIVED      ? "RECEIVED"
                   : beam.state == INTERRUPTED ? "INTERRUPTED"
                                               : "NOT_ESTABLISHED";
    doc["counter"] = beam.counter;
    doc["start"] = beam.start_time;
    doc["finish"] = beam.finish_time;
    doc["change"] = beam.change_time;
    doc["crossings"] = beam.crossings;
    doc["time"] = micros();
    doc["adc_value"] = beam.adc_value;
    doc["adc_threshold"] = beam.adc_threshold;
    doc["samples"] = beam.samples;
    doc["sample_rate"] = beam.sample_rate;
    doc["intensity"] = intensity;
    // doc["touchread"] = touchRead(TOUCH_STRIP_PIN);
    // doc["free_heap"] = ESP.getFreeHeap();
    serializeJson(doc, buf);
    socket.textAll(buf);
  }
}

void IRAM_ATTR ISR_touch_strip() { touch_strip_touched = true; }

void ws_data_handler(StaticJsonDocument<256> doc) {
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (kv.key() == "mode") {
      beam.mode = str_to_detection_mode(kv.value().as<const char *>());
      prefs.putUInt("mode", static_cast<int>(beam.mode));
      if (app_task_handle) {
        vTaskDelete(app_task_handle);
        app_task_handle = NULL;
      }
    } else if (kv.key() == "adc_threshold") {
      beam.adc_threshold = kv.value().as<int>();
      prefs.putUInt("adc_threshold", beam.adc_threshold);
      LOGF("ADC threshold updated to %d\n", kv.value().as<int>())
    } else if (kv.key() == "crossings") {
      beam.crossings = kv.value().as<int>();
      prefs.putUInt("crossings", beam.crossings);
      LOGF("Wheel crossings updated to %d\n", kv.value().as<int>());
    } else if (kv.key() == "intensity") {
      intensity = kv.value().as<int>();
      set_display_intensity(intensity);
      prefs.putUInt("intensity", intensity);
      LOGF("Display intensity updated to %d\n", intensity);
    }
  }
}

void handle_touch() {
  if (touch_strip_touched) {
    static unsigned long last_touch_time = micros();
    if (micros() - last_touch_time > TOUCH_STRIP_TIMEOUT_US) {
      uint8_t intensity = (prefs.getUInt("intensity", 4) + 1) % 16;
      set_display_intensity(intensity);
      prefs.putUInt("intensity", intensity);
      last_touch_time = micros();
    }
    touch_strip_touched = false;
  }
}