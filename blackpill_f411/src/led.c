/**
 * @file led.c
 * @brief LED blink feedback on USB serial RX.
 *
 * BlackPill F411: single LED on PC13 (active LOW, sink mode).
 * TIM3 runs at 10kHz. When Led_BlinkOnRx() is called, the LED
 * turns on for ~50ms (500 ISR ticks), then the ISR turns it off.
 *
 * Note: F411 has no TIM6/TIM7. Using TIM3 (general purpose, APB1).
 * APB1 timer clock = PCLK1 * 2 = 48 * 2 = 96 MHz (since APB1 prescaler > 1).
 */

#include "led.h"

#define BLINK_TICKS     500U   /* 500 ticks @ 10kHz = 50ms blink */

TIM_HandleTypeDef htim3;

static volatile uint16_t blink_counter = 0;
static volatile uint8_t  blink_active  = 0;

void Led_BlinkOnRx(void)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_RESET);  /* LED ON */
    blink_counter = BLINK_TICKS;
    blink_active  = 1;
}

void Led_Init(void)
{
    /* TIM3 on APB1. Timer clock = 96 MHz (PCLK1*2 since APB1 div=2).
     * Prescaler = 959 -> 96MHz / 960 = 100kHz tick.
     * Period = 9 -> 100kHz / 10 = 10kHz overflow rate. */
    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 959U;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 9U;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;

    if (HAL_TIM_Base_Init(&htim3) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_TIM_Base_Start_IT(&htim3) != HAL_OK) {
        Error_Handler();
    }
}

/** TIM3 ISR callback — counts down blink timer and turns LED off. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM3) return;

    if (blink_active) {
        if (blink_counter > 0) {
            blink_counter--;
        } else {
            HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_SET);  /* LED OFF */
            blink_active = 0;
        }
    }
}
