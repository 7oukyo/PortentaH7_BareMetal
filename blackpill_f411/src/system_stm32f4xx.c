/**
 * @file system_stm32f4xx.c
 * @brief CMSIS Cortex-M4 system initialization for STM32F411.
 *
 * SystemInit() is called from Reset_Handler before main().
 * It enables the FPU and sets the vector table offset.
 * Clock configuration is done in main() via SystemClock_Config().
 */

#include "stm32f4xx.h"

/* System clock frequency — updated by SystemClock_Config() via HAL.
 * Default value = HSI (16 MHz) until PLL is configured. */
uint32_t SystemCoreClock = 16000000U;

/* AHB prescaler table for clock calculation */
const uint8_t AHBPrescTable[16] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 3, 4, 6, 7, 8, 9
};

/* APB prescaler table */
const uint8_t APBPrescTable[8] = {
    0, 0, 0, 0, 1, 2, 3, 4
};

/**
 * @brief SystemInit — called before main.
 * Enables FPU (CP10/CP11 full access) and sets VTOR to flash base.
 */
void SystemInit(void)
{
    /* FPU settings: enable CP10 and CP11 full access */
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << (10 * 2)) | (3UL << (11 * 2)));
#endif

    /* Vector table relocation to flash base (no bootloader) */
    SCB->VTOR = FLASH_BASE;
}

/**
 * @brief Update SystemCoreClock from RCC registers.
 * Called by HAL_RCC_ClockConfig() automatically.
 */
void SystemCoreClockUpdate(void)
{
    uint32_t tmp, pllvco, pllp, pllsource, pllm;

    tmp = RCC->CFGR & RCC_CFGR_SWS;

    switch (tmp) {
    case 0x00:  /* HSI */
        SystemCoreClock = HSI_VALUE;
        break;
    case 0x04:  /* HSE */
        SystemCoreClock = HSE_VALUE;
        break;
    case 0x08:  /* PLL */
        pllsource = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) >> RCC_PLLCFGR_PLLSRC_Pos;
        pllm = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;

        if (pllsource != 0) {
            pllvco = (HSE_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        } else {
            pllvco = (HSI_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        }

        pllp = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLP) >> 16) + 1) * 2;
        SystemCoreClock = pllvco / pllp;
        break;
    default:
        SystemCoreClock = HSI_VALUE;
        break;
    }

    /* Apply AHB prescaler */
    tmp = AHBPrescTable[((RCC->CFGR & RCC_CFGR_HPRE) >> 4)];
    SystemCoreClock >>= tmp;
}
