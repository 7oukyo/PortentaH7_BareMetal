/**
 * @file startup_stm32f411cetx.s
 * @brief STM32F411CE Cortex-M4 startup — vector table + Reset_Handler.
 *
 * Boot sequence:
 *   1. Load stack pointer from _estack
 *   2. Call SystemInit() (FPU enable, VTOR)
 *   3. Copy .data from flash to SRAM
 *   4. Zero .bss
 *   5. Call __libc_init_array() (static constructors)
 *   6. Call main()
 */

    .syntax unified
    .cpu    cortex-m4
    .fpu    fpv4-sp-d16
    .thumb

/* Start address for the initialization values of the .data section */
.word _sidata
/* Start address for the .data section */
.word _sdata
/* End address for the .data section */
.word _edata
/* Start address for the .bss section */
.word _sbss
/* End address for the .bss section */
.word _ebss

/**
 * @brief Reset_Handler — entry point after reset.
 */
    .section .text.Reset_Handler
    .weak   Reset_Handler
    .type   Reset_Handler, %function
Reset_Handler:
    ldr   sp, =_estack      /* set stack pointer */

    /* Call SystemInit — enables FPU, sets VTOR */
    bl    SystemInit

    /* Copy .data from flash to SRAM */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_sidata
    movs  r3, #0
    b     LoopCopyDataInit

CopyDataInit:
    ldr   r4, [r2, r3]
    str   r4, [r0, r3]
    adds  r3, r3, #4

LoopCopyDataInit:
    adds  r4, r0, r3
    cmp   r4, r1
    bcc   CopyDataInit

    /* Zero fill .bss */
    ldr   r2, =_sbss
    ldr   r4, =_ebss
    movs  r3, #0
    b     LoopFillZerobss

FillZerobss:
    str   r3, [r2]
    adds  r2, r2, #4

LoopFillZerobss:
    cmp   r2, r4
    bcc   FillZerobss

    /* Call static constructors */
    bl    __libc_init_array

    /* Call application entry point */
    bl    main
    bx    lr

    .size Reset_Handler, .-Reset_Handler

/**
 * @brief Default handler for unimplemented interrupts — infinite loop.
 */
    .section .text.Default_Handler, "ax", %progbits
Default_Handler:
Infinite_Loop:
    b     Infinite_Loop
    .size Default_Handler, .-Default_Handler

