/**
 * @file main.c
 * @brief Portenta H7 bare-metal entry point (CM7 only).
 *
 * Automated sofa backplane controller:
 *   C4001 mmWave detects presence -> motor closes backplane -> INA226 detects
 *   current spike (contact) -> motor stops -> person leaves -> motor resets.
 * See docs/sofa-mechanism-flowchart.md for full state machine.
 *
 * Init order: PJ0 LOW -> HAL_Init -> I-Cache -> PH1 HIGH (oscillator) ->
 * SystemClock_Config (480 MHz) -> PeriphCommonClock (PLL2) -> GPIO LEDs ->
 * PMIC_Init (I2C1) -> USB CDC -> LED blink -> C4001 -> Motor relay ->
 * INA226 (I2C3) -> sofa controller.
 * See docs/current-config.md.
 */

#include "main.h"
#include "pmic.h"
#include "led_pwm.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "c4001.h"
#include "ina226.h"
#include "motor_relay.h"
#include <string.h>

static void SystemClock_Config(void);
static void PeriphCommonClock_Config(void);
static void GPIO_LEDs_Init(void);
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

/* Adaptive baseline overcurrent detection.
 * Instead of fixed thresholds, track an EMA of running current (baseline)
 * and trigger when current exceeds baseline + offset.  This adapts to
 * motor warm-up drift (cold ~1450mA -> warm ~1000mA). */
#define SOFA_CONTACT_OFFSET_MA   130    /* contact = baseline + this (CLOSING) */
#define SOFA_CONTACT_SUSTAIN_MS  200    /* how long current must stay above threshold */
#define SOFA_STALL_OFFSET_MA     250    /* stall = baseline + this (RESETTING) */
#define SOFA_STALL_SUSTAIN_MS    200    /* how long stall current must persist */
#define SOFA_BASELINE_ALPHA      0.99f  /* EMA smoothing (higher = slower adapt). tau ~10s at 100ms sample */
#define SOFA_MONITOR_SAMPLE_MS   100    /* current sample interval after settling */

/* Adaptive settle detection — replaces fixed blanking window.
 * After motor starts, samples current at a fixed interval and checks slope.
 * While current is still falling (negative slope), it's still the startup
 * spike decaying — keep waiting. Once current stops falling and stays flat
 * or rises for SETTLE_STABLE_MS, declare settled and begin overcurrent
 * detection. No hard-coded drop threshold needed. */
#define SOFA_SETTLE_SAMPLE_MS    100    /* interval between current samples during settling */
#define SOFA_SETTLE_NOISE_MA     30     /* drop within this counts as "flat", not "still falling" */
#define SOFA_SETTLE_STABLE_MS    150    /* must be flat/rising this long to declare settled */
#define SOFA_SETTLE_TIMEOUT_MS   3000   /* safety fallback if settle never detected */

static SofaState_t sofa_state = SOFA_IDLE;
static uint32_t sofa_state_enter_ms = 0;   /* tick when current state was entered */
static uint32_t sofa_last_report_ms = 0;   /* last status print */
static uint32_t sofa_clear_since_ms = 0;   /* tick when presence first went CLEAR */
static uint32_t sofa_detect_since_ms = 0;  /* tick when presence first went DETECTED */
static uint32_t sofa_overcurrent_since_ms = 0; /* tick when current first exceeded threshold */
static bool     sofa_enabled = true;       /* false = manual mode, sofa logic paused */
static float    sofa_contact_offset_ma = SOFA_CONTACT_OFFSET_MA; /* adjustable via sofa_thresh */
static bool     sofa_prev_present = false; /* previous presence state for edge detect */

/* Adaptive settle detection state — tracks motor startup spike slope */
static float    sofa_settle_peak_ma = 0.0f;         /* highest current seen (diagnostics) */
static float    sofa_settle_prev_ma = 0.0f;         /* previous sample for slope comparison */
static uint32_t sofa_settle_last_sample_ms = 0;     /* tick of last current sample */
static uint32_t sofa_settle_stable_since = 0;       /* tick when slope first became flat/rising */
static bool     sofa_motor_settled = false;          /* true once startup transient is over */

/* Adaptive baseline — EMA of running current, adapts to motor warm-up drift */
static float    sofa_baseline_ma = 0.0f;             /* current EMA baseline */

/* Enter a new sofa state */
static void sofa_enter(SofaState_t new_state)
{
    sofa_state = new_state;
    sofa_state_enter_ms = HAL_GetTick();
    sofa_overcurrent_since_ms = 0;  /* reset sustained-current tracker */
    sofa_settle_peak_ma = 0.0f;     /* reset settle detection */
    sofa_settle_prev_ma = 0.0f;
    sofa_settle_last_sample_ms = 0;
    sofa_settle_stable_since = 0;
    sofa_motor_settled = false;
    sofa_baseline_ma = 0.0f;        /* re-seeded when settling completes */
}

