/**
 * @file pmic.c
 * @brief NXP PF1550 PMIC initialization via HAL I2C1.
 *
 * Called AFTER HAL_Init() and SystemClock_Config(). The MCU boots on
 * PMIC OTP default voltages — HAL and clock config do not require the
 * PMIC to be configured first. This matches the working reference:
 * skjafar Portenta_Cube_Template/CM7/Core/Src/main.c
 *
 * I2C1: PB6=SCL, PB7=SDA, AF4, 7-bit address 0x08.
 * I2C1 timing 0x307075B1 for D2PCLK1 = 120MHz after SystemClock_Config.
 * Reference: Portenta_Cube_Template/CM7/Core/Src/i2c.c
 *
 * HISTORY:
 * - Original code used bare-register I2C4 on PD12/PD13 — wrong bus entirely.
 * - Second attempt used bare-register I2C1 before HAL_Init — failed on cold boot.
 * - Current (correct): HAL I2C1 after full system init, matching reference.
 */

#include "pmic.h"

/* HAL I2C handle — initialized by PMIC_Init(), used for all PMIC writes. */
static I2C_HandleTypeDef hi2c1;

/* PF1550 7-bit I2C address (shifted left by HAL_I2C_Master_Transmit) */
#define PMIC_ADDR  (0x08U << 1U)

/**
 * @brief Write one register to the PF1550 PMIC.
 * @param reg  Register address
 * @param val  Value to write
 * @return 0 on success, -1 on failure
 */
static int pmic_write(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = { reg, val };
    if (HAL_I2C_Master_Transmit(&hi2c1, PMIC_ADDR, data, 2, 100) != HAL_OK) {
        return -1;
    }
    return 0;
}

/**
 * @brief Initialize I2C1 for PMIC communication.
 *
 * Timing 0x307075B1: from reference, for D2PCLK1 = 120MHz (after
 * SystemClock_Config with APB1 = HCLK/2 = 120MHz).
 * HAL_I2C_MspInit() in stm32h7xx_hal_msp.c handles GPIO and clock enable.
 *
 * Reference: Portenta_Cube_Template/CM7/Core/Src/i2c.c MX_I2C1_Init()
 */
static void PMIC_I2C_Init(void)
{
    hi2c1.Instance              = I2C1;
    hi2c1.Init.Timing           = 0x307075B1U;
    hi2c1.Init.OwnAddress1      = 0;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief Configure PF1550 PMIC power rails via I2C1.
 *
 * Must be called after HAL_Init() and SystemClock_Config().
 * Initializes I2C1, then writes all PF1550 registers to set:
 *   SW1/SW2 → 3.3V, LDO1 → 1.0V, LDO2 → 1.8V, LDO3 → 1.2V,
 *   charger LED off, SW3 current limit, VBUS current limit.
 *
 * Register values and order from reference initPMIC().
 */
void PMIC_Init(void)
{
    PMIC_I2C_Init();

    /* LDO2 to 1.8V */
    pmic_write(0x4F, 0x00U);

    /* LDO1 to 1.0V */
    pmic_write(0x4C, 0x05U);
    pmic_write(0x4D, 0x0FU);

    /* LDO3 to 1.2V */
    pmic_write(0x52, 0x09U);
    pmic_write(0x53, 0x0FU);

    HAL_Delay(10);

    /* Charger LED off: duty cycle (0x9C) then disable (0x9E).
     * Orange LED on board turns off here — visual confirmation that
     * I2C is working and PMIC is responding. */
    pmic_write(0x9C, (1U << 7U));
    pmic_write(0x9E, (1U << 5U));

    HAL_Delay(10);

    /* SW3: 2A current limit (keeps rail stable at high current loads) */
    pmic_write(0x42, 0x02U);

    HAL_Delay(10);

    /* VBUS input current limit 1.5A */
    pmic_write(0x94, (20U << 3U));

    /* SW2 -> 3.3V: main 3.3V rail for MCU and board peripherals. */
    pmic_write(0x38, 0x07U);   /* RUN = 3.3V */
    pmic_write(0x39, 0x06U);   /* STANDBY = 3.0V */
    pmic_write(0x3A, 0x06U);   /* SLEEP = 3.0V */
    pmic_write(0x3B, 0x0FU);   /* SW2_CTRL */

    /* SW1 -> 3.3V: VCAP / MCU digital voltage supply. */
    pmic_write(0x32, 0x07U);   /* RUN = 3.3V */
    pmic_write(0x33, 0x06U);   /* STANDBY = 3.0V */
    pmic_write(0x34, 0x06U);   /* SLEEP = 3.0V */
    pmic_write(0x35, 0x0FU);   /* SW1_CTRL */

    HAL_Delay(10);

    /* LDO2 enable — written last per reference comment: writing 0x50
     * while other transactions are pending can kill I2C responsiveness. */
    pmic_write(0x50, 0x0FU);
}
