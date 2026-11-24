#include "powerControl.h"
#include <math.h>

#ifndef OUTPUT_POWER_TESTING
#define OUTPUT_POWER_TESTING 0
#endif

power_ctrl_state_t g_powerCtrl;

static float clampf(float val, float lo, float hi)
{
    if (val < lo)
        return lo;
    if (val > hi)
        return hi;
    return val;
}

void power_control_init(power_ctrl_state_t *ctrl, uint16_t initial_pwm)
{
    ctrl->cfg.mode          = (uint8_t)POWER_CONTROL_MODE;
    ctrl->cfg.interval_ms   = (uint16_t)POWER_CONTROL_INTERVAL_MS;
    ctrl->cfg.pwm_min       = (uint16_t)POWER_PWM_MIN;
    ctrl->cfg.pwm_max       = (uint16_t)POWER_PWM_MAX;
    ctrl->cfg.kp            = (float)POWER_PID_KP;
    ctrl->cfg.ki            = (float)POWER_PID_KI;
    ctrl->cfg.kd            = (float)POWER_PID_KD;
    ctrl->cfg.ema_alpha     = (float)POWER_PID_EMA_ALPHA;
    ctrl->cfg.deadband_mv   = (float)POWER_PID_DEADBAND_MV;
    ctrl->cfg.max_step_down = (float)POWER_PID_MAX_STEP_DOWN;
    ctrl->cfg.max_step_up   = (float)POWER_PID_MAX_STEP_UP;
    ctrl->cfg.hysteresis    = (float)POWER_PID_HYSTERESIS;

    ctrl->last_ms     = 0U;
    ctrl->vpd_filt    = 0.0f;
    ctrl->y_prev1     = 0.0f;
    ctrl->y_prev2     = 0.0f;
    ctrl->pwm_f       = (float)initial_pwm;
    ctrl->initialized = 0U;
}

void power_control_on_power_change(
    power_ctrl_state_t *ctrl,
    uint16_t current_pwm,
    uint8_t amp_state)
{
    if (!amp_state)
    {
        ctrl->initialized = 0U;
        ctrl->pwm_f = (float)current_pwm;
        return;
    }

    if (!ctrl->initialized)
    {
        ctrl->pwm_f = (float)current_pwm;
        return;
    }

    /* If caller explicitly forced pwm_val to a new value (e.g. overdrive clamp),
     * resync pwm_f; otherwise preserve fractional accumulator for bumpless step. */
    uint16_t rounded = (uint16_t)(ctrl->pwm_f + 0.5f);
    if (current_pwm != rounded)
    {
        ctrl->pwm_f = (float)current_pwm;
    }
}

