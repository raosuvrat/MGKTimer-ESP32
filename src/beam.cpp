#include "beam.h"

#include <Arduino.h>
#include <driver/adc.h>
#include <soc/sens_reg.h>
#include <soc/sens_struct.h>

#include "debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static hw_timer_t *ir_pulse_train_timer = NULL, *poll_beam_timer = NULL;
static portMUX_TYPE ir_pulse_train_spinlock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE recv_isr_spinlock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t poll_task_handle = NULL;

void init_ir_pulse_train() {
  ledcSetup(IR_PULSE_TRAIN_PWM_CHANNEL, IR_PULSE_CARRIER_FQ,
            IR_PULSE_TRAIN_RES);

  ir_pulse_train_timer =
      timerBegin(IR_PULSE_TRAIN_TIMER, IR_PULSE_TRAIN_TIMER_PRESCALER, true);
  timerAttachInterrupt(ir_pulse_train_timer, ISR_ir_pulse_train_gen, true);
  timerAlarmWrite(ir_pulse_train_timer, IR_PULSE_TRAIN_TIMER_INTERVAL, true);
  timerAlarmEnable(ir_pulse_train_timer);

  LOGF("IR Pulse Train started on channel %d\n", IR_PULSE_TRAIN_PWM_CHANNEL);
}

void reset_beam() {
  beam.start_time = 0;
  beam.change_time = 0;
  beam.finish_time = 0;
  beam.counter = 0;
  beam.samples = 0;
  beam.sample_rate = 0;
  beam.state = NOT_ESTABLISHED;
  beam.adc_value = 0;
}

void init_beam() {
  pinMode(LASER_PIN, OUTPUT);
  pinMode(IR_LED_PIN, OUTPUT);
  pinMode(IR_RECV_PIN, INPUT);
  pinMode(PHOTOTRANS_PIN, INPUT);

  ledcDetachPin(LASER_PIN);
  ledcDetachPin(IR_LED_PIN);
  detachInterrupt(IR_RECV_PIN);
  detachInterrupt(PHOTOTRANS_PIN);

  digitalWrite(LASER_PIN, LOW);
  digitalWrite(IR_LED_PIN, LOW);

  if (poll_task_handle) {
    vTaskDelete(poll_task_handle);
    poll_task_handle = NULL;
  }

  reset_beam();

  switch (beam.mode) {
    case LASER_IR_RECV:
      if (!ir_pulse_train_timer) {
        init_ir_pulse_train();
      }
      ledcAttachPin(LASER_PIN, IR_PULSE_TRAIN_PWM_CHANNEL);
      LOGF("Laser IR-modulated beam send configured\n");
      // attachInterrupt(IR_RECV_PIN, ISR_ir_recv_state_change, CHANGE);
      LOGF("IR Receiver configured\n");
      break;

    case IR_LED_IR_RECV:
      if (!ir_pulse_train_timer) {
        init_ir_pulse_train();
      }
      ledcAttachPin(IR_LED_PIN, IR_PULSE_TRAIN_PWM_CHANNEL);
      LOGF("LED IR-modulated beam send configured\n");
      // attachInterrupt(IR_RECV_PIN, ISR_ir_recv_state_change, CHANGE);
      LOGF("IR Receiver configured\n");
      break;

    case LASER_PHOTOTRANS_DIG:
      digitalWrite(LASER_PIN, HIGH);
      LOGF("Laser configured\n");
      // attachInterrupt(PHOTOTRANS_PIN, ISR_phototrans_recv_state_change,
      // CHANGE);
      LOGF("Laser phototransistor dig recv configured\n");
      break;

    case LASER_PHOTOTRANS_ADC:
      digitalWrite(LASER_PIN, HIGH);
      analogRead(PHOTOTRANS_PIN);
      adc1_config_width(ADC_WIDTH_BIT_12);
      adc1_config_channel_atten(PHOTOTRANS_ADC1_CHANNEL, ADC_ATTEN_DB_11);
      adc1_get_raw(PHOTOTRANS_ADC1_CHANNEL);
      local_adc1_read(PHOTOTRANS_ADC1_CHANNEL);
      LOGF("Laser phototransistor adc recv configured\n");
      break;
  }
  if (!poll_beam_timer) {
    poll_beam_timer =
        timerBegin(POLL_BEAM_TIMER, POLL_BEAM_TIMER_PRESCALER, true);
    timerAttachInterrupt(poll_beam_timer, ISR_poll_beam, true);
    timerAlarmWrite(poll_beam_timer, POLL_BEAM_TIMER_INTERVAL, true);
    timerAlarmEnable(poll_beam_timer);
  }
}

