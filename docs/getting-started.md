# Getting Started: Bare-Metal STM32 on Arduino Portenta H7

A beginner's guide to understanding everything happening in this firmware — from power-on to blinking an LED. Written for someone with zero STM32 or MCU experience.

---

## Table of Contents

1. [What is an MCU?](#1-what-is-an-mcu)
2. [The STM32H747 on the Portenta H7](#2-the-stm32h747-on-the-portenta-h7)
3. [Memory: Where Things Live](#3-memory-where-things-live)
4. [What Happens at Power-On](#4-what-happens-at-power-on)
5. [The Startup File (Assembly)](#5-the-startup-file-assembly)
6. [The Linker Script: Mapping Memory](#6-the-linker-script-mapping-memory)
7. [SystemInit: First C Code to Run](#7-systeminit-first-c-code-to-run)
8. [main.c: Your Application](#8-mainc-your-application)
9. [Clocks: The Heartbeat of Everything](#9-clocks-the-heartbeat-of-everything)
10. [GPIO: Controlling Pins](#10-gpio-controlling-pins)
11. [The HAL: ST's Hardware Abstraction Layer](#11-the-hal-sts-hardware-abstraction-layer)
12. [MSP Callbacks: The HAL's Hook System](#12-msp-callbacks-the-hals-hook-system)
13. [Interrupts: Reacting to Hardware Events](#13-interrupts-reacting-to-hardware-events)
14. [Peripherals: Talking to the Outside World](#14-peripherals-talking-to-the-outside-world)
15. [The Build Process: From C to Binary](#15-the-build-process-from-c-to-binary)
16. [Flashing: Getting Code onto the Chip](#16-flashing-getting-code-onto-the-chip)
17. [Debugging: When Things Go Wrong](#17-debugging-when-things-go-wrong)
18. [The Portenta's Special Needs](#18-the-portentas-special-needs)
19. [Project File Map](#19-project-file-map)
20. [Common Gotchas](#20-common-gotchas)
21. [DMA: Direct Memory Access](#21-dma-direct-memory-access)
22. [Bitwise Operations: The Language of Hardware](#22-bitwise-operations-the-language-of-hardware)
23. [How to Read a Datasheet and Reference Manual](#23-how-to-read-a-datasheet-and-reference-manual)
24. [Ring Buffers: The Interrupt-Safe FIFO](#24-ring-buffers-the-interrupt-safe-fifo)
25. [The Bus Architecture: AXI, AHB, and APB](#25-the-bus-architecture-axi-ahb-and-apb)
26. [Fixed-Point Math: Floats Without the Float](#26-fixed-point-math-floats-without-the-float)
27. [State Machines: Structuring Embedded Logic](#27-state-machines-structuring-embedded-logic)
28. [Debugging Techniques Beyond the Basics](#28-debugging-techniques-beyond-the-basics)
29. [Memory-Mapped I/O: How Peripherals Actually Work](#29-memory-mapped-io-how-peripherals-actually-work)
30. [Common Embedded Patterns](#30-common-embedded-patterns)
31. [Toolchain Deep Dive: What Each Tool Does](#31-toolchain-deep-dive-what-each-tool-does)
32. [Glossary](#32-glossary)

---

## 1. What is an MCU?

A microcontroller (MCU) is a tiny computer on a single chip. Unlike your PC, it has no operating system, no hard drive, and no display. It has:

- **Processor (CPU)** — executes your code, one instruction at a time
- **Flash** — non-volatile memory where your compiled code lives (survives power off)
- **RAM** — volatile memory for variables while your code runs (lost on power off)
- **Peripherals** — built-in hardware blocks for talking to the outside world (GPIO pins, UART, I2C, SPI, USB, timers, ADC, etc.)

When you apply power, the processor starts executing instructions from flash. That's it. There's no bootloader menu, no kernel, no filesystem. Your code *is* the entire system.

### Bare-metal vs. Arduino/RTOS

- **Arduino**: Hides everything behind `setup()` and `loop()`. You never see clocks, interrupts, or memory maps. Easy but limited.
- **RTOS** (FreeRTOS, Zephyr): Adds a mini operating system with tasks, scheduling, and synchronization. Good for complex multi-threaded work.
- **Bare-metal** (this project): You control everything directly. You configure every clock, every pin, every interrupt. Maximum control, maximum understanding, maximum responsibility.

---

## 2. The STM32H747 on the Portenta H7

Our chip is the **STM32H747XIH6**. Let's decode that:

| Part | Meaning |
|------|---------|
| STM32 | ST Microelectronics' 32-bit ARM MCU family |
| H7 | High-performance family (Cortex-M7 core) |
| 47 | Dual-core variant (Cortex-M7 + Cortex-M4) |
| XI | 2 MB flash |
| H | BGA package (tiny ball-grid underneath the chip) |
| 6 | Temperature range (-40 to +85C) |

The key specs:

- **Cortex-M7 core**: Runs at up to 480 MHz. This is what we use.
- **Cortex-M4 core**: Runs at up to 240 MHz. We have this **disabled** — we don't have firmware for it and leaving stale code in its flash bank causes problems.
- **2 MB flash**: Where our compiled code lives
- **1 MB total RAM**: Split across multiple regions (more on this below)
- **Many peripherals**: UART, SPI, I2C, USB, Ethernet, timers, ADC, DAC, and more

### What is ARM Cortex-M7?

ARM doesn't make chips — they design CPU architectures and license them to companies like ST. "Cortex-M7" is a CPU design optimized for microcontrollers:

- 32-bit processor (handles 32 bits of data at once)
- Thumb-2 instruction set (compact, efficient instructions)
- Hardware floating-point unit (FPv5-D16 — can do decimal math in hardware)
- I-Cache and D-Cache (small fast memories that speed up flash/RAM access)
- NVIC (Nested Vectored Interrupt Controller — manages hardware interrupts)

---

## 3. Memory: Where Things Live

This is the most important concept to understand. Unlike a PC where you just have "RAM" and "disk", an MCU has multiple distinct memory regions, each at a specific address and with different characteristics.

### The Memory Map

Every memory region has a fixed address range. When your code reads address `0x08000000`, it's reading the start of flash. When it writes to `0x24000000`, it's writing to AXI SRAM. The hardware routes the address to the correct physical memory.

```
Address Range          Region      Size    What lives here
---------------------------------------------------------------------
0x0000_0000-0x0000_FFFF  ITCMRAM    64 KB   Tightly-coupled instruction RAM (fast, unused)
0x0800_0000-0x081F_FFFF  FLASH      2 MB    Your compiled code + constants
0x2000_0000-0x2001_FFFF  DTCMRAM    128 KB  Tightly-coupled data RAM (fast, unused)
0x2400_0000-0x2407_FFFF  AXI SRAM   512 KB  Main RAM: variables, heap, stack
0x3000_0000-0x3004_7FFF  SRAM D2    288 KB  DMA buffers, shared memory
0x3800_0000-0x3800_FFFF  SRAM D3    64 KB   Available for low-power domain
0xC000_0000-0xC07F_FFFF  SDRAM      8 MB    External (needs FMC init, not used yet)
```

### Why So Many RAMs?

The STM32H7 has a complex bus architecture with multiple "domains" (D1, D2, D3). Each domain has its own RAM and bus interconnect:

- **AXI SRAM** (D1, 512 KB): Connected to the main AXI bus. This is where we put all our variables, heap, and stack. It's the "general purpose" RAM.
- **SRAM D2** (288 KB): Connected to the D2 domain (AHB bus). DMA controllers can access this efficiently. Used for USB buffers, Ethernet buffers, etc.
- **SRAM D3** (64 KB): Connected to the D3 domain. Can remain powered in low-power modes.
- **DTCMRAM** (128 KB): Zero-wait-state RAM directly coupled to the CPU. Fastest possible access but only the CPU can use it (no DMA). Great for stack and time-critical variables.
- **ITCMRAM** (64 KB): Same as DTCMRAM but for code. You can copy functions here for fastest execution.

We currently use AXI SRAM for everything. DTCMRAM/ITCMRAM would be faster but we haven't optimized for that yet.

### What Goes in Flash vs. RAM?

The compiler and linker decide this:

| What | Where | Why |
|------|-------|-----|
| Code (functions) | Flash | Executed directly from flash. Doesn't change at runtime. |
| `const` data | Flash | Read-only. String literals, lookup tables, etc. |
| Global/static variables with initial values | Flash AND RAM | Initial values stored in flash, **copied to RAM** at startup. |
| Global/static variables without initial values | RAM (.bss) | Zero-filled at startup. |
| Local variables | RAM (stack) | Created when function is called, destroyed when it returns. |
| `malloc`/dynamically allocated | RAM (heap) | We avoid this on bare-metal (no `_sbrk` provided). |

### The Stack

The stack is a region of RAM that grows **downward** (from high addresses to low). Every time you call a function, the CPU pushes the return address and local variables onto the stack. When the function returns, they're popped off.

In our linker script:
```
_estack = ORIGIN(RAM) + LENGTH(RAM);   /* 0x24080000 — top of AXI SRAM */
```

The stack starts at the very top of AXI SRAM and grows downward. If you use too much stack (deep recursion, huge local arrays), it overflows into the heap/BSS and corrupts memory — a common embedded bug with no safety net.

---

## 4. What Happens at Power-On

Here's the exact sequence from power applied to your `main()` running:

```
Power on
  |
  v
MCU hardware reset
  |  - All registers set to default values
  |  - CPU reads address 0x08000000 -> gets initial stack pointer value
  |  - CPU reads address 0x08000004 -> gets Reset_Handler address
  |  - CPU jumps to Reset_Handler
  |
  v
Reset_Handler (startup_stm32h747xihx.s)
  |  - Sets stack pointer
  |  - Calls SystemInit() — resets clocks, enables FPU, sets vector table
  |  - Copies .data from flash to RAM (initialized globals)
  |  - Zeros .bss in RAM (uninitialized globals)
  |  - Calls __libc_init_array (C library init)
  |  - Calls main()
  |
  v
main() (your code)
  |  - Configures PMIC, clocks, GPIO, peripherals
  |  - Enters infinite while(1) loop
  |
  v
Your application runs forever (or until power loss / reset / fault)
```

The key insight: **the MCU reads the first two words from flash to know where the stack is and where to start executing**. This is why the vector table must be at the start of flash.

---

## 5. The Startup File (Assembly)

File: `startup/startup_stm32h747xihx.s`

This is the only assembly file in the project. It does three critical things:

### 5.1 The Vector Table

```asm
g_pfnVectors:
  .word  _estack            /* Initial stack pointer (address 0x08000000) */
  .word  Reset_Handler      /* Reset handler        (address 0x08000004) */
  .word  NMI_Handler        /* Non-maskable interrupt */
  .word  HardFault_Handler  /* Hard fault */
  .word  MemManage_Handler  /* Memory management fault */
  ...
  .word  UART4_IRQHandler   /* UART4 interrupt */
  .word  TIM6_DAC_IRQHandler /* TIM6 interrupt */
  ...
```

This is a table of function pointers placed at the very start of flash. The CPU hardware uses this table to:
1. Load the initial stack pointer (first entry)
2. Jump to `Reset_Handler` on power-on (second entry)
3. Jump to the correct handler when an interrupt fires (remaining entries)

Each entry corresponds to a specific interrupt number. The position in the table determines which interrupt it handles — this is defined by ARM and ST, not by us.

### 5.2 Weak Symbols

```asm
.weak      UART4_IRQHandler
.thumb_set UART4_IRQHandler,Default_Handler
```

Every interrupt handler is declared as "weak" — meaning it points to `Default_Handler` (an infinite loop) unless you provide your own implementation. When you write `void UART4_IRQHandler(void)` in your C code, the linker replaces the weak symbol with your function. This is how you "install" an interrupt handler — just define a C function with the right name.

### 5.3 Reset_Handler

```asm
Reset_Handler:
  ldr   sp, =_estack      /* 1. Set stack pointer */
  bl    SystemInit         /* 2. Basic hardware init */

  /* 3. Copy initialized data from flash to RAM */
  /* The .data section has initial values stored in flash. */
  /* This loop copies them to their runtime RAM addresses. */

  /* 4. Zero-fill the .bss section */
  /* Uninitialized globals must be zeroed per C standard. */

  bl    __libc_init_array  /* 5. C runtime init */
  bl    main               /* 6. YOUR CODE STARTS HERE */
```

The `.data` copy is necessary because global variables like `int counter = 42;` need their initial value (42) to be in RAM at runtime. The compiler stores 42 in flash, and the startup code copies it to the RAM address of `counter`.

The `.bss` zero-fill is required by the C standard — all uninitialized globals must be zero when `main()` starts.

---

## 6. The Linker Script: Mapping Memory

File: `linker/STM32H747XIHX_CM7.ld`

The linker script tells the linker (the program that combines all your compiled `.o` files into one executable) where to place everything in memory.

### Memory Regions

```
MEMORY
{
  FLASH    (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  RAM      (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
  RAM_D2   (xrw) : ORIGIN = 0x30000000, LENGTH = 288K
  ...
}
```

This defines the physical memory available. The flags mean:
- `r` = readable
- `w` = writable
- `x` = executable

Flash is `rx` (read + execute, not writable at runtime). RAM is `xrw` (read + write + execute).

### Sections

```
.isr_vector : { ... } >FLASH          /* Vector table goes first in flash */
.text :       { ... } >FLASH          /* Code goes in flash */
.rodata :     { ... } >FLASH          /* Read-only data in flash */
.data :       { ... } >RAM AT> FLASH  /* Variables in RAM, initial values in flash */
.bss :        { ... } >RAM            /* Zero-initialized variables in RAM */
```

The magic line is `.data : { ... } >RAM AT> FLASH`. This means:
- At runtime (VMA), `.data` is in RAM (so your code reads/writes RAM)
- At load time (LMA), `.data` is in flash (so the initial values survive power-off)
- The startup code copies from flash (LMA) to RAM (VMA) before `main()` runs

### Key Symbols

The linker script exports symbols that the startup code uses:

| Symbol | Meaning |
|--------|---------|
| `_estack` | Top of stack (end of AXI SRAM = `0x24080000`) |
| `_sidata` | Address of .data initial values in flash |
| `_sdata` / `_edata` | Start/end of .data in RAM |
| `_sbss` / `_ebss` | Start/end of .bss in RAM |

---

## 7. SystemInit: First C Code to Run

File: `src/system_stm32h7xx.c`

Called from `Reset_Handler` before `.data` copy (so it can't use initialized globals!). It:

1. **Enables the FPU** — The floating-point unit is disabled by default. Without this, any float operation causes a fault.
   ```c
   SCB->CPACR |= ((3UL << 20) | (3UL << 22));  /* Enable CP10 and CP11 */
   ```

2. **Resets all clocks to default** — Clears all PLL settings, switches to HSI (internal 64 MHz oscillator). This gives us a known starting point regardless of what was configured before.

3. **Disables FMC Bank1** — The Flexible Memory Controller is enabled by default after reset, and its speculative accesses can interfere with other peripherals.

4. **Sets the Vector Table location** — Tells the CPU where to find interrupt handlers:
   ```c
   SCB->VTOR = FLASH_BANK1_BASE;  /* 0x08000000 */
   ```

5. **Applies silicon errata workarounds** — Early STM32H7 revisions have hardware bugs that need software fixes.

After `SystemInit` returns, the startup code finishes copying `.data` and zeroing `.bss`, then calls `main()`.

---

## 8. main.c: Your Application

This is where your logic lives. Let's walk through our `main()` step by step:

### Step 1: PMIC Standby Pin

```c
__HAL_RCC_GPIOJ_CLK_ENABLE();
HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_0, GPIO_PIN_RESET);  /* PJ0 = LOW */
```

The Portenta H7 has an external power management IC (NXP PF1550). Its STANDBY pin is connected to PJ0. Pulling it LOW puts the PMIC in RUN mode so it can be configured. **This must be the very first thing** — before `HAL_Init()`, before anything.

### Step 2: HAL_Init()

```c
HAL_Init();
```

Initializes the HAL library:
- Configures flash prefetch and instruction cache settings
- Sets the SysTick timer to fire every 1ms (this is how `HAL_Delay()` works)
- Calls `HAL_MspInit()` (your board-level init callback)

After this call, `HAL_Delay()` and `HAL_GetTick()` work.

### Step 3: Enable I-Cache

```c
SCB_EnableICache();
```

The Cortex-M7 has instruction and data caches. Enabling I-Cache dramatically speeds up code execution from flash (flash is slow — 4 wait states at 480 MHz). We skip D-Cache because it breaks DMA-based peripherals unless you do careful cache maintenance (flushing/invalidating cache lines).

### Step 4: Enable External Oscillator

```c
HAL_GPIO_WritePin(GPIOH, GPIO_PIN_1, GPIO_PIN_SET);  /* PH1 HIGH = OSCEN */
HAL_Delay(10);
```

The Portenta H7 has a 25 MHz crystal oscillator that's gated by a GPIO pin (PH1). We drive it HIGH to enable the oscillator, then wait 10ms for it to stabilize. This crystal is much more accurate than the internal oscillator and is required for USB.

### Step 5: Configure Clocks

```c
SystemClock_Config();       /* PLL1: 25 MHz -> 480 MHz SYSCLK */
PeriphCommonClock_Config(); /* PLL2: for SPI peripherals */
```

See [Clocks section](#9-clocks-the-heartbeat-of-everything) below.

### Step 6: GPIO, PMIC, USB, Timers, Sensors

```c
GPIO_LEDs_Init();         /* Configure LED pins as outputs */
PMIC_Init();              /* Configure power rails via I2C */
/* USB PHY reset */
MX_USB_DEVICE_Init();     /* USB CDC Virtual COM Port */
LedPwm_Init();            /* TIM6 for LED blink timing */
C4001_Init();             /* mmWave sensor on UART4 */
```

Each of these initializes a peripheral. Order matters — the PMIC must be configured before USB (because the USB PHY is powered by PMIC rails).

### Step 7: Main Loop

```c
while (1)
{
    C4001_Poll();
}
```

The main loop runs forever. There's no `return` from `main()` on an MCU — if you did, the CPU would execute garbage instructions and crash.

---

## 9. Clocks: The Heartbeat of Everything

Clocks are arguably the most important concept in MCU programming. Every peripheral, every bus, every timer runs at a specific clock frequency. Get the clocks wrong and nothing works.

### Why Clocks Matter

- A UART configured for 9600 baud needs an accurate clock to generate the right timing
- A timer counting at 10 kHz needs to know its input clock to set the right prescaler
- USB requires exactly 48 MHz — any drift and the connection fails
- Flash memory has a maximum access speed — if the CPU clock is too fast, you need "wait states" (the CPU pauses while flash responds)

### Clock Sources

The STM32H7 has multiple clock sources:

| Source | Frequency | Accuracy | Used for |
|--------|-----------|----------|----------|
| HSI | 64 MHz | ~1% | Default after reset. Internal RC oscillator. |
| HSE | 25 MHz | Very accurate | External crystal. Required for USB. |
| LSI | 32 kHz | ~5% | Low-speed internal. Watchdog timer. |
| LSE | 32.768 kHz | Very accurate | External crystal for RTC. |
| CSI | 4 MHz | ~1% | Low-power internal. |

On the Portenta, the HSE is a 25 MHz crystal oscillator gated by PH1. After enabling it, we use it as the source for the PLLs.

### PLLs: Frequency Multipliers

A PLL (Phase-Locked Loop) takes a low input frequency and multiplies it to a higher frequency:

```
PLL output = (Input / M) * N / P

Our PLL1: (25 MHz / 5) * 192 / 2 = 480 MHz
           ^^^^^^^^   ^^^   ^^^
           M divider   N     P divider
           (input to   multiplier  (output
            5 MHz)     (to 960 MHz) to 480 MHz)
```

We have three PLLs:

| PLL | Input | Output | Used for |
|-----|-------|--------|----------|
| PLL1 | HSE 25 MHz | 480 MHz | CPU (SYSCLK) |
| PLL2 | HSE 25 MHz | 120/180 MHz | SPI peripherals |
| PLL3 | HSE 25 MHz | 48 MHz | USB clock |

### Clock Tree (Simplified)

```
HSE 25 MHz
  |
  +---> PLL1 ---> 480 MHz SYSCLK (CPU)
  |                  |
  |                  +---> /2 = 240 MHz HCLK (AHB bus, RAM, DMA)
  |                           |
  |                           +---> /2 = 120 MHz APB1 (UART, I2C, TIM6)
  |                           |         |
  |                           |         +---> x2 = 240 MHz (timer clocks)
  |                           |
  |                           +---> /2 = 120 MHz APB2 (SPI, USART1)
  |
  +---> PLL2 ---> 120 MHz SPI2/3 clock, 180 MHz SPI4/5 clock
  |
  +---> PLL3 ---> 48 MHz USB clock
```

The "x2" on timer clocks is a gotcha: when the APB prescaler is >1, the hardware automatically doubles the timer clock. So APB1 at 120 MHz with prescaler=2 gives timers 240 MHz, not 120 MHz.

### Power and Voltage Scaling

At 480 MHz, the CPU needs the highest voltage to run stably. We configure this in `SystemClock_Config()`:

```c
HAL_PWREx_ConfigSupply(PWR_SMPS_1V8_SUPPLIES_LDO);      /* Use SMPS + LDO */
__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);  /* VOS0 = max performance */
while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}          /* Wait until voltage is stable */
```

You must set the voltage scale **before** increasing the clock frequency, and you must set the flash wait states to match the clock speed. Getting this wrong causes random crashes or hard faults.

---

## 10. GPIO: Controlling Pins

GPIO (General-Purpose Input/Output) is the simplest peripheral — it lets you set pins high or low (output) or read whether they're high or low (input).

### Pin Naming

STM32 pins are organized into ports: GPIOA, GPIOB, ... GPIOK. Each port has up to 16 pins (0-15). So "PK6" means Port K, Pin 6.

### Using a GPIO Pin

Every GPIO pin must be configured before use:

```c
/* Step 1: Enable the port's clock (GPIOK for our LEDs) */
__HAL_RCC_GPIOK_CLK_ENABLE();

/* Step 2: Configure the pin */
GPIO_InitTypeDef gpio = {0};
gpio.Pin   = GPIO_PIN_6;           /* Which pin(s) */
gpio.Mode  = GPIO_MODE_OUTPUT_PP;  /* Push-pull output */
gpio.Pull  = GPIO_NOPULL;          /* No pull-up or pull-down resistor */
gpio.Speed = GPIO_SPEED_FREQ_LOW;  /* Slew rate (LOW is fine for LEDs) */
HAL_GPIO_Init(GPIOK, &gpio);

/* Step 3: Use it */
HAL_GPIO_WritePin(GPIOK, GPIO_PIN_6, GPIO_PIN_RESET);  /* LOW (LED ON — active low) */
HAL_GPIO_WritePin(GPIOK, GPIO_PIN_6, GPIO_PIN_SET);    /* HIGH (LED OFF) */
```

### Clock Enable: The #1 Beginner Mistake

**Every peripheral needs its clock enabled before you can use it.** This includes GPIO ports. If you forget `__HAL_RCC_GPIOK_CLK_ENABLE()`, the GPIO registers are inaccessible and your `HAL_GPIO_Init()` silently does nothing. Your pin won't work and there's no error message.

This applies to everything: I2C needs `__HAL_RCC_I2C1_CLK_ENABLE()`, UART needs `__HAL_RCC_UART4_CLK_ENABLE()`, timers need their clock enabled, etc.

### Alternate Functions

Most pins can do more than just GPIO. A pin might be:
- GPIO (you control it directly)
- UART TX (the UART peripheral controls it)
- I2C SDA (the I2C peripheral controls it)
- Timer PWM output
- etc.

Which function a pin serves is selected by its **Alternate Function (AF)** number. For example, PA0 can be:
- GPIO (default)
- AF8 = UART4_TX

You configure this with:
```c
gpio.Mode      = GPIO_MODE_AF_PP;       /* Alternate function, push-pull */
gpio.Alternate = GPIO_AF8_UART4;        /* AF8 = UART4 */
```

The AF mappings are fixed by the chip design — you find them in the datasheet's alternate function table. Not every pin can do every function.

### Active Low

Our LEDs are **active low**: writing LOW (GPIO_PIN_RESET) turns them ON, HIGH (GPIO_PIN_SET) turns them OFF. This is because the LED anode is connected to 3.3V and the cathode goes to the GPIO pin — current flows (LED on) when the pin is low.

---

## 11. The HAL: ST's Hardware Abstraction Layer

ST provides a library called HAL (Hardware Abstraction Layer) that wraps the raw hardware registers in C functions. Instead of writing to cryptic register addresses, you call functions like `HAL_GPIO_WritePin()`.

### HAL vs. Register Access

**HAL approach:**
```c
HAL_GPIO_WritePin(GPIOK, GPIO_PIN_6, GPIO_PIN_RESET);
```

**Register approach (equivalent):**
```c
GPIOK->BSRR = (uint32_t)GPIO_PIN_6 << 16U;   /* Set bit in reset register */
```

Both do the same thing. HAL is more readable; registers are faster and smaller. We use HAL for init code and register access for performance-critical paths.

### HAL Module System

Each peripheral has its own HAL module that must be explicitly enabled:

```c
/* In stm32h7xx_hal_conf.h */
#define HAL_GPIO_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED     /* USB device */
```

If you forget to enable a module, the HAL functions for that peripheral won't exist and you'll get linker errors.

You also need to add the `.c` files to your Makefile:
```makefile
HAL_SRC = \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c
```

### HAL Init Pattern

Every peripheral follows the same pattern:

```c
/* 1. Declare a handle structure */
UART_HandleTypeDef huart4;

/* 2. Fill in the configuration */
huart4.Instance          = UART4;
huart4.Init.BaudRate     = 9600;
huart4.Init.WordLength   = UART_WORDLENGTH_8B;
huart4.Init.StopBits     = UART_STOPBITS_1;
huart4.Init.Parity       = UART_PARITY_NONE;
huart4.Init.Mode         = UART_MODE_TX_RX;

/* 3. Call HAL init — this also triggers the MSP callback */
HAL_UART_Init(&huart4);

/* 4. Use the peripheral */
HAL_UART_Transmit(&huart4, data, len, timeout);
```

The handle (`huart4`) stores both the configuration and the runtime state. You pass it to every HAL function.

---

## 12. MSP Callbacks: The HAL's Hook System

MSP stands for "MCU Support Package." It's the HAL's way of separating *what* a peripheral does from *how* it's wired on a specific board.

When you call `HAL_UART_Init()`, the HAL internally calls `HAL_UART_MspInit()` — a function **you** write that configures the GPIO pins, enables clocks, and sets up interrupts for that specific UART on your specific board.

```c
/* In stm32h7xx_hal_msp.c or c4001.c */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) return;

    /* Enable clocks */
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    /* Configure GPIO pins for UART4 */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_0;           /* PA0 = TX */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin       = GPIO_PIN_9;           /* PI9 = RX */
    gpio.Pull      = GPIO_PULLUP;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOI, &gpio);

    /* Enable interrupt */
    HAL_NVIC_SetPriority(UART4_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
}
```

This separation means the HAL driver code (`stm32h7xx_hal_uart.c`) never changes — it's the same for every STM32H7 board. Only your MSP code is board-specific.

---

## 13. Interrupts: Reacting to Hardware Events

Interrupts are the mechanism that lets hardware say "hey CPU, something happened!" without the CPU having to constantly check (poll).

### How Interrupts Work

1. A hardware event occurs (e.g., a byte arrives on UART4)
2. The peripheral sets an interrupt flag
3. The NVIC (interrupt controller) checks if that interrupt is enabled and its priority
4. If enabled, the CPU pauses what it's doing, saves its state on the stack
5. CPU looks up the handler address in the vector table
6. CPU jumps to your handler function
7. Your handler runs (should be short and fast!)
8. CPU restores its state and resumes what it was doing

### Writing an Interrupt Handler

```c
/* In stm32h7xx_it.c — the interrupt "traffic cop" */
void UART4_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart4);  /* Let HAL figure out what happened */
}
```

The function name **must** match the name in the vector table. `UART4_IRQHandler` replaces the weak default (infinite loop) defined in the startup file.

The HAL's `IRQHandler` function reads the peripheral's status registers to determine what happened (byte received? transmit complete? error?) and calls the appropriate callback:

```c
/* HAL calls this when a byte is received */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) return;
    /* Process the received byte */
    /* Re-arm for next byte */
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1);
}
```

### Interrupt Priorities

The NVIC supports priorities from 0 (highest) to 15 (lowest). Higher-priority interrupts can preempt lower-priority ones.

In our project:
- SysTick: priority 15 (lowest — just increments a counter)
- TIM6: priority 5 (medium — LED blink timing)
- UART4: priority 6 (medium — sensor data)
- USB: priority 15 (lowest — not latency-critical)

### SysTick: The System Heartbeat

SysTick is a special ARM core timer that fires every 1ms (configured by `HAL_Init()`). Its handler increments a tick counter:

```c
void SysTick_Handler(void)
{
    HAL_IncTick();  /* Increments a global counter every 1ms */
}
```

This is how `HAL_Delay(100)` knows to wait 100ms, and how `HAL_GetTick()` returns the uptime in milliseconds.

### Volatile: The Interrupt Keyword

When a variable is modified in an interrupt handler and read in the main loop, you **must** declare it `volatile`:

```c
static volatile uint8_t  blink_active = 0;   /* Modified by ISR, read by main */
```

Without `volatile`, the compiler might optimize away the read in the main loop because it doesn't see the variable change within the loop. `volatile` tells the compiler "this value can change at any time — always read it from memory."

---

## 14. Peripherals: Talking to the Outside World

### UART (Serial Communication)

UART sends data one bit at a time over two wires (TX and RX). Both sides must agree on the baud rate (bits per second), data bits, parity, and stop bits. Our C4001 sensor uses UART4 at 9600 baud, 8N1 (8 data bits, No parity, 1 stop bit).

```
MCU PA0 (TX) ----------> Sensor RX
MCU PI9 (RX) <---------- Sensor TX
                GND ---- GND (must be shared)
```

### I2C (Two-Wire Bus)

I2C uses two wires (SCL = clock, SDA = data) and supports multiple devices on the same bus, each with a unique address. Our PMIC is on I2C1 at address 0x08.

```
MCU PB6 (SCL) ---+--- PMIC SCL
                  |
MCU PB7 (SDA) ---+--- PMIC SDA
                  |
                 [Pull-up resistors to 3.3V]
```

### USB (Universal Serial Bus)

USB is complex. Our setup uses:
- An external USB3320 ULPI PHY chip (converts USB signals)
- 12 ULPI data/control pins from the MCU to the PHY
- The USB-C connector wired to the PHY
- A CDC (Communication Device Class) — makes the MCU appear as a virtual COM port on the PC

### Timers

Hardware timers count up (or down) at a configurable rate and can trigger interrupts on overflow. We use TIM6 as a simple counter:

```
Timer clock: 240 MHz
Prescaler:   2400     -> 240 MHz / 2400 = 100 kHz tick rate
Period:      10       -> 100 kHz / 10 = 10 kHz overflow rate
```

Every 0.1ms (10 kHz), the timer overflows and fires an interrupt. We use this to time a 50ms LED blink (500 ticks at 10 kHz = 50ms).

---

## 15. The Build Process: From C to Binary

```
                    Compile              Link                  Convert
 .c files  ------>  .o files  ------>  firmware.elf  ------>  firmware.bin
 (source)    gcc    (object)    gcc     (executable)  objcopy  (raw binary)
                                 |
                          linker script
                          tells where to
                          place everything
```

### Step by Step

1. **Compile** (gcc): Each `.c` file is compiled independently into a `.o` (object) file containing machine code, but with unresolved references to functions/variables in other files.

2. **Link** (gcc + linker script): All `.o` files are combined into one `.elf` file. The linker:
   - Resolves all cross-references (e.g., `main.c` calls `PMIC_Init()` in `pmic.c`)
   - Places code in flash and data in RAM per the linker script
   - Removes unused functions (`--gc-sections` with `-ffunction-sections`)
   - Reports memory usage

3. **Convert** (objcopy): The `.elf` file contains debug info, symbol tables, and metadata. `objcopy` extracts just the raw binary that goes into flash.

### Key Compiler Flags

```makefile
MCU = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
```

| Flag | Meaning |
|------|---------|
| `-mcpu=cortex-m7` | Generate instructions for Cortex-M7 |
| `-mthumb` | Use Thumb instruction set (compact, efficient) |
| `-mfpu=fpv5-d16` | Use the hardware floating-point unit |
| `-mfloat-abi=hard` | Pass float arguments in FPU registers (faster) |

```makefile
DEFS = -DUSE_HAL_DRIVER -DSTM32H747xx -DCORE_CM7
```

| Define | Purpose |
|--------|---------|
| `USE_HAL_DRIVER` | Enables HAL includes in CMSIS headers |
| `STM32H747xx` | Selects the correct register definitions for our chip |
| `CORE_CM7` | Tells shared code we're building for the M7 core |

### Why `nano.specs`?

```makefile
LDFLAGS = ... --specs=nano.specs ...
```

The standard C library (newlib) is huge and includes printf, malloc, file I/O, etc. `nano.specs` uses a stripped-down version (newlib-nano) that's much smaller. This is critical on MCUs where every kilobyte of flash counts.

Caveat: even nano.specs' `snprintf` pulls in heap management (`_sbrk`). That's why we use manual integer-to-string formatting in our code.

---

## 16. Flashing: Getting Code onto the Chip

"Flashing" means writing your compiled binary into the MCU's flash memory. We use an **ST-Link V3 Mini** debug probe connected via SWD (Serial Wire Debug) — a 2-wire debug protocol.

```
PC (USB) --> ST-Link V3 Mini --> SWD (2 wires) --> MCU Flash
             (debug probe)      (SWDIO + SWCLK)   (0x08000000)
```

### OpenOCD

OpenOCD is the software that talks to the ST-Link and controls the MCU:

```bash
openocd -f openocd.cfg \
  -c "program build/firmware.bin 0x08000000 verify reset exit"
```

This command:
1. Connects to the ST-Link
2. Halts the MCU
3. Erases the necessary flash sectors
4. Writes `firmware.bin` starting at address `0x08000000`
5. Reads it back to verify
6. Resets the MCU (which starts running your new code)
7. Disconnects

### No Bootloader

Unlike Arduino (which has a bootloader that accepts code over USB), we write directly to `0x08000000` — the very start of flash. Our vector table is the first thing there. The MCU runs our code directly from power-on with no intermediary.

---

## 17. Debugging: When Things Go Wrong

### SWD Debugging with GDB

The ST-Link doesn't just flash — it's also a debugger. OpenOCD starts a GDB server, and VS Code's Cortex-Debug extension connects to it. You can:

- Set breakpoints
- Step through code line by line
- Inspect variables and registers
- See the call stack

### Hard Faults

When the CPU encounters an unrecoverable error, it triggers a **HardFault**:

- Accessing invalid memory addresses
- Executing invalid instructions
- Stack overflow
- Unaligned memory access
- Division by zero (if enabled)

Our HardFault handler captures the faulting PC address so you can see where the crash happened in the debugger:

```c
void HardFault_Handler(void)
{
    __asm volatile (
        " mrs  r0, msp      \n"   /* Get stack pointer */
        " ldr  r1, [r0, #20]\n"   /* Read stacked PC */
        " bkpt #0           \n"   /* Trigger debugger breakpoint */
    );
    while (1) {}
}
```

### Common Crash Causes on STM32

| Symptom | Likely cause |
|---------|-------------|
| Immediate HardFault after reset | Wrong clock config, missing clock enable, bad flash wait states |
| HardFault after calling a function | Stack overflow, missing MSP init |
| Peripheral doesn't respond | Forgot to enable its clock |
| Intermittent crashes | D-Cache + DMA conflict, interrupt priority issue |
| Orange LED on Portenta | MCU is in bootloader mode (needs reflashing) |

---

## 18. The Portenta's Special Needs

The Arduino Portenta H7 is not a typical STM32 dev board. It has several unique requirements:

### PMIC (Power Management IC)

Most dev boards power everything from USB directly. The Portenta has an NXP PF1550 PMIC that controls individual power rails. On a cold boot, only the MCU core voltage and basic I/O are powered (OTP defaults). To use USB, SDRAM, Ethernet, or other peripherals, you must:

1. Configure PJ0 LOW (STANDBY pin) — very first thing
2. Initialize I2C1
3. Write registers to the PMIC to enable SW1, SW2, LDO1, LDO2, LDO3

### Oscillator Gate

The 25 MHz HSE crystal has a gate signal on PH1. You must drive PH1 HIGH and wait 10ms before the crystal is usable. Most boards don't have this gate.

### USB PHY

The USB-C connector doesn't connect directly to the MCU's USB pins. It goes through an external USB3320C ULPI PHY chip. This means:
- 12 GPIO pins configured as ULPI alternate function
- PHY must be reset via PJ4 (after PMIC powers it)
- Must use `PCD_SPEED_FULL`, not `PCD_SPEED_HIGH` (HS mode silently fails on this board)

### Dual Core

The STM32H747 has two cores, but we only use the CM7. The CM4 boot is disabled because stale code in flash bank 2 can interfere with the CM7 if the CM4 starts executing garbage.

---

## 19. Project File Map

```
PortentaH7_Claude/
|
|-- src/                          Application source code
|   |-- main.c                    Entry point, init sequence, main loop
|   |-- system_stm32h7xx.c       SystemInit (runs before main)
|   |-- stm32h7xx_it.c           All interrupt handlers
|   |-- stm32h7xx_hal_msp.c      MSP callbacks (I2C, TIM GPIO/clock setup)
|   |-- pmic.c                   PMIC register configuration via I2C1
|   |-- led_pwm.c                Green LED blink on USB RX (TIM6)
|   |-- c4001.c                  mmWave sensor driver (UART4)
|   |-- usb_device.c             USB CDC init wrapper
|   |-- usbd_conf.c              USB low-level config (PLL3, ULPI GPIOs)
|   |-- usbd_desc.c              USB device descriptors
|   |-- usbd_cdc_if.c            USB CDC callbacks (receive, transmit)
|
|-- include/                      Project headers
|   |-- main.h                    LED pin defines, Error_Handler declaration
|   |-- stm32h7xx_hal_conf.h     HAL module enables and clock values
|   |-- c4001.h                  Sensor API, pin defines, data structures
|   |-- led_pwm.h                LED blink API
|   |-- pmic.h                   PMIC_Init declaration
|   |-- usb_device.h             USB init declaration
|   |-- usbd_conf.h              USB constants
|   |-- usbd_desc.h              Descriptor declarations
|   |-- usbd_cdc_if.h            CDC transmit API
|
|-- startup/
|   |-- startup_stm32h747xihx.s  Vector table + Reset_Handler (assembly)
|
|-- linker/
|   |-- STM32H747XIHX_CM7.ld    Memory map + section placement
|
|-- drivers/                      ST-provided code (DO NOT MODIFY)
|   |-- CMSIS/                    ARM core definitions + STM32 register maps
|   |-- STM32H7xx_HAL_Driver/    HAL library source and headers
|
|-- middlewares/                   ST USB Device Library (DO NOT MODIFY)
|
|-- docs/                         Hardware notes, driver docs, this guide
|-- reference/                    Reference projects (read-only)
|-- build/                        Compiler output (gitignored)
|-- Makefile                      Build system
|-- openocd.cfg                   Debugger/flasher configuration
```

### How Files Relate

```
Power on
  |
  startup_stm32h747xihx.s      [vector table, .data copy, .bss zero]
  |
  system_stm32h7xx.c           [FPU enable, clock reset, VTOR]
  |
  main.c                       [init sequence, main loop]
  |   |
  |   +-- stm32h7xx_hal_msp.c  [GPIO/clock setup for I2C, TIM]
  |   +-- pmic.c               [PMIC I2C register writes]
  |   +-- usbd_conf.c          [USB PHY GPIO, PLL3, FIFO]
  |   +-- usb_device.c         [USB stack init]
  |   +-- led_pwm.c            [TIM6 ISR for LED blink]
  |   +-- c4001.c              [UART4 ISR for sensor + MSP]
  |
  stm32h7xx_it.c               [routes all IRQs to HAL handlers]
```

---

## 20. Common Gotchas

Things that will waste hours of your life if you don't know about them:

### Clock Enables
Every single peripheral (including GPIO ports!) needs its clock enabled before use. Forgetting this is silent — no error, just nothing works.

### Init Order
On the Portenta, order is critical:
1. PJ0 LOW (PMIC standby) — before HAL_Init
2. HAL_Init — before any HAL function
3. Oscillator enable (PH1) — before clock config
4. Clock config — before any peripheral that depends on accurate timing
5. PMIC init — before USB/SDRAM/Ethernet (they need PMIC power rails)
6. USB PHY reset — after PMIC has powered the PHY

### Flash Wait States
Flash memory is slow compared to the CPU. At 480 MHz, we need 4 wait states. If you change the CPU clock without updating wait states, the CPU reads garbage from flash and crashes.

### D-Cache + DMA
DMA (Direct Memory Access) lets peripherals read/write RAM without the CPU. But if D-Cache is enabled, the CPU's cached view of RAM and DMA's actual RAM view can be different. We disable D-Cache to avoid this. Enabling it requires manual cache maintenance (`SCB_CleanDCache_by_Addr` / `SCB_InvalidateDCache_by_Addr`).

### nano.specs and _sbrk
Using `printf`, `snprintf`, or `malloc` with nano.specs pulls in heap management code that calls `_sbrk()` — which we don't provide. The linker will error. Use manual string formatting instead.

### Volatile
Any variable shared between an interrupt handler and the main loop must be `volatile`. The compiler optimizes aggressively and will cache values in registers, missing changes made by the interrupt.

### Active Low Signals
LEDs, reset pins, and standby pins on the Portenta are often active low. Setting a pin LOW turns things ON. Read the schematic carefully.

### Weak Symbols
If you define a function like `void UART4_IRQHandler(void)` in your code but it never gets called, check that the function name matches the vector table entry exactly. A typo means the weak default (infinite loop) stays in place and your handler is never reached.

---

## 21. DMA: Direct Memory Access

DMA lets peripherals transfer data to/from RAM without involving the CPU. Instead of the CPU reading one byte from UART, storing it in RAM, reading the next byte, etc., you tell the DMA controller: "take the next 100 bytes from UART4's data register and put them at this RAM address." The CPU is free to do other work while the transfer happens.

### Why DMA Matters

Without DMA (our current UART4 approach):

```
Byte arrives at UART4
  -> Interrupt fires
  -> CPU stops what it's doing
  -> CPU reads the byte from UART data register
  -> CPU writes it to your buffer in RAM
  -> CPU returns from interrupt
  -> Repeat for every single byte
```

With DMA:

```
You configure: "DMA, move 100 bytes from UART4->DR to buffer[0..99]"
  -> DMA hardware moves each byte automatically
  -> CPU does other work
  -> DMA fires ONE interrupt when all 100 bytes are done
```

At 9600 baud this doesn't matter much. At 1 Mbaud or higher, byte-at-a-time interrupts would consume most of your CPU time.

### DMA on the STM32H7

The H7 has two DMA controllers (DMA1, DMA2) each with 8 streams. Each stream can be connected to a specific peripheral via a request mux (DMAMUX). The key gotcha: **DMA controllers can only access SRAM D2 (0x30000000) efficiently.** If your DMA buffer is in AXI SRAM (0x24000000), it works but may be slower and has cache coherency issues.

This is why our USB buffers are placed in SRAM D2 — the USB peripheral uses DMA internally.

### When to Use DMA

| Scenario | Use DMA? | Why |
|----------|----------|-----|
| UART at 9600 baud | No | Interrupt overhead is negligible |
| UART at 115200+ with long messages | Yes | Saves significant CPU time |
| SPI to an LCD at 10 MHz | Yes | Transfer huge frame buffers without CPU |
| ADC continuous conversion | Yes | Sample thousands of readings into a buffer |
| I2C to PMIC (occasional, short) | No | Simple, infrequent transfers |

### DMA + D-Cache: The Classic Trap

This is why we have D-Cache disabled. When the CPU writes data to RAM, D-Cache may hold the data in cache and not write it to physical RAM yet. When DMA reads that RAM address, it sees stale data. Going the other way: DMA writes new data to RAM, but the CPU reads the cached (old) version.

Solutions:

1. **Disable D-Cache** (our approach) — simple, costs ~10-20% performance
2. **Cache maintenance** — call `SCB_CleanDCache_by_Addr()` before DMA TX (flush cache to RAM), call `SCB_InvalidateDCache_by_Addr()` after DMA RX (discard cached values)
3. **Use non-cacheable memory** — place DMA buffers in a region marked as non-cacheable via MPU (Memory Protection Unit)

---

## 22. Bitwise Operations: The Language of Hardware

In embedded programming, you manipulate individual bits constantly. Every hardware register is a collection of bit fields, each controlling a different setting.

### The Operations

| Operation | Symbol | What it does | Example |
|-----------|--------|-------------|---------|
| AND | `&` | Keeps only bits that are 1 in both | `0b1100 & 0b1010 = 0b1000` |
| OR | `\|` | Sets bits that are 1 in either | `0b1100 \| 0b1010 = 0b1110` |
| XOR | `^` | Flips bits that are 1 in the mask | `0b1100 ^ 0b1010 = 0b0110` |
| NOT | `~` | Inverts all bits | `~0b1100 = 0b0011` (simplified) |
| Left shift | `<<` | Moves bits left N positions | `1 << 3 = 0b1000` (= 8) |
| Right shift | `>>` | Moves bits right N positions | `0b1000 >> 2 = 0b0010` (= 2) |

### Common Patterns

**Set a bit** (turn ON):
```c
register |= (1 << bit_position);

/* Example: Enable bit 5 */
RCC->AHB4ENR |= (1 << 5);   /* Enable GPIOF clock (bit 5 of AHB4ENR) */
```

**Clear a bit** (turn OFF):
```c
register &= ~(1 << bit_position);

/* Example: Clear bit 3 */
GPIOK->ODR &= ~(1 << 6);   /* Set PK6 low (clear bit 6 of output data register) */
```

**Toggle a bit** (flip):
```c
register ^= (1 << bit_position);

/* Example: Toggle bit 6 */
GPIOK->ODR ^= (1 << 6);    /* Toggle PK6 */
```

**Check a bit** (test):
```c
if (register & (1 << bit_position)) { /* bit is set */ }

/* Example: Is bit 2 set? */
if (GPIOK->IDR & (1 << 6)) { /* PK6 is high */ }
```

**Set a multi-bit field** (e.g., a 2-bit mode field):
```c
/* Clear the field first, then set the new value */
register &= ~(0x3 << field_position);    /* Clear 2 bits */
register |=  (new_value << field_position); /* Set new value */

/* Example: Set GPIO mode to AF (0b10) for pin 6 */
/* MODER register: 2 bits per pin, pin 6 starts at bit 12 */
GPIOK->MODER &= ~(0x3 << 12);   /* Clear pin 6 mode bits */
GPIOK->MODER |=  (0x2 << 12);   /* Set to AF mode (0b10) */
```

### Why HAL Hides This

The HAL does all this for you behind functions like `HAL_GPIO_Init()`. But when you read HAL source code, see register-level examples, or need performance, you'll encounter these patterns everywhere.

### The `1UL` Convention

You'll often see `1UL` instead of `1`:
```c
register |= (1UL << 31);   /* Correct: unsigned long, won't sign-extend */
register |= (1 << 31);     /* Dangerous: signed int, bit 31 is the sign bit */
```

On a 32-bit MCU, `1 << 31` is technically undefined behavior in C because it overflows a signed int. `1UL` ensures unsigned arithmetic.

---

## 23. How to Read a Datasheet and Reference Manual

This is the most important skill in embedded development — and the hardest to learn. Datasheets and reference manuals are dense, but they're the ultimate source of truth.

### The STM32 Documentation Hierarchy

| Document | What it tells you | When to use it |
|----------|-------------------|----------------|
| **Datasheet** (DS12923) | Pin assignments, AF table, electrical specs, package | "Which pins can UART4 use?" "What's the max current on a GPIO?" |
| **Reference Manual** (RM0399) | How every peripheral works in detail: registers, bit fields, operation modes | "How do I configure UART4's baud rate register?" "What bits control DMA?" |
| **Programming Manual** (PM0253) | Cortex-M7 core: instructions, NVIC, SCB, MPU, caches, FPU | "How do interrupt priorities work?" "How do I configure the MPU?" |
| **Errata** (ES0396) | Known silicon bugs and workarounds | "The chip does something weird that doesn't match the manual" |

### Reading a Register Description (RM0399 Style)

A typical register description looks like this:

```
USART_CR1 (Control Register 1)
Address offset: 0x00
Reset value: 0x0000 0000

Bits 31:29  Reserved
Bit 28      M1: Word length (together with bit 12)
            0: see bit 12
            1: 1 start bit, 7 data bits
Bits 27:21  Reserved
Bit 15      OVER8: Oversampling mode
            0: oversampling by 16
            1: oversampling by 8
Bit 13      UE: USART enable
            0: disabled
            1: enabled
Bit 12      M0: Word length (together with bit 28)
            0: 1 start bit, 8 data bits
            1: 1 start bit, 9 data bits
...
```

How to read this:

- **Address offset**: Add this to the peripheral's base address to get the register's actual address. UART4 base is `0x40004C00`, so `USART_CR1` is at `0x40004C00 + 0x00 = 0x40004C00`.
- **Reset value**: What the register contains after a reset. Usually all zeros.
- **Bit fields**: Each bit or group of bits has a name (M1, OVER8, UE) and a description of what each value means.
- **Reserved bits**: Don't write 1 to these. Always read-modify-write to preserve them.

### The Alternate Function Table (Datasheet)

To find which AF number connects a pin to a peripheral:

1. Open the datasheet, find the "Alternate Function" table
2. Find your pin (e.g., PA0)
3. Read across to find the peripheral (e.g., UART4_TX is AF8)

This is how we know to use `GPIO_AF8_UART4` in our code. You can't guess AF numbers — they're fixed in silicon.

### Practical Tips

1. **Use the PDF search** — Don't read linearly. Search for the register name or peripheral.
2. **Read the "Functional Description" first** — Before diving into registers, read the prose explanation of how the peripheral works.
3. **Check the "Clock" section** — Every peripheral chapter has a section on which clock drives it and how to calculate baud rates, timer periods, etc.
4. **Look at the block diagram** — The peripheral chapter usually starts with a block diagram showing the data flow. This is worth studying.
5. **Cross-reference the errata** — If something doesn't work as described, check the errata for known bugs.

---

## 24. Ring Buffers: The Interrupt-Safe FIFO

A ring buffer (circular buffer) is the most common data structure in embedded systems. It's used whenever two contexts (like an ISR and the main loop) need to exchange data safely.

### The Problem

The UART interrupt fires when a byte arrives. The main loop processes bytes. If the ISR writes directly to a variable and the main loop reads it, you have a race condition — the ISR might overwrite data before the main loop reads it.

### How a Ring Buffer Works

```
Buffer: [A][B][C][D][ ][ ][ ][ ]
         ^           ^
         tail        head
         (read)      (write)

ISR writes at head, advances head.
Main loop reads at tail, advances tail.
When head or tail reaches the end, it wraps to 0.
```

```c
#define BUF_SIZE 128                /* Must be power of 2 for masking */
static volatile uint8_t buf[BUF_SIZE];
static volatile uint16_t head = 0;  /* ISR writes here */
static volatile uint16_t tail = 0;  /* Main loop reads here */

/* ISR: add a byte */
void rx_isr(uint8_t byte) {
    uint16_t next = (head + 1) % BUF_SIZE;
    if (next != tail) {             /* Buffer not full? */
        buf[head] = byte;
        head = next;
    }
    /* If full, byte is dropped — better than corrupting data */
}

/* Main loop: read a byte */
int rx_read(uint8_t *out) {
    if (tail == head) return 0;     /* Buffer empty */
    *out = buf[tail];
    tail = (tail + 1) % BUF_SIZE;
    return 1;
}
```

### Why It's Safe Without Locks

The key insight: **the ISR only modifies `head`, and the main loop only modifies `tail`**. Each side reads the other's variable but never writes it. On a single-core MCU (like our CM7-only setup), this is safe without any locking as long as `head` and `tail` are `volatile` and the reads/writes are atomic (which 16-bit and 32-bit accesses are on Cortex-M7).

### Power-of-2 Optimization

If `BUF_SIZE` is a power of 2, you can replace the modulo (`%`) with a bitwise AND:
```c
#define BUF_SIZE 128
#define BUF_MASK (BUF_SIZE - 1)   /* 127 = 0x7F */

next = (head + 1) & BUF_MASK;    /* Same as % 128, but faster */
```

This matters in ISRs where you want minimum execution time.

Our C4001 driver uses exactly this pattern: the UART4 RX callback writes bytes into a ring buffer, and `C4001_Poll()` in the main loop drains it line by line.

---

## 25. The Bus Architecture: AXI, AHB, and APB

Understanding the bus architecture explains *why* the STM32H7 has so many clock domains and memory regions.

### What Is a Bus?

A bus is a shared communication highway connecting the CPU to memory and peripherals. Think of it as a road system — wider roads (buses) carry more data, faster roads have higher clock speeds, and intersections (bridges) connect different roads.

### The Three Bus Types

```
CPU (480 MHz)
  |
  AXI Bus (240 MHz, 64-bit) -----> AXI SRAM, Flash controller, FMC (SDRAM)
  |
  AHB Bus (240 MHz, 32-bit) -----> DMA, Ethernet, USB, SRAM D2, GPIO
  |
  +--- APB1 (120 MHz, 32-bit) --> UART2/3/4/5/7/8, I2C1/2/3, SPI2/3, TIM2-7/12-14
  |
  +--- APB2 (120 MHz, 32-bit) --> USART1/6, SPI1/4/5, TIM1/8/15-17
  |
  +--- APB3 (120 MHz, 32-bit) --> LTDC (LCD controller)
  |
  +--- APB4 (120 MHz, 32-bit) --> SYSCFG, EXTI, RTC, LPUART
```

| Bus | Speed | Width | Connected to |
|-----|-------|-------|-------------|
| **AXI** | 240 MHz | 64-bit | High-bandwidth: main RAM, flash, SDRAM |
| **AHB** | 240 MHz | 32-bit | Medium-speed: DMA, crypto, hash, Ethernet MAC, GPIO |
| **APB** | 120 MHz | 32-bit | Peripherals: UART, I2C, SPI, timers |

### Domains: D1, D2, D3

The STM32H7 groups peripherals into three power/clock domains:

- **D1 (CPU domain)**: CPU, AXI SRAM, flash, LTDC, FMC. This is the high-performance domain.
- **D2 (Peripheral domain)**: Most peripherals, SRAM D2, DMA, USB, Ethernet. Peripherals live here.
- **D3 (Low-power domain)**: BDMA, LPUART, RTC, SRAM D3. Can stay alive in low-power modes.

Each domain can be independently clock-gated or powered down. This is why DMA buffers go in SRAM D2 (same domain as the DMA controller and peripherals), and why there's a separate D3 SRAM for things that need to survive sleep modes.

### Why This Matters In Practice

1. **Clock enables**: `__HAL_RCC_GPIOK_CLK_ENABLE()` gates the AHB4 clock to GPIOK. Each bus has its own clock enable register (RCC_AHB1ENR, RCC_APB1LENR, etc.).
2. **DMA placement**: DMA buffers must be in a region accessible by the DMA controller. AXI SRAM works but SRAM D2 is preferred.
3. **Timing calculations**: UART baud rate is derived from APB1 clock (120 MHz). Timer tick rate is derived from the timer clock (240 MHz for APB1 timers, due to the prescaler doubling rule).
4. **Bridge latency**: Accessing a peripheral across a bridge (e.g., CPU -> AHB -> APB -> UART register) adds a few clock cycles of latency. Not usually noticeable, but matters for tight timing loops.

---

## 26. Fixed-Point Math: Floats Without the Float

Floating-point operations (`float`, `double`) are expensive on many MCUs. The Cortex-M7 has a hardware FPU so it's not terrible, but `printf("%f")` and `snprintf` with floats pull in enormous library code (~10-20 KB of flash). In embedded, we often avoid this.

### The Technique

Instead of printing `2.45` using `snprintf(buf, 16, "%.2f", val)`, we split the float into integer and fractional parts manually:

```c
float val = 2.45f;
uint32_t integer_part = (uint32_t)val;                           /* 2 */
uint32_t frac_part = (uint32_t)((val - (float)integer_part) * 100.0f);  /* 45 */

/* Now format as "2.45" using integer-only formatting */
buf[0] = '0' + integer_part;  /* '2' */
buf[1] = '.';
buf[2] = '0' + (frac_part / 10);  /* '4' */
buf[3] = '0' + (frac_part % 10);  /* '5' */
```

This is exactly what our `c4001.c` does for range and speed values. No `snprintf`, no heap, no massive library. The cost: you handle negative numbers and multi-digit integers yourself.

### Integer-to-String: The uint_to_str Pattern

Converting an integer to a string without `sprintf`:

```c
/* Write decimal digits of `val` to buf, return pointer to first digit */
static char *uint_to_str(uint32_t val, char *buf_end) {
    *--buf_end = '\0';
    if (val == 0) {
        *--buf_end = '0';
        return buf_end;
    }
    while (val > 0) {
        *--buf_end = '0' + (val % 10);
        val /= 10;
    }
    return buf_end;
}
```

This writes digits backwards from the end of a buffer (because you don't know how many digits there are until you're done). Our code uses this pattern for frame counts, byte counts, and timing values.

### When to Use Hardware Floats

The Cortex-M7 FPU handles `float` (single precision) in hardware — addition, multiplication, and comparison are fast (1-3 cycles). So using `float` in calculations is fine. The issue is only with **formatting floats to strings** (printf/snprintf), which drags in massive library code. Calculate with floats, display with integer tricks.

---

## 27. State Machines: Structuring Embedded Logic

As firmware grows, managing complex behavior with scattered `if/else` chains becomes unmaintainable. State machines are the standard solution.

### What Is a State Machine?

A state machine has:
- A set of **states** (what the system is currently doing)
- **Events** that trigger transitions between states
- **Actions** that happen on entering a state or during a transition

### Example: A Sensor Connection Manager

```c
typedef enum {
    STATE_INIT,          /* Waiting for sensor to boot */
    STATE_CONFIGURING,   /* Sending config commands */
    STATE_RUNNING,       /* Normal operation, receiving data */
    STATE_ERROR,         /* Something went wrong, retrying */
} SensorState_t;

static SensorState_t state = STATE_INIT;
static uint32_t state_entered_at = 0;

void Sensor_Poll(void) {
    uint32_t now = HAL_GetTick();

    switch (state) {
    case STATE_INIT:
        if (now - state_entered_at > 2000) {   /* 2s boot timeout */
            send_config_commands();
            state = STATE_CONFIGURING;
            state_entered_at = now;
        }
        break;

    case STATE_CONFIGURING:
        if (config_ack_received) {
            state = STATE_RUNNING;
            state_entered_at = now;
        } else if (now - state_entered_at > 5000) {  /* 5s timeout */
            state = STATE_ERROR;
            state_entered_at = now;
        }
        break;

    case STATE_RUNNING:
        process_sensor_data();
        if (now - last_data_received > 10000) {   /* 10s no data */
            state = STATE_ERROR;
            state_entered_at = now;
        }
        break;

    case STATE_ERROR:
        reset_sensor();
        state = STATE_INIT;
        state_entered_at = now;
        break;
    }
}
```

### Why State Machines Work Well on MCUs

- **No blocking**: Each call to `Sensor_Poll()` checks the state and returns immediately. The main loop keeps running other peripherals.
- **Timeouts are natural**: Just compare `HAL_GetTick()` against when you entered the state.
- **Easy to debug**: Print or log the current state. You always know what the system is doing.
- **No RTOS needed**: State machines give you cooperative multitasking in a bare-metal `while(1)` loop.

Our C4001 driver is a simple implicit state machine: init -> running, with the main loop polling for data. For more complex protocols (multi-step handshakes, retry logic), an explicit state enum makes the logic much clearer.

---

## 28. Debugging Techniques Beyond the Basics

Section 17 covered GDB and HardFaults. Here are more practical techniques:

### Printf Debugging (Serial Logging)

The most common embedded debugging technique: print values over USB CDC (or UART) and watch in a serial terminal.

```c
/* Quick-and-dirty: output a message with a value */
char msg[] = "counter=";
CDC_Transmit_HS((uint8_t *)msg, sizeof(msg) - 1);
```

Pros: Works without a debugger. Cons: Changes timing (especially at high baud rates), can mask race conditions.

### LED Debugging

When serial isn't available (early boot, USB not initialized yet):

```c
/* Blink patterns to indicate state */
void debug_blink(int count) {
    for (int i = 0; i < count; i++) {
        HAL_GPIO_WritePin(GPIOK, GPIO_PIN_5, GPIO_PIN_RESET);  /* Red ON */
        HAL_Delay(200);
        HAL_GPIO_WritePin(GPIOK, GPIO_PIN_5, GPIO_PIN_SET);    /* Red OFF */
        HAL_Delay(200);
    }
    HAL_Delay(1000);  /* Pause between repetitions */
}

/* Call before/after suspicious code to isolate the crash point:
   debug_blink(1) -> if you see 1 blink, code got this far
   suspicious_function();
   debug_blink(2) -> if you see 2 blinks, it survived
*/
```

### Logic Analyzer / Oscilloscope

For timing-sensitive debugging (UART garbled data, SPI glitches), connect a logic analyzer to the signal pins. Tools like Saleae Logic or PulseView decode protocols and show exact timing. If UART data looks garbled, check:

1. Baud rate matches on both sides
2. Clock is accurate (HSI at ~1% vs HSE crystal)
3. TX/RX aren't swapped
4. Ground is connected

### GPIO Toggle Profiling

Measure how long code sections take by toggling a GPIO:

```c
HAL_GPIO_WritePin(GPIOK, GPIO_PIN_7, GPIO_PIN_RESET);  /* Blue ON (start) */
expensive_function();
HAL_GPIO_WritePin(GPIOK, GPIO_PIN_7, GPIO_PIN_SET);    /* Blue OFF (end) */
/* Measure the pulse width on an oscilloscope = execution time */
```

This is more accurate than `HAL_GetTick()` (which has 1ms resolution). A scope shows microsecond timing.

### Fault Register Inspection

When a HardFault occurs, several registers tell you *why*:

| Register | What it tells you |
|----------|-------------------|
| `SCB->CFSR` | Configurable Fault Status — which type of fault |
| `SCB->HFSR` | HardFault Status — was it an escalated fault? |
| `SCB->MMFAR` | Memory Management Fault Address — what address was accessed |
| `SCB->BFAR` | Bus Fault Address — what address caused a bus error |

In the debugger, read these after a HardFault:

```
(gdb) x/x 0xE000ED28    # CFSR
(gdb) x/x 0xE000ED2C    # HFSR
(gdb) x/x 0xE000ED34    # MMFAR
(gdb) x/x 0xE000ED38    # BFAR
```

Common CFSR patterns:

- `PRECISERR` + valid BFAR: Your code accessed an invalid or non-clocked peripheral address
- `INVSTATE`: CPU tried to execute ARM instructions in Thumb mode (corrupted function pointer)
- `UNDEFINSTR`: CPU hit an undefined instruction (jumped to garbage)
- `STKERR`: Stack pointer is corrupted (stack overflow)

---

## 29. Memory-Mapped I/O: How Peripherals Actually Work

On a Cortex-M, peripherals don't have special instructions. They're accessed by reading and writing specific memory addresses. This is called **memory-mapped I/O**.

### Example: How `HAL_GPIO_WritePin()` Really Works

When you write:
```c
HAL_GPIO_WritePin(GPIOK, GPIO_PIN_6, GPIO_PIN_RESET);
```

The HAL translates this to a write to the BSRR (Bit Set/Reset Register):
```c
/* Simplified HAL implementation */
GPIOK->BSRR = (uint32_t)GPIO_PIN_6 << 16U;   /* Write to address 0x58022818 */
```

`GPIOK` is a pointer to a struct overlaid at address `0x58022800` (GPIOK's base address in the memory map):

```c
/* From the CMSIS header stm32h747xx.h */
typedef struct {
    volatile uint32_t MODER;    /* Offset 0x00 — Mode register */
    volatile uint32_t OTYPER;   /* Offset 0x04 — Output type */
    volatile uint32_t OSPEEDR;  /* Offset 0x08 — Speed */
    volatile uint32_t PUPDR;    /* Offset 0x0C — Pull-up/down */
    volatile uint32_t IDR;      /* Offset 0x10 — Input data (read pin state) */
    volatile uint32_t ODR;      /* Offset 0x14 — Output data */
    volatile uint32_t BSRR;     /* Offset 0x18 — Bit set/reset */
    volatile uint32_t LCKR;     /* Offset 0x1C — Lock */
    volatile uint32_t AFR[2];   /* Offset 0x20 — Alternate function */
} GPIO_TypeDef;

#define GPIOK  ((GPIO_TypeDef *)0x58022800UL)
```

When you write `GPIOK->BSRR = value`, the CPU performs a store to address `0x58022800 + 0x18 = 0x58022818`. The bus interconnect routes this to the GPIOK hardware, which changes the pin state.

### Why Everything Is `volatile`

Every register in the struct is `volatile` because:

1. Hardware can change register values at any time (e.g., IDR changes when a pin changes)
2. Writing to a register has side effects (BSRR changes pin state)
3. The compiler must not optimize away or reorder these accesses

Without `volatile`, the compiler might:

- Cache a register read and never re-read it (missing pin state changes)
- Eliminate "redundant" writes (but each write triggers hardware action)
- Reorder register accesses (breaking init sequences that depend on order)

### The Full Address Map

The STM32H7's 4 GB address space is divided into regions:

```
0x00000000 - 0x1FFFFFFF   Code region (Flash, ITCMRAM, boot ROM)
0x20000000 - 0x3FFFFFFF   SRAM region (all RAMs)
0x40000000 - 0x5FFFFFFF   Peripheral region (APB/AHB peripherals)
0x60000000 - 0x9FFFFFFF   External RAM (FMC SDRAM, etc.)
0xA0000000 - 0xBFFFFFFF   External device
0xC0000000 - 0xDFFFFFFF   External RAM (SDRAM bank 2)
0xE0000000 - 0xFFFFFFFF   System (NVIC, SCB, debug, core peripherals)
```

Every peripheral register, every RAM byte, and every flash word has a unique address. The hardware decodes addresses and routes them to the right destination.

---

## 30. Common Embedded Patterns

### Millisecond Timekeeping Without Blocking

Never use `HAL_Delay()` in the main loop for timing — it blocks everything else. Instead, use `HAL_GetTick()`:

```c
static uint32_t last_print = 0;

while (1) {
    C4001_Poll();           /* Always runs */

    if (HAL_GetTick() - last_print >= 1000) {   /* Every 1 second */
        last_print = HAL_GetTick();
        print_status();
    }

    /* Other periodic tasks here */
}
```

This pattern is non-blocking — all your peripherals keep getting serviced. Multiple tasks can each have their own `last_time` variable and interval.

Note: `HAL_GetTick()` returns `uint32_t` (wraps at ~49.7 days). The subtraction `now - last` works correctly even across the wrap because unsigned arithmetic wraps naturally. This is a standard C idiom in embedded.

### Guard Against Repeated Init

```c
static uint8_t initialized = 0;

void Peripheral_Init(void) {
    if (initialized) return;   /* Don't init twice */
    /* ... init code ... */
    initialized = 1;
}
```

Calling `HAL_UART_Init()` twice on the same peripheral can cause issues (re-triggering MSP, resetting state mid-transfer).

### Compile-Time Assertions

Catch configuration errors at compile time, not at runtime:

```c
/* Ensure buffer size is a power of 2 (required for ring buffer masking) */
_Static_assert((BUF_SIZE & (BUF_SIZE - 1)) == 0, "BUF_SIZE must be power of 2");

/* Ensure struct packing matches hardware expectations */
_Static_assert(sizeof(PacketHeader) == 8, "PacketHeader must be 8 bytes");
```

### Defensive Timeout Loops

When waiting for hardware, always add a timeout:

```c
/* BAD: hangs forever if PLL never locks */
while (!__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY)) {}

/* GOOD: timeout after 100ms */
uint32_t start = HAL_GetTick();
while (!__HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY)) {
    if (HAL_GetTick() - start > 100) {
        Error_Handler();   /* PLL failed to lock */
    }
}
```

The HAL already does this internally (`HAL_MAX_DELAY`), but in custom code, always protect against infinite waits.

### Struct-Based Configuration

Instead of passing many arguments to init functions, use a configuration struct:

```c
typedef struct {
    uint32_t baud_rate;
    uint8_t  trig_sensitivity;
    uint8_t  keep_sensitivity;
    float    range_min;
    float    range_max;
    uint8_t  micromotion;
} C4001_Config_t;

/* Clear, self-documenting, extensible */
C4001_Config_t cfg = {
    .baud_rate = 9600,
    .trig_sensitivity = 7,
    .keep_sensitivity = 7,
    .range_min = 0.3f,
    .range_max = 1.5f,
    .micromotion = 1,
};
```

C99 designated initializers (`.field = value`) make the meaning of each parameter obvious. Unspecified fields are zero-initialized.

---

## 31. Toolchain Deep Dive: What Each Tool Does

### The Cross-Compiler: arm-none-eabi-gcc

`arm-none-eabi-gcc` is a cross-compiler — it runs on your PC but generates code for ARM processors.

The name decodes as:

- `arm` — target architecture
- `none` — no operating system (bare-metal)
- `eabi` — Embedded Application Binary Interface (calling convention)
- `gcc` — GNU Compiler Collection

You also have:

- `arm-none-eabi-as` — assembler (for .s files)
- `arm-none-eabi-ld` — linker (usually invoked through gcc)
- `arm-none-eabi-objcopy` — binary converter (ELF to raw binary)
- `arm-none-eabi-objdump` — disassembler (inspect generated code)
- `arm-none-eabi-size` — memory usage report
- `arm-none-eabi-nm` — symbol listing
- `arm-none-eabi-gdb` — debugger

### Useful Commands

**See how big your functions are:**

```bash
arm-none-eabi-nm --size-sort --print-size build/firmware.elf | tail -20
```

Shows the 20 largest symbols. Useful for finding what's eating your flash.

**Disassemble a function:**

```bash
arm-none-eabi-objdump -d build/firmware.elf | grep -A 30 "<C4001_Poll>:"
```

See the actual ARM instructions the compiler generated. Useful for understanding optimization or debugging HardFaults (find the instruction at the faulting PC address).

**Memory usage summary:**

```bash
arm-none-eabi-size build/firmware.elf
```

```
   text    data     bss     dec     hex filename
  58592     272   24336   83200   14500 build/firmware.elf
```

- `text` = code + constants (goes in flash)
- `data` = initialized globals (stored in flash, copied to RAM)
- `bss` = uninitialized globals (zero-filled in RAM)
- Total flash usage = text + data
- Total RAM usage = data + bss + stack (stack isn't counted here)

### The Map File

Add `-Wl,-Map=build/firmware.map` to your linker flags to generate a map file. This shows exactly where every function and variable is placed in memory:

```
.text          0x08000000     0xe4c0
 *(.isr_vector)
 .isr_vector    0x08000000      0x298  build/startup_stm32h747xihx.o
 ...
 .text.C4001_Poll
                0x0800abcd       0x1a0  build/c4001.o
```

This is invaluable for:

- Finding what's using the most flash
- Verifying that sections are placed correctly
- Debugging linker script issues
- Checking that unused code is actually removed by `--gc-sections`

---

## 32. Glossary

Quick reference for embedded jargon you'll encounter:

| Term | Meaning |
|------|---------|
| **AF** | Alternate Function — a pin's secondary purpose (UART TX, SPI clock, etc.) |
| **AHB** | Advanced High-performance Bus — connects fast peripherals to the CPU |
| **APB** | Advanced Peripheral Bus — connects slower peripherals |
| **BSS** | Block Started by Symbol — the section for zero-initialized globals |
| **CDC** | Communication Device Class — USB standard for virtual serial ports |
| **CMSIS** | Cortex Microcontroller Software Interface Standard — ARM's standard headers |
| **D-Cache** | Data Cache — small fast memory between CPU and main RAM |
| **DMA** | Direct Memory Access — hardware that moves data without CPU involvement |
| **ELF** | Executable and Linkable Format — the compiled binary with debug info |
| **EXTI** | External Interrupt — GPIO pin change interrupts |
| **FPU** | Floating-Point Unit — hardware for fast float math |
| **GPIO** | General-Purpose Input/Output — digital pins you control directly |
| **HAL** | Hardware Abstraction Layer — ST's peripheral driver library |
| **HardFault** | Unrecoverable CPU error (like a kernel panic) |
| **HCLK** | High-speed bus clock (AHB clock) |
| **HSE** | High-Speed External oscillator (25 MHz crystal on Portenta) |
| **HSI** | High-Speed Internal oscillator (64 MHz RC, less accurate) |
| **I-Cache** | Instruction Cache — speeds up code execution from flash |
| **I2C** | Inter-Integrated Circuit — 2-wire serial bus (SCL + SDA) |
| **IRQ** | Interrupt Request |
| **ISR** | Interrupt Service Routine — the function that handles an interrupt |
| **JTAG** | Joint Test Action Group — debug interface (SWD is the 2-wire subset) |
| **LDO** | Low-Dropout Regulator — voltage regulator |
| **LMA** | Load Memory Address — where data is stored in flash (for .data section) |
| **MCU** | Microcontroller Unit |
| **MMIO** | Memory-Mapped I/O — accessing peripherals as memory addresses |
| **MPU** | Memory Protection Unit — controls access permissions for memory regions |
| **MSP** | MCU Support Package — HAL callback for board-specific init |
| **NVIC** | Nested Vectored Interrupt Controller — manages interrupt priorities |
| **OTP** | One-Time Programmable — fuses burned at the factory |
| **PCD** | Peripheral Controller Device — HAL's USB device-mode driver |
| **PLL** | Phase-Locked Loop — frequency multiplier circuit |
| **PMIC** | Power Management Integrated Circuit — external power controller |
| **PWM** | Pulse-Width Modulation — digital signal that simulates analog by varying duty cycle |
| **SCB** | System Control Block — core CPU configuration registers |
| **SMPS** | Switched-Mode Power Supply — efficient voltage converter |
| **SPI** | Serial Peripheral Interface — high-speed 4-wire bus |
| **SRAM** | Static Random-Access Memory — fast, volatile memory |
| **SWD** | Serial Wire Debug — 2-wire debug protocol (SWDIO + SWCLK) |
| **SysTick** | System Tick Timer — ARM core timer, fires every 1ms for HAL_Delay |
| **UART** | Universal Asynchronous Receiver/Transmitter — serial communication |
| **ULPI** | UTMI+ Low Pin Interface — connects external USB PHY |
| **VMA** | Virtual Memory Address — where data lives at runtime (for .data section) |
| **VOS** | Voltage Output Scaling — CPU voltage level (VOS0 = highest performance) |
| **VTOR** | Vector Table Offset Register — tells CPU where the vector table is |

---

## Where to Go From Here

1. **Read the code** — Start with `main.c` and follow the init sequence. Read each function it calls.
2. **Read the docs** — `docs/drivers/*.md` documents every peripheral we've brought up.
3. **Try modifying** — Change the LED blink duration in `led_pwm.c`. Change the sensor poll rate. Small changes build understanding.
4. **Read the reference manual** — ST's RM0399 is the bible for this chip. It's 3000+ pages but you only need the chapter for whatever peripheral you're working on.
5. **Use the debugger** — Set breakpoints, step through code, inspect registers. This is the fastest way to understand what the hardware is actually doing.
6. **Disassemble your code** — Run `arm-none-eabi-objdump -d build/firmware.elf` and compare the generated assembly to your C code. You'll learn what the compiler actually does.
7. **Break something on purpose** — Comment out a clock enable and see what happens. Remove a `volatile` and watch the optimizer break your interrupt. Intentional failures teach more than successes.
8. **Build a new peripheral driver** — Pick something simple (an LED on a different pin, a basic timer) and write it from scratch using the reference manual. The first one is hard; the second one is twice as easy.

---

## Companion Reading: Senior-Level Deep Dive

When the basics feel comfortable and you want to understand the things that silently corrupt firmware (and that no junior tutorial covers), read [mcu-senior-guide.md](mcu-senior-guide.md). It's a focused 15-section deep dive on:

- The `volatile` myth and memory barriers (`__DSB`, `__DMB`, `__ISB`)
- Cache coherency for DMA (the reason this project disables D-cache)
- ISR concurrency, BASEPRI vs PRIMASK, lock-free atomics with LDREX/STREX
- Stack overflow on Cortex-M (no MMU = silent corruption) and how to detect it
- Weak-symbol mechanics and HAL override gotchas
- Newlib retargeting, `_sbrk`, `_write`, and why printf costs 30 KB
- FPU lazy stacking and the ISR floating-point trap
- Boot modes, DFU, and dual-bank flash for OTA
- Watchdog patterns that actually catch hangs
- Power modes and what state survives each
- Reading the silicon errata sheet
- Map-file forensics for code-size optimization
- Hardware-in-the-loop test patterns
- The mental model shifts from server engineering to embedded
