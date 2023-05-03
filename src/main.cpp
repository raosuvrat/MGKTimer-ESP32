#include <Arduino.h>
#include <Preferences.h>

#include "beam.h"
#include "debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "md_max.h"
#include "pins.h"
#include "web.h"

// struct app_state_t {
//   beam_t beam;
//   Preferences prefs;
//   MD_Parola display;
//   DNSServer dns_server;
//   AsyncWebServer server;

// } app;

void ws_data_handler(StaticJsonDocument<256> doc);
void update_clients(const char *msg = NULL);

beam_t beam;

static Preferences prefs;
static MD_Parola md_max =
    MD_Parola(MD_MAX72XX::FC16_HW, MD_CS_PIN, MD_MAX_DEVICES);
static DNSServer dns_server;
static AsyncWebServer server(HTTP_PORT);
static AsyncWebSocket socket(WEBSOCKET_NAME);
static StaticJsonDocument<512> doc;
static char buf[512];

void setup() {
#if DEBUG == 1
  Serial.begin(960000);
  LOGF("Serial debugging enabled\n");
#endif

  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(PHOTOTRANS_2_PIN, INPUT);

  init_fs();
  init_wifi(&dns_server);
  init_webserver(&server, &socket, ws_data_handler);
  init_display(&md_max);

  prefs.begin("MGKTimer", false);
  beam.mode = static_cast<detection_mode_t>(prefs.getUInt("mode"));
  beam.crossings = prefs.getUInt("crossings", 4);
  beam.adc_threshold = prefs.getUInt("adc_threshold", 512);
  display_intensity(prefs.getUInt("intensity", 4));

  init_beam();
}

void loop() {
  while (true) {
    bool beam_stable = true;
    display_print("Checkbm");
    update_clients("Checkbeam");
    for (int i = 0; i < 100; ++i) {
      beam_stable &= beam.state == RECEIVED;
      delay(10);
    }
    if (beam_stable) {
      LOGF("Beam stable\n");
      update_clients("Beam stable");
      break;
    }
  }
  reset_beam();
  update_clients("Ready");
  while (!beam.counter) {
    display_print("Ready");
    update_clients("Ready");
    delay(100);
  }
  update_clients("Ready");
  while (!beam.finish_time) {
    display_time(beam.start_time, micros());
    update_clients("Ready");
    delay(100);
  }
  display_time(beam.start_time, beam.finish_time);
  update_clients("Ready");
  // Wait 5 seconds to make sure other boards are ready
  delay(5000);
}

void update_clients(const char *msg) {
  if (socket.availableForWriteAll()) {
    doc["counter"] = beam.counter;
    doc["samples"] = beam.samples;
    doc["sample_rate"] = beam.sample_rate;
    doc["adc_threshold"] = beam.adc_threshold;
    doc["adc_value"] = beam.adc_value;
    // doc["adc2_value"] = local_adc1_read(PHOTOTRANS_2_ADC1_CHANNEL);
    doc["touchread"] = touchRead(27);
    doc["state"] = beam.state == RECEIVED      ? "RECEIVED"
                   : beam.state == INTERRUPTED ? "INTERRUPTED"
                                               : "NOT_ESTABLISHED";
    doc["mode"] = beam.mode == LASER_IR_RECV          ? "LASER_IR_RECV"
                  : beam.mode == IR_LED_IR_RECV       ? "IR_LED_IR_RECV"
                  : beam.mode == LASER_PHOTOTRANS_ADC ? "LASER_PHOTOTRANS_ADC"
                  : beam.mode == LASER_PHOTOTRANS_DIG ? "LASER_PHOTOTRANS_DIG"
                                                      : "?";
    doc["start"] = beam.start_time;
    doc["finish"] = beam.finish_time;
    doc["change"] = beam.change_time;
    doc["crossings"] = beam.crossings;
    doc["time"] = micros();
    doc["msg"] = msg ? msg : "";
    serializeJson(doc, buf);
    socket.textAll(buf);
  }
}

void ws_data_handler(StaticJsonDocument<256> doc) {
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (kv.key() == "mode") {
      if (kv.value() == "LASER_IR_RECV")
        beam.mode = LASER_IR_RECV;
      else if (kv.value() == "IR_LED_IR_RECV")
        beam.mode = IR_LED_IR_RECV;
      else if (kv.value() == "LASER_PHOTOTRANS_DIG")
        beam.mode = LASER_PHOTOTRANS_DIG;
      else if (kv.value() == "LASER_PHOTOTRANS_ADC")
        beam.mode = LASER_PHOTOTRANS_ADC;
      else
        LOGF("Invalid beam mode from client: %s\n", kv.value());

      prefs.putUInt("mode", static_cast<int>(beam.mode));
      init_beam();
    } else if (kv.key() == "adc_threshold") {
      beam.adc_threshold = kv.value().as<int>();
      prefs.putUInt("adc_threshold", beam.adc_threshold);
      LOGF("ADC threshold updated to %d\n", kv.value().as<int>())
    } else if (kv.key() == "crossings") {
      beam.crossings = kv.value().as<int>();
      prefs.putUInt("crossings", beam.crossings);
      LOGF("Beam crossings updated to %d\n", kv.value().as<int>());
    } else if (kv.key() == "intensity") {
      int intensity = kv.value().as<int>();
      display_intensity(intensity);
      prefs.putUInt("intensity", intensity);
      LOGF("Screen intensity updated to %d\n", intensity);
    }
  }
}