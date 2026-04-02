/**
 * @file led_pwm.c
 * @brief Green LED blink feedback on USB serial RX.
 *
 * TIM6 runs at 10kHz. When LedPwm_BlinkOnRx() is called, the green LED
 * turns on for ~50ms (500 ISR ticks), then the ISR turns it off.
 *
 * RM0399 §43 — TIM6 basic timer.
 */

#include "led_pwm.h"

#define BLINK_TICKS     500U   /* 500 ticks @ 10kHz = 50ms blink */

TIM_HandleTypeDef htim6;

static volatile uint16_t blink_counter = 0;
static volatile uint8_t  blink_active  = 0;

/** Trigger a short green LED blink. Called from CDC receive callback. */
void LedPwm_BlinkOnRx(void)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
    blink_counter = BLINK_TICKS;
    blink_active  = 1;
}

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

/** TIM6 ISR callback — counts down blink timer and turns LED off. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM6) {
        return;
    }

    if (blink_active) {
        if (blink_counter > 0) {
            blink_counter--;
        } else {
            HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GREEN_PIN, GPIO_PIN_SET);
            blink_active = 0;
        }
    }
}