uint8_t power_control_update(
    power_ctrl_state_t *ctrl,
    uint32_t now_ms,
    uint32_t (*read_vpd_fn)(void),
    uint16_t setpoint_vpd,
    uint8_t amp_state,
    uint32_t *out_raw_vpd,
    uint16_t *io_pwm_val)
{
    uint32_t dt_ms = now_ms - ctrl->last_ms;
    if (dt_ms < (uint32_t)ctrl->cfg.interval_ms)
    {
        return 0U;
    }
    ctrl->last_ms = now_ms;

    uint32_t raw_vpd = read_vpd_fn();
    *out_raw_vpd = raw_vpd;

#if OUTPUT_POWER_TESTING
    (void)amp_state;
    ctrl->vpd_filt = (float)raw_vpd;
    if (raw_vpd > setpoint_vpd && *io_pwm_val < ctrl->cfg.pwm_max)
    {
        (*io_pwm_val)++;
        ctrl->pwm_f = (float)(*io_pwm_val);
        return 1U;
    }
    if (raw_vpd < setpoint_vpd && *io_pwm_val > ctrl->cfg.pwm_min)
    {
        (*io_pwm_val)--;
        ctrl->pwm_f = (float)(*io_pwm_val);
        return 1U;
    }
    return 0U;
#else
    if (!amp_state)
    {
        ctrl->initialized = 0U;
        ctrl->vpd_filt    = (float)raw_vpd;
        ctrl->pwm_f       = (float)(*io_pwm_val);
        return 0U;
    }

    if (ctrl->cfg.mode == POWER_CTRL_MODE_LEGACY)
    {
        ctrl->initialized = 0U;
        ctrl->vpd_filt    = (float)raw_vpd;
        if (raw_vpd > setpoint_vpd && *io_pwm_val < ctrl->cfg.pwm_max)
        {
            (*io_pwm_val)++;
            ctrl->pwm_f = (float)(*io_pwm_val);
            return 1U;
        }
        if (raw_vpd < setpoint_vpd && *io_pwm_val > ctrl->cfg.pwm_min)
        {
            (*io_pwm_val)--;
            ctrl->pwm_f = (float)(*io_pwm_val);
            return 1U;
        }
        ctrl->pwm_f = (float)(*io_pwm_val);
        return 0U;
    }

    /* Bumpless initialization on PA power-up or after a >50 ms loop stall (e.g. EEPROM erase) */
    float y = (float)raw_vpd;
    if (!ctrl->initialized || dt_ms > 50U)
    {
        if (!ctrl->initialized)
        {
            ctrl->vpd_filt = y;
        }
        else
        {
            ctrl->vpd_filt += ctrl->cfg.ema_alpha * (y - ctrl->vpd_filt);
        }
        ctrl->y_prev1     = ctrl->vpd_filt;
        ctrl->y_prev2     = ctrl->vpd_filt;
        ctrl->pwm_f       = clampf((float)(*io_pwm_val), (float)ctrl->cfg.pwm_min, (float)ctrl->cfg.pwm_max);
        ctrl->initialized = 1U;

        uint16_t clamped = (uint16_t)(ctrl->pwm_f + 0.5f);
        if (clamped != *io_pwm_val)
        {
            *io_pwm_val = clamped;
            return 1U;
        }
        return 0U;
    }

    float dt_scale = clampf((float)dt_ms / (float)ctrl->cfg.interval_ms, 0.5f, 3.0f);

    /* 1. 1st-order EMA filter to suppress ADC & 50/60 Hz video modulation ripple */
    ctrl->vpd_filt += ctrl->cfg.ema_alpha * (y - ctrl->vpd_filt);

    /* 2. Error with deadband applied only to integral action */
    float err   = ctrl->vpd_filt - (float)setpoint_vpd;
    float err_i = (fabsf(err) <= ctrl->cfg.deadband_mv) ? 0.0f : err;

    /* 3. Type-C (I-PD) measurement differences: eliminates proportional/derivative kick */
    float dy  = ctrl->vpd_filt - ctrl->y_prev1;
    float d2y = ctrl->vpd_filt - 2.0f * ctrl->y_prev1 + ctrl->y_prev2;
    ctrl->y_prev2 = ctrl->y_prev1;
    ctrl->y_prev1 = ctrl->vpd_filt;

    /* 4. Incremental control output with asymmetric slew-rate limit */
    float du = ctrl->cfg.kp * dy
             + (ctrl->cfg.ki * dt_scale) * err_i
             + (ctrl->cfg.kd / dt_scale) * d2y;
    du = clampf(du, -ctrl->cfg.max_step_down, ctrl->cfg.max_step_up);

    /* 5. Floating-point accumulation + rail clamping */
    ctrl->pwm_f = clampf(ctrl->pwm_f + du, (float)ctrl->cfg.pwm_min, (float)ctrl->cfg.pwm_max);

    /* 6. Schmitt-trigger hysteresis quantization to eliminate 1-LSB chatter */
    uint8_t at_rail = (ctrl->pwm_f <= (float)ctrl->cfg.pwm_min && *io_pwm_val != ctrl->cfg.pwm_min) ||
                      (ctrl->pwm_f >= (float)ctrl->cfg.pwm_max && *io_pwm_val != ctrl->cfg.pwm_max);
    if (at_rail || fabsf(ctrl->pwm_f - (float)(*io_pwm_val)) >= ctrl->cfg.hysteresis)
    {
        *io_pwm_val = (uint16_t)(ctrl->pwm_f + 0.5f);
        return 1U;
    }

    return 0U;
#endif
}

