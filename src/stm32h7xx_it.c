/**
 * @file stm32h7xx_it.c
 * @brief Cortex-M7 and peripheral interrupt handlers.
 * All handlers for unused peripherals are provided as weak aliases
 * in the startup file and redirect to Default_Handler (infinite loop).
 */

#include "main.h"
#include "led_pwm.h"
#include "stm32h7xx_hal.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

/* NMI: treat as unrecoverable */
void NMI_Handler(void)
{
    while (1) {}
}

/* HardFault: capture fault address then halt for debugger.
 * The inline asm reads PC from the stack frame. RM0399 §B1.5.14 */
void HardFault_Handler(void)
{
    __asm volatile (
        " movs r0, #4       \n"
        " movs r1, lr       \n"
        " tst  r0, r1       \n"
        " beq  _MSP         \n"
        " mrs  r0, psp      \n"
        " b    _HALT        \n"
        "_MSP:              \n"
        " mrs  r0, msp      \n"
        "_HALT:             \n"
        " ldr  r1, [r0, #20]\n"  /* stacked PC */
        " bkpt #0           \n"
    );
    while (1) {}
}

void MemManage_Handler(void)
{
    while (1) {}
}

void BusFault_Handler(void)
{
    while (1) {}
}

void UsageFault_Handler(void)
{
    while (1) {}
}

void DebugMon_Handler(void)
{
}

/* SysTick: HAL timebase tick increment. Must call HAL_IncTick(). */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* TIM6 overflow (shared vector with DAC). Delegates to HAL which calls
 * HAL_TIM_PeriodElapsedCallback() implemented in led_pwm.c. */
void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim6);
}

/* USB OTG HS interrupt — delegates to HAL PCD driver. */
void OTG_HS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}
