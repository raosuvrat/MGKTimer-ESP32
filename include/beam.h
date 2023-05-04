#ifndef BEAM_H
#define BEAM_H

#include <Arduino.h>

#include "pins.h"

#define IR_PULSE_HIGH_TIME_US 100000
#define IR_PULSE_LOW_TIME_US 17000
#define IR_PULSE_PERIOD_US IR_PULSE_HIGH_TIME_US + IR_PULSE_LOW_TIME_US
#define IR_PULSE_TRAIN_RES 2
#define IR_PULSE_TRAIN_DUTY_CYCLE 2
#define IR_PULSE_TRAIN_PWM_CHANNEL 0
#define IR_PULSE_CARRIER_FQ 38000

#define IR_PULSE_TRAIN_TIMER 0
#define IR_PULSE_TRAIN_TIMER_PRESCALER 80
#define IR_PULSE_TRAIN_TIMER_INTERVAL 1000
#define POLL_BEAM_TIMER 1
#define POLL_BEAM_TIMER_PRESCALER 80
#define POLL_BEAM_TIMER_INTERVAL 100

#define DEFAULT_ADC_THRESHOLD 512

enum detection_mode_t {
  LASER_PHOTOTRANS_DIG,
  LASER_PHOTOTRANS_ADC,
  LASER_IR_RECV,
  IR_LED_IR_RECV,
};

enum beam_state_t {
  NOT_ESTABLISHED,
  RECEIVED,
  INTERRUPTED,
};

void init_pulse_train();
void init_beam();
void reset_beam();
void set_beam_mode(detection_mode_t mode);

void IRAM_ATTR ISR_ir_pulse_train_gen();
void IRAM_ATTR ISR_ir_recv_state_change();
void IRAM_ATTR ISR_poll_beam();
void IRAM_ATTR ISR_phototrans_recv_state_change();
void poll_phototrans_adc_task(void *pvp);
void IRAM_ATTR update_beam_state(bool recv, unsigned long t);
int IRAM_ATTR local_adc1_read(int channel);

struct beam_t {
  detection_mode_t mode = LASER_PHOTOTRANS_DIG;
  volatile unsigned long start_time = 0;
  volatile unsigned long change_time = 0;
  volatile unsigned long finish_time = 0;
  volatile unsigned int counter = 0;
  volatile unsigned long samples = 0;
  volatile double sample_rate = 0;
  unsigned int crossings = 4;

  volatile beam_state_t state = NOT_ESTABLISHED;

  unsigned int adc_threshold = DEFAULT_ADC_THRESHOLD;
  volatile unsigned int adc_value = 0;
};

extern beam_t beam;

#endif