/**
 * @file main.c
 * @brief BlackPill F411 bare-metal entry point.
 *
 * Port of the Portenta H7 sofa auto-adjust controller.
 * Automated sofa backplane controller:
 *   C4001 mmWave detects presence -> motor closes backplane -> INA226 detects
 *   current spike (contact) -> motor stops -> person leaves -> motor resets.
 *
 * Init order: HAL_Init -> SystemClock_Config (96 MHz) -> GPIO LED ->
 * USB CDC -> LED blink -> C4001 -> Motor relay -> INA226 -> sofa controller.
 *
 * Differences from H7: no PMIC, no I-Cache, no PLL2/3, USB OTG FS (internal PHY),
 * single LED (PC13), TIM3 instead of TIM6, I2C1 instead of I2C3, USART1 instead of UART4.
 */

#include "main.h"
#include "led.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "c4001.h"
#include "ina226.h"
#include "motor_relay.h"
#include <string.h>

static void SystemClock_Config(void);
static void GPIO_LED_Init(void);
static void send_report(void);
static void sofa_tick(void);
static void send_sofa_status(void);
static char *uint_to_str_main(char *dst, uint32_t val);

/* ---- Sofa auto-adjust state machine ---- */

typedef enum {
    SOFA_IDLE,      /* No person — backplane fully retracted */
    SOFA_CLOSING,   /* Person detected — motor driving backplane toward person */
    SOFA_CONTACT,   /* Current spike detected — backplane touching person's back */
    SOFA_RESETTING, /* Person left — motor reversing to reset position */
} SofaState_t;

/* Tunable parameters (adjustable via serial commands) */
#define SOFA_CLOSE_TIMEOUT_MS    10000  /* max motor-on time when closing */
#define SOFA_RESET_DURATION_MS   10000  /* reverse time to reach absolute max open */
#define SOFA_REPORT_INTERVAL_MS  200    /* status print interval */
#define SOFA_CLEAR_DEBOUNCE_MS   2000   /* presence must be CLEAR this long to reset */
#define SOFA_DETECT_DEBOUNCE_MS  500    /* presence must be DETECTED this long to close */

/* Adaptive baseline overcurrent detection */
#define SOFA_CONTACT_OFFSET_MA   130    /* contact = baseline + this (CLOSING) */
#define SOFA_CONTACT_SUSTAIN_MS  200    /* how long current must stay above threshold */
#define SOFA_STALL_OFFSET_MA     250    /* stall = baseline + this (RESETTING) */
#define SOFA_STALL_SUSTAIN_MS    200    /* how long stall current must persist */
#define SOFA_BASELINE_ALPHA      0.99f  /* EMA smoothing factor */
#define SOFA_MONITOR_SAMPLE_MS   100    /* current sample interval after settling */

/* Adaptive settle detection */
#define SOFA_SETTLE_SAMPLE_MS    100    /* interval between current samples during settling */
#define SOFA_SETTLE_NOISE_MA     30     /* drop within this counts as "flat" */
#define SOFA_SETTLE_STABLE_MS    150    /* must be flat/rising this long to declare settled */
#define SOFA_SETTLE_TIMEOUT_MS   3000   /* safety fallback if settle never detected */

static SofaState_t sofa_state = SOFA_IDLE;
static uint32_t sofa_state_enter_ms = 0;
static uint32_t sofa_last_report_ms = 0;
static uint32_t sofa_clear_since_ms = 0;
static uint32_t sofa_detect_since_ms = 0;
static uint32_t sofa_overcurrent_since_ms = 0;
static bool     sofa_enabled = true;
static float    sofa_contact_offset_ma = SOFA_CONTACT_OFFSET_MA;
static bool     sofa_prev_present = false;

/* Adaptive settle detection state */
static float    sofa_settle_peak_ma = 0.0f;
static float    sofa_settle_prev_ma = 0.0f;
static uint32_t sofa_settle_last_sample_ms = 0;
static uint32_t sofa_settle_stable_since = 0;
static bool     sofa_motor_settled = false;