#if POWER_PID_TUNING

#include "targets.h"
#include "common.h"
#include "openVTxEEPROM.h"
#include "rtc6705.h"
#include "serial.h"
#include "printf.h"
#include <string.h>

#define TUNING_SAMPLE_INTERVAL_MS 20U
#define TUNING_DEFAULT_BURST_MS   1500U

static char     s_cmdBuf[40];
static uint8_t  s_cmdLen = 0U;
static uint8_t  s_continuousStream = 0U;
static uint8_t  s_burstActive = 0U;
static uint32_t s_burstStartMs = 0U;
static uint32_t s_burstEndMs = 0U;
static uint32_t s_lastStreamMs = 0U;

static void tuning_send_str(const char *s)
{
    Serial_write_len((uint8_t *)s, (uint32_t)strlen(s));
}

static float parse_float_simple(const char *s)
{
    while (*s == ' ' || *s == '=' || *s == ':')
    {
        s++;
    }

    float sign = 1.0f;
    if (*s == '-')
    {
        sign = -1.0f;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }

    float val = 0.0f;
    while (*s >= '0' && *s <= '9')
    {
        val = val * 10.0f + (float)(*s - '0');
        s++;
    }

    if (*s == '.')
    {
        s++;
        float place = 0.1f;
        while (*s >= '0' && *s <= '9')
        {
            val += (float)(*s - '0') * place;
            place *= 0.1f;
            s++;
        }
    }

    return sign * val;
}

static void format_milli(char *out, size_t out_sz, float val, uint8_t decimals)
{
    int32_t scale = (decimals == 4U) ? 10000 : (decimals == 3U) ? 1000 : 10;
    int32_t scaled = (int32_t)(val * (float)scale + (val >= 0.0f ? 0.5f : -0.5f));
    if (scaled < 0)
    {
        scaled = -scaled;
        if (decimals == 4U)
            snprintf(out, out_sz, "-%ld.%04ld", (long)(scaled / scale), (long)(scaled % scale));
        else if (decimals == 3U)
            snprintf(out, out_sz, "-%ld.%03ld", (long)(scaled / scale), (long)(scaled % scale));
        else
            snprintf(out, out_sz, "-%ld.%01ld", (long)(scaled / scale), (long)(scaled % scale));
    }
    else
    {
        if (decimals == 4U)
            snprintf(out, out_sz, "%ld.%04ld", (long)(scaled / scale), (long)(scaled % scale));
        else if (decimals == 3U)
            snprintf(out, out_sz, "%ld.%03ld", (long)(scaled / scale), (long)(scaled % scale));
        else
            snprintf(out, out_sz, "%ld.%01ld", (long)(scaled / scale), (long)(scaled % scale));
    }
}

