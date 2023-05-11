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
  pinMode(LASER_PIN, OUTPUT);
  pinMode(IR_RECV_PIN, INPUT);
  pinMode(PHOTOTRANS_PIN, INPUT);
  pinMode(PHOTOTRANS_2_PIN, INPUT);
  pinMode(TOUCH_STRIP_PIN, INPUT);

  touchAttachInterrupt(TOUCH_STRIP_PIN, ISR_touch_strip, 40);

  init_fs();
  init_wifi(&dns_server);
  init_webserver(&server, &socket, ws_data_handler);
  init_display(&md_max);

  prefs.begin("MGKTimer", false);
  // beam.mode = static_cast<detection_mode_t>(prefs.getUInt("mode"));
  beam.mode = LASER_PHOTOTRANS_DIG;
  beam.crossings = prefs.getUInt("crossings", 4);
  beam.adc_threshold = prefs.getUInt("adc_threshold", 512);
  set_display_intensity(prefs.getUInt("intensity", 4));
}

void app_task(void *pvParameters) {
  display_print("Ready");

  while (true) {
    reset_beam();
    while (!beam.counter) {
      update_clients("Ready");
      vTaskDelay(100);
    }
    update_clients("Ready");
    while (!beam.finish_time) {
      update_clients("Timer running");
      display_time(beam.start_time, micros());
      vTaskDelay(100);
    }
    display_time(beam.start_time, beam.finish_time);
    update_clients("Finish");
  }
}

void loop() {
  handle_touch();
  if (!app_task_handle) {
    update_clients("Checking beam stability");
    bool check_beam_stable = true;
    init_beam(&beam);
    delay(500);
    for (int i = 0; i < 32; ++i) {
      check_beam_stable &= beam.state == RECEIVED && !beam.counter;
      buf[i] = '|';
      buf[i + 1] = '\0';
      display_print(buf, 0);
      delay(2000 / 32);
    }
    if (check_beam_stable) {
      update_clients("Locked");
      display_print("Locked");
      delay(1000);
      xTaskCreatePinnedToCore(app_task, "app_task", 10000, NULL, 1,
                              &app_task_handle, 1);
    } else {
      display_print("No lock");
    }
  }
}

void update_clients(const char *msg) {
  if (socket.availableForWriteAll()) {
    doc["counter"] = beam.counter;
    doc["samples"] = beam.samples;
    doc["sample_rate"] = beam.sample_rate;
    doc["adc_threshold"] = beam.adc_threshold;
    doc["adc_value"] = beam.adc_value;
    // doc["adc2_value"] = local_adc1_read(PHOTOTRANS_2_ADC1_CHANNEL);
    doc["intensity"] = prefs.getUInt("intensity", 4);
    doc["touchread"] = touchRead(27);
    doc["state"] = beam.state == RECEIVED      ? "RECEIVED"
                   : beam.state == INTERRUPTED ? "INTERRUPTED"
                                               : "NOT_ESTABLISHED";
    doc["mode"] = beam.mode == LASER_IR_RECV          ? "LASER_IR_RECV"
                  : beam.mode == LASER_PHOTOTRANS_ADC ? "LASER_PHOTOTRANS_ADC"
                  : beam.mode == LASER_PHOTOTRANS_DIG ? "LASER_PHOTOTRANS_DIG"
                                                      : "?";
    doc["start"] = beam.start_time;
    doc["finish"] = beam.finish_time;
    doc["change"] = beam.change_time;
    doc["crossings"] = beam.crossings;
    doc["time"] = micros();
    doc["msg"] = msg ? msg : "";
    doc["free_heap"] = ESP.getFreeHeap();
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
      vTaskDelete(app_task_handle);
      app_task_handle = NULL;
    } else if (kv.key() == "adc_threshold") {
      beam.adc_threshold = kv.value().as<int>();
      prefs.putUInt("adc_threshold", beam.adc_threshold);
      LOGF("ADC threshold updated to %d\n", kv.value().as<int>())
    } else if (kv.key() == "crossings") {
      beam.crossings = kv.value().as<int>();
      prefs.putUInt("crossings", beam.crossings);
      LOGF("Wheel crossings updated to %d\n", kv.value().as<int>());
    } else if (kv.key() == "intensity") {
      uint8_t intensity = kv.value().as<int>();
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