#ifndef MAIN_H
#define MAIN_H

#include "stm32f4xx_hal.h"

/* Onboard LED on PC13 (active LOW — sink mode, drive LOW to turn ON) */
#define LED_PIN        GPIO_PIN_13
#define LED_GPIO_PORT  GPIOC

void Error_Handler(void);

/** Process a command received from USB CDC. Dispatches to the right module. */
void HandleSerialCmd(const char *cmd, uint16_t len);

#endif /* MAIN_H */
