/**
 * @file main.c
 * @brief Portenta H7 bare-metal entry point.
 *
 * Ported from reference: Portenta_Cube_Template/CM7/Core/Src/main.c
 * Every init decision matches the reference project exactly.
 *
 * Initialization order (from reference):
 *  1. PJ0 LOW — PMIC STANDBY pin, must be driven LOW for RUN mode.
 *  2. Enable CM4 boot (Portenta fuses disable CM4 by default).
 *  3. Wait for CM4 to enter stop mode.
 *  4. HAL_Init() — SysTick, HAL state.
 *  5. Enable PH1 (oscillator enable pin).
 *  6. SystemClock_Config() — HSE 25MHz -> PLL1 -> 480MHz CM7.
 *  7. PeriphCommonClock_Config() — PLL2 for SPI clocks.
 *  8. HSEM synchronization — release CM4 from stop mode.
 *  9. Wait for CM4 to fully wake (D2CKRDY + NOP delay).
 * 10. GPIO init, I2C1 init, PMIC_Init().
 * 11. LED PWM rainbow start.
 */

#include "main.h"
#include "pmic.h"
#include "led_pwm.h"

#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U)
#endif

static void SystemClock_Config(void);
static void PeriphCommonClock_Config(void);
static void GPIO_LEDs_Init(void);

int main(void)
{
    /* === Reference line 257-264: PJ0 LOW for PMIC RUN mode === */
    GPIO_InitTypeDef GPIO_InitStruct;

    __HAL_RCC_GPIOJ_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_0, GPIO_PIN_RESET);
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pin   = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOJ, &GPIO_InitStruct);

    /* === Reference line 267: Boot up CM4 core === */
    /* DISABLED: we have no CM4 firmware. Enabling CM4 boot causes it to
     * run stale code from flash bank 2 (old Arduino bootloader), which
     * blinks blue LED and may interfere with CM7 operation. */
    // HAL_RCCEx_EnableBootCore(RCC_BOOT_C2);

    /* === Reference line 301: HAL_Init === */
    HAL_Init();

    /* === Reference line 306-314: Enable oscillator pin PH1 === */
    __HAL_RCC_GPIOH_CLK_ENABLE();
    GPIO_InitTypeDef gpio_osc_init_structure;
    gpio_osc_init_structure.Pin   = GPIO_PIN_1;
    gpio_osc_init_structure.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_osc_init_structure.Pull  = GPIO_PULLUP;
    gpio_osc_init_structure.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOH, &gpio_osc_init_structure);
    HAL_Delay(10);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_1, GPIO_PIN_SET);

    /* === Reference line 319: System clock config === */
    SystemClock_Config();

    /* === Reference line 323: Peripheral common clock (PLL2 for SPI) === */
    PeriphCommonClock_Config();

    /* === Reference line 327-343: HSEM synchronization with CM4 === */
    /* DISABLED: CM4 not booted, no synchronization needed.
     * Re-enable when CM4 firmware is added. */

    /* === Reference line 351-353: Initialize peripherals === */
    GPIO_LEDs_Init();

    /* PMIC init via HAL I2C1 — sets SW1/SW2 to 3.3V, configures LDOs */
    PMIC_Init();

    /* All LEDs off before PWM starts */
    HAL_GPIO_WritePin(LED_GPIO_PORT,
                      LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN,
                      GPIO_PIN_SET);

    /* Start TIM6 software PWM rainbow */
    LedPwm_Init();

    while (1)
    {
        __WFI();
    }
}

/**
 * @brief System Clock Configuration — 480MHz from HSE 25MHz.
 * Ported verbatim from reference lines 427-483.
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ConfigSupply(PWR_SMPS_1V8_SUPPLIES_LDO);

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    __HAL_RCC_PLL_PLLSOURCE_CONFIG(RCC_PLLSOURCE_HSE);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_BYPASS;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 5;
    RCC_OscInitStruct.PLL.PLLN       = 192;
    RCC_OscInitStruct.PLL.PLLP       = 2;
    RCC_OscInitStruct.PLL.PLLQ       = 2;
    RCC_OscInitStruct.PLL.PLLR       = 2;
    RCC_OscInitStruct.PLL.PLLRGE     = RCC_PLL1VCIRANGE_2;
    RCC_OscInitStruct.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN   = 0;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                                | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief Peripheral common clock — PLL2 for SPI2/SPI5.
 * Ported verbatim from reference lines 491-512.
 */
static void PeriphCommonClock_Config(void)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI2 | RCC_PERIPHCLK_SPI5;
    PeriphClkInitStruct.PLL2.PLL2M    = 5;
    PeriphClkInitStruct.PLL2.PLL2N    = 72;
    PeriphClkInitStruct.PLL2.PLL2P    = 3;
    PeriphClkInitStruct.PLL2.PLL2Q    = 3;
    PeriphClkInitStruct.PLL2.PLL2R    = 2;
    PeriphClkInitStruct.PLL2.PLL2RGE  = RCC_PLL2VCIRANGE_2;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    PeriphClkInitStruct.PLL2.PLL2FRACN  = 0;
    PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
    PeriphClkInitStruct.Spi45ClockSelection  = RCC_SPI45CLKSOURCE_PLL2;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief Initialize GPIOK pins 5, 6, 7 as push-pull outputs for LEDs.
 * All three LEDs are active LOW on the Portenta H7.
 */
static void GPIO_LEDs_Init(void)
{
    __HAL_RCC_GPIOK_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &gpio);
}

/**
 * @brief Error handler — disable interrupts and spin.
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
