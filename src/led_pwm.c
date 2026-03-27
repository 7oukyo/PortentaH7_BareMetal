/**
 * @file led_pwm.c
 * @brief Software PWM rainbow cycling for onboard RGB LED (PK5/PK6/PK7).
 *
 * PK5/PK6/PK7 have no hardware timer PWM alternate function (FMC/LTDC pins).
 * TIM6 (basic timer, APB1) generates interrupts at 10kHz; each ISR tick
 * drives the GPIO based on a software PWM counter vs per-channel duty value.
 *
 * Timing (after SystemClock_Config: APB1=120MHz, TIM6 clock=240MHz):
 *   PSC=2399, ARR=9 → TIM6 overflow at 240MHz/2400/10 = 10kHz
 *   PWM period  = 100 ticks  → PWM frequency = 100Hz
 *   Hue update  = every PWM period (100 ticks = 10ms)
 *   Rainbow cycle = 360 hue steps × 10ms = 3.6s
 *
 * RM0399 §43 — TIM6 basic timer.
 */

#include "led_pwm.h"

#define PWM_PERIOD   100U   /* software PWM counter range 0..99 */

TIM_HandleTypeDef htim6;

/**
 * @brief Convert HSV (H: 0-359, S=V=100%) to RGB duty values (0-100).
 * Full saturation and value simplify the formula — p=0, t and q only vary.
 */
static void hsv_to_rgb(uint16_t h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint16_t region = h / 60U;
    uint16_t rem    = h % 60U;
    uint8_t  q = (uint8_t)(PWM_PERIOD - (PWM_PERIOD * rem) / 60U);  /* falling */
    uint8_t  t = (uint8_t)((PWM_PERIOD * rem) / 60U);               /* rising  */

    switch (region) {
        case 0: *r = PWM_PERIOD; *g = t;          *b = 0;          break;
        case 1: *r = q;          *g = PWM_PERIOD; *b = 0;          break;
        case 2: *r = 0;          *g = PWM_PERIOD; *b = t;          break;
        case 3: *r = 0;          *g = q;          *b = PWM_PERIOD; break;
        case 4: *r = t;          *g = 0;          *b = PWM_PERIOD; break;
        case 5: *r = PWM_PERIOD; *g = 0;          *b = q;          break;
        default: *r = 0; *g = 0; *b = 0; break;
    }
}

/**
 * @brief Initialize TIM6 for 10kHz interrupt and start rainbow PWM.
 *
 * TIM6 clock source: APB1 timer clock = 240MHz (APB1 prescaler=2 → ×2).
 * PSC=2399 → tick clock 100kHz. ARR=9 → overflow every 10 ticks = 10kHz.
 * HAL_TIM_Base_MspInit() (in stm32h7xx_hal_msp.c) enables the clock and NVIC.
 */
void LedPwm_Init(void)
{
    htim6.Instance               = TIM6;
    htim6.Init.Prescaler         = 2399U;  /* 240MHz / 2400 = 100kHz tick */
    htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim6.Init.Period            = 9U;     /* 100kHz / 10 = 10kHz overflow */
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief TIM6 period elapsed callback — software PWM + rainbow hue advance.
 *
 * Runs at 10kHz. Drives PK5/PK6/PK7 via BSRR (single register write for all
 * three channels). Active LOW: BSRR[31:16] resets pin (LED on), [15:0] sets
 * pin (LED off). RM0399 §11.4.7 — GPIOx_BSRR.
 *
 * Hue advances once per PWM_PERIOD ticks (every 10ms), cycling 0-359.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM6) {
        return;
    }

    static uint8_t  pwm_counter = 0;
    static uint16_t hue         = 0;
    static uint8_t  duty_r      = PWM_PERIOD;  /* start: red */
    static uint8_t  duty_g      = 0;
    static uint8_t  duty_b      = 0;

    /* Single BSRR write: active LOW — counter < duty means LED on (pin LOW). */
    uint32_t bsrr = 0;
    bsrr |= (pwm_counter < duty_r) ? ((uint32_t)LED_RED_PIN   << 16) : LED_RED_PIN;
    bsrr |= (pwm_counter < duty_g) ? ((uint32_t)LED_GREEN_PIN << 16) : LED_GREEN_PIN;
    bsrr |= (pwm_counter < duty_b) ? ((uint32_t)LED_BLUE_PIN  << 16) : LED_BLUE_PIN;
    LED_GPIO_PORT->BSRR = bsrr;

    pwm_counter++;
    if (pwm_counter >= PWM_PERIOD) {
        pwm_counter = 0;
        hue = (hue + 1U) % 360U;
        hsv_to_rgb(hue, &duty_r, &duty_g, &duty_b);
    }
}