/**
 * @brief Interrupt vector table for STM32F411xE.
 *        Stored in .isr_vector section (first thing in flash).
 */
    .section .isr_vector, "a", %progbits
    .type  g_pfnVectors, %object
    .size  g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
    /* Cortex-M4 system exceptions */
    .word _estack                       /* 0x000  Initial stack pointer */
    .word Reset_Handler                 /* 0x004  Reset */
    .word NMI_Handler                   /* 0x008  NMI */
    .word HardFault_Handler             /* 0x00C  Hard fault */
    .word MemManage_Handler             /* 0x010  Memory management fault */
    .word BusFault_Handler              /* 0x014  Bus fault */
    .word UsageFault_Handler            /* 0x018  Usage fault */
    .word 0                             /* 0x01C  Reserved */
    .word 0                             /* 0x020  Reserved */
    .word 0                             /* 0x024  Reserved */
    .word 0                             /* 0x028  Reserved */
    .word SVC_Handler                   /* 0x02C  SVCall */
    .word DebugMon_Handler              /* 0x030  Debug monitor */
    .word 0                             /* 0x034  Reserved */
    .word PendSV_Handler                /* 0x038  PendSV */
    .word SysTick_Handler               /* 0x03C  SysTick */

    /* STM32F411 device-specific interrupts */
    .word WWDG_IRQHandler               /* 0  Window watchdog */
    .word PVD_IRQHandler                /* 1  PVD through EXTI */
    .word TAMP_STAMP_IRQHandler         /* 2  Tamper + timestamp */
    .word RTC_WKUP_IRQHandler           /* 3  RTC wakeup */
    .word FLASH_IRQHandler              /* 4  Flash */
    .word RCC_IRQHandler                /* 5  RCC */
    .word EXTI0_IRQHandler              /* 6  EXTI line 0 */
    .word EXTI1_IRQHandler              /* 7  EXTI line 1 */
    .word EXTI2_IRQHandler              /* 8  EXTI line 2 */
    .word EXTI3_IRQHandler              /* 9  EXTI line 3 */
    .word EXTI4_IRQHandler              /* 10 EXTI line 4 */
    .word DMA1_Stream0_IRQHandler       /* 11 DMA1 stream 0 */
    .word DMA1_Stream1_IRQHandler       /* 12 DMA1 stream 1 */
    .word DMA1_Stream2_IRQHandler       /* 13 DMA1 stream 2 */
    .word DMA1_Stream3_IRQHandler       /* 14 DMA1 stream 3 */
    .word DMA1_Stream4_IRQHandler       /* 15 DMA1 stream 4 */
    .word DMA1_Stream5_IRQHandler       /* 16 DMA1 stream 5 */
    .word DMA1_Stream6_IRQHandler       /* 17 DMA1 stream 6 */
    .word ADC_IRQHandler                /* 18 ADC */
    .word 0                             /* 19 Reserved */
    .word 0                             /* 20 Reserved */
    .word 0                             /* 21 Reserved */
    .word 0                             /* 22 Reserved */
    .word EXTI9_5_IRQHandler            /* 23 EXTI lines 5-9 */
    .word TIM1_BRK_TIM9_IRQHandler      /* 24 TIM1 break + TIM9 */
    .word TIM1_UP_TIM10_IRQHandler      /* 25 TIM1 update + TIM10 */
    .word TIM1_TRG_COM_TIM11_IRQHandler /* 26 TIM1 trigger + TIM11 */
    .word TIM1_CC_IRQHandler            /* 27 TIM1 capture compare */
    .word TIM2_IRQHandler               /* 28 TIM2 */
    .word TIM3_IRQHandler               /* 29 TIM3 */
    .word TIM4_IRQHandler               /* 30 TIM4 */
    .word I2C1_EV_IRQHandler            /* 31 I2C1 event */
    .word I2C1_ER_IRQHandler            /* 32 I2C1 error */
    .word I2C2_EV_IRQHandler            /* 33 I2C2 event */
    .word I2C2_ER_IRQHandler            /* 34 I2C2 error */
    .word SPI1_IRQHandler               /* 35 SPI1 */
    .word SPI2_IRQHandler               /* 36 SPI2 */
    .word USART1_IRQHandler             /* 37 USART1 */
    .word USART2_IRQHandler             /* 38 USART2 */
    .word 0                             /* 39 Reserved */
    .word EXTI15_10_IRQHandler          /* 40 EXTI lines 10-15 */
    .word RTC_Alarm_IRQHandler          /* 41 RTC alarm A/B */
    .word OTG_FS_WKUP_IRQHandler        /* 42 USB OTG FS wakeup */
    .word 0                             /* 43 Reserved */
    .word 0                             /* 44 Reserved */
    .word 0                             /* 45 Reserved */
    .word 0                             /* 46 Reserved */
    .word DMA1_Stream7_IRQHandler       /* 47 DMA1 stream 7 */
    .word 0                             /* 48 Reserved */
    .word SDIO_IRQHandler               /* 49 SDIO */
    .word TIM5_IRQHandler               /* 50 TIM5 */
    .word SPI3_IRQHandler               /* 51 SPI3 */
    .word 0                             /* 52 Reserved */
    .word 0                             /* 53 Reserved */
    .word 0                             /* 54 Reserved */
    .word 0                             /* 55 Reserved */
    .word DMA2_Stream0_IRQHandler       /* 56 DMA2 stream 0 */
    .word DMA2_Stream1_IRQHandler       /* 57 DMA2 stream 1 */
    .word DMA2_Stream2_IRQHandler       /* 58 DMA2 stream 2 */
    .word DMA2_Stream3_IRQHandler       /* 59 DMA2 stream 3 */
    .word DMA2_Stream4_IRQHandler       /* 60 DMA2 stream 4 */
    .word 0                             /* 61 Reserved */
    .word 0                             /* 62 Reserved */
    .word 0                             /* 63 Reserved */
    .word 0                             /* 64 Reserved */
    .word 0                             /* 65 Reserved */
    .word 0                             /* 66 Reserved */
    .word OTG_FS_IRQHandler             /* 67 USB OTG FS */
    .word DMA2_Stream5_IRQHandler       /* 68 DMA2 stream 5 */
    .word DMA2_Stream6_IRQHandler       /* 69 DMA2 stream 6 */
    .word DMA2_Stream7_IRQHandler       /* 70 DMA2 stream 7 */
    .word USART6_IRQHandler             /* 71 USART6 */
    .word I2C3_EV_IRQHandler            /* 72 I2C3 event */
    .word I2C3_ER_IRQHandler            /* 73 I2C3 error */
    .word 0                             /* 74 Reserved */
    .word 0                             /* 75 Reserved */
    .word 0                             /* 76 Reserved */
    .word 0                             /* 77 Reserved */
    .word 0                             /* 78 Reserved */
    .word 0                             /* 79 Reserved */
    .word 0                             /* 80 Reserved */
    .word FPU_IRQHandler                /* 81 FPU */
    .word 0                             /* 82 Reserved */
    .word 0                             /* 83 Reserved */
    .word SPI4_IRQHandler               /* 84 SPI4 */
    .word SPI5_IRQHandler               /* 85 SPI5 */

