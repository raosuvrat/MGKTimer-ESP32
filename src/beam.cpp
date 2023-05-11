#include "beam.h"

#include <Arduino.h>
#include <driver/adc.h>
#include <soc/sens_reg.h>
#include <soc/sens_struct.h>

#include "debug.h"

static hw_timer_t *ir_pulse_train_timer = NULL, *poll_beam_timer = NULL;
static portMUX_TYPE ir_pulse_train_spinlock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE recv_isr_spinlock = portMUX_INITIALIZER_UNLOCKED;

static beam_t *beam_ptr = NULL;

void reset_beam() {
  beam_ptr->start_time = 0;
  beam_ptr->change_time = 0;
  beam_ptr->finish_time = 0;
  beam_ptr->counter = 0;
  beam_ptr->samples = 0;
  beam_ptr->sample_rate = 0;
  beam_ptr->state = NOT_ESTABLISHED;
  beam_ptr->adc_value = 0;
}

void init_beam(beam_t *beam) {
  beam_ptr = beam;
  if (ir_pulse_train_timer) {
    ledcDetachPin(LASER_PIN);
    timerDetachInterrupt(ir_pulse_train_timer);
    timerEnd(ir_pulse_train_timer);
  }
  if (poll_beam_timer) {
    timerDetachInterrupt(poll_beam_timer);
    timerEnd(poll_beam_timer);
  }
  poll_beam_timer =
      timerBegin(POLL_BEAM_TIMER, POLL_BEAM_TIMER_PRESCALER, true);

  switch (beam_ptr->mode) {
    case LASER_IR_RECV:
      ledcSetup(IR_PULSE_TRAIN_PWM_CHANNEL, IR_PULSE_CARRIER_FQ,
                IR_PULSE_TRAIN_RES);

      ir_pulse_train_timer = timerBegin(IR_PULSE_TRAIN_TIMER,
                                        IR_PULSE_TRAIN_TIMER_PRESCALER, true);
      timerAttachInterrupt(ir_pulse_train_timer, ISR_ir_pulse_train_gen, true);
      timerAlarmWrite(ir_pulse_train_timer, IR_PULSE_TRAIN_TIMER_INTERVAL,
                      true);
      timerAlarmEnable(ir_pulse_train_timer);

      LOGF("IR Pulse Train started on channel %d\n",
           IR_PULSE_TRAIN_PWM_CHANNEL);

      ledcAttachPin(LASER_PIN, IR_PULSE_TRAIN_PWM_CHANNEL);
      LOGF("IR Receiver configured\n");
      timerAlarmWrite(poll_beam_timer, POLL_BEAM_TIMER_INTERVAL_IR, true);
      break;

    case LASER_PHOTOTRANS_DIG:
      digitalWrite(LASER_PIN, HIGH);
      timerAlarmWrite(poll_beam_timer, POLL_BEAM_TIMER_INTERVAL_DIG, true);
      LOGF("Laser phototransistor dig recv configured\n");
      break;

    case LASER_PHOTOTRANS_ADC:
      digitalWrite(LASER_PIN, HIGH);
      analogRead(PHOTOTRANS_PIN);
      adc1_config_width(ADC_WIDTH_BIT_12);
      adc1_config_channel_atten(PHOTOTRANS_ADC1_CHANNEL, ADC_ATTEN_DB_11);
      LOGF("Laser phototransistor adc recv configured\n");
      timerAlarmWrite(poll_beam_timer, POLL_BEAM_TIMER_INTERVAL_ADC, true);
      break;
  }

  timerAttachInterrupt(poll_beam_timer, ISR_poll_beam, true);
  timerAlarmEnable(poll_beam_timer);
  reset_beam();
}

uint16_t IRAM_ATTR local_adc1_read(int channel) {
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
  switch (beam_ptr->mode) {
    case LASER_PHOTOTRANS_ADC:
      beam_ptr->adc_value = local_adc1_read(PHOTOTRANS_ADC1_CHANNEL);
      recv = beam_ptr->adc_value > beam_ptr->adc_threshold;
      update_beam_state(recv, t);
      break;
    case LASER_PHOTOTRANS_DIG:
      recv = digitalRead(PHOTOTRANS_PIN);
      update_beam_state(recv, t);
      break;
    case LASER_IR_RECV:
      if ((t % IR_PULSE_PERIOD_US) <= IR_PULSE_HIGH_TIME_US) {
        recv = !digitalRead(IR_RECV_PIN);
        update_beam_state(recv, t);
      }
      break;
  }
  portEXIT_CRITICAL_ISR(&recv_isr_spinlock);
}

void IRAM_ATTR update_beam_state(bool recv, unsigned long t) {
  if (beam_ptr->state == NOT_ESTABLISHED) {
    if (!recv) return;
  }
  if (beam_ptr->state == RECEIVED && !recv && !beam_ptr->counter) {
    beam_ptr->start_time = t;
  } else if (beam_ptr->state == INTERRUPTED && recv) {
    beam_ptr->counter++;
  }
  if (beam_ptr->counter && !(beam_ptr->counter % beam_ptr->crossings)) {
    beam_ptr->finish_time = t;
  }
  beam_ptr->state = recv ? RECEIVED : INTERRUPTED;
  beam_ptr->samples++;
  double sample_rate = 1000000.0 / (t - beam_ptr->change_time);
  beam_ptr->sample_rate =
      (beam_ptr->sample_rate * (beam_ptr->samples - 1) + sample_rate) /
      beam_ptr->samples;
  beam_ptr->change_time = t;
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

detection_mode_t str_to_detection_mode(const char *str) {
  if (!strcmp(str, "LASER_IR_RECV")) {
    return LASER_IR_RECV;
  } else if (!strcmp(str, "LASER_PHOTOTRANS_DIG")) {
    return LASER_PHOTOTRANS_DIG;
  } else if (!strcmp(str, "LASER_PHOTOTRANS_ADC")) {
    return LASER_PHOTOTRANS_ADC;
  }
  return INVALID;
}