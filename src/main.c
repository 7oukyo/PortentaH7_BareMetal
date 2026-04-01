/**
 * @file main.c
 * @brief Portenta H7 bare-metal entry point (CM7 only).
 *
 * Init order: PJ0 LOW -> HAL_Init -> I-Cache -> PH1 HIGH (oscillator) ->
 * SystemClock_Config (480 MHz) -> PeriphCommonClock (PLL2) -> GPIO LEDs ->
 * PMIC_Init (I2C1) -> LED PWM rainbow. See docs/current-config.md.
 */

#include "main.h"
#include "pmic.h"
#include "led_pwm.h"
#include "usb_device.h"

static void SystemClock_Config(void);
static void PeriphCommonClock_Config(void);
static void GPIO_LEDs_Init(void);

int main(void)
{
    /* PJ0 LOW — PMIC STANDBY pin must be LOW for RUN mode before anything. */
    GPIO_InitTypeDef GPIO_InitStruct;

    __HAL_RCC_GPIOJ_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_0, GPIO_PIN_RESET);
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pin   = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOJ, &GPIO_InitStruct);

    /* CM4 boot disabled — no CM4 firmware, stale flash bank 2 causes interference. */

    HAL_Init();

    /* I-Cache improves code fetch from flash. D-Cache skipped — breaks
     * DMA-based peripherals (Ethernet) without cache maintenance. */
    SCB_EnableICache();

    /* Enable 25 MHz oscillator: PH1 HIGH gates OSCEN net. */
    __HAL_RCC_GPIOH_CLK_ENABLE();
    GPIO_InitTypeDef gpio_osc = {0};
    gpio_osc.Pin   = GPIO_PIN_1;
    gpio_osc.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_osc.Pull  = GPIO_NOPULL;
    gpio_osc.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOH, &gpio_osc);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_1, GPIO_PIN_SET);
    HAL_Delay(10);  /* oscillator startup time */

    SystemClock_Config();
    PeriphCommonClock_Config();

    GPIO_LEDs_Init();

    /* PMIC init via HAL I2C1 — sets SW1/SW2 to 3.3V, configures LDOs */
    PMIC_Init();

    /* USB3320 ULPI PHY reset via PJ4: low -> high with delays.
     * Requires SW1 (+3V1SW) and LDO2 (+1V8) from PMIC to be up. */
    GPIO_InitTypeDef gpio_usb = {0};
    gpio_usb.Pin   = GPIO_PIN_4;
    gpio_usb.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_usb.Pull  = GPIO_NOPULL;
    gpio_usb.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOJ, &gpio_usb);
    HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_Delay(10);

    /* USB CDC Virtual COM Port over USB-C (USB3320 ULPI PHY) */
    MX_USB_DEVICE_Init();

    /* All LEDs off before PWM starts */
    HAL_GPIO_WritePin(LED_GPIO_PORT,
                      LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN,
                      GPIO_PIN_SET);

    /* Start TIM6 software PWM rainbow */
    LedPwm_Init();

    while (1)
    {
        LedPwm_HeartbeatPoll();
    }
}

/** @brief System Clock — HSE 25 MHz -> PLL1 -> 480 MHz SYSCLK. */
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

/** @brief PLL2 for SPI2/SPI5 peripheral clocks. */
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
