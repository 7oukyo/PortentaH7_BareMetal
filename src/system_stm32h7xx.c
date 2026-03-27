/**
 * @file system_stm32h7xx.c
 * @brief CMSIS SystemInit for STM32H747 CM7 — dual-core boot version.
 *
 * Ported from reference: Portenta_Cube_Template/Common/Src/
 *   system_stm32h7xx_dualcore_boot_cm4_cm7.c
 *
 * Called from Reset_Handler before main(). Performs full RCC reset to
 * known state, enables FPU, sets vector table, disables FMC bank1
 * speculation, and applies revY AXI SRAM workaround.
 */

#include "stm32h7xx.h"
#include <math.h>

#if !defined(HSE_VALUE)
#define HSE_VALUE    ((uint32_t)25000000)
#endif

#if !defined(CSI_VALUE)
#define CSI_VALUE    ((uint32_t)4000000)
#endif

#if !defined(HSI_VALUE)
#define HSI_VALUE    ((uint32_t)64000000)
#endif

uint32_t SystemCoreClock = 64000000;
uint32_t SystemD2Clock = 64000000;
const uint8_t D1CorePrescTable[16] = {0, 0, 0, 0, 1, 2, 3, 4, 1, 2, 3, 4, 6, 7, 8, 9};

/**
 * @brief Setup the microcontroller system — runs before main().
 *
 * Matches reference exactly: full RCC reset, flash latency, FMC bank1
 * disable, HSEM EXTI enable, revY AXI SRAM workaround, VTOR to flash.
 */
void SystemInit(void)
{
    /* FPU settings */
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << (10*2)) | (3UL << (11*2)));
#endif

    /* SEVONPEND: WFI/WFE can be woken by any pending interrupt from CM4 */
    SCB->SCR |= SCB_SCR_SEVONPEND_Msk;

#ifdef CORE_CM7
    /* Reset the RCC clock configuration to the default reset state */

    /* Increasing the CPU frequency — set flash latency first */
    if (FLASH_LATENCY_DEFAULT > (READ_BIT((FLASH->ACR), FLASH_ACR_LATENCY)))
    {
        MODIFY_REG(FLASH->ACR, FLASH_ACR_LATENCY, (uint32_t)(FLASH_LATENCY_DEFAULT));
    }

    /* Set HSION bit */
    RCC->CR |= RCC_CR_HSION;

    /* Reset CFGR register */
    RCC->CFGR = 0x00000000;

    /* Reset HSEON, HSECSSON, CSION, RC48ON, CSIKERON, PLL1ON, PLL2ON and PLL3ON bits */
    RCC->CR &= 0xEAF6ED7FU;

    /* Decreasing the number of wait states because of lower CPU frequency */
    if (FLASH_LATENCY_DEFAULT < (READ_BIT((FLASH->ACR), FLASH_ACR_LATENCY)))
    {
        MODIFY_REG(FLASH->ACR, FLASH_ACR_LATENCY, (uint32_t)(FLASH_LATENCY_DEFAULT));
    }

    /* Reset D1CFGR register */
    RCC->D1CFGR = 0x00000000;

    /* Reset D2CFGR register */
    RCC->D2CFGR = 0x00000000;

    /* Reset D3CFGR register */
    RCC->D3CFGR = 0x00000000;

    /* Reset PLLCKSELR register */
    RCC->PLLCKSELR = 0x02020200;

    /* Reset PLLCFGR register */
    RCC->PLLCFGR = 0x01FF0000;

    /* Reset PLL1DIVR register */
    RCC->PLL1DIVR = 0x01010280;

    /* Reset PLL1FRACR register */
    RCC->PLL1FRACR = 0x00000000;

    /* Reset PLL2DIVR register */
    RCC->PLL2DIVR = 0x01010280;

    /* Reset PLL2FRACR register */
    RCC->PLL2FRACR = 0x00000000;

    /* Reset PLL3DIVR register */
    RCC->PLL3DIVR = 0x01010280;

    /* Reset PLL3FRACR register */
    RCC->PLL3FRACR = 0x00000000;

    /* Reset HSEBYP bit */
    RCC->CR &= 0xFFFBFFFFU;

    /* Disable all RCC interrupts */
    RCC->CIER = 0x00000000;

    /* Enable CortexM7 HSEM EXTI line (line 78) — for CM4 synchronization */
    EXTI_D2->EMR3 |= 0x4000UL;

    /* RevY silicon workaround: AXI SRAM target read issuing capability */
    if ((DBGMCU->IDCODE & 0xFFFF0000U) < 0x20000000U)
    {
        *((__IO uint32_t*)0x51008108) = 0x000000001U;
    }

    /* Disable FMC bank1 (enabled after reset) — prevents CPU speculation
     * access that blocks FMC for 24us, interfering with LTDC. */
    FMC_Bank1_R->BTCR[0] = 0x000030D2;

    /* Configure the Vector Table location to start of flash */
    SCB->VTOR = FLASH_BANK1_BASE;

