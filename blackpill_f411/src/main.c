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
static void Buttons_Init(void);
static void send_report(void);
static void sofa_tick(void);
static void send_sofa_status(void);
static void buttons_poll(uint32_t now);
static void mode_switch_poll(uint32_t now);
static void user_key_poll(uint32_t now);
static void manual_tick(uint32_t now);
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
#define SOFA_CLEAR_DEBOUNCE_MS   900000 /* presence must be CLEAR this long to reset (15 min) */
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
/* AUTO close fires only on a fresh presence rising edge from IDLE.
 * Set on clear->detected transition, cleared when CLOSING is entered or
 * whenever the state machine is forced back to IDLE (manual override
 * release, toggle deassert, sofa_start). Prevents auto-re-close on
 * continuous presence after a manual override. */
static bool     sofa_armed = false;

/* Adaptive settle detection state */
static float    sofa_settle_peak_ma = 0.0f;
static float    sofa_settle_prev_ma = 0.0f;
static uint32_t sofa_settle_last_sample_ms = 0;
static uint32_t sofa_settle_stable_since = 0;
static bool     sofa_motor_settled = false;

/* Adaptive baseline */
static float    sofa_baseline_ma = 0.0f;

/* ---- Manual override ----
 * Mode-select toggle:  PB12 (pull-up, switch shorts pin to GND).
 *   Press   (pin falling) -> sofa_mode = AUTO + FORCE CLOSE one-shot.
 *   Release (pin rising)  -> sofa_mode = MANUAL-only; motor/state preserved.
 *
 * Manual fwd button:   PB2  (active HIGH, internal pull-down; cap-touch IC).
 * Manual bwd button:   PB10 (active HIGH, internal pull-down; cap-touch IC).
 *   Press   -> override motor direction; sofa_tick paused via manual_sub gate.
 *   Release -> Motor_EmergencyStop only; sofa_state preserved.
 *
 * Park-at-IDLE (and the implicit `sofa_armed` clear inside `sofa_enter`) only
 * happens via the natural 15-min retract path or an explicit `sofa_start`.
 * Neither the toggle release nor a manual-button release can force it.
 *
 * Cap-touch IC has built-in adjacent-key suppression — both channels can never
 * be HIGH simultaneously. M_BOTH is kept only as a safety interlock for a
 * future multi-touch IC swap.
 */
#define MODE_SW_PORT       GPIOB
#define MODE_SW_PIN        GPIO_PIN_12
#define BTN_FWD_PORT       GPIOB
#define BTN_FWD_PIN        GPIO_PIN_2
#define BTN_BWD_PORT       GPIOB
#define BTN_BWD_PIN        GPIO_PIN_10
#define USER_KEY_PORT      GPIOA
#define USER_KEY_PIN       GPIO_PIN_0
#define BTN_DEBOUNCE_MS    25
#define MODE_DEBOUNCE_MS   25
#define USER_KEY_DEBOUNCE_MS 25

/* Direction inversion. The sofa "close" and "retract" directions depend on
 * how the motor leads are wired. `dir_inverted` flips which MotorDir_t
 * corresponds to "close". Onboard USER_KEY (PA0, active LOW) toggles this
 * at runtime so polarity can be corrected without reflashing. */
static bool dir_inverted = true;   /* matches the as-wired sofa */

static inline MotorDir_t dir_close(void)
{
    return dir_inverted ? MOTOR_REV : MOTOR_FWD;
}
static inline MotorDir_t dir_retract(void)
{
    return dir_inverted ? MOTOR_FWD : MOTOR_REV;
}

typedef enum {
    SOFA_MODE_AUTO,    /* toggle asserted: sofa_tick() runs */
    SOFA_MODE_MANUAL,  /* toggle deasserted: sofa_tick() paused */
} SofaMode_t;

typedef enum {
    M_IDLE,
    M_FORWARD,
    M_BACKWARD,
    M_BOTH,
} ManualSubState_t;