int main(void)
{
    /* PJ0 LOW — PMIC STANDBY pin must be LOW for RUN mode before anything. */
    GPIO_InitTypeDef GPIO_InitStruct;

    __HAL_RCC_GPIOJ_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_0, GPIO_PIN_RESET);
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pin   = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOJ, &GPIO_InitStruct);

    /* CM4 boot disabled — no CM4 firmware, stale flash bank 2 causes interference. */

    HAL_Init();

    /* I-Cache improves code fetch from flash. D-Cache skipped — breaks
     * DMA-based peripherals (Ethernet) without cache maintenance. */
    SCB_EnableICache();

    /* Enable 25 MHz oscillator: PH1 HIGH gates OSCEN net. */
    __HAL_RCC_GPIOH_CLK_ENABLE();
    GPIO_InitTypeDef gpio_osc = {0};
    gpio_osc.Pin   = GPIO_PIN_1;
    gpio_osc.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_osc.Pull  = GPIO_NOPULL;
    gpio_osc.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOH, &gpio_osc);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_1, GPIO_PIN_SET);
    HAL_Delay(10);  /* oscillator startup time */

    SystemClock_Config();
    PeriphCommonClock_Config();

    GPIO_LEDs_Init();

    /* PMIC init via HAL I2C1 — sets SW1/SW2 to 3.3V, configures LDOs */
    PMIC_Init();

    /* USB3320 ULPI PHY reset via PJ4: low -> high with delays.
     * Requires SW1 (+3V1SW) and LDO2 (+1V8) from PMIC to be up. */
    GPIO_InitTypeDef gpio_usb = {0};
    gpio_usb.Pin   = GPIO_PIN_4;
    gpio_usb.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_usb.Pull  = GPIO_NOPULL;
    gpio_usb.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOJ, &gpio_usb);
    HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_Delay(10);

    /* USB CDC Virtual COM Port over USB-C (USB3320 ULPI PHY) */
    MX_USB_DEVICE_Init();

    /* All LEDs off before PWM starts */
    HAL_GPIO_WritePin(LED_GPIO_PORT,
                      LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN,
                      GPIO_PIN_SET);

    /* TIM6 for green LED blink timing on USB RX */
    LedPwm_Init();

    /* C4001 mmWave presence sensor on UART4 (PA0/PI9) */
    C4001_Init();

    /* Motor relay H-bridge: PC15 (relay 1) + PD5 (relay 2) */
    Motor_Init();

    /* INA226 current/power monitor on I2C3 (PH7/PH8, breakout I2C_0) */
    INA226_Init();

    /* Sofa controller starts in IDLE */
    sofa_enter(SOFA_IDLE);
    sofa_prev_present = false;

    /* Print startup config over VCP so external tools can read thresholds */
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
        CDC_Transmit_HS((uint8_t *)cfg, (uint16_t)(p - cfg));
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

/** @brief System Clock — HSE 25 MHz -> PLL1 -> 480 MHz SYSCLK. */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ConfigSupply(PWR_SMPS_1V8_SUPPLIES_LDO);

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    __HAL_RCC_PLL_PLLSOURCE_CONFIG(RCC_PLLSOURCE_HSE);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_BYPASS;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 5;
    RCC_OscInitStruct.PLL.PLLN       = 192;
    RCC_OscInitStruct.PLL.PLLP       = 2;
    RCC_OscInitStruct.PLL.PLLQ       = 2;
    RCC_OscInitStruct.PLL.PLLR       = 2;
    RCC_OscInitStruct.PLL.PLLRGE     = RCC_PLL1VCIRANGE_2;
    RCC_OscInitStruct.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN   = 0;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                                | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

/** @brief PLL2 for SPI2/SPI5 peripheral clocks. */
static void PeriphCommonClock_Config(void)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI2 | RCC_PERIPHCLK_SPI5;
    PeriphClkInitStruct.PLL2.PLL2M    = 5;
    PeriphClkInitStruct.PLL2.PLL2N    = 72;
    PeriphClkInitStruct.PLL2.PLL2P    = 3;
    PeriphClkInitStruct.PLL2.PLL2Q    = 3;
    PeriphClkInitStruct.PLL2.PLL2R    = 2;
    PeriphClkInitStruct.PLL2.PLL2RGE  = RCC_PLL2VCIRANGE_2;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    PeriphClkInitStruct.PLL2.PLL2FRACN  = 0;
    PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
    PeriphClkInitStruct.Spi45ClockSelection  = RCC_SPI45CLKSOURCE_PLL2;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief Initialize GPIOK pins 5, 6, 7 as push-pull outputs for LEDs.
 * All three LEDs are active LOW on the Portenta H7.
 */