static void tuning_print_status(uint16_t setpoint_vpd, uint32_t raw_vpd, uint16_t pwm_val, uint8_t amp_state)
{
    char kp_s[16], ki_s[16], kd_s[16], a_s[16], db_s[16], dn_s[16], up_s[16], hy_s[16], vf_s[16];
    char line[192];

    format_milli(kp_s, sizeof(kp_s), g_powerCtrl.cfg.kp, 4U);
    format_milli(ki_s, sizeof(ki_s), g_powerCtrl.cfg.ki, 4U);
    format_milli(kd_s, sizeof(kd_s), g_powerCtrl.cfg.kd, 4U);
    format_milli(a_s,  sizeof(a_s),  g_powerCtrl.cfg.ema_alpha, 3U);
    format_milli(db_s, sizeof(db_s), g_powerCtrl.cfg.deadband_mv, 1U);
    format_milli(dn_s, sizeof(dn_s), g_powerCtrl.cfg.max_step_down, 1U);
    format_milli(up_s, sizeof(up_s), g_powerCtrl.cfg.max_step_up, 1U);
    format_milli(hy_s, sizeof(hy_s), g_powerCtrl.cfg.hysteresis, 3U);
    format_milli(vf_s, sizeof(vf_s), g_powerCtrl.vpd_filt, 1U);

    int len = snprintf(
        line,
        sizeof(line),
        "# CFG mode=%u kp=%s ki=%s kd=%s alpha=%s db=%s down=%s up=%s hyst=%s freq=%u sp=%u raw=%lu filt=%s pwm=%u amp=%u\r\n",
        (unsigned)g_powerCtrl.cfg.mode,
        kp_s, ki_s, kd_s, a_s, db_s, dn_s, up_s, hy_s,
        (unsigned)myEEPROM.currFreq,
        (unsigned)setpoint_vpd,
        (unsigned long)raw_vpd,
        vf_s,
        (unsigned)pwm_val,
        (unsigned)amp_state);
    if (len > 0)
    {
        Serial_write_len((uint8_t *)line, (uint32_t)len);
    }
}

void power_control_tuning_notify_step(uint32_t now_ms, uint32_t duration_ms)
{
    s_burstActive  = 1U;
    s_burstStartMs = now_ms;
    s_burstEndMs   = now_ms + duration_ms;
    s_lastStreamMs = now_ms - TUNING_SAMPLE_INTERVAL_MS;
    tuning_send_str("# START ms,sp,raw,filt,pwm\r\n");
}

static void tuning_handle_cmd(
    const char *cmd,
    uint32_t now_ms,
    uint16_t setpoint_vpd,
    uint32_t raw_vpd,
    uint16_t pwm_val,
    uint8_t amp_state)
{
    while (*cmd == ' ')
    {
        cmd++;
    }
    if (*cmd == '\0')
    {
        return;
    }

    if (strncmp(cmd, "boot", 4) == 0)
    {
        uint32_t bl_baud = POWER_PID_TUNING_BAUD;
        const char *arg = cmd + 4;
        while (*arg == ' ' || *arg == '=')
            arg++;
        if (strncmp(arg, "sa", 2) == 0)
            bl_baud = 4801U;
        else if (strncmp(arg, "msp", 3) == 0 || strncmp(arg, "tramp", 5) == 0)
            bl_baud = 9600U;
        tuning_send_str("# REBOOT_BL\r\n");
        reboot_into_bootloader(bl_baud);
        return;
    }

    if (strncmp(cmd, "db", 2) == 0)
    {
        float db = parse_float_simple(cmd + 2);
        pitMode = (db <= 0.0f) ? 1U : 0U;
        setPowerdB(db);
        power_control_tuning_notify_step(now_ms, TUNING_DEFAULT_BURST_MS);
        return;
    }

    char c = cmd[0];
    if (c >= 'A' && c <= 'Z')
    {
        c = (char)(c - 'A' + 'a');
    }
    float val = parse_float_simple(cmd + 1);

    switch (c)
    {
    case '?':
    case 'h':
        tuning_print_status(setpoint_vpd, raw_vpd, pwm_val, amp_state);
        return;
    case 'p':
        if (val >= 0.0f && val <= 2.0f)
            g_powerCtrl.cfg.kp = val;
        break;
    case 'i':
        if (val >= 0.0f && val <= 2.0f)
            g_powerCtrl.cfg.ki = val;
        break;
    case 'd':
        if (val >= 0.0f && val <= 2.0f)
            g_powerCtrl.cfg.kd = val;
        break;
    case 'a':
        if (val >= 0.01f && val <= 1.0f)
            g_powerCtrl.cfg.ema_alpha = val;
        break;
    case 'b':
        if (val >= 0.0f && val <= 100.0f)
            g_powerCtrl.cfg.deadband_mv = val;
        break;
    case 'l':
        if (val >= 0.5f && val <= 500.0f)
            g_powerCtrl.cfg.max_step_down = val;
        break;
    case 'u':
        if (val >= 0.5f && val <= 500.0f)
            g_powerCtrl.cfg.max_step_up = val;
        break;
    case 'y':
        if (val >= 0.0f && val <= 5.0f)
            g_powerCtrl.cfg.hysteresis = val;
        break;
    case 'm':
        g_powerCtrl.cfg.mode = (val > 0.5f) ? POWER_CTRL_MODE_PID : POWER_CTRL_MODE_LEGACY;
        break;
    case 'w':
        if (val <= 0.0f)
        {
            pitMode = 1U;
            setPowerdB(0.0f);
        }
        else
        {
            pitMode = 0U;
            setPowermW((uint16_t)(val + 0.5f));
        }
        power_control_tuning_notify_step(now_ms, TUNING_DEFAULT_BURST_MS);
        return;
    case 'f':
        if (val >= 5000.0f && val <= 6000.0f)
        {
            rtc6705WriteFrequency((uint32_t)(val + 0.5f));
            power_control_tuning_notify_step(now_ms, TUNING_DEFAULT_BURST_MS);
        }
        return;
    case 's':
    {
        uint32_t dur = (val > 10.0f) ? (uint32_t)val : TUNING_DEFAULT_BURST_MS;
        power_control_tuning_notify_step(now_ms, dur);
        return;
    }
    case 't':
        s_continuousStream = (val > 0.5f) ? 1U : 0U;
        s_burstActive = 0U;
        tuning_send_str(s_continuousStream ? "# STREAM=1\r\n" : "# STREAM=0\r\n");
        return;
    default:
        tuning_send_str("# ERR unknown cmd (use ? p= i= d= a= b= l= u= m= db= w= f= s= t= boot)\r\n");
        return;
    }

    tuning_print_status(setpoint_vpd, raw_vpd, pwm_val, amp_state);
}