static SofaMode_t       sofa_mode = SOFA_MODE_AUTO;
static ManualSubState_t manual_sub = M_IDLE;

/* Mode-toggle debounce */
static bool     mode_sw_state = false;       /* debounced: true = asserted (AUTO) */
static bool     mode_sw_raw_prev = false;
static uint32_t mode_sw_change_ms = 0;

/* Button debounce */
static bool     btn_fwd_state = false;
static bool     btn_fwd_raw_prev = false;
static uint32_t btn_fwd_change_ms = 0;
static bool     btn_bwd_state = false;
static bool     btn_bwd_raw_prev = false;
static uint32_t btn_bwd_change_ms = 0;

/* USER_KEY (PA0) debounce — onboard button, flips dir_inverted on press */
static bool     user_key_state = false;
static bool     user_key_raw_prev = false;
static uint32_t user_key_change_ms = 0;

/* Enter a new sofa state. Entering IDLE always disarms — AUTO close needs a
 * fresh presence rising edge before firing again. */
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
    if (new_state == SOFA_IDLE) {
        sofa_armed = false;
    }
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

    /* Manual override buttons: PB2 (fwd) + PB10 (bwd), active HIGH, pull-down */
    Buttons_Init();

    /* INA226 current/power monitor on I2C1 (PB6/PB7) */
    INA226_Init();

    /* Sofa controller starts in IDLE */
    sofa_enter(SOFA_IDLE);
    sofa_prev_present = false;
    /* Seed both presence timestamps so the first force-close into CONTACT
     * (with no one on the sofa) can still satisfy the 15-min clear gate. */
    sofa_clear_since_ms = HAL_GetTick();
    sofa_detect_since_ms = HAL_GetTick();

    /* Sample toggle once so we boot into the mode matching its physical
     * position without firing a force-close edge. */
    {
        bool asserted = (HAL_GPIO_ReadPin(MODE_SW_PORT, MODE_SW_PIN) == GPIO_PIN_RESET);
        mode_sw_state = asserted;
        mode_sw_raw_prev = asserted;
        sofa_mode = asserted ? SOFA_MODE_AUTO : SOFA_MODE_MANUAL;
    }

    /* Seed USER_KEY state so a held-at-boot key doesn't trigger an inversion */
    {
        bool key_pressed = (HAL_GPIO_ReadPin(USER_KEY_PORT, USER_KEY_PIN) == GPIO_PIN_RESET);
        user_key_state = key_pressed;
        user_key_raw_prev = key_pressed;
    }

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
        uint32_t now = HAL_GetTick();
        C4001_Poll();
        buttons_poll(now);
        mode_switch_poll(now);
        user_key_poll(now);
        manual_tick(now);
        if (C4001_HasNewFrame()) {
            send_report();
        }
        /* sofa_tick runs only when: enabled, toggle asserted (AUTO),
         * and no manual button is overriding the relays right now. */
        if (sofa_enabled && sofa_mode == SOFA_MODE_AUTO && manual_sub == M_IDLE) {
            sofa_tick();
        } else if (now - sofa_last_report_ms >= SOFA_REPORT_INTERVAL_MS) {
            /* Paused (MANUAL-only, sofa_stop, or manual override active):
             * keep status reports flowing on the same cadence. */
            sofa_last_report_ms = now;
            send_sofa_status();
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

/** @brief Init manual-override inputs:
 *   PB12 = mode-select toggle, pull-up, active LOW (switch shorts to GND).
 *          Asserted = AUTO armed; deasserted = MANUAL-only.
 *   PB2  = forward button,      pull-down, active HIGH (cap-touch IC).
 *   PB10 = backward button,     pull-down, active HIGH (cap-touch IC).
 *   PA0  = onboard USER_KEY,    pull-up, active LOW (flips dir_inverted).
 */
static void Buttons_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* Mode-select toggle: pull-up; closed-to-GND reads LOW = asserted */
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = MODE_SW_PIN;
    HAL_GPIO_Init(MODE_SW_PORT, &gpio);

    /* Direction buttons: pull-down so a tristated cap-touch output reads LOW */
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Pin  = BTN_FWD_PIN;
    HAL_GPIO_Init(BTN_FWD_PORT, &gpio);

    gpio.Pin  = BTN_BWD_PIN;
    HAL_GPIO_Init(BTN_BWD_PORT, &gpio);

    /* USER_KEY: onboard PA0 momentary, pull-up, active LOW */
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin  = USER_KEY_PIN;
    HAL_GPIO_Init(USER_KEY_PORT, &gpio);
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
        /* Resets the sofa state machine to a fresh disarmed IDLE. The toggle
         * switch (PB12) still has authority over AUTO/MANUAL mode — if the
         * toggle is deasserted, sofa_tick() stays paused regardless. AUTO
         * close will fire on the next presence rising edge. */
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

/* Append sofa state name to buffer.
 *   MAN/<dir>     = manual button override active (in either mode)
 *   MANUAL/IDLE   = toggle deasserted, no button pressed
 *   IDLE/CLOSING/CONTACT/RESETTING = AUTO state machine */
static char *append_sofa_state(char *p)
{
    if (manual_sub != M_IDLE) {
        const char *m = "MAN/";
        while (*m) *p++ = *m++;
        const char *sub;
        switch (manual_sub) {
        case M_FORWARD:  sub = "FWD";  break;
        case M_BACKWARD: sub = "BWD";  break;
        case M_BOTH:     sub = "BOTH"; break;
        default:         sub = "?";    break;
        }
        while (*sub) *p++ = *sub++;
        return p;
    }
    if (sofa_mode == SOFA_MODE_MANUAL) {
        const char *m = "MANUAL/IDLE";
        while (*m) *p++ = *m++;
        return p;
    }
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

    const char *di = dir_inverted ? " DIR=INV" : " DIR=NORM";
    while (*di) *p++ = *di++;

    *p++ = '\r'; *p++ = '\n';
    CDC_Transmit_FS((uint8_t *)buf, (uint16_t)(p - buf));
}

/* ---- Manual override: input polling + state machine ---- */

/* Sample both buttons and update debounced state. Active HIGH = pressed. */
static void buttons_poll(uint32_t now)
{
    bool fwd_raw = (HAL_GPIO_ReadPin(BTN_FWD_PORT, BTN_FWD_PIN) == GPIO_PIN_SET);
    bool bwd_raw = (HAL_GPIO_ReadPin(BTN_BWD_PORT, BTN_BWD_PIN) == GPIO_PIN_SET);

    if (fwd_raw != btn_fwd_raw_prev) {
        btn_fwd_raw_prev = fwd_raw;
        btn_fwd_change_ms = now;
    } else if (now - btn_fwd_change_ms >= BTN_DEBOUNCE_MS) {
        btn_fwd_state = fwd_raw;
    }

    if (bwd_raw != btn_bwd_raw_prev) {
        btn_bwd_raw_prev = bwd_raw;
        btn_bwd_change_ms = now;
    } else if (now - btn_bwd_change_ms >= BTN_DEBOUNCE_MS) {
        btn_bwd_state = bwd_raw;
    }
}

/* USER_KEY (PA0) polling. Press edge flips dir_inverted and emergency-stops
 * the motor so a wrong-direction run doesn't continue. Release does nothing. */
static void user_key_poll(uint32_t now)
{
    bool raw = (HAL_GPIO_ReadPin(USER_KEY_PORT, USER_KEY_PIN) == GPIO_PIN_RESET);

    if (raw != user_key_raw_prev) {
        user_key_raw_prev = raw;
        user_key_change_ms = now;
        return;
    }
    if (now - user_key_change_ms < USER_KEY_DEBOUNCE_MS) return;
    if (raw == user_key_state) return;

    user_key_state = raw;

    if (raw) {
        /* Press edge: flip direction mapping and stop motor */
        dir_inverted = !dir_inverted;
        Motor_EmergencyStop();

        char buf[32];
        char *p = buf;
        const char *h = "[DIR] inverted=";
        while (*h) *p++ = *h++;
        *p++ = dir_inverted ? '1' : '0';
        *p++ = '\r'; *p++ = '\n';
        CDC_Transmit_FS((uint8_t *)buf, (uint16_t)(p - buf));
    }
}

/* Sample mode-select toggle (pull-up + switch-to-GND; pressed = pin LOW).
 *   Press   (pin falling edge): enter AUTO + FORCE CLOSE one-shot.
 *   Release (pin rising edge):  switch to MANUAL-only. Motor + sofa state are
 *                               left untouched — the natural 15-min retract
 *                               path is the only thing that parks at IDLE. */
static void mode_switch_poll(uint32_t now)
{
    bool sw_raw = (HAL_GPIO_ReadPin(MODE_SW_PORT, MODE_SW_PIN) == GPIO_PIN_RESET);

    if (sw_raw != mode_sw_raw_prev) {
        mode_sw_raw_prev = sw_raw;
        mode_sw_change_ms = now;
        return;
    }
    if (now - mode_sw_change_ms < MODE_DEBOUNCE_MS) return;

    bool want_pressed = sw_raw;
    if (want_pressed == mode_sw_state) return;   /* no edge — idempotent */

    mode_sw_state = want_pressed;

    if (want_pressed) {
        /* Press: AUTO + force close from any current state. A second rapid
         * press is just another press edge — same path, no double-click timer. */
        sofa_mode = SOFA_MODE_AUTO;
        sofa_enabled = true;
        if (manual_sub == M_IDLE) {
            Motor_SetDir(dir_close());
            sofa_enter(SOFA_CLOSING);
        }
        /* If a manual button is held at this instant the force-close is
         * suppressed — manual override has priority. */
    } else {
        /* Release: only flip the mode flag. Motor keeps doing whatever it's
         * doing; sofa_state and sofa_armed are preserved. The state machine
         * is paused (sofa_tick skipped) until the next press. */
        sofa_mode = SOFA_MODE_MANUAL;
    }
}

/* Manual button override. Active in BOTH modes.
 *   Press:   override motor direction. sofa_state preserved; sofa_tick is
 *            paused via the manual_sub gate in the main loop.
 *   Release: Motor_EmergencyStop only. sofa_state, sofa_armed, and presence
 *            timers are preserved. Whatever state the machine was in before
 *            the press resumes on the next sofa_tick. */
static void manual_tick(uint32_t now)
{
    (void)now;
    bool fwd = btn_fwd_state;
    bool bwd = btn_bwd_state;

    ManualSubState_t new_sub;
    if (fwd && bwd)      new_sub = M_BOTH;       /* unreachable with current cap-touch IC */
    else if (fwd)        new_sub = M_FORWARD;
    else if (bwd)        new_sub = M_BACKWARD;
    else                 new_sub = M_IDLE;

    if (new_sub == manual_sub) return;

    switch (new_sub) {
    case M_IDLE:     Motor_EmergencyStop();        break;
    case M_FORWARD:  Motor_SetDir(dir_close());    break;  /* FWD btn = close */
    case M_BACKWARD: Motor_SetDir(dir_retract());  break;  /* BWD btn = retract */
    case M_BOTH:     Motor_EmergencyStop();        break;
    }
    manual_sub = new_sub;
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
        sofa_armed = true;   /* fresh sit-down — AUTO close eligible */
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
        if (sofa_armed && present &&
            (now - sofa_detect_since_ms >= SOFA_DETECT_DEBOUNCE_MS)) {
            sofa_armed = false;   /* consume one-shot trigger */
            Motor_SetDir(dir_close());
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
            Motor_SetDir(dir_retract());
            sofa_enter(SOFA_RESETTING);
        }
        break;

    case SOFA_CONTACT:
        if (!present && (now - sofa_clear_since_ms >= SOFA_CLEAR_DEBOUNCE_MS)) {
            Motor_SetDir(dir_retract());
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
