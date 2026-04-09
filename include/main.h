#ifndef MAIN_H
#define MAIN_H

#include "stm32h7xx_hal.h"

/* Onboard LED pins on GPIOK (active LOW) */
#define LED_RED_PIN    GPIO_PIN_5
#define LED_GREEN_PIN  GPIO_PIN_6
#define LED_BLUE_PIN   GPIO_PIN_7
#define LED_GPIO_PORT  GPIOK

void Error_Handler(void);

/** Process a command received from USB CDC. Dispatches to the right module. */
void HandleSerialCmd(const char *cmd, uint16_t len);

#endif /* MAIN_H */