static void GPIO_LEDs_Init(void)
{
    __HAL_RCC_GPIOK_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN;
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
        /* Quick current + voltage readout */
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
        CDC_Transmit_HS((uint8_t *)rbuf, (uint16_t)(rp - rbuf));
        return;
    }

    /* Motor manual commands (disable sofa mode on manual override) */
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

    /* Sofa contact offset adjustment: "sofa_thresh <mA>" — sets offset above baseline */
    if (len > 13 && memcmp(cmd, "sofa_thresh ", 12) == 0) {
        uint32_t val = 0;
        for (uint16_t i = 12; i < len; i++) {
            if (cmd[i] >= '0' && cmd[i] <= '9')
                val = val * 10 + (uint32_t)(cmd[i] - '0');
        }
        if (val > 0) sofa_contact_offset_ma = (float)val;
        /* Echo new offset */
        char buf[60];
        char *p = buf;
        const char *hdr = "[SOFA] offset=+";
        while (*hdr) *p++ = *hdr++;
        p = uint_to_str_main(p, val);
        const char *unit = "mA\r\n";
        while (*unit) *p++ = *unit++;
        CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));
        return;
    }

    /* Everything else goes to C4001 sensor (pass-through for calibration) */
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

    /* Presence status */
    const char *stat = pres.present ? "DETECTED" : "CLEAR";
    while (*stat) *p++ = *stat++;

    /* Sofa state */
    const char *sf = " | sofa=";
    while (*sf) *p++ = *sf++;
    p = append_sofa_state(p);

    /* Frame count + RX bytes */
    const char *fc = " | frames=";
    while (*fc) *p++ = *fc++;
    p = uint_to_str_main(p, C4001_GetFrameCount());

    const char *rb = " rx=";
    while (*rb) *p++ = *rb++;
    p = uint_to_str_main(p, C4001_GetRxByteCount());
    *p++ = 'B';

    /* Last raw sensor line */
    const char *raw = C4001_GetLastRaw();
    if (raw[0] != '\0') {
        const char *lt = " | raw=";
        while (*lt && p < buf + sizeof(buf) - 40) *p++ = *lt++;
        while (*raw && p < buf + sizeof(buf) - 30) *p++ = *raw++;
    }

    /* INA226 current reading */
    const char *sep = " | ";
    while (*sep) *p++ = *sep++;
    p = append_current(p);

    *p++ = '\r';
    *p++ = '\n';
    CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));

    /* Red LED: ON while presence detected (active LOW) */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_RED_PIN,
                      pres.present ? GPIO_PIN_RESET : GPIO_PIN_SET);
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

    /* Motor direction */
    MotorDir_t dir = Motor_GetDir();
    const char *dstr;
    if (dir == MOTOR_FWD) dstr = " MTR=FWD";
    else if (dir == MOTOR_REV) dstr = " MTR=REV";
    else dstr = " MTR=STOP";
    while (*dstr) *p++ = *dstr++;

    /* Time in state */
    const char *tis = " t=";
    while (*tis) *p++ = *tis++;
    uint32_t elapsed = now - sofa_state_enter_ms;
    p = uint_to_str_main(p, elapsed / 1000U);
    *p++ = '.';
    p = uint_to_str_main(p, (elapsed % 1000U) / 100U);
    *p++ = 's';

    /* Current */
    *p++ = ' ';
    p = append_current(p);

    /* Settle + baseline state — shown during CLOSING/RESETTING for debugging */
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

    /* Presence */
    C4001_PresenceData_t pres = C4001_GetPresence();
    const char *pr = pres.present ? " PRS=1" : " PRS=0";
    while (*pr) *p++ = *pr++;

    *p++ = '\r'; *p++ = '\n';
    CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));
}

/* ---- Sofa auto-adjust state machine ---- */

