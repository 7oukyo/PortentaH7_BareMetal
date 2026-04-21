/**
 * @file led.h
 * @brief LED blink feedback on USB serial RX.
 *
 * BlackPill F411: single LED on PC13 (active LOW, sink mode).
 * TIM3 at 10kHz times the ~50ms blink triggered by Led_BlinkOnRx().
 *
 * Note: F411 has no TIM6/TIM7. Using TIM3 (general purpose, APB1) instead.
 */

#ifndef LED_H
#define LED_H

#include "main.h"

extern TIM_HandleTypeDef htim3;

/** Initialize TIM3 for blink timing. */
void Led_Init(void);

/** Trigger a short LED blink (call from CDC receive). */
void Led_BlinkOnRx(void);

#endif /* LED_H */
