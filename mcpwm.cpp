
// Support for MCPWM, eg for H-bridge motor controller
//
// s60sc 2024
//

/*
MCPWM peripheral has 2 units, each unit can support:
- 3 pairs of PWM outputs (6 pins)
- 3 fault input pins to detect faults like overcurrent, overvoltage, etc.
- 3 sync input pins to synchronize output signals
- 3 input pins to gather feedback from controlled motors, using e.g. hall sensors

MX1508 DC Motor Driver with PWM Control
- 4 PWM gpio inputs, 2 per motor (forward & reverse)
- Two H-channel drive circuits for 2 DC motors 
- 1.5A (peak 2A)
- 2-10V DC input, 1.8-7V Dc output
- Outputs are OUT1 - OUT4 corresponding to IN1 to IN4
- IN1 / OUT1 A1
- IN2 / OUT2 B1
- IN3 / OUT3 A2
- IN4 / OUT4 B2
*/

#include "appGlobals.h"

#if INCLUDE_MCPWM
#if !INCLUDE_PERIPH
#error "Need INCLUDE_PERIPH true"
#endif

// Includes code from github.com/espressif/idf-extra-components/blob/master/bdc_motor
//  modified to compile with c++:
// - github.com/espressif/idf-extra-components/blob/master/bdc_motor/include/bdc_motor.h
// - github.com/espressif/idf-extra-components/blob/master/bdc_motor/interface/bdc_motor_interface.h
// - github.com/espressif/idf-extra-components/blob/master/bdc_motor/src/bdc_motor.c
// - github.com/espressif/idf-extra-components/tree/master/bdc_motor/src/bdc_motor_mcpwm_impl.c

/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/mcpwm_prelude.h"

 /**
 * @brief BDC Motor Configuration
 */
typedef struct {
    uint32_t pwma_gpio_num; /*!< BDC Motor PWM A gpio number */
    uint32_t pwmb_gpio_num; /*!< BDC Motor PWM B gpio number */
    uint32_t pwm_freq_hz;   /*!< PWM frequency, in Hz */
} bdc_motor_config_t;

/**
 * @brief BDC Motor MCPWM specific configuration
 */
typedef struct {
    int group_id;           /*!< MCPWM group number */
    uint32_t resolution_hz; /*!< MCPWM timer resolution */
} bdc_motor_mcpwm_config_t;


/**
 * @brief Brushed DC Motor handle
 */
struct bdc_motor_t {
    esp_err_t (*enable)(bdc_motor_t *motor);
    esp_err_t (*disable)(bdc_motor_t *motor);
    esp_err_t (*set_speed)(bdc_motor_t *motor, uint32_t speed);
    esp_err_t (*forward)(bdc_motor_t *motor);
    esp_err_t (*reverse)(bdc_motor_t *motor);
    esp_err_t (*coast)(bdc_motor_t *motor);
    esp_err_t (*brake)(bdc_motor_t *motor);
    esp_err_t (*del)(bdc_motor_t *motor);
};

typedef struct {
    bdc_motor_t base;
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t oper;
    mcpwm_cmpr_handle_t cmpa;
    mcpwm_cmpr_handle_t cmpb;
    mcpwm_gen_handle_t gena;
    mcpwm_gen_handle_t genb;
} bdc_motor_mcpwm_obj;

typedef struct bdc_motor_t *bdc_motor_handle_t;

static const char *TAG = "bdc_motor";

static esp_err_t bdc_motor_mcpwm_set_speed(bdc_motor_t *motor, uint32_t speed)
{
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    ESP_RETURN_ON_ERROR(mcpwm_comparator_set_compare_value(mcpwm_motor->cmpa, speed), TAG, "set compare value failed");
    ESP_RETURN_ON_ERROR(mcpwm_comparator_set_compare_value(mcpwm_motor->cmpb, speed), TAG, "set compare value failed");
    return ESP_OK;
}

static esp_err_t bdc_motor_mcpwm_enable(bdc_motor_t *motor)
{
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    ESP_RETURN_ON_ERROR(mcpwm_timer_enable(mcpwm_motor->timer), TAG, "enable timer failed");
    ESP_RETURN_ON_ERROR(mcpwm_timer_start_stop(mcpwm_motor->timer, MCPWM_TIMER_START_NO_STOP), TAG, "start timer failed");
    return ESP_OK;
}

