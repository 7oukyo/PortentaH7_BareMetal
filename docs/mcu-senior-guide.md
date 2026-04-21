# MCU Programming for the Senior Software Engineer

A companion to [getting-started.md](getting-started.md). The beginner guide explains _what_ each piece is. This guide covers _what will silently corrupt your data and burn a week_ if you don't know it — the things experienced server / web / mobile engineers consistently get wrong when they first go bare-metal.

Each section follows the same shape: **motivation** → **what goes wrong** → **the rule** → **concrete example, often anchored to this codebase**.

## Table of Contents

1. [The `volatile` Myth and Memory Barriers](#1-the-volatile-myth-and-memory-barriers)
2. [Cache Coherency and DMA Buffers](#2-cache-coherency-and-dma-buffers)
3. [ISR Concurrency: The Read-Modify-Write Hazard](#3-isr-concurrency-the-read-modify-write-hazard)
4. [Critical Sections: PRIMASK, BASEPRI, and Lock-Free Atomics](#4-critical-sections-primask-basepri-and-lock-free-atomics)
5. [Stack Overflow Has No Safety Net](#5-stack-overflow-has-no-safety-net)
6. [Weak Symbols and HAL Override Mechanics](#6-weak-symbols-and-hal-override-mechanics)
7. [Newlib Retargeting: `_sbrk`, `_write`, and Reentrancy](#7-newlib-retargeting-_sbrk-_write-and-reentrancy)
8. [FPU Lazy Stacking and the ISR FPU Trap](#8-fpu-lazy-stacking-and-the-isr-fpu-trap)
9. [Boot Modes, DFU, and Dual-Bank Flash for OTA](#9-boot-modes-dfu-and-dual-bank-flash-for-ota)
10. [Watchdogs: The One Thing No Shipped Product Skips](#10-watchdogs-the-one-thing-no-shipped-product-skips)
11. [Power Modes: What Actually Survives](#11-power-modes-what-actually-survives)
12. [Silicon Has Bugs: Read the Errata Sheet](#12-silicon-has-bugs-read-the-errata-sheet)
13. [Map Files and Code-Size Forensics](#13-map-files-and-code-size-forensics)
14. [Hardware-in-the-Loop and Host-Side Test Patterns](#14-hardware-in-the-loop-and-host-side-test-patterns)
15. [Mental Model Shifts](#15-mental-model-shifts)

---

## 1. The `volatile` Myth and Memory Barriers

### Motivation

Every embedded tutorial says "use `volatile` for hardware registers and ISR-shared variables." That's necessary but it is **not sufficient**. `volatile` only constrains the compiler. The CPU and the bus fabric have their own reordering rules that `volatile` does not touch.

### What goes wrong

Two classes of bug:

1. **Compiler-level**: the compiler caches a value in a register, hoists a load out of a loop, or reorders independent accesses. `volatile` fixes this.
2. **CPU/bus-level**: the Cortex-M7 in this project is in-order for instruction issue but has a **store buffer** and the bus fabric (AXI → AHB → APB) reorders **independent** writes to different peripherals. The CPU can also continue past a peripheral write before that write has actually reached the peripheral.

The `volatile` keyword tells the compiler "emit the load/store every time, in order." It says nothing to the CPU.

### Concrete bite

You enable a peripheral clock, then immediately use the peripheral:

```c
__HAL_RCC_GPIOA_CLK_ENABLE();      /* RCC->AHB4ENR |= GPIOA bit */
GPIOA->MODER = ...;                /* may execute BEFORE clock is gated on */
```

The RCC write goes into the store buffer and may not have propagated through the bus interconnect by the time the GPIOA write tries to land. GPIOA is unclocked, so the write is silently dropped.

Look at the HAL macro and you'll see why ST does this:

```c
#define __HAL_RCC_GPIOA_CLK_ENABLE() do { \
    __IO uint32_t tmpreg; \
    SET_BIT(RCC->AHB4ENR, RCC_AHB4ENR_GPIOAEN); \
    /* Delay after RCC enable: read back the bit so the bus stalls */ \
    tmpreg = READ_BIT(RCC->AHB4ENR, RCC_AHB4ENR_GPIOAEN); \
    UNUSED(tmpreg); \
} while (0)
```

The dummy read forces the store to drain. This is a common pattern: **read after write** when the next instruction depends on the write having landed.

### The rule

- Use `volatile` for any memory that hardware or another execution context can touch.
- Use **memory barriers** when the order of writes _across peripherals or across CPU/peripheral boundaries_ matters:
  - `__DSB()` — data synchronization barrier; no memory access after this finishes until all prior accesses have completed.
  - `__DMB()` — data memory barrier; orders memory accesses but doesn't wait for completion.
  - `__ISB()` — instruction synchronization barrier; flush pipeline. Needed after writing to control registers like SCB->VTOR or NVIC priorities.
- Read-back-after-write is the lazy-but-effective alternative for peripherals.
- Read [ARMv7-M Architecture Reference Manual section A3.7](https://developer.arm.com/documentation/ddi0403) once. You'll never wonder again.

---

## 2. Cache Coherency and DMA Buffers

### Motivation

The Cortex-M7 has a real I-cache and D-cache (16/16 KB on the H7). DMA bypasses the CPU and writes directly into RAM. Cache and DMA do not talk to each other automatically.

This is exactly why `docs/current-config.md` says the project keeps **D-cache disabled**: it sidesteps the entire coherency problem at the cost of some performance. A senior engineer needs to understand the tradeoff so the next project can choose differently.

### What goes wrong

Two failure modes, both invisible:

1. **CPU writes a buffer, starts DMA TX**: the buffer is in the D-cache, not yet flushed to RAM. DMA reads stale RAM contents and transmits garbage.
2. **DMA writes into a buffer (RX), CPU reads the buffer**: the CPU reads its cached copy, which still holds whatever was there before DMA wrote. The fresh DMA data sits in RAM ignored.

These bugs are intermittent — they trigger only when a cache line happens to contain the buffer at the wrong moment. They survive code review and unit tests. They show up at customer sites.

### The rule

Pick one of three strategies. Don't mix them:

1. **Disable the D-cache** (this project's choice). Simple, slower.
2. **Place DMA buffers in a non-cacheable region** via the MPU (Memory Protection Unit). Best performance for normal code; DMA buffers don't pay cache costs but normal data does.
3. **Manually maintain coherency** at every DMA boundary:
   - Before TX: `SCB_CleanDCache_by_Addr((uint32_t *)buf, sizeof(buf));`
   - After RX: `SCB_InvalidateDCache_by_Addr((uint32_t *)buf, sizeof(buf));`
   - Buffers MUST be aligned to 32 bytes (cache line size) and sized to a multiple of 32 bytes — partial-line invalidate corrupts neighboring data.

### Concrete

Look at our `usbd_conf.c` PCD setup. USB OTG_HS uses DMA. We avoid the issue by running with D-cache off. If a future project needs D-cache enabled, allocate USB buffers like:

```c
__attribute__((section(".dma_buffers"), aligned(32)))
static uint8_t usb_rx_buffer[64];
```

…and reserve that section in the linker script as MPU-marked Non-Cacheable.

### Anti-pattern

```c
static uint8_t small_buf[7];   /* Not aligned, not cache-line sized — DMA-broken */
```

If you ever see DMA into a 7-byte struct member, that's a future bug in waiting.

---

## 3. ISR Concurrency: The Read-Modify-Write Hazard

### Motivation

You probably reach for "shared `volatile` variable" the first time main and an ISR need to share state. That works for **one-direction, single-word, write-once** patterns. For anything else it's a race.

### What goes wrong

Cortex-M loads and stores 32-bit aligned words atomically — ARMv7-M guarantees this. So `volatile uint32_t x = 5;` and `int v = x;` are race-free as standalone operations.

But `x++` is not one operation. It's:

```
LDR  r0, [x]      ; load
ADD  r0, r0, #1   ; modify
STR  r0, [x]      ; store
```

If an ISR fires between LDR and STR and also modifies `x`, the ISR's update is overwritten when main's STR finishes. Same hazard for `x &= mask`, `x |= flag`, `if (x > 10) x--`, etc.

This bites every newcomer. The classic symptom: a counter that occasionally skips a value, or a flag that gets stuck because an ISR cleared it but main re-set it from a stale read.

### The rule

For shared mutable state:

- **Single-writer, single-reader, word-sized**: `volatile` is enough.
- **Multi-writer or read-modify-write**: protect with a critical section (Section 4) **or** use atomics.
  - C11 `<stdatomic.h>` — works but pulls in libc.
  - GCC built-ins: `__atomic_fetch_add`, `__atomic_compare_exchange_n` — what you actually want on Cortex-M. They expand to LDREX/STREX exclusive-monitor instructions.
- For shared **structs** (more than one word), critical section is the only safe option without locks; the CPU cannot atomically store >32 bits.

### Concrete

Looking at our project's `c4001.c`, the UART RX ISR writes into a circular buffer indexed by `head`, while main reads via `tail`. Single producer / single consumer with each side touching only its own index = safe with `volatile` alone. If we ever added a second ISR writing to the same buffer, we'd need critical sections immediately.

---

## 4. Critical Sections: PRIMASK, BASEPRI, and Lock-Free Atomics

### Motivation

When you do need to lock out concurrency, Cortex-M offers three knobs and two of them are usually wrong.

### The three mechanisms

| Mechanism | Effect | When to use |
|-----------|--------|-------------|
| `__disable_irq()` / `PRIMASK` | Masks **all** maskable interrupts including system tick | Almost never. Rarely needed in well-designed firmware. |
| `__set_BASEPRI(value)` | Masks interrupts at priority **numerically ≥ value** (lower priorities) | The right tool for almost every critical section. |
| `__LDREX` / `__STREX` | Exclusive monitor — implements lock-free atomics | Counters, queue indices, flags |

### What goes wrong with PRIMASK

Disabling all interrupts globally:

- Breaks SysTick, so `HAL_GetTick()` stops advancing → any timeout based on it freezes the CPU.
- Increases worst-case interrupt latency for unrelated, time-critical handlers (audio, motor control).
- Often used to "fix" races that the developer doesn't fully understand. Hides the real problem.

### Why BASEPRI is better

Set BASEPRI to your "I'm in a critical section, ignore lower-priority ISRs" threshold. Higher-priority ISRs (numerically smaller priority value) still run. This keeps fast paths responsive and only blocks the ISRs that actually share state with you.

```c
uint32_t prev = __get_BASEPRI();
__set_BASEPRI(SOME_PRIO_NUMBER << (8 - __NVIC_PRIO_BITS));
/* critical section: ISRs with priority value >= SOME_PRIO_NUMBER are blocked */
__set_BASEPRI(prev);
```

`__NVIC_PRIO_BITS` is 4 on STM32H7 (16 priority levels), so the shift puts the value in the high nibble.

### When LDREX/STREX is better

If you only need an atomic counter or compare-exchange, no critical section is needed. The CPU's exclusive monitor handles it lock-free:

```c
uint32_t old, new_val;
do {
    old = __LDREXW(&shared_counter);
    new_val = old + 1;
} while (__STREXW(new_val, &shared_counter) != 0);
```

GCC built-ins `__atomic_*` generate this for you. Use them.

### The rule

Default to BASEPRI for critical sections. Use `__atomic_*` for lock-free counters. Reserve `__disable_irq()` for "this is the early-boot init sequence and nothing else is running yet."

---

## 5. Stack Overflow Has No Safety Net

### Motivation

On a server, stack overflow is a segfault. The OS catches it; you see a crash, you fix it. On a Cortex-M7 (no MMU on Cortex-M class), stack overflow silently corrupts whatever happens to live in the address below the stack. Sometimes the corruption is your `.bss` data. Sometimes it's another task's stack (FreeRTOS). Sometimes it's vector table-adjacent code. The bug manifests as "everything works fine for an hour, then a peripheral does something nonsensical."

### What goes wrong

- A deep recursion or an oversize local array (`uint8_t buf[8192];`) overruns the stack.
- The SP wraps below the stack region into adjacent memory.
- Writes corrupt that region. Reads return garbage.
- HardFault may eventually happen — or may not, if the corrupted memory is RAM and the corrupted bits are valid pointers/data structures.

There is no MMU on Cortex-M7. There is no guard page. There is no signal.

### Detection options

1. **Linker symbols + boot canary**: at startup, fill the stack region from `_estack` down to a known low-water mark with a sentinel pattern (e.g., `0xDEADBEEF`). Periodically scan from the bottom up and find the highest still-`0xDEADBEEF` address; that's your high-water mark. FreeRTOS does this for tasks (`uxTaskGetStackHighWaterMark`).

2. **MSPLIM register** (Cortex-M33+ / ARMv8-M only — **not** available on M7). On Portenta H7, you can't use it. Plan accordingly.

3. **MPU as guard region**: place a 32-byte MPU region at `_estack - stack_size - 32` with no-access permissions. SP overrun triggers a MemManage fault before silent corruption. This is the closest thing to a guard page you get on Cortex-M7.

4. **Static analysis**: GCC's `-fstack-usage` produces a `.su` file per translation unit. Sum the worst-case call paths and compare to the linker-allocated stack size. Catches overflow at build time.

### The rule

- Estimate stack usage at build time. Set the linker `_estack` to give you ~50% headroom.
- Add a canary pattern in the startup file or `SystemInit()` and a `stack_check()` helper you can call from your watchdog tick.
- For shipped products: set up MPU guard region. Cost is one of the eight MPU regions on Cortex-M7.
- Never put large arrays on the stack. `static` them or `malloc` them (and prefer `static`).

---

## 6. Weak Symbols and HAL Override Mechanics

### Motivation

You've seen the HAL ship with empty functions like `HAL_GPIO_EXTI_Callback` that you're "supposed to override." The mechanism is the GCC `__attribute__((weak))` — and once you understand it, a lot of HAL plumbing makes sense.

### How it works

Two definitions of a symbol are normally a linker error. With `weak`, the linker prefers a non-weak definition if one exists, otherwise falls back to the weak one.

In `startup_stm32h747xihx.s` the entire vector table is full of weak labels:

```
.weak   USART1_IRQHandler
.thumb_set USART1_IRQHandler, Default_Handler
```

When you write a function called `USART1_IRQHandler` anywhere in your project, it overrides the weak default. No registration call needed — just the right function name.

In HAL C code:

```c
__weak void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* default does nothing; override in your code */
}
```

You provide a non-weak `HAL_UART_RxCpltCallback` in your project; the linker picks yours.

### Gotchas

- **Wrong name = silent no-op**. `HAL_UART_RxCpltCallBack` (capital B) doesn't override `HAL_UART_RxCpltCallback`. The build succeeds. The default runs. You wonder why the callback never fires. Look this up if a callback isn't being called.
- **Multiple non-weak definitions** = linker error. Only one place may override.
- **Link order matters with archives**. If your override is in a `.a` archive, the linker may not pull it in unless something forces it. Bare object files (the way this project is built) avoid the issue.
- The HAL's own `HAL_MspInit` is weak. If you don't override it, the HAL runs an empty function and your peripherals never get clocks/pins set up. See `src/stm32h7xx_hal_msp.c` in this project for the override.

### The rule

When the HAL says "override this callback", you do it by name and signature exactly. When you replace a weak default, remove the `__weak` attribute from your version (or simply omit it — by default symbols are strong).

---

## 7. Newlib Retargeting: `_sbrk`, `_write`, and Reentrancy

### Motivation

You include `<stdio.h>`, call `printf("hello\n")`, get either link errors or a 30 KB code-size jump. Welcome to libc retargeting.

### What goes wrong

Newlib (and the smaller newlib-nano this project uses via `-specs=nano.specs`) needs OS services it doesn't have on bare metal: `_write` for stdio output, `_sbrk` for heap, `_close`, `_lseek`, `_read`, `_isatty`, `_kill`, `_getpid`, `_fstat`, `_exit`. We're using `-specs=nosys.specs`, which provides minimal stubs — that's why the linker prints those `_close is not implemented and will always fail` warnings during the BlackPill build. The stubs link, but they fail at runtime.

If you want functioning stdio:

- Override `_write(int fd, const char *buf, int len)` to send to your UART/USB-CDC.
- Override `_sbrk(int incr)` to extend the heap (commonly between `_end` and `_estack` from the linker script). Without `_sbrk`, every `malloc`/`new` returns NULL — and any printf that needs to allocate (`%f` does) silently fails or crashes.

### Code-size shock

`printf` in newlib supports the full C99 format spec, including `%f`. Bringing in `%f` pulls in 30+ KB of soft-float code on top of newlib's already heavy formatting machinery. Newlib-nano cuts most of this but you have to opt in:

```
-u _printf_float        # link-time: include float printf
-specs=nano.specs       # use newlib-nano (default float-less)
```

For 99% of MCU work, build your own `mini_printf` that handles `%d`, `%x`, `%s`, `%c`, leaves out floats, and weighs ~1 KB. We don't even use printf in this project — `uint_to_str_main()` in `main.c` and friends do all the integer formatting by hand. That's why the firmware fits.

### Reentrancy

Newlib (not nano) keeps per-thread `errno` and IO buffers in a `_reent` struct. Bare-metal code uses `_impure_ptr` which points to a single global one — meaning if an ISR calls into libc while main is mid-call, you can corrupt `errno` and FILE state. Newlib-nano sidesteps this by being mostly non-reentrant and small.

### The rule

- Use newlib-nano. Stay away from `printf`. Roll your own integer formatter.
- If you need floats in serial output, format with a fixed-point conversion (Section 26 of the beginner guide) or `snprintf` with `-u _printf_float` and pay the size.
- Never call libc from an ISR. Especially not `malloc`. Especially not `srand`/`rand`.
- If you genuinely need a heap, override `_sbrk` and pick a fixed heap region in the linker script. Otherwise omit the heap entirely — `static` arrays are deterministic and analyzable.

---

## 8. FPU Lazy Stacking and the ISR FPU Trap

### Motivation

The Cortex-M7's FPU is too heavy to push every register on every interrupt. ARM's solution is "lazy stacking": the CPU reserves space on the exception stack frame for FPU registers but only fills them in if the ISR actually executes an FPU instruction. Clever, but the failure mode when you misconfigure it is one of the worst silent corruptions in embedded.

### What goes wrong

FPCCR (Floating-Point Context Control Register) defaults are fine for most projects. But if you disable lazy stacking (`FPCCR.LSPEN = 0`) without enabling automatic stacking (`FPCCR.ASPEN = 0` together) — or vice versa — the ISR's first FPU instruction reads garbage from the FPU registers (which still contain main's float values), then writes back the corrupted result on exit, silently destroying main's computation.

Even with defaults: if your ISR uses any floating-point math (a `sinf()` call, an `_ftol`, even an implicit float-to-int conversion), you pay 17 extra cycles on every ISR entry/exit. If your ISR is short (a few lines) and runs at high frequency, that's a lot.

### The rule

- Don't use floats in ISRs. If you must, audit FPCCR — or accept the latency.
- If your project uses floats heavily (DSP, control loops), enable the FPU **explicitly** in `SystemInit()`:
  ```c
  SCB->CPACR |= ((3UL << 10*2) | (3UL << 11*2));   /* CP10, CP11 full access */
  __DSB();
  __ISB();
  ```
  Without this, the first FPU instruction triggers a UsageFault. The H7 startup file handles this; if you write your own startup, don't forget it.
- Watch for accidental float promotion: `printf("%f", 0)` promotes the int to double and pulls in soft-float. Same with `1.0 / x` if `x` is `int`.

---

## 9. Boot Modes, DFU, and Dual-Bank Flash for OTA

### Motivation

Shipped products eventually need field updates. Designing boot/update plumbing _after_ tape-out is painful. Designing it before tape-out costs nothing.

### Boot modes on STM32H7

The chip samples BOOT0 (and on some parts, BOOT_ADD0/1 option bytes) at reset to choose where to start executing:

| BOOT0 | Source | Use case |
|-------|--------|----------|
| 0 | Main flash @ 0x08000000 | Normal operation |
| 1 | System memory (ST ROM bootloader) | DFU update over USB / UART / SPI / I2C / CAN — built into the chip, no firmware needed |

The Portenta breakout board has a BOOT0 DIP switch. Set it HIGH, reset, plug in USB, and the chip enumerates as a DFU device. This is your "bricked board" recovery route — see `docs/hardware-notes.md` for the cold-boot recovery procedure.

### Dual-bank flash and OTA

STM32H747 has 2 MB of flash split into two 1 MB banks. With proper linker setup you can run from bank 1 while writing bank 2 (or vice versa), then swap the boot bank via option bytes and reset. This gives you A/B updates with rollback: if the new firmware bricks, the bootloader detects no successful checkpoint and falls back to the previous bank.

Minimum viable OTA architecture:

1. Bootloader at 0x08000000, ~16 KB, never updated. Validates main app's CRC, jumps to it. Knows how to enter DFU on demand.
2. App slot A at 0x08010000 (1 MB), slot B at 0x08110000 (1 MB).
3. App marks "boot succeeded" by writing a flag in the last sector of its own slot.
4. Bootloader on next boot: if last-boot flag is missing in active slot, fall back to the other slot.
5. New firmware downloads (via USB CDC, BLE, Wi-Fi, whatever) into the inactive slot. Bootloader switches active slot on next reset.

### The rule

If the product will ever ship: design a bootloader on day one, even if it's just a stub that jumps to the app. Adding it later means rebuilding flash layout, linker scripts, and startup code. Bootloader-from-scratch is a 200-line project; bootloader-retrofit is a 2000-line project.

For prototypes (this project): you can defer until needed. Just keep the option open by leaving room at the bottom of flash if you can.

---

## 10. Watchdogs: The One Thing No Shipped Product Skips

### Motivation

Your firmware will hang. Some bug, some peripheral edge case, some cosmic ray will eventually put the MCU into a state where it stops responding. Without a watchdog, the product is dead until somebody power-cycles it.

### IWDG vs WWDG on STM32H7

| Watchdog | Clock | Behavior | Use case |
|----------|-------|----------|----------|
| **IWDG** (Independent) | LSI 32 kHz, internal RC | Free-running counter; you reload before it expires; if it expires, MCU resets | Production safety net. Always on. |
| **WWDG** (Window) | APB1 clock | Must be reloaded inside a precise time window — too early or too late triggers reset | Catches "code took an unexpected fast path and is now blasting through the loop too fast" |

Most products use IWDG only. WWDG is for safety-critical loops where _both_ "stuck" and "running too fast" are failures (motor control, dosing pumps).

### Where to kick

The right answer is **from the main loop**, after every iteration has confirmed each subsystem is alive. The wrong answer is "from a high-priority timer ISR" — that ISR will keep firing even if main is hung in an infinite loop, and the watchdog will never trip. The whole point of the watchdog is to detect that main is stuck.

A pattern that scales:

```c
static volatile uint32_t subsystem_alive_flags;

#define SUBSYS_C4001    (1 << 0)
#define SUBSYS_INA226   (1 << 1)
#define SUBSYS_USB      (1 << 2)
#define SUBSYS_ALL      (SUBSYS_C4001 | SUBSYS_INA226 | SUBSYS_USB)

/* Each subsystem sets its flag when it does work successfully */
void c4001_tick(void) {
    if (process_frame()) subsystem_alive_flags |= SUBSYS_C4001;
}

/* Main loop: only kick the dog when ALL subsystems have run */
void main_loop_iteration(void) {
    c4001_tick();
    ina226_tick();
    usb_tick();
    if ((subsystem_alive_flags & SUBSYS_ALL) == SUBSYS_ALL) {
        HAL_IWDG_Refresh(&hiwdg);
        subsystem_alive_flags = 0;
    }
}
```

This way a hung subsystem (USB stack stuck in a callback, sensor disconnected) eventually triggers a reset, even though the main loop itself is technically still running.

### The rule

Enable IWDG before your main loop starts. Set the timeout to ~2× your worst-case loop iteration. Kick from main only, after verifying subsystem liveness. Test the watchdog by deliberately hanging the firmware once; if reset doesn't happen, your kick path is wrong.

---

## 11. Power Modes: What Actually Survives

### Motivation

Battery-powered products live or die by current draw. A senior engineer needs to know not just _that_ the chip can sleep, but _what state is preserved_ in each mode.

### STM32H7 modes (simplified)

| Mode | CPU | RAM | Peripherals | Wakeup latency | Typical current |
|------|-----|-----|-------------|----------------|-----------------|
| Run | On | On | On | n/a | 200 mA @ 480 MHz |
| Sleep | Off (WFI) | On | On | ~1 µs | 100 mA |
| Stop | Off | Retained | Most off, EXTI/RTC alive | ~10 µs | ~1 mA |
| Standby | Off | Lost (except backup) | Off | ~250 µs (full reset) | ~2 µA |

### What goes wrong

- Your USB CDC stack works fine in Run, then mysteriously fails after a Sleep cycle. Reason: USB clocks gated off during Sleep on some configs; you must re-init or use the USB-aware low-power profile.
- You enter Stop and wake up from RTC. RAM is retained, your variables look right — but the system clock is now HSI 64 MHz, not the 480 MHz PLL you booted with. Every timing-sensitive routine is wrong by 7.5×. You must re-run `SystemClock_Config()` after every Stop.
- You enter Standby. RAM is lost. The chip resets like a power-on. Anything you needed to remember must be in the Backup SRAM domain (4 KB, powered by VBAT) or in flash.

### The rule

For each mode you use:

1. List exactly which peripherals stay alive. Datasheet table 5 ("Functionalities depending on the operating power supply") in RM0399 is the authoritative reference.
2. List which clocks stay alive. The PLL is _always_ off in Stop. SysTick is _always_ off in Sleep with WFE.
3. After wakeup, re-init anything that needs it. `SystemClock_Config()` is non-negotiable after Stop.
4. Measure current with a real meter, not the datasheet number. Datasheet numbers assume optimal config — you'll be 2-10× off until you've audited every pin's pull and every peripheral's enable bit.

---

## 12. Silicon Has Bugs: Read the Errata Sheet

### Motivation

Every silicon vendor publishes an errata sheet. Many engineers never read it. The bugs are usually subtle and silent — "if you do A then B within N cycles, the result is undefined" — and they survive every test you'd think to write.

### Real examples from STM32H7

ST's errata document `ES0392` for the STM32H747 lists ~150 known bugs. Highlights:

- **2.2.1**: Cortex-M7 cache invalidation may not complete if a specific instruction sequence happens. Workaround: insert `__DSB()` between cache maintenance and subsequent access.
- **2.6.1**: I2C Stop condition may not be detected if SCL is held low by the slave at a specific moment. Workaround: software timeout and bus reset.
- **2.13.1**: USB OTG_HS PHY may fail to enumerate at cold boot under specific power sequencing. Workaround (which our project applies): explicit PJ4 reset toggle of the USB3320 PHY before USB init. See [docs/hardware-notes.md](hardware-notes.md).

The cold-boot reset loop bug we hit (`docs/hardware-notes.md` 2026-03-27) is partly an errata-related dance: the Cortex-M7 R0p1 silicon has the well-known AXI SRAM workaround (`AXI->TARG7_FN_MOD = 0x1`) which our `SystemInit()` applies. Skip that workaround, get random AXI bus stalls.

### The rule

For every chip you use:

1. Download the errata sheet on day one.
2. Read every entry. Most don't apply; the ones that do, you'll be glad you saw before they bit you.
3. When something behaves "impossibly," errata is the third place to look (after your own code and after the HAL source).

---

## 13. Map Files and Code-Size Forensics

### Motivation

Embedded systems have hard size limits. The day comes when your firmware is at 99% of flash and you need to find 8 KB. The map file is your scalpel.

### What's in a map file

The linker's `.map` output (enable with `-Wl,-Map=build/firmware.map` in your `LDFLAGS`) lists, for every symbol that ended up in the binary:

- Which file/object it came from
- Its address and size in flash or RAM
- Which section it was placed in
- What pulled it in (sometimes — depends on linker)

### Quick wins on size

```bash
# What are the biggest things in flash?
arm-none-eabi-nm --size-sort --print-size build/firmware.elf | tail -30

# Per-section breakdown
arm-none-eabi-size --format=sysv -A build/firmware.elf

# Dependencies — what pulled in `_printf_float`?
grep -B1 "_printf_float" build/firmware.map
```

Common culprits, in rough order of typical impact:

1. **`printf` family with float support** — 20-30 KB
2. **`malloc` + heap management** — 5-10 KB
3. **C++ exceptions or RTTI** (in C++ projects) — 30+ KB
4. **Unused HAL modules** compiled in but not used — 5-15 KB. The linker should `--gc-sections` them out, but only if you compiled with `-ffunction-sections -fdata-sections`. Check your CFLAGS.
5. **Debug strings** in `__FILE__`-style asserts — surprisingly large; consider stripping in release.
6. **Lookup tables** for sin/cos, CRCs, etc. — sometimes worth recomputing.

### LTO

`-flto` enables link-time optimization, which can give 5-15% size reduction by inlining across translation units and eliminating dead code more aggressively. Costs: longer link times, harder debugging, and occasional pathological cases where LTO breaks weak-symbol overrides. Worth trying once your project is stable; not worth it during active development.

### The rule

When you need to find space:

1. `nm --size-sort` to find the biggest symbols.
2. Map file to understand dependencies.
3. Cut from the top. The biggest symbol is usually the easiest target — a single oversize lookup table or a printf call.
4. After every cut, rebuild and check `make size` to confirm the reduction. Sometimes a "cut" doesn't reduce size because the linker had already gc'd it.

---

## 14. Hardware-in-the-Loop and Host-Side Test Patterns

### Motivation

Embedded code without tests rots fast. Hardware tests are slow and require physical setup. The senior pattern: split your code so most logic can run on the host PC.

### The split

Aim for three layers:

1. **Pure logic** — state machines, parsers, decoders, math. Zero HAL calls. Compiles and runs on host. Unit-tested with normal C test framework (Unity, CMocka, or just stdio + asserts).
2. **HAL adapters** — thin wrappers around HAL calls, behind interfaces (function pointers in C, vtable structs in fancier projects).
3. **Hardware drivers** — register-level code. Tested only on hardware.

The C4001 driver in this project is a good candidate for refactor: the `parse_line` function (frame parsing) is pure logic. It could be extracted to a host-testable file with a stub `HAL_UART_Transmit` shim. We haven't done it because the project is small enough not to need it — but for a 50 KLOC firmware, this is the difference between "I can refactor with confidence" and "every change risks breaking the field deployment."

### The host-side test scaffold

Conceptually:

```c
/* In your host test runner — not on the MCU */
#include "c4001_parser.h"

int main(void) {
    C4001_PresenceData_t out;
    int ok = c4001_parse_line("$DFHPD,1\r\n", &out);
    assert(ok && out.present == true);
    /* ... 50 more cases ... */
    printf("OK\n");
}
```

Run this on every commit via CI. Any regression in the parser shows up before the firmware reaches hardware.

### Hardware-in-the-loop (HIL)

For the parts that genuinely need hardware (timing-sensitive ISRs, peripheral edge cases):

- Wire a "test mode" command into your VCP serial: `test_run <name>` triggers a self-test that exercises peripherals and prints a verdict.
- Our project has `ina_test` exactly for this — verify the INA226 driver against known reference loads.
- Automate with the Python tools in `tools/`. The `current_monitor.py` console can drive an entire test sequence and capture the output for assertion. Add a `tools/run_tests.py` that pipes commands and checks output against an expected log.

### The rule

The earlier you split logic from hardware in the codebase, the cheaper testing gets forever. Do it in the first 1000 lines of any new project — retrofitting is an order of magnitude harder.

---

## 15. Mental Model Shifts

A handful of perspective shifts that distinguish "experienced server engineer" from "experienced embedded engineer":

### Memory is the budget, not CPU

On a server, you optimize CPU time and pay for RAM with cash. On an MCU, you have ~100 KB of RAM forever. Every decision that allocates memory matters. `static` arrays sized to worst case beat dynamic allocation because they're _analyzable_ — you can sum them up at compile time and prove you fit.

### Determinism beats throughput

A control loop that runs at exactly 1 kHz, never missing, is more valuable than one that averages 5 kHz with occasional 10 ms hiccups. ISR latency, jitter, and worst-case timing are first-class metrics. "Average performance" is a server concept.

### The compiler is a peer, not an adversary

On a server, you trust the compiler and OS to do the right thing with your code. On bare metal, the compiler is the tool that translates your C into instructions — but the rest is on you. Read the assembly when behavior is mysterious. `arm-none-eabi-objdump -d build/firmware.elf | less` is your friend. You'll discover all sorts of "the compiler is doing what?" moments that explain the bug.

### Hardware is a state machine you must respect

Every peripheral has init order requirements, write-once registers, post-write delays, and "you can't change this while the peripheral is running" gotchas. The reference manual is the contract. Read it _before_ writing the driver, not after the bug. The beginner guide's section 23 is right: 2 hours with the manual saves a week of debugging.

### Failure modes are different

A server crash is a stack trace, a restart, an alert. An MCU "crash" is "the device on the customer's desk is unresponsive and the LED is half-lit and nobody knows why." Build tools (UART log, SWO trace, post-mortem fault info written to flash, watchdog reset reason in `RCC->RSR`) so that when the field unit eventually comes back, you can read out what happened.

### Read tools

- **STMicroelectronics**: RM0399 (reference manual), DS12117 (datasheet), ES0392 (errata), AN5354 (USB on STM32H7), AN5197 (cache management).
- **ARM**: Cortex-M7 Technical Reference Manual, ARMv7-M Architecture Reference Manual, Cortex-M Procedure Call Standard.
- **General**: Jack Ganssle's columns at ganssle.com — decades of hard-won embedded wisdom; Miro Samek's "Practical UML Statecharts in C/C++" for ISR-safe state machine design.

### Final rule

Embedded engineering is _the discipline of getting one thing right with no margin for sloppiness_. Server engineering can sometimes paper over bugs with redundancy and observability. Embedded usually cannot. Build the habit of "I will understand this completely before shipping it" — and of being honest with yourself when you don't.

---

## See Also

- [getting-started.md](getting-started.md) — beginner-oriented walkthrough; sections 21 (DMA), 24 (ring buffers), 28 (debugging), 29 (memory-mapped IO), 31 (toolchain) overlap conceptually.
- [current-config.md](current-config.md) — concrete configuration of this project that some sections above reference.
- [hardware-notes.md](hardware-notes.md) — real bugs and their fixes; great companion reading for sections 1, 2, 12.
- [vcp-serial-format.md](vcp-serial-format.md) — what the host tools expect from the firmware.