/**
 * Weak aliases — all unimplemented handlers point to Default_Handler.
 */
    .weak      NMI_Handler
    .thumb_set NMI_Handler, Default_Handler

    .weak      HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler

    .weak      MemManage_Handler
    .thumb_set MemManage_Handler, Default_Handler

    .weak      BusFault_Handler
    .thumb_set BusFault_Handler, Default_Handler

    .weak      UsageFault_Handler
    .thumb_set UsageFault_Handler, Default_Handler

    .weak      SVC_Handler
    .thumb_set SVC_Handler, Default_Handler

    .weak      DebugMon_Handler
    .thumb_set DebugMon_Handler, Default_Handler

    .weak      PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler

    .weak      SysTick_Handler
    .thumb_set SysTick_Handler, Default_Handler

    .weak      WWDG_IRQHandler
    .thumb_set WWDG_IRQHandler, Default_Handler

    .weak      PVD_IRQHandler
    .thumb_set PVD_IRQHandler, Default_Handler

    .weak      TAMP_STAMP_IRQHandler
    .thumb_set TAMP_STAMP_IRQHandler, Default_Handler

    .weak      RTC_WKUP_IRQHandler
    .thumb_set RTC_WKUP_IRQHandler, Default_Handler

    .weak      FLASH_IRQHandler
    .thumb_set FLASH_IRQHandler, Default_Handler

    .weak      RCC_IRQHandler
    .thumb_set RCC_IRQHandler, Default_Handler

    .weak      EXTI0_IRQHandler
    .thumb_set EXTI0_IRQHandler, Default_Handler

    .weak      EXTI1_IRQHandler
    .thumb_set EXTI1_IRQHandler, Default_Handler

    .weak      EXTI2_IRQHandler
    .thumb_set EXTI2_IRQHandler, Default_Handler

    .weak      EXTI3_IRQHandler
    .thumb_set EXTI3_IRQHandler, Default_Handler

    .weak      EXTI4_IRQHandler
    .thumb_set EXTI4_IRQHandler, Default_Handler

    .weak      DMA1_Stream0_IRQHandler
    .thumb_set DMA1_Stream0_IRQHandler, Default_Handler

    .weak      DMA1_Stream1_IRQHandler
    .thumb_set DMA1_Stream1_IRQHandler, Default_Handler

    .weak      DMA1_Stream2_IRQHandler
    .thumb_set DMA1_Stream2_IRQHandler, Default_Handler

    .weak      DMA1_Stream3_IRQHandler
    .thumb_set DMA1_Stream3_IRQHandler, Default_Handler

    .weak      DMA1_Stream4_IRQHandler
    .thumb_set DMA1_Stream4_IRQHandler, Default_Handler

    .weak      DMA1_Stream5_IRQHandler
    .thumb_set DMA1_Stream5_IRQHandler, Default_Handler

    .weak      DMA1_Stream6_IRQHandler
    .thumb_set DMA1_Stream6_IRQHandler, Default_Handler

    .weak      ADC_IRQHandler
    .thumb_set ADC_IRQHandler, Default_Handler

    .weak      EXTI9_5_IRQHandler
    .thumb_set EXTI9_5_IRQHandler, Default_Handler

    .weak      TIM1_BRK_TIM9_IRQHandler
    .thumb_set TIM1_BRK_TIM9_IRQHandler, Default_Handler

    .weak      TIM1_UP_TIM10_IRQHandler
    .thumb_set TIM1_UP_TIM10_IRQHandler, Default_Handler

    .weak      TIM1_TRG_COM_TIM11_IRQHandler
    .thumb_set TIM1_TRG_COM_TIM11_IRQHandler, Default_Handler

    .weak      TIM1_CC_IRQHandler
    .thumb_set TIM1_CC_IRQHandler, Default_Handler

    .weak      TIM2_IRQHandler
    .thumb_set TIM2_IRQHandler, Default_Handler

    .weak      TIM3_IRQHandler
    .thumb_set TIM3_IRQHandler, Default_Handler

    .weak      TIM4_IRQHandler
    .thumb_set TIM4_IRQHandler, Default_Handler

    .weak      I2C1_EV_IRQHandler
    .thumb_set I2C1_EV_IRQHandler, Default_Handler

    .weak      I2C1_ER_IRQHandler
    .thumb_set I2C1_ER_IRQHandler, Default_Handler

    .weak      I2C2_EV_IRQHandler
    .thumb_set I2C2_EV_IRQHandler, Default_Handler

    .weak      I2C2_ER_IRQHandler
    .thumb_set I2C2_ER_IRQHandler, Default_Handler

    .weak      SPI1_IRQHandler
    .thumb_set SPI1_IRQHandler, Default_Handler

    .weak      SPI2_IRQHandler
    .thumb_set SPI2_IRQHandler, Default_Handler

    .weak      USART1_IRQHandler
    .thumb_set USART1_IRQHandler, Default_Handler

    .weak      USART2_IRQHandler
    .thumb_set USART2_IRQHandler, Default_Handler

    .weak      EXTI15_10_IRQHandler
    .thumb_set EXTI15_10_IRQHandler, Default_Handler

    .weak      RTC_Alarm_IRQHandler
    .thumb_set RTC_Alarm_IRQHandler, Default_Handler

    .weak      OTG_FS_WKUP_IRQHandler
    .thumb_set OTG_FS_WKUP_IRQHandler, Default_Handler

    .weak      DMA1_Stream7_IRQHandler
    .thumb_set DMA1_Stream7_IRQHandler, Default_Handler

    .weak      SDIO_IRQHandler
    .thumb_set SDIO_IRQHandler, Default_Handler

    .weak      TIM5_IRQHandler
    .thumb_set TIM5_IRQHandler, Default_Handler

    .weak      SPI3_IRQHandler
    .thumb_set SPI3_IRQHandler, Default_Handler

    .weak      DMA2_Stream0_IRQHandler
    .thumb_set DMA2_Stream0_IRQHandler, Default_Handler

    .weak      DMA2_Stream1_IRQHandler
    .thumb_set DMA2_Stream1_IRQHandler, Default_Handler

    .weak      DMA2_Stream2_IRQHandler
    .thumb_set DMA2_Stream2_IRQHandler, Default_Handler

    .weak      DMA2_Stream3_IRQHandler
    .thumb_set DMA2_Stream3_IRQHandler, Default_Handler

    .weak      DMA2_Stream4_IRQHandler
    .thumb_set DMA2_Stream4_IRQHandler, Default_Handler

    .weak      OTG_FS_IRQHandler
    .thumb_set OTG_FS_IRQHandler, Default_Handler

    .weak      DMA2_Stream5_IRQHandler
    .thumb_set DMA2_Stream5_IRQHandler, Default_Handler

    .weak      DMA2_Stream6_IRQHandler
    .thumb_set DMA2_Stream6_IRQHandler, Default_Handler

    .weak      DMA2_Stream7_IRQHandler
    .thumb_set DMA2_Stream7_IRQHandler, Default_Handler

    .weak      USART6_IRQHandler
    .thumb_set USART6_IRQHandler, Default_Handler

    .weak      I2C3_EV_IRQHandler
    .thumb_set I2C3_EV_IRQHandler, Default_Handler

    .weak      I2C3_ER_IRQHandler
    .thumb_set I2C3_ER_IRQHandler, Default_Handler

    .weak      FPU_IRQHandler
    .thumb_set FPU_IRQHandler, Default_Handler

    .weak      SPI4_IRQHandler
    .thumb_set SPI4_IRQHandler, Default_Handler

    .weak      SPI5_IRQHandler
    .thumb_set SPI5_IRQHandler, Default_Handler
