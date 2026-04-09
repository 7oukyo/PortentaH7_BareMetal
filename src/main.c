/**
 * @file main.c
 * @brief Portenta H7 bare-metal entry point (CM7 only).
 *
 * Init order: PJ0 LOW -> HAL_Init -> I-Cache -> PH1 HIGH (oscillator) ->
 * SystemClock_Config (480 MHz) -> PeriphCommonClock (PLL2) -> GPIO LEDs ->
 * PMIC_Init (I2C1) -> USB CDC -> LED blink -> C4001 mmWave.
 * See docs/current-config.md.
 */

#include "main.h"
#include "pmic.h"
#include "led_pwm.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "c4001.h"
#include "acs712.h"
#include "motor_relay.h"
#include <string.h>

static void SystemClock_Config(void);
static void PeriphCommonClock_Config(void);
static void GPIO_LEDs_Init(void);
static void send_report(void);
static void send_motor_status(void);
static void motor_test_tick(void);
static char *uint_to_str_main(char *dst, uint32_t val);

/* Motor test state machine */
#define MOTOR_TEST_PERIOD_MS    2000  /* run each direction for 2s */
#define MOTOR_STOP_PERIOD_MS    1000  /* pause between directions */
#define MOTOR_REPORT_INTERVAL_MS 100  /* print current every 100ms during test */
#define MOTOR_CURRENT_THRESHOLD_MA 50.0f  /* emergency stop if current > this */

static uint32_t motor_test_start = 0;
static uint32_t motor_last_report = 0;
static uint8_t  motor_test_phase = 0;  /* 0=stop1, 1=fwd, 2=stop2, 3=rev */
static bool     motor_test_active = false;
static bool     motor_tripped = false; /* true if current trip stopped motor */

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

    /* ACS712 current sensor — ADC1 CH0 (PA0_C) */
    ACS712_Init();

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

    /* Start motor test sequence after 3s settling */
    motor_test_start = HAL_GetTick() + 3000;
    motor_test_active = true;
    motor_test_phase = 0;

    while (1)
    {
        C4001_Poll();
        if (C4001_HasNewFrame()) {
            send_report();
        }
        if (motor_test_active) {
            motor_test_tick();
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

    /* ACS712 diagnostics */
    if (len == 8 && memcmp(cmd, "adc_diag", 8) == 0) {
        ACS712_PrintRawSamples();
        return;
    }

    /* Motor commands */
    if (len == 9 && memcmp(cmd, "motor_fwd", 9) == 0) {
        Motor_SetDir(MOTOR_FWD); return;
    }
    if (len == 9 && memcmp(cmd, "motor_rev", 9) == 0) {
        Motor_SetDir(MOTOR_REV); return;
    }
    if (len == 10 && memcmp(cmd, "motor_stop", 10) == 0) {
        Motor_EmergencyStop();
        motor_test_active = false;
        return;
    }
    if (len == 10 && memcmp(cmd, "motor_test", 10) == 0) {
        motor_test_active = true;
        motor_test_phase = 0;
        motor_test_start = HAL_GetTick();
        motor_tripped = false;
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

/** Send combined C4001 + ACS712 status over USB CDC. */
static void send_report(void)
{
    C4001_PresenceData_t pres = C4001_GetPresence();
    C4001_SpeedData_t spd_data = C4001_GetSpeed();
    uint32_t now = HAL_GetTick();
    char buf[256];
    char *p = buf;

    /* Timestamp: [sec.ms] */
    *p++ = '[';
    p = uint_to_str_main(p, now / 1000U);
    *p++ = '.';
    uint32_t ms = now % 1000U;
    *p++ = '0' + (char)((ms / 100U) % 10U);
    *p++ = '0' + (char)((ms / 10U) % 10U);
    *p++ = '0' + (char)(ms % 10U);
    *p++ = ']';
    *p++ = ' ';

    /* Presence status */
    const char *stat = pres.present ? "DETECTED" : "CLEAR";
    while (*stat) *p++ = *stat++;

    /* Speed mode extra fields */
    if (spd_data.last_update > 0) {
        const char *rt = " | range=";
        while (*rt) *p++ = *rt++;
        uint32_t r_int = (uint32_t)spd_data.range_m;
        uint32_t r_frac = (uint32_t)((spd_data.range_m - (float)r_int) * 100.0f);
        p = uint_to_str_main(p, r_int);
        *p++ = '.';
        if (r_frac < 10) *p++ = '0';
        p = uint_to_str_main(p, r_frac);
        *p++ = 'm';

        const char *st = " spd=";
        while (*st) *p++ = *st++;
        float spd = spd_data.speed_mps;
        if (spd < 0.0f) { *p++ = '-'; spd = -spd; }
        uint32_t s_int = (uint32_t)spd;
        uint32_t s_frac = (uint32_t)((spd - (float)s_int) * 100.0f);
        p = uint_to_str_main(p, s_int);
        *p++ = '.';
        if (s_frac < 10) *p++ = '0';
        p = uint_to_str_main(p, s_frac);
        const char *mps = "m/s";
        while (*mps) *p++ = *mps++;

        const char *et = " e=";
        while (*et) *p++ = *et++;
        p = uint_to_str_main(p, spd_data.energy);
    }

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
        while (*lt) *p++ = *lt++;
        while (*raw && p < buf + sizeof(buf) - 40) *p++ = *raw++;
    }

    /* ACS712 current reading */
    if (ACS712_IsFault()) {
        const char *ft = " | I=FAULT";
        while (*ft && p < buf + sizeof(buf) - 4) *p++ = *ft++;
    } else {
        const char *it = " | I=";
        while (*it) *p++ = *it++;
        float ma = ACS712_ReadCurrent_mA();
        if (ma < 0.0f) { *p++ = '-'; ma = -ma; }
        uint32_t ma_int  = (uint32_t)ma;
        uint32_t ma_frac = (uint32_t)((ma - (float)ma_int) * 10.0f);
        p = uint_to_str_main(p, ma_int);
        *p++ = '.';
        p = uint_to_str_main(p, ma_frac);
        const char *unit = "mA";
        while (*unit) *p++ = *unit++;
    }

    *p++ = '\r';
    *p++ = '\n';
    CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));

    /* Red LED: ON while presence detected (active LOW) */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_RED_PIN,
                      pres.present ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* ---- Motor test state machine ---- */

static void send_motor_status(void)
{
    char buf[120];
    char *p = buf;
    uint32_t now = HAL_GetTick();

    *p++ = '[';
    p = uint_to_str_main(p, now / 1000U);
    *p++ = '.';
    uint32_t ms = now % 1000U;
    *p++ = '0' + (char)((ms / 100U) % 10U);
    *p++ = '0' + (char)((ms / 10U) % 10U);
    *p++ = '0' + (char)(ms % 10U);
    *p++ = ']';
    *p++ = ' ';

    const char *dir_str;
    MotorDir_t dir = Motor_GetDir();
    if (dir == MOTOR_FWD) dir_str = "MOTOR=FWD";
    else if (dir == MOTOR_REV) dir_str = "MOTOR=REV";
    else dir_str = "MOTOR=STOP";
    while (*dir_str) *p++ = *dir_str++;

    /* Current reading */
    if (ACS712_IsFault()) {
        const char *ft = " I=FAULT";
        while (*ft) *p++ = *ft++;
    } else {
        const char *it = " I=";
        while (*it) *p++ = *it++;
        float ma = ACS712_ReadCurrent_mA();
        if (ma < 0.0f) { *p++ = '-'; ma = -ma; }
        uint32_t ma_int  = (uint32_t)ma;
        uint32_t ma_frac = (uint32_t)((ma - (float)ma_int) * 10.0f);
        p = uint_to_str_main(p, ma_int);
        *p++ = '.';
        p = uint_to_str_main(p, ma_frac);
        const char *unit = "mA";
        while (*unit) *p++ = *unit++;
    }

    if (motor_tripped) {
        const char *trip = " TRIPPED";
        while (*trip) *p++ = *trip++;
    }

    *p++ = '\r'; *p++ = '\n';
    CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));
}