/* Adaptive baseline */
static float    sofa_baseline_ma = 0.0f;

/* Enter a new sofa state */
static void sofa_enter(SofaState_t new_state)
{
    sofa_state = new_state;
    sofa_state_enter_ms = HAL_GetTick();
    sofa_overcurrent_since_ms = 0;
    sofa_settle_peak_ma = 0.0f;
    sofa_settle_prev_ma = 0.0f;
    sofa_settle_last_sample_ms = 0;
    sofa_settle_stable_since = 0;
    sofa_motor_settled = false;
    sofa_baseline_ma = 0.0f;
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    GPIO_LED_Init();

    /* USB CDC Virtual COM Port over USB-C (internal FS PHY, PA11/PA12) */
    MX_USB_DEVICE_Init();

    /* LED off before timer starts */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_SET);

    /* TIM3 for LED blink timing on USB RX */
    Led_Init();

    /* C4001 mmWave presence sensor on USART1 (PA9/PA10) */
    C4001_Init();

    /* Motor relay H-bridge: PB0 (relay 1) + PB1 (relay 2) */
    Motor_Init();

    /* INA226 current/power monitor on I2C1 (PB6/PB7) */
    INA226_Init();

    /* Sofa controller starts in IDLE */
    sofa_enter(SOFA_IDLE);
    sofa_prev_present = false;

    /* Print startup config over VCP */
    HAL_Delay(500);  /* let USB CDC enumerate */
    {
        char cfg[160];
        char *p = cfg;
        const char *h = "[CFG] CONTACT=+";
        while (*h) *p++ = *h++;
        p = uint_to_str_main(p, SOFA_CONTACT_OFFSET_MA);
        h = "mA/"; while (*h) *p++ = *h++;
        p = uint_to_str_main(p, SOFA_CONTACT_SUSTAIN_MS);
        h = "ms STALL=+"; while (*h) *p++ = *h++;
        p = uint_to_str_main(p, SOFA_STALL_OFFSET_MA);
        h = "mA/"; while (*h) *p++ = *h++;
        p = uint_to_str_main(p, SOFA_STALL_SUSTAIN_MS);
        h = "ms SETTLE=noise"; while (*h) *p++ = *h++;
        p = uint_to_str_main(p, SOFA_SETTLE_NOISE_MA);
        h = "/stable"; while (*h) *p++ = *h++;
        p = uint_to_str_main(p, SOFA_SETTLE_STABLE_MS);
        h = "/max"; while (*h) *p++ = *h++;
        p = uint_to_str_main(p, SOFA_SETTLE_TIMEOUT_MS);
        h = "ms CLOSE_T="; while (*h) *p++ = *h++;
        p = uint_to_str_main(p, SOFA_CLOSE_TIMEOUT_MS / 1000U);
        h = "s RESET_T="; while (*h) *p++ = *h++;
        p = uint_to_str_main(p, SOFA_RESET_DURATION_MS / 1000U);
        h = "s\r\n"; while (*h) *p++ = *h++;
        CDC_Transmit_FS((uint8_t *)cfg, (uint16_t)(p - cfg));
    }

    while (1)
    {
        C4001_Poll();
        if (C4001_HasNewFrame()) {
            send_report();
        }
        if (sofa_enabled) {
            sofa_tick();
        }
    }
}

/** @brief System Clock — HSE 25 MHz -> PLL -> 96 MHz SYSCLK.
 *  PLL Q=4 -> 48 MHz for USB OTG FS.
 *  APB1 = 48 MHz (max 50 MHz), APB2 = 96 MHz (max 100 MHz). */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Enable power controller clock and set voltage scaling */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* HSE 25 MHz -> PLL: M=25 (VCO_in=1MHz), N=192 (VCO=192MHz),
     * P=2 (SYSCLK=96MHz), Q=4 (USB=48MHz) */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 25;
    RCC_OscInitStruct.PLL.PLLN       = 192;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 4;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* SYSCLK from PLL, AHB=96MHz, APB1=48MHz (div2), APB2=96MHz (div1) */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    /* 3 wait states for 96 MHz at VDD=3.3V */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }
}

