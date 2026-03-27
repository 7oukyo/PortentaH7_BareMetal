#ifndef PMIC_H
#define PMIC_H

#include "main.h"

/**
 * @brief Initialize NXP PF1550 PMIC via HAL I2C1.
 *
 * Must be called AFTER HAL_Init() and SystemClock_Config().
 * Initializes I2C1 (PB6=SCL, PB7=SDA, AF4) then writes PMIC registers
 * to set SW1/SW2 to 3.3V and configure LDOs.
 */
void PMIC_Init(void);

#endif /* PMIC_H */
