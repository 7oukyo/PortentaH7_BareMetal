/**
 * @file motor_relay.c
 * @brief H-bridge motor control via two relay modules (active-LOW).
 *
 * PC15 (Relay 1) and PD5 (Relay 2) control motor polarity.
 * Active-LOW: pulling pin LOW energizes relay (COM→NO = +29V).
 * Safety: NEVER both LOW. 100ms dead time on direction change.
 */

#include "motor_relay.h"

static MotorDir_t current_dir = MOTOR_STOP;

/* Set both relays OFF (HIGH) — motor braked. */
static void relays_off(void)
{
    HAL_GPIO_WritePin(MOTOR_RELAY1_PORT, MOTOR_RELAY1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_RELAY2_PORT, MOTOR_RELAY2_PIN, GPIO_PIN_SET);
}

void Motor_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* Start with both relays OFF (HIGH) before configuring as output */
    HAL_GPIO_WritePin(MOTOR_RELAY1_PORT, MOTOR_RELAY1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_RELAY2_PORT, MOTOR_RELAY2_PIN, GPIO_PIN_SET);

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = MOTOR_RELAY1_PIN;
    HAL_GPIO_Init(MOTOR_RELAY1_PORT, &gpio);

    gpio.Pin = MOTOR_RELAY2_PIN;
    HAL_GPIO_Init(MOTOR_RELAY2_PORT, &gpio);

    current_dir = MOTOR_STOP;
}

void Motor_SetDir(MotorDir_t dir)
{
    if (dir == current_dir) return;

    /* Always go through STOP first to prevent both-LOW condition */
    relays_off();

    if (current_dir != MOTOR_STOP && dir != MOTOR_STOP) {
        /* Direction reversal: enforce dead time */
        HAL_Delay(MOTOR_DEADTIME_MS);
    }

    switch (dir) {
    case MOTOR_FWD:
        HAL_GPIO_WritePin(MOTOR_RELAY1_PORT, MOTOR_RELAY1_PIN, GPIO_PIN_RESET);
        break;
    case MOTOR_REV:
        HAL_GPIO_WritePin(MOTOR_RELAY2_PORT, MOTOR_RELAY2_PIN, GPIO_PIN_RESET);
        break;
    case MOTOR_STOP:
    default:
        /* Already off from relays_off() above */
        break;
    }

    current_dir = dir;
}

MotorDir_t Motor_GetDir(void)
{
    return current_dir;
}

void Motor_EmergencyStop(void)
{
    relays_off();
    current_dir = MOTOR_STOP;
}