int IRAM_ATTR local_adc1_read(int channel) {
  uint16_t adc_value;
  SENS.sar_meas_start1.sar1_en_pad = (1 << channel);
  while (SENS.sar_slave_addr1.meas_status != 0)
    ;
  SENS.sar_meas_start1.meas1_start_sar = 0;
  SENS.sar_meas_start1.meas1_start_sar = 1;
  while (SENS.sar_meas_start1.meas1_done_sar == 0)
    ;
  adc_value = SENS.sar_meas_start1.meas1_data_sar;
  return adc_value;
}

void IRAM_ATTR ISR_poll_beam() {
  portENTER_CRITICAL_ISR(&recv_isr_spinlock);
  bool recv;
  unsigned long t = micros();
  switch (beam.mode) {
    case LASER_PHOTOTRANS_ADC:
      beam.adc_value = local_adc1_read(PHOTOTRANS_ADC1_CHANNEL);
      recv = beam.adc_value > beam.adc_threshold;
      update_beam_state(recv, t);
      break;
    case LASER_PHOTOTRANS_DIG:
      recv = digitalRead(PHOTOTRANS_PIN);
      update_beam_state(recv, t);
      break;
    case LASER_IR_RECV:
    case IR_LED_IR_RECV:
      if ((t % IR_PULSE_PERIOD_US) <= IR_PULSE_HIGH_TIME_US) {
        recv = !digitalRead(IR_RECV_PIN);
        update_beam_state(recv, t);
      }
      break;
  }
  portEXIT_CRITICAL_ISR(&recv_isr_spinlock);
}

void IRAM_ATTR update_beam_state(bool recv, unsigned long t) {
  if (beam.state == NOT_ESTABLISHED) {
    if (!recv) return;
  }
  if (beam.state == RECEIVED && !recv && !beam.counter) {
    beam.start_time = t;
  } else if (beam.state == INTERRUPTED && recv) {
    beam.counter++;
  }
  if (beam.counter && !(beam.counter % beam.crossings)) {
    beam.finish_time = t;
  }
  beam.state = recv ? RECEIVED : INTERRUPTED;
  beam.samples++;
  double sample_rate = 1000000.0 / (t - beam.change_time);
  beam.sample_rate =
      (beam.sample_rate * (beam.samples - 1) + sample_rate) / beam.samples;
  beam.change_time = t;
  digitalWrite(STATUS_LED_PIN, recv);
}

void IRAM_ATTR ISR_ir_pulse_train_gen() {
  portENTER_CRITICAL_ISR(&ir_pulse_train_spinlock);
  if ((micros() % IR_PULSE_PERIOD_US) <= IR_PULSE_HIGH_TIME_US) {
    ledcWrite(IR_PULSE_TRAIN_PWM_CHANNEL, IR_PULSE_TRAIN_DUTY_CYCLE);
  } else {
    ledcWrite(IR_PULSE_TRAIN_PWM_CHANNEL, 0);
  }
  portEXIT_CRITICAL_ISR(&ir_pulse_train_spinlock);
}

void IRAM_ATTR ISR_ir_recv_state_change() {
  portENTER_CRITICAL_ISR(&recv_isr_spinlock);
  bool recv = !digitalRead(IR_RECV_PIN);
  unsigned long t = micros();

  if ((micros() % IR_PULSE_PERIOD_US) <= IR_PULSE_HIGH_TIME_US) {
    update_beam_state(recv, t);
  }
  portEXIT_CRITICAL_ISR(&recv_isr_spinlock);
}

void IRAM_ATTR ISR_phototrans_recv_state_change() {
  portENTER_CRITICAL_ISR(&recv_isr_spinlock);
  bool recv = digitalRead(PHOTOTRANS_PIN);
  unsigned long t = micros();
  update_beam_state(recv, t);
  portEXIT_CRITICAL_ISR(&recv_isr_spinlock);
}