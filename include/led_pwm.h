/**
 * @file led_pwm.h
 * @brief LED feedback blink and serial heartbeat.
 *
 * Green LED blinks briefly on serial RX. Heartbeat prints uptime
 * over CDC every 5 seconds. TIM6 runs at 10kHz for blink timing.
 */

#ifndef LED_PWM_H
#define LED_PWM_H

#include "main.h"

extern TIM_HandleTypeDef htim6;

/** Initialize TIM6 for blink timing. */
void LedPwm_Init(void);

/** Trigger a short green LED blink (call from CDC receive). */
void LedPwm_BlinkOnRx(void);

/** Poll from main loop — sends uptime heartbeat every 5 seconds. */
void LedPwm_HeartbeatPoll(void);

#endif /* LED_PWM_H */