void power_control_tuning_init(void)
{
    serial_begin(POWER_PID_TUNING_BAUD, UART_TX, UART_RX, 1U);
    vtxModeLocked = 1U;
    tuning_send_str("# OPENVTX_PID_TUNER READY (send ? for status)\r\n");
}

void power_control_tuning_poll(
    uint32_t now_ms,
    uint16_t setpoint_vpd,
    uint32_t raw_vpd,
    uint16_t pwm_val,
    uint8_t amp_state)
{
    while (serial_available())
    {
        char ch = (char)serial_read();
        if (ch == '\r' || ch == '\n')
        {
            if (s_cmdLen > 0U)
            {
                s_cmdBuf[s_cmdLen] = '\0';
                tuning_handle_cmd(s_cmdBuf, now_ms, setpoint_vpd, raw_vpd, pwm_val, amp_state);
                s_cmdLen = 0U;
            }
        }
        else if (s_cmdLen < (sizeof(s_cmdBuf) - 1U))
        {
            s_cmdBuf[s_cmdLen++] = ch;
        }
    }

    if (s_burstActive && (int32_t)(now_ms - s_burstEndMs) >= 0)
    {
        s_burstActive = 0U;
        tuning_send_str("# END\r\n");
    }

    if ((s_burstActive || s_continuousStream) &&
        (now_ms - s_lastStreamMs) >= TUNING_SAMPLE_INTERVAL_MS)
    {
        s_lastStreamMs = now_ms;
        uint32_t rel_ms = s_burstActive ? (now_ms - s_burstStartMs) : now_ms;
        char vf_s[16];
        char line[64];
        format_milli(vf_s, sizeof(vf_s), g_powerCtrl.vpd_filt, 1U);
        int len = snprintf(
            line,
            sizeof(line),
            "D,%lu,%u,%lu,%s,%u\r\n",
            (unsigned long)rel_ms,
            (unsigned)setpoint_vpd,
            (unsigned long)raw_vpd,
            vf_s,
            (unsigned)pwm_val);
        if (len > 0)
        {
            Serial_write_len((uint8_t *)line, (uint32_t)len);
        }
    }
}

#endif /* POWER_PID_TUNING */
