#pragma once

#include <stdint.h>

/* =========================================================================
 * Default Power Control Configuration (overridable via platformio.ini -D...)
 * ========================================================================= */

/* 1 = Type-C Incremental PID (I-PD), 0 = Legacy +-1 step */
#ifndef POWER_CONTROL_MODE
#define POWER_CONTROL_MODE          1
#endif

/* Closed-loop execution interval (ms) */
#ifndef POWER_CONTROL_INTERVAL_MS
#define POWER_CONTROL_INTERVAL_MS   5U
#endif

/* Active closed-loop PWM range (pwm ↑ => RF power ↓, pwm ↓ => RF power ↑) */
#ifndef POWER_PWM_MIN
#define POWER_PWM_MIN               2000U
#endif

#ifndef POWER_PWM_MAX
#define POWER_PWM_MAX               2500U
#endif

/* Default I-PD gains (VPD in mV, PWM counts in 2000..2500) */
#ifndef POWER_PID_KP
#define POWER_PID_KP                0.05f
#endif

#ifndef POWER_PID_KI
#define POWER_PID_KI                0.02f
#endif

#ifndef POWER_PID_KD
#define POWER_PID_KD                0.00f
#endif

/* 1st-order EMA low-pass alpha on VPD (0.25 @ 5ms => fc ~ 6.8 Hz) */
#ifndef POWER_PID_EMA_ALPHA
#define POWER_PID_EMA_ALPHA         0.25f
#endif

/* Integral error deadband around VpdSetPoint (mV) */
#ifndef POWER_PID_DEADBAND_MV
#define POWER_PID_DEADBAND_MV       3.0f
#endif

/* Per-tick (5ms) slew-rate clamp on PWM output:
 * - MAX_STEP_DOWN: max -du per tick (ramps RF power UP, conservative for PA safety)
 * - MAX_STEP_UP:   max +du per tick (ramps RF power DOWN)
 */
#ifndef POWER_PID_MAX_STEP_DOWN
#define POWER_PID_MAX_STEP_DOWN     10.0f
#endif

#ifndef POWER_PID_MAX_STEP_UP
#define POWER_PID_MAX_STEP_UP       15.0f
#endif

/* Output quantization Schmitt hysteresis (in PWM counts, 0.75 => +-0.25 LSB band) */
#ifndef POWER_PID_HYSTERESIS
#define POWER_PID_HYSTERESIS        0.75f
#endif

/* Live UART tuning CLI & high-rate CSV step telemetry (0 = disabled, 1 = enabled) */
#ifndef POWER_PID_TUNING
#define POWER_PID_TUNING            0
#endif

#ifndef POWER_PID_TUNING_BAUD
#define POWER_PID_TUNING_BAUD       57600U
#endif

typedef enum
{
    POWER_CTRL_MODE_LEGACY = 0,
    POWER_CTRL_MODE_PID    = 1,
} power_ctrl_mode_e;

typedef struct
{
    uint8_t  mode;
    uint16_t interval_ms;
    uint16_t pwm_min;
    uint16_t pwm_max;
    float    kp;
    float    ki;
    float    kd;
    float    ema_alpha;
    float    deadband_mv;
    float    max_step_down;
    float    max_step_up;
    float    hysteresis;
} power_ctrl_config_t;

typedef struct
{
    power_ctrl_config_t cfg;
    uint32_t last_ms;
    float    vpd_filt;
    float    y_prev1;
    float    y_prev2;
    float    pwm_f;
    uint8_t  initialized;
} power_ctrl_state_t;

extern power_ctrl_state_t g_powerCtrl;

void power_control_init(power_ctrl_state_t *ctrl, uint16_t initial_pwm);

/* Notify controller when target_set_power_dB() updates setpoint / amp_state / pwm_val */
void power_control_on_power_change(
    power_ctrl_state_t *ctrl,
    uint16_t current_pwm,
    uint8_t amp_state);

/* Run periodic power control update.
 * If ready to sample, reads ADC via read_vpd_fn(), updates *out_raw_vpd and *io_pwm_val,
 * and returns 1 when *io_pwm_val changed and should be written to PWM hardware.
 */
uint8_t power_control_update(
    power_ctrl_state_t *ctrl,
    uint32_t now_ms,
    uint32_t (*read_vpd_fn)(void),
    uint16_t setpoint_vpd,
    uint8_t amp_state,
    uint32_t *out_raw_vpd,
    uint16_t *io_pwm_val);

#if POWER_PID_TUNING
void power_control_tuning_init(void);
void power_control_tuning_poll(
    uint32_t now_ms,
    uint16_t setpoint_vpd,
    uint32_t raw_vpd,
    uint16_t pwm_val,
    uint8_t amp_state);
void power_control_tuning_notify_step(uint32_t now_ms, uint32_t duration_ms);
#endif