static void motor_test_tick(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - motor_test_start;

    /* Current monitoring: emergency stop if threshold exceeded */
    if (Motor_GetDir() != MOTOR_STOP && !ACS712_IsFault()) {
        float ma = ACS712_ReadCurrent_mA();
        if (ma > MOTOR_CURRENT_THRESHOLD_MA || ma < -MOTOR_CURRENT_THRESHOLD_MA) {
            Motor_EmergencyStop();
            motor_test_active = false;
            motor_tripped = true;
            send_motor_status();
            return;
        }
    }

    /* High-frequency motor status reports */
    if (now - motor_last_report >= MOTOR_REPORT_INTERVAL_MS) {
        motor_last_report = now;
        send_motor_status();
    }

    /* Phase transitions */
    switch (motor_test_phase) {
    case 0: /* Initial stop */
        if (elapsed >= MOTOR_STOP_PERIOD_MS) {
            Motor_SetDir(MOTOR_FWD);
            motor_test_start = now;
            motor_test_phase = 1;
        }
        break;
    case 1: /* Forward */
        if (elapsed >= MOTOR_TEST_PERIOD_MS) {
            Motor_SetDir(MOTOR_STOP);
            motor_test_start = now;
            motor_test_phase = 2;
        }
        break;
    case 2: /* Stop between directions */
        if (elapsed >= MOTOR_STOP_PERIOD_MS) {
            Motor_SetDir(MOTOR_REV);
            motor_test_start = now;
            motor_test_phase = 3;
        }
        break;
    case 3: /* Reverse */
        if (elapsed >= MOTOR_TEST_PERIOD_MS) {
            Motor_SetDir(MOTOR_STOP);
            motor_test_start = now;
            motor_test_phase = 0; /* loop */
        }
        break;
    }
}

/**
 * @brief Error handler — disable interrupts and spin.
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
