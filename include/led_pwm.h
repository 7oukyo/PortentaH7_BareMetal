/**
 * @file led_pwm.h
 * @brief Green LED blink feedback on USB serial RX.
 *
 * TIM6 at 10kHz times the ~50ms green LED blink triggered by LedPwm_BlinkOnRx().
 */

#ifndef LED_PWM_H
#define LED_PWM_H

#include "main.h"

extern TIM_HandleTypeDef htim6;

/** Initialize TIM6 for blink timing. */
void LedPwm_Init(void);

/** Trigger a short green LED blink (call from CDC receive). */
void LedPwm_BlinkOnRx(void);

#endif /* LED_PWM_H */
