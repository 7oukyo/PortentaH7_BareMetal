/**
 * @file led_pwm.h
 * @brief Software PWM rainbow cycling on the Portenta H7 onboard RGB LED.
 *
 * PK5/PK6/PK7 have no hardware timer output, so PWM is generated in a
 * TIM6 interrupt. TIM6 fires at 10kHz; 100 ticks = one PWM period (100Hz).
 * Hue advances once per PWM period, cycling the full rainbow in ~3.6s.
 */

#ifndef LED_PWM_H
#define LED_PWM_H

#include "main.h"

extern TIM_HandleTypeDef htim6;

/** Initialize TIM6 and start the rainbow PWM interrupt. */
void LedPwm_Init(void);

#endif /* LED_PWM_H */