static void sofa_tick(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - sofa_state_enter_ms;
    C4001_PresenceData_t pres = C4001_GetPresence();
    bool present = pres.present;

    /* Track debounce edges */
    if (present && !sofa_prev_present) {
        sofa_detect_since_ms = now;  /* rising edge */
    }
    if (!present && sofa_prev_present) {
        sofa_clear_since_ms = now;   /* falling edge */
    }
    sofa_prev_present = present;

    /* Periodic status output */
    if (now - sofa_last_report_ms >= SOFA_REPORT_INTERVAL_MS) {
        sofa_last_report_ms = now;
        send_sofa_status();
    }

    switch (sofa_state) {

    case SOFA_IDLE:
        /* Wait for person detection (debounced) */
        if (present && (now - sofa_detect_since_ms >= SOFA_DETECT_DEBOUNCE_MS)) {
            Motor_SetDir(MOTOR_FWD);
            sofa_enter(SOFA_CLOSING);
        }
        break;

    case SOFA_CLOSING:
        /* Motor is driving backplane toward person.
         * Slope-based settle: while current is still falling, we're in the
         * startup spike decay phase. Once it stops falling (flat/rising) for
         * SETTLE_STABLE_MS, declare settled and begin overcurrent detection. */
        if (!sofa_motor_settled) {
            /* Sample at fixed interval to measure slope */
            if (sofa_settle_last_sample_ms == 0 ||
                (now - sofa_settle_last_sample_ms >= SOFA_SETTLE_SAMPLE_MS)) {
                float ma = INA226_ReadCurrent_mA();
                if (ma < 0.0f) ma = -ma;

                if (ma > sofa_settle_peak_ma)
                    sofa_settle_peak_ma = ma;

                /* Need at least one prior sample to compare slope */
                if (sofa_settle_prev_ma > 0.0f) {
                    /* "Still falling" = prev dropped more than noise margin */
                    bool still_falling = (sofa_settle_prev_ma - ma) > (float)SOFA_SETTLE_NOISE_MA;
                    if (still_falling) {
                        sofa_settle_stable_since = 0;  /* reset — still decaying */
                    } else {
                        if (sofa_settle_stable_since == 0)
                            sofa_settle_stable_since = now;
                        if (now - sofa_settle_stable_since >= SOFA_SETTLE_STABLE_MS) {
                            sofa_motor_settled = true;
                            sofa_baseline_ma = ma;  /* seed baseline with first stable reading */
                        }
                    }
                }

                sofa_settle_prev_ma = ma;
                sofa_settle_last_sample_ms = now;
            }
            /* Safety: force settle after timeout */
            if (elapsed >= SOFA_SETTLE_TIMEOUT_MS) {
                sofa_motor_settled = true;
                sofa_baseline_ma = sofa_settle_prev_ma;  /* seed with last reading */
            }
        } else {
            /* Motor settled — adaptive overcurrent = contact.
             * Sample at fixed interval, update baseline EMA when normal,
             * freeze baseline during overcurrent to prevent drift. */
            if (now - sofa_settle_last_sample_ms >= SOFA_MONITOR_SAMPLE_MS) {
                float ma = INA226_ReadCurrent_mA();
                if (ma < 0.0f) ma = -ma;
                float thresh = sofa_baseline_ma + sofa_contact_offset_ma;

                if (ma > thresh) {
                    /* Overcurrent — don't update baseline */
                    if (sofa_overcurrent_since_ms == 0)
                        sofa_overcurrent_since_ms = now;
                    if (now - sofa_overcurrent_since_ms >= SOFA_CONTACT_SUSTAIN_MS) {
                        Motor_EmergencyStop();
                        sofa_enter(SOFA_CONTACT);
                        break;
                    }
                } else {
                    sofa_overcurrent_since_ms = 0;
                    /* Update baseline EMA — tracks motor warm-up drift */
                    sofa_baseline_ma = sofa_baseline_ma * SOFA_BASELINE_ALPHA
                                     + ma * (1.0f - SOFA_BASELINE_ALPHA);
                }

                sofa_settle_last_sample_ms = now;
            }
        }

        /* Safety timeout — stop even without current feedback */
        if (elapsed >= SOFA_CLOSE_TIMEOUT_MS) {
            Motor_EmergencyStop();
            sofa_enter(SOFA_CONTACT);
        }

        /* If person leaves during closing, abort and reset */
        if (!present && (now - sofa_clear_since_ms >= SOFA_CLEAR_DEBOUNCE_MS)) {
            Motor_SetDir(MOTOR_REV);
            sofa_enter(SOFA_RESETTING);
        }
        break;

    case SOFA_CONTACT:
        /* Backplane touching person — hold position, motor off */

        /* When person leaves, start reset */
        if (!present && (now - sofa_clear_since_ms >= SOFA_CLEAR_DEBOUNCE_MS)) {
            Motor_SetDir(MOTOR_REV);
            sofa_enter(SOFA_RESETTING);
        }
        break;

    case SOFA_RESETTING:
        /* Motor reversing to fully retracted position.
         * ALWAYS runs full SOFA_RESET_DURATION_MS regardless of C4001.
         * Same slope-based settle detection before stall monitoring. */
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
            /* Motor settled — adaptive stall detection (safety cutoff) */
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

        /* After reset duration, stop and go idle */
        if (elapsed >= SOFA_RESET_DURATION_MS) {
            Motor_EmergencyStop();
            sofa_enter(SOFA_IDLE);
        }
        break;
    }

    /* Blue LED: ON during motor activity (active LOW) */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_BLUE_PIN,
                      Motor_GetDir() != MOTOR_STOP ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/**
 * @brief Error handler — disable interrupts and spin.
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