/** @brief Initialize PC13 as push-pull output for onboard LED. */
static void GPIO_LED_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &gpio);
}

/* ---- Serial command dispatcher ---- */

void HandleSerialCmd(const char *cmd, uint16_t len)
{
    /* Strip trailing \r\n */
    while (len > 0 && (cmd[len - 1] == '\r' || cmd[len - 1] == '\n')) {
        len--;
    }
    if (len == 0) return;

    /* INA226 diagnostics */
    if (len == 8 && memcmp(cmd, "ina_diag", 8) == 0) {
        INA226_PrintDiag();
        return;
    }
    if (len == 8 && memcmp(cmd, "ina_test", 8) == 0) {
        INA226_SelfTest();
        return;
    }
    if (len == 8 && memcmp(cmd, "ina_read", 8) == 0) {
        char rbuf[80];
        char *rp = rbuf;
        const char *rh = "[INA226] I=";
        while (*rh) *rp++ = *rh++;
        float ma = INA226_ReadCurrent_mA();
        if (ma < 0.0f) { *rp++ = '-'; ma = -ma; }
        rp = uint_to_str_main(rp, (uint32_t)ma);
        *rp++ = '.';
        rp = uint_to_str_main(rp, (uint32_t)((ma - (float)(uint32_t)ma) * 10.0f));
        const char *mu = "mA Vbus=";
        while (*mu) *rp++ = *mu++;
        float vb = INA226_ReadBusVoltage_mV();
        rp = uint_to_str_main(rp, (uint32_t)vb);
        const char *mv = "mV\r\n";
        while (*mv) *rp++ = *mv++;
        CDC_Transmit_FS((uint8_t *)rbuf, (uint16_t)(rp - rbuf));
        return;
    }

    /* Motor manual commands */
    if (len == 9 && memcmp(cmd, "motor_fwd", 9) == 0) {
        sofa_enabled = false;
        Motor_SetDir(MOTOR_FWD);
        return;
    }
    if (len == 9 && memcmp(cmd, "motor_rev", 9) == 0) {
        sofa_enabled = false;
        Motor_SetDir(MOTOR_REV);
        return;
    }
    if (len == 10 && memcmp(cmd, "motor_stop", 10) == 0) {
        sofa_enabled = false;
        Motor_EmergencyStop();
        return;
    }

    /* Sofa commands */
    if (len == 10 && memcmp(cmd, "sofa_start", 10) == 0) {
        sofa_enabled = true;
        sofa_enter(SOFA_IDLE);
        Motor_EmergencyStop();
        sofa_prev_present = false;
        return;
    }
    if (len == 9 && memcmp(cmd, "sofa_stop", 9) == 0) {
        sofa_enabled = false;
        Motor_EmergencyStop();
        return;
    }
    if (len == 11 && memcmp(cmd, "sofa_status", 11) == 0) {
        send_sofa_status();
        return;
    }

    /* Sofa contact offset adjustment */
    if (len > 13 && memcmp(cmd, "sofa_thresh ", 12) == 0) {
        uint32_t val = 0;
        for (uint16_t i = 12; i < len; i++) {
            if (cmd[i] >= '0' && cmd[i] <= '9')
                val = val * 10 + (uint32_t)(cmd[i] - '0');
        }
        if (val > 0) sofa_contact_offset_ma = (float)val;
        char buf[60];
        char *p = buf;
        const char *hdr = "[SOFA] offset=+";
        while (*hdr) *p++ = *hdr++;
        p = uint_to_str_main(p, val);
        const char *unit = "mA\r\n";
        while (*unit) *p++ = *unit++;
        CDC_Transmit_FS((uint8_t *)buf, (uint16_t)(p - buf));
        return;
    }

    /* Everything else goes to C4001 sensor */
    C4001_HandleSerialCmd(cmd, len);
}

