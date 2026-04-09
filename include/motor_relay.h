/**
 * @file motor_relay.h
 * @brief H-bridge motor control via two relay modules (active-LOW).
 *
 * Relay 1 (PC15 / GPIO_1): Motor+ polarity
 * Relay 2 (PD5  / GPIO_3): Motor- polarity
 * Both HIGH = motor stopped/braked. NEVER both LOW (dead short at 29V).
 * 100ms dead time enforced between direction changes.
 */

#ifndef MOTOR_RELAY_H
#define MOTOR_RELAY_H

#include "main.h"

/* GPIO pin assignments */
#define MOTOR_RELAY1_PORT   GPIOC
#define MOTOR_RELAY1_PIN    GPIO_PIN_15
#define MOTOR_RELAY2_PORT   GPIOD
#define MOTOR_RELAY2_PIN    GPIO_PIN_5

/* Dead time between direction changes (ms) */
#define MOTOR_DEADTIME_MS   100

typedef enum {
    MOTOR_STOP    = 0,  /* Both relays OFF (HIGH) — braked */
    MOTOR_FWD     = 1,  /* Relay 1 ON, Relay 2 OFF */
    MOTOR_REV     = 2,  /* Relay 1 OFF, Relay 2 ON */
} MotorDir_t;

/** Initialize relay GPIO pins (both OFF / HIGH). */
void Motor_Init(void);

/** Set motor direction. Enforces dead time if changing from FWD<->REV. */
void Motor_SetDir(MotorDir_t dir);

/** Get current motor direction. */
MotorDir_t Motor_GetDir(void);

/** Emergency stop — both relays OFF immediately, no dead time. */
void Motor_EmergencyStop(void);

#endif /* MOTOR_RELAY_H */
