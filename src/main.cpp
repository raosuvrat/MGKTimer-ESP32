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
#define APP_TASK_CORE 1
#define APP_TASK_PRI 1

#define DEFAULT_MODE LASER_PHOTOTRANS_ADC
#define DEFAULT_INTENSITY 4
#define DEFAULT_WHEEL_CROSSINGS 3
#define DEFAULT_ADC_THRESHOLD 512
#define DEFAULT_BEAM_CROSS_LOCKOUT_MS 0

void ws_command_handler(StaticJsonDocument<512> doc);
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
static StaticJsonDocument<1024> txdoc;
static char txbuf[1024];
static volatile bool touch_strip_touched = false;

void init_pins() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(LASER_PIN, OUTPUT);
  pinMode(IR_RECV_PIN, INPUT);
  pinMode(TOUCH_STRIP_PIN, INPUT);

  pinMode(PHOTOTRANS_100000_OHM_LOAD_PIN, OUTPUT);
  digitalWrite(PHOTOTRANS_100000_OHM_LOAD_PIN, LOW);
  pinMode(PHOTOTRANS_PIN, INPUT_PULLDOWN);
  pinMode(PHOTOTRANS_2_PIN, INPUT_PULLUP);

  touchAttachInterrupt(TOUCH_STRIP_PIN, ISR_touch_strip, 40);
}

void init_prefs() {
  prefs.begin("MGKTimer", false);

  intensity = prefs.getUInt("intensity", DEFAULT_INTENSITY);
  beam.mode = static_cast<detection_mode_t>(
      prefs.getUInt("mode", static_cast<int>(DEFAULT_MODE)));

  beam.crossings = prefs.getUInt("crossings", DEFAULT_WHEEL_CROSSINGS);
  beam.adc_threshold = prefs.getUInt("adc_threshold", DEFAULT_ADC_THRESHOLD);
  beam.beam_cross_lockout_ms =
      prefs.getUInt("beam_cross_lockout_ms", DEFAULT_BEAM_CROSS_LOCKOUT_MS);
  set_display_intensity(intensity);
}
}

void setup() {
#if DEBUG == 1
  Serial.begin(960000);
  LOGF("Serial debugging enabled\n");
#endif

  init_fs();
  init_wifi(&dns_server);
  init_webserver(&server, &socket, ws_command_handler);
  init_display(&md_max);
  init_pins();
  init_prefs();
}

void app_task(void *pvParameters) {
  display_print("Ready");
  update_clients("Ready");

  while (true) {
    reset_beam();
    while (!beam.counter) {
      update_clients("Ready");
      vTaskDelay(100);
    }
    while (!beam.finish_time) {
      update_clients("Running");
      display_time(beam.start_time, micros());
      vTaskDelay(100);
    }
    display_time(beam.start_time, beam.finish_time);
    update_clients("Finish");
  }
}

bool check_beam_stability() {
  bool check_beam_stable = true;
  init_beam(&beam);
  delay(500);
  char buf[33];
  for (int i = 0; i < 32; ++i) {
    update_clients("Checking");
    check_beam_stable &= beam.state == RECEIVED && !beam.counter;
    buf[i] = ';';
    buf[i + 1] = '\0';
    display_print(buf, 0);
    delay(2000 / 32);
  }
  return check_beam_stable;
}

void loop() {
  ArduinoOTA.handle();
  handle_touch();
  if (!app_task_handle) {
    if (check_beam_stability()) {
      display_print("Locked");
      delay(1000);
      xTaskCreatePinnedToCore(app_task, "app_task", 100000, NULL, APP_TASK_PRI,
                              &app_task_handle, APP_TASK_CORE);
    } else {
      display_print("No lock");
    }
  }
}

void update_clients(const char *msg) {
  char msg_l[strlen(msg) + 1] = {0};
  if (msg) {
    for (int i = 0; i < strlen(msg); ++i) msg_l[i] = tolower(msg[i]);
  }

  if (socket.availableForWriteAll()) {
    txdoc["msg"] = msg ? msg_l : "";
    txdoc["mode"] = detection_mode_to_str(beam.mode);
    txdoc["state"] = beam.state == RECEIVED      ? "RECEIVED"
                     : beam.state == INTERRUPTED ? "INTERRUPTED"
                                                 : "NOT_ESTABLISHED";
    txdoc["counter"] = beam.counter;
    txdoc["start"] = beam.start_time;
    txdoc["finish"] = beam.finish_time;
    txdoc["change"] = beam.change_time;
    txdoc["crossings"] = beam.crossings;
    txdoc["beam_cross_lockout_ms"] = beam.beam_cross_lockout_ms;
    txdoc["time"] = micros();
    txdoc["adc_value"] = beam.adc_value;
    txdoc["adc_threshold"] = beam.adc_threshold;
    txdoc["adc_sample_time"] = beam.adc_sample_time;
    txdoc["samples"] = beam.samples;
    txdoc["sample_rate"] = beam.sample_rate;
    txdoc["intensity"] = intensity;
    txdoc["touchread"] = touchRead(TOUCH_STRIP_PIN);
    txdoc["free_heap"] = ESP.getFreeHeap();
    txdoc["txdoc_size"] = 1024;
    txdoc["txdoc_size"] = txdoc.memoryUsage();

    serializeJson(txdoc, txbuf);
    socket.textAll(txbuf);
  }
}

void IRAM_ATTR ISR_touch_strip() { touch_strip_touched = true; }

void ws_command_handler(StaticJsonDocument<512> doc) {
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
    } else if (kv.key() == "beam_cross_lockout_ms") {
      beam.beam_cross_lockout_ms = kv.value().as<int>();
      prefs.putUInt("beam_cross_lockout_us", beam.beam_cross_lockout_ms);
      LOGF("Beam cross lockout updated to %d\n", beam.beam_cross_lockout_ms);
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