static esp_err_t bdc_motor_mcpwm_disable(bdc_motor_t *motor)
{
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    ESP_RETURN_ON_ERROR(mcpwm_timer_start_stop(mcpwm_motor->timer, MCPWM_TIMER_STOP_EMPTY), TAG, "stop timer failed");
    ESP_RETURN_ON_ERROR(mcpwm_timer_disable(mcpwm_motor->timer), TAG, "disable timer failed");
    return ESP_OK;
}

static esp_err_t bdc_motor_mcpwm_forward(bdc_motor_t *motor)
{
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->gena, -1, true), TAG, "disable force level for gena failed");
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->genb, 0, true), TAG, "set force level for genb failed");
    return ESP_OK;
}

static esp_err_t bdc_motor_mcpwm_reverse(bdc_motor_t *motor)
{
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->genb, -1, true), TAG, "disable force level for genb failed");
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->gena, 0, true), TAG, "set force level for gena failed");
    return ESP_OK;
}

static esp_err_t bdc_motor_mcpwm_coast(bdc_motor_t *motor)
{
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->gena, 0, true), TAG, "set force level for gena failed");
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->genb, 0, true), TAG, "set force level for genb failed");
    return ESP_OK;
}

static esp_err_t bdc_motor_mcpwm_brake(bdc_motor_t *motor)
{
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->gena, 1, true), TAG, "set force level for gena failed");
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->genb, 1, true), TAG, "set force level for genb failed");
    return ESP_OK;
}

static esp_err_t bdc_motor_mcpwm_del(bdc_motor_t *motor)
{
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    mcpwm_del_generator(mcpwm_motor->gena);
    mcpwm_del_generator(mcpwm_motor->genb);
    mcpwm_del_comparator(mcpwm_motor->cmpa);
    mcpwm_del_comparator(mcpwm_motor->cmpb);
    mcpwm_del_operator(mcpwm_motor->oper);
    mcpwm_del_timer(mcpwm_motor->timer);
    free(mcpwm_motor);
    return ESP_OK;
}

static esp_err_t bdc_motor_new_mcpwm_device(const bdc_motor_config_t *motor_config, const bdc_motor_mcpwm_config_t *mcpwm_config, bdc_motor_handle_t *ret_motor)
{
    bdc_motor_mcpwm_obj *mcpwm_motor = NULL;
    esp_err_t ret = ESP_OK;

    // mcpwm timer
    mcpwm_timer_config_t timer_config = {
        .group_id = mcpwm_config->group_id,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = mcpwm_config->resolution_hz,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = mcpwm_config->resolution_hz / motor_config->pwm_freq_hz,
    };

    mcpwm_operator_config_t operator_config = {
        .group_id = mcpwm_config->group_id,
    };

    mcpwm_comparator_config_t comparator_config = {
      .flags = {
        .update_cmp_on_tez = true,
      }
    };

    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = (int)motor_config->pwma_gpio_num,
    };

    ESP_GOTO_ON_FALSE(motor_config && mcpwm_config && ret_motor, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");

    mcpwm_motor = (bdc_motor_mcpwm_obj*)calloc(1, sizeof(bdc_motor_mcpwm_obj));
    ESP_GOTO_ON_FALSE(mcpwm_motor, ESP_ERR_NO_MEM, err, TAG, "no mem for rmt motor");

    ESP_GOTO_ON_ERROR(mcpwm_new_timer(&timer_config, &mcpwm_motor->timer), err, TAG, "create MCPWM timer failed");

    ESP_GOTO_ON_ERROR(mcpwm_new_operator(&operator_config, &mcpwm_motor->oper), err, TAG, "create MCPWM operator failed");

    ESP_GOTO_ON_ERROR(mcpwm_operator_connect_timer(mcpwm_motor->oper, mcpwm_motor->timer), err, TAG, "connect timer and operator failed");

    ESP_GOTO_ON_ERROR(mcpwm_new_comparator(mcpwm_motor->oper, &comparator_config, &mcpwm_motor->cmpa), err, TAG, "create comparator failed");
    ESP_GOTO_ON_ERROR(mcpwm_new_comparator(mcpwm_motor->oper, &comparator_config, &mcpwm_motor->cmpb), err, TAG, "create comparator failed");

    // set the initial compare value for both comparators
    mcpwm_comparator_set_compare_value(mcpwm_motor->cmpa, 0);
    mcpwm_comparator_set_compare_value(mcpwm_motor->cmpb, 0);

    ESP_GOTO_ON_ERROR(mcpwm_new_generator(mcpwm_motor->oper, &generator_config, &mcpwm_motor->gena), err, TAG, "create generator failed");
    generator_config.gen_gpio_num = motor_config->pwmb_gpio_num;
    ESP_GOTO_ON_ERROR(mcpwm_new_generator(mcpwm_motor->oper, &generator_config, &mcpwm_motor->genb), err, TAG, "create generator failed");

    mcpwm_generator_set_actions_on_timer_event(mcpwm_motor->gena,
            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
            MCPWM_GEN_TIMER_EVENT_ACTION_END());
    mcpwm_generator_set_actions_on_compare_event(mcpwm_motor->gena,
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, mcpwm_motor->cmpa, MCPWM_GEN_ACTION_LOW),
            MCPWM_GEN_COMPARE_EVENT_ACTION_END());
    mcpwm_generator_set_actions_on_timer_event(mcpwm_motor->genb,
            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH),
            MCPWM_GEN_TIMER_EVENT_ACTION_END());
    mcpwm_generator_set_actions_on_compare_event(mcpwm_motor->genb,
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, mcpwm_motor->cmpb, MCPWM_GEN_ACTION_LOW),
            MCPWM_GEN_COMPARE_EVENT_ACTION_END());

    mcpwm_motor->base.enable = bdc_motor_mcpwm_enable;
    mcpwm_motor->base.disable = bdc_motor_mcpwm_disable;
    mcpwm_motor->base.forward = bdc_motor_mcpwm_forward;
    mcpwm_motor->base.reverse = bdc_motor_mcpwm_reverse;
    mcpwm_motor->base.coast = bdc_motor_mcpwm_coast;
    mcpwm_motor->base.brake = bdc_motor_mcpwm_brake;
    mcpwm_motor->base.set_speed = bdc_motor_mcpwm_set_speed;
    mcpwm_motor->base.del = bdc_motor_mcpwm_del;
    *ret_motor = &mcpwm_motor->base;
    return ESP_OK;

err:
    if (mcpwm_motor) {
        if (mcpwm_motor->gena) {
            mcpwm_del_generator(mcpwm_motor->gena);
        }
        if (mcpwm_motor->genb) {
            mcpwm_del_generator(mcpwm_motor->genb);
        }
        if (mcpwm_motor->cmpa) {
            mcpwm_del_comparator(mcpwm_motor->cmpa);
        }
        if (mcpwm_motor->cmpb) {
            mcpwm_del_comparator(mcpwm_motor->cmpb);
        }
        if (mcpwm_motor->oper) {
            mcpwm_del_operator(mcpwm_motor->oper);
        }
        if (mcpwm_motor->timer) {
            mcpwm_del_timer(mcpwm_motor->timer);
        }
        free(mcpwm_motor);
    }
    return ret;
}

static esp_err_t bdc_motor_enable(bdc_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return motor->enable(motor);
}

static esp_err_t bdc_motor_disable(bdc_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return motor->disable(motor);
}

static esp_err_t bdc_motor_set_speed(bdc_motor_handle_t motor, uint32_t speed)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return motor->set_speed(motor, speed);
}

static esp_err_t bdc_motor_forward(bdc_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return motor->forward(motor);
}

static esp_err_t bdc_motor_reverse(bdc_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return motor->reverse(motor);
}

static esp_err_t bdc_motor_coast(bdc_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return motor->coast(motor);
}

esp_err_t bdc_motor_brake(bdc_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return motor->brake(motor);
}

static esp_err_t bdc_motor_del(bdc_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return motor->del(motor);
}