/* ---- Report formatting ---- */

static char *uint_to_str_main(char *dst, uint32_t val)
{
    char tmp[10];
    int i = 0;
    if (val == 0) { *dst++ = '0'; return dst; }
    while (val > 0) { tmp[i++] = '0' + (char)(val % 10U); val /= 10U; }
    while (i > 0) *dst++ = tmp[--i];
    return dst;
}

/* Append "[sec.ms] " timestamp to buffer */
static char *append_timestamp(char *p, uint32_t now)
{
    *p++ = '[';
    p = uint_to_str_main(p, now / 1000U);
    *p++ = '.';
    uint32_t ms = now % 1000U;
    *p++ = '0' + (char)((ms / 100U) % 10U);
    *p++ = '0' + (char)((ms / 10U) % 10U);
    *p++ = '0' + (char)(ms % 10U);
    *p++ = ']';
    *p++ = ' ';
    return p;
}

/* Append current reading to buffer */
static char *append_current(char *p)
{
    *p++ = 'I'; *p++ = '=';
    float ma = INA226_ReadCurrent_mA();
    if (ma < 0.0f) { *p++ = '-'; ma = -ma; }
    uint32_t ma_int  = (uint32_t)ma;
    uint32_t ma_frac = (uint32_t)((ma - (float)ma_int) * 10.0f);
    p = uint_to_str_main(p, ma_int);
    *p++ = '.';
    p = uint_to_str_main(p, ma_frac);
    const char *unit = "mA";
    while (*unit) *p++ = *unit++;
    return p;
}

/* Append sofa state name to buffer */
static char *append_sofa_state(char *p)
{
    const char *name;
    switch (sofa_state) {
    case SOFA_IDLE:      name = "IDLE";      break;
    case SOFA_CLOSING:   name = "CLOSING";   break;
    case SOFA_CONTACT:   name = "CONTACT";   break;
    case SOFA_RESETTING: name = "RESETTING"; break;
    default:             name = "UNKNOWN";   break;
    }
    while (*name) *p++ = *name++;
    return p;
}

/** Send combined C4001 + INA226 + sofa status over USB CDC. */
static void send_report(void)
{
    C4001_PresenceData_t pres = C4001_GetPresence();
    uint32_t now = HAL_GetTick();
    char buf[256];
    char *p = buf;

    p = append_timestamp(p, now);

    const char *stat = pres.present ? "DETECTED" : "CLEAR";
    while (*stat) *p++ = *stat++;

    const char *sf = " | sofa=";
    while (*sf) *p++ = *sf++;
    p = append_sofa_state(p);

    const char *fc = " | frames=";
    while (*fc) *p++ = *fc++;
    p = uint_to_str_main(p, C4001_GetFrameCount());

    const char *rb = " rx=";
    while (*rb) *p++ = *rb++;
    p = uint_to_str_main(p, C4001_GetRxByteCount());
    *p++ = 'B';

    const char *raw = C4001_GetLastRaw();
    if (raw[0] != '\0') {
        const char *lt = " | raw=";
        while (*lt && p < buf + sizeof(buf) - 40) *p++ = *lt++;
        while (*raw && p < buf + sizeof(buf) - 30) *p++ = *raw++;
    }

    const char *sep = " | ";
    while (*sep) *p++ = *sep++;
    p = append_current(p);

    *p++ = '\r';
    *p++ = '\n';
    CDC_Transmit_FS((uint8_t *)buf, (uint16_t)(p - buf));

    /* No per-function LED on BlackPill (single LED used for RX blink only) */
}

/* ---- Sofa status output ---- */

static void send_sofa_status(void)
{
    uint32_t now = HAL_GetTick();
    char buf[160];
    char *p = buf;

    p = append_timestamp(p, now);

    const char *hdr = "SOFA=";
    while (*hdr) *p++ = *hdr++;
    p = append_sofa_state(p);

    MotorDir_t dir = Motor_GetDir();
    const char *dstr;
    if (dir == MOTOR_FWD) dstr = " MTR=FWD";
    else if (dir == MOTOR_REV) dstr = " MTR=REV";
    else dstr = " MTR=STOP";
    while (*dstr) *p++ = *dstr++;

    const char *tis = " t=";
    while (*tis) *p++ = *tis++;
    uint32_t elapsed = now - sofa_state_enter_ms;
    p = uint_to_str_main(p, elapsed / 1000U);
    *p++ = '.';
    p = uint_to_str_main(p, (elapsed % 1000U) / 100U);
    *p++ = 's';

    *p++ = ' ';
    p = append_current(p);

    if (sofa_state == SOFA_CLOSING || sofa_state == SOFA_RESETTING) {
        const char *stl = sofa_motor_settled ? " STL=1" : " STL=0";
        while (*stl) *p++ = *stl++;
        const char *pk = " PK=";
        while (*pk) *p++ = *pk++;
        p = uint_to_str_main(p, (uint32_t)sofa_settle_peak_ma);
        const char *bl = " BL=";
        while (*bl) *p++ = *bl++;
        p = uint_to_str_main(p, (uint32_t)sofa_baseline_ma);
    }

    C4001_PresenceData_t pres = C4001_GetPresence();
    const char *pr = pres.present ? " PRS=1" : " PRS=0";
    while (*pr) *p++ = *pr++;

    *p++ = '\r'; *p++ = '\n';
    CDC_Transmit_FS((uint8_t *)buf, (uint16_t)(p - buf));
}

/* ---- Sofa auto-adjust state machine ---- */

