#ifndef PINS_H
#define PINS_H

#include <driver/adc.h>

#define IR_RECV_PIN 19
#define IR_LED_PIN 25
#define STATUS_LED_PIN 21
#define LASER_PIN 22
#define PHOTOTRANS_PIN 34
#define PHOTOTRANS_100000_OHM_LOAD_PIN 26
#define PHOTOTRANS_ADC1_CHANNEL ADC1_CHANNEL_6
#define PHOTOTRANS_2_PIN 35
#define PHOTOTRANS_2_ADC1_CHANNEL ADC1_CHANNEL_7

#define MD_CLK_PIN 18   // or SCK
#define MD_DATA_PIN 23  // or MOSI
#define MD_CS_PIN 5     // or SS

#endif