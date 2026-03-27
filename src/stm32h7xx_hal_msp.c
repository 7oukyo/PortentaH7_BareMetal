/**
 * @file stm32h7xx_hal_msp.c
 * @brief HAL MSP (MCU Support Package) init/deinit callbacks.
 * Called by HAL drivers during HAL_xxx_Init() to configure clocks and GPIO.
 */

#include "main.h"

/**
 * @brief Global MSP init — called by HAL_Init().
 * Enables SYSCFG clock and sets PendSV priority (required for HAL).
 */
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_NVIC_SetPriority(PendSV_IRQn, 15, 0);
}

/**
 * @brief I2C1 MSP init — called by HAL_I2C_Init(&hi2c1).
 * Configures I2C1 peripheral clock source, enables GPIOB and I2C1 clocks,
 * and sets PB6/PB7 as I2C1 AF4 open-drain.
 * Reference: Portenta_Cube_Template/CM7/Core/Src/i2c.c HAL_I2C_MspInit()
 */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) {
        /* I2C1 kernel clock = D2PCLK1 (120MHz after SystemClock_Config).
         * This matches the reference timing 0x307075B1. RM0399 §8.7.46 */
        RCC_PeriphCLKInitTypeDef clk = {0};
        clk.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
        clk.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
        if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) {
            Error_Handler();
        }

        __HAL_RCC_GPIOB_CLK_ENABLE();

        /* PB6=SCL, PB7=SDA: AF4, open-drain, no pull, very high speed. */
        GPIO_InitTypeDef gpio = {0};
        gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
        gpio.Mode      = GPIO_MODE_AF_OD;
        gpio.Pull      = GPIO_NOPULL;
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

/**
 * @brief TIM6 MSP init — called by HAL_TIM_Base_Init().
 * Enables TIM6 clock and configures NVIC for TIM6_DAC_IRQn.
 * TIM6 is on APB1 (low-speed). RM0399 §8.7.36 — RCC_APB1LENR.
 */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        __HAL_RCC_TIM6_CLK_ENABLE();
        HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
        __HAL_RCC_TIM6_CLK_DISABLE();
    }
}
