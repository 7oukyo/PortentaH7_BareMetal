/**
 * @file stm32f4xx_it.c
 * @brief Cortex-M4 and peripheral interrupt handlers.
 * All handlers for unused peripherals are provided as weak aliases
 * in the startup file and redirect to Default_Handler (infinite loop).
 */

#include "main.h"
#include "led.h"
#include "c4001.h"
#include "stm32f4xx_hal.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* NMI: treat as unrecoverable */
void NMI_Handler(void)
{
    while (1) {}
}

/* HardFault: halt for debugger */
void HardFault_Handler(void)
{
    __asm volatile (
        " movs r0, #4       \n"
        " movs r1, lr       \n"
        " tst  r0, r1       \n"
        " beq  _MSP_f4      \n"
        " mrs  r0, psp      \n"
        " b    _HALT_f4     \n"
        "_MSP_f4:           \n"
        " mrs  r0, msp      \n"
        "_HALT_f4:          \n"
        " ldr  r1, [r0, #20]\n"
        " bkpt #0           \n"
    );
    while (1) {}
}

void MemManage_Handler(void)  { while (1) {} }
void BusFault_Handler(void)   { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void DebugMon_Handler(void)   { }

/* SysTick: HAL timebase tick increment */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* TIM3 overflow — delegates to HAL which calls
 * HAL_TIM_PeriodElapsedCallback() implemented in led.c */
void TIM3_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim3);
}

/* USB OTG FS interrupt */
void OTG_FS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}

/* USART1 interrupt — C4001 mmWave sensor RX */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}