#endif /* CORE_CM7 */
}

/**
 * @brief Update SystemCoreClock from current RCC register state.
 *
 * Ported from reference — full implementation using PLL/HSI/HSE/CSI
 * register readback with fractional PLL support.
 */
void SystemCoreClockUpdate(void)
{
    uint32_t pllp, pllsource, pllm, pllfracen, hsivalue, tmp;
    uint32_t common_system_clock;
    float_t fracn1, pllvco;

    switch (RCC->CFGR & RCC_CFGR_SWS)
    {
    case RCC_CFGR_SWS_HSI:
        common_system_clock = (uint32_t)(HSI_VALUE >> ((RCC->CR & RCC_CR_HSIDIV) >> 3));
        break;

    case RCC_CFGR_SWS_CSI:
        common_system_clock = CSI_VALUE;
        break;

    case RCC_CFGR_SWS_HSE:
        common_system_clock = HSE_VALUE;
        break;

    case RCC_CFGR_SWS_PLL1:
        pllsource = (RCC->PLLCKSELR & RCC_PLLCKSELR_PLLSRC);
        pllm = ((RCC->PLLCKSELR & RCC_PLLCKSELR_DIVM1) >> 4);
        pllfracen = ((RCC->PLLCFGR & RCC_PLLCFGR_PLL1FRACEN) >> RCC_PLLCFGR_PLL1FRACEN_Pos);
        fracn1 = (float_t)(uint32_t)(pllfracen * ((RCC->PLL1FRACR & RCC_PLL1FRACR_FRACN1) >> 3));

        if (pllm != 0U)
        {
            switch (pllsource)
            {
            case RCC_PLLCKSELR_PLLSRC_HSI:
                hsivalue = (HSI_VALUE >> ((RCC->CR & RCC_CR_HSIDIV) >> 3));
                pllvco = ((float_t)hsivalue / (float_t)pllm) * ((float_t)(uint32_t)(RCC->PLL1DIVR & RCC_PLL1DIVR_N1) + (fracn1 / (float_t)0x2000) + (float_t)1);
                break;

            case RCC_PLLCKSELR_PLLSRC_CSI:
                pllvco = ((float_t)CSI_VALUE / (float_t)pllm) * ((float_t)(uint32_t)(RCC->PLL1DIVR & RCC_PLL1DIVR_N1) + (fracn1 / (float_t)0x2000) + (float_t)1);
                break;

            case RCC_PLLCKSELR_PLLSRC_HSE:
                pllvco = ((float_t)HSE_VALUE / (float_t)pllm) * ((float_t)(uint32_t)(RCC->PLL1DIVR & RCC_PLL1DIVR_N1) + (fracn1 / (float_t)0x2000) + (float_t)1);
                break;

            default:
                hsivalue = (HSI_VALUE >> ((RCC->CR & RCC_CR_HSIDIV) >> 3));
                pllvco = ((float_t)hsivalue / (float_t)pllm) * ((float_t)(uint32_t)(RCC->PLL1DIVR & RCC_PLL1DIVR_N1) + (fracn1 / (float_t)0x2000) + (float_t)1);
                break;
            }
            pllp = (((RCC->PLL1DIVR & RCC_PLL1DIVR_P1) >> 9) + 1U);
            common_system_clock = (uint32_t)(float_t)(pllvco / (float_t)pllp);
        }
        else
        {
            common_system_clock = 0U;
        }
        break;

    default:
        common_system_clock = (uint32_t)(HSI_VALUE >> ((RCC->CR & RCC_CR_HSIDIV) >> 3));
        break;
    }

    /* Compute SystemClock frequency */
    tmp = D1CorePrescTable[(RCC->D1CFGR & RCC_D1CFGR_D1CPRE) >> RCC_D1CFGR_D1CPRE_Pos];
    common_system_clock >>= tmp;

    /* SystemD2Clock: CM4 CPU, AXI and AHBs Clock frequency */
    SystemD2Clock = (common_system_clock >> ((D1CorePrescTable[(RCC->D1CFGR & RCC_D1CFGR_HPRE) >> RCC_D1CFGR_HPRE_Pos]) & 0x1FU));

    SystemCoreClock = common_system_clock;
}