/************************* custom code s60sc *************************/

#define MCPWM_TIMER_HZ 100000 
static bdc_motor_handle_t BDCmotor[6] = {NULL, NULL, NULL, NULL, NULL, NULL}; // max 6 motors
bool useBDC = false;
int motorRevPin;
int motorFwdPin;
int motorRevPinR;
int motorFwdPinR;
int pwmFreq = 50;
bool trackSteer = false;

static bool prepBDCmotor(int groupId, int motorId, int pwmAgpio, int pwmBgpio) {
  bdc_motor_config_t BDCmotorConfig = {
      .pwma_gpio_num = (uint32_t)pwmAgpio, // forward pin 
      .pwmb_gpio_num = (uint32_t)pwmBgpio, // reverse pin
      .pwm_freq_hz = (uint32_t)pwmFreq,
  };
  bdc_motor_mcpwm_config_t BDCmcpwmConfig = {
      .group_id = groupId, // MCPWM peripheral number (0, 1)
      .resolution_hz = MCPWM_TIMER_HZ,
  };
  esp_err_t res = bdc_motor_new_mcpwm_device(&BDCmotorConfig, &BDCmcpwmConfig, &BDCmotor[motorId]);
  if (res == ESP_OK) res = bdc_motor_enable(BDCmotor[motorId]);
  if (res == ESP_OK) LOG_INF("Initialising MCPWM unit %d, motor %d, using pins %d, %d", groupId, motorId, pwmAgpio, pwmBgpio);
  else LOG_ERR("%s", espErrMsg(res));
  return res == ESP_OK ? true : false;
}

static void motorDirection(uint32_t dutyTicks, int motorId, bool goFwd) {
  if (dutyTicks > 0) {
    // set direction
    goFwd ? bdc_motor_forward(BDCmotor[motorId]) : bdc_motor_reverse(BDCmotor[motorId]);
  }
  // set speed
  bdc_motor_set_speed(BDCmotor[motorId], dutyTicks);
}

void motorSpeed(int speedVal, bool leftMotor) {
  // speedVal is signed duty cycle, convert to unsigned uint32_t duty ticks
  if (abs(speedVal) < minDutyCycle) speedVal = 0;
  uint32_t dutyTicks = abs(speedVal) * MCPWM_TIMER_HZ / pwmFreq / 100;
  if (leftMotor) {
    // left motor steering or all motor direction
    if (motorRevPin > 0 && speedVal < 0) motorDirection(dutyTicks, 0, false); 
    else if (motorFwdPin > 0) motorDirection(dutyTicks, 0, true);
  } else {
    // right motor steering
    if (motorRevPinR > 0 && speedVal < 0) motorDirection(dutyTicks, 1, false); 
    else if (motorFwdPinR > 0) motorDirection(dutyTicks, 1, true);
  }
}

static inline int clampValue(int value, int maxValue) {
  // clamp value to the allowable range
  return value > maxValue ? maxValue : (value < -maxValue ? -maxValue : value);
}

void trackSteeering(int controlVal, bool steering) {
  // set left and right motor speed values depending on requested speed and request steering angle
  // steering = true ? controlVal = steer angle : controlVal = speed change
  static int driveSpeed = 0; // -ve for reverse
  static int steerAngle = 0; // -ve for left turn
  steering ? steerAngle = controlVal - servoCenter : driveSpeed = controlVal;
  int turnSpeed = (clampValue(steerAngle, maxSteerAngle) * maxTurnSpeed / 2) / maxSteerAngle; 
  if (driveSpeed < 0) turnSpeed = 0 - turnSpeed;
  motorSpeed(clampValue(driveSpeed + turnSpeed, maxDutyCycle)); // left
  motorSpeed(clampValue(driveSpeed - turnSpeed, maxDutyCycle), false); //right
}

void prepMotors() {
  if (useBDC) {
    if (motorFwdPin > 0) {
      prepBDCmotor(0, 0, motorFwdPin, motorRevPin);
      if (trackSteer) prepBDCmotor(0, 1, motorFwdPinR, motorRevPinR);
    } else LOG_WRN("BDC motor pins not defined");
  }
}

#endif
