/**
 * @file stm32f4xx_hal_msp.c
 * @brief HAL MSP (MCU Support Package) init/deinit callbacks.
 * Called by HAL drivers during HAL_xxx_Init() to configure clocks and GPIO.
 *
 * Note: UART MSP is in c4001.c (HAL_UART_MspInit/MspDeInit).
 * Note: I2C MSP is here for I2C1 (INA226).
 */

#include "main.h"

/** Global MSP init — called by HAL_Init(). */
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_NVIC_SetPriority(PendSV_IRQn, 15, 0);
}

/** I2C1 MSP init — called by HAL_I2C_Init(&hi2c1).
 * Configures PB6=SCL, PB7=SDA as AF4 open-drain for I2C1. */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) {
        __HAL_RCC_GPIOB_CLK_ENABLE();

        /* PB6=SCL, PB7=SDA: AF4 (I2C1), open-drain, pull-up */
        GPIO_InitTypeDef gpio = {0};
        gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
        gpio.Mode      = GPIO_MODE_AF_OD;
        gpio.Pull      = GPIO_PULLUP;
        gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF4_I2C1;
        HAL_GPIO_Init(GPIOB, &gpio);

        __HAL_RCC_I2C1_CLK_ENABLE();
    }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) {
        __HAL_RCC_I2C1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6);
        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_7);
    }
}

/** TIM3 MSP init — called by HAL_TIM_Base_Init().
 * Enables TIM3 clock and configures NVIC. */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        HAL_NVIC_SetPriority(TIM3_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(TIM3_IRQn);
    }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        HAL_NVIC_DisableIRQ(TIM3_IRQn);
        __HAL_RCC_TIM3_CLK_DISABLE();
    }
}