static void sofa_tick(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - sofa_state_enter_ms;
    C4001_PresenceData_t pres = C4001_GetPresence();
    bool present = pres.present;

    if (present && !sofa_prev_present) {
        sofa_detect_since_ms = now;
    }
    if (!present && sofa_prev_present) {
        sofa_clear_since_ms = now;
    }
    sofa_prev_present = present;

    if (now - sofa_last_report_ms >= SOFA_REPORT_INTERVAL_MS) {
        sofa_last_report_ms = now;
        send_sofa_status();
    }

    switch (sofa_state) {

    case SOFA_IDLE:
        if (present && (now - sofa_detect_since_ms >= SOFA_DETECT_DEBOUNCE_MS)) {
            Motor_SetDir(MOTOR_FWD);
            sofa_enter(SOFA_CLOSING);
        }
        break;

    case SOFA_CLOSING:
        if (!sofa_motor_settled) {
            if (sofa_settle_last_sample_ms == 0 ||
                (now - sofa_settle_last_sample_ms >= SOFA_SETTLE_SAMPLE_MS)) {
                float ma = INA226_ReadCurrent_mA();
                if (ma < 0.0f) ma = -ma;

                if (ma > sofa_settle_peak_ma)
                    sofa_settle_peak_ma = ma;

                if (sofa_settle_prev_ma > 0.0f) {
                    bool still_falling = (sofa_settle_prev_ma - ma) > (float)SOFA_SETTLE_NOISE_MA;
                    if (still_falling) {
                        sofa_settle_stable_since = 0;
                    } else {
                        if (sofa_settle_stable_since == 0)
                            sofa_settle_stable_since = now;
                        if (now - sofa_settle_stable_since >= SOFA_SETTLE_STABLE_MS) {
                            sofa_motor_settled = true;
                            sofa_baseline_ma = ma;
                        }
                    }
                }

                sofa_settle_prev_ma = ma;
                sofa_settle_last_sample_ms = now;
            }
            if (elapsed >= SOFA_SETTLE_TIMEOUT_MS) {
                sofa_motor_settled = true;
                sofa_baseline_ma = sofa_settle_prev_ma;
            }
        } else {
            if (now - sofa_settle_last_sample_ms >= SOFA_MONITOR_SAMPLE_MS) {
                float ma = INA226_ReadCurrent_mA();
                if (ma < 0.0f) ma = -ma;
                float thresh = sofa_baseline_ma + sofa_contact_offset_ma;

                if (ma > thresh) {
                    if (sofa_overcurrent_since_ms == 0)
                        sofa_overcurrent_since_ms = now;
                    if (now - sofa_overcurrent_since_ms >= SOFA_CONTACT_SUSTAIN_MS) {
                        Motor_EmergencyStop();
                        sofa_enter(SOFA_CONTACT);
                        break;
                    }
                } else {
                    sofa_overcurrent_since_ms = 0;
                    sofa_baseline_ma = sofa_baseline_ma * SOFA_BASELINE_ALPHA
                                     + ma * (1.0f - SOFA_BASELINE_ALPHA);
                }

                sofa_settle_last_sample_ms = now;
            }
        }

        if (elapsed >= SOFA_CLOSE_TIMEOUT_MS) {
            Motor_EmergencyStop();
            sofa_enter(SOFA_CONTACT);
        }

        if (!present && (now - sofa_clear_since_ms >= SOFA_CLEAR_DEBOUNCE_MS)) {
            Motor_SetDir(MOTOR_REV);
            sofa_enter(SOFA_RESETTING);
        }
        break;

    case SOFA_CONTACT:
        if (!present && (now - sofa_clear_since_ms >= SOFA_CLEAR_DEBOUNCE_MS)) {
            Motor_SetDir(MOTOR_REV);
            sofa_enter(SOFA_RESETTING);
        }
        break;

    case SOFA_RESETTING:
        if (!sofa_motor_settled) {
            if (sofa_settle_last_sample_ms == 0 ||
                (now - sofa_settle_last_sample_ms >= SOFA_SETTLE_SAMPLE_MS)) {
                float ma = INA226_ReadCurrent_mA();
                if (ma < 0.0f) ma = -ma;

                if (ma > sofa_settle_peak_ma)
                    sofa_settle_peak_ma = ma;

                if (sofa_settle_prev_ma > 0.0f) {
                    bool still_falling = (sofa_settle_prev_ma - ma) > (float)SOFA_SETTLE_NOISE_MA;
                    if (still_falling) {
                        sofa_settle_stable_since = 0;
                    } else {
                        if (sofa_settle_stable_since == 0)
                            sofa_settle_stable_since = now;
                        if (now - sofa_settle_stable_since >= SOFA_SETTLE_STABLE_MS) {
                            sofa_motor_settled = true;
                            sofa_baseline_ma = ma;
                        }
                    }
                }

                sofa_settle_prev_ma = ma;
                sofa_settle_last_sample_ms = now;
            }
            if (elapsed >= SOFA_SETTLE_TIMEOUT_MS) {
                sofa_motor_settled = true;
                sofa_baseline_ma = sofa_settle_prev_ma;
            }
        } else {
            if (now - sofa_settle_last_sample_ms >= SOFA_MONITOR_SAMPLE_MS) {
                float ma = INA226_ReadCurrent_mA();
                if (ma < 0.0f) ma = -ma;
                float thresh = sofa_baseline_ma + (float)SOFA_STALL_OFFSET_MA;

                if (ma > thresh) {
                    if (sofa_overcurrent_since_ms == 0)
                        sofa_overcurrent_since_ms = now;
                    if (now - sofa_overcurrent_since_ms >= SOFA_STALL_SUSTAIN_MS) {
                        Motor_EmergencyStop();
                        sofa_enter(SOFA_IDLE);
                        break;
                    }
                } else {
                    sofa_overcurrent_since_ms = 0;
                    sofa_baseline_ma = sofa_baseline_ma * SOFA_BASELINE_ALPHA
                                     + ma * (1.0f - SOFA_BASELINE_ALPHA);
                }

                sofa_settle_last_sample_ms = now;
            }
        }

        if (elapsed >= SOFA_RESET_DURATION_MS) {
            Motor_EmergencyStop();
            sofa_enter(SOFA_IDLE);
        }
        break;
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
