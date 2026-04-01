# Portenta H7 Bare Metal Firmware

## What this is

Bare metal firmware for Arduino Portenta H7 (STM32H747XIH6, dual Cortex-M7/M4).
Using CM7 core only. No Arduino framework, no mbed, no RTOS (initially).
Goal: direct hardware control via STM32 HAL and register-level programming.

## Hardware & config quick reference

See `docs/current-config.md` for the full current state: memory map, clock tree, PMIC registers, init sequence, and OTP boot behavior. This file focuses on rules and conventions.

Key facts repeated here for convenience:

- **MCU**: STM32H747XIH6 — CM7 @ 480 MHz (CM4 disabled, see config doc for why)
- **Board**: Arduino Portenta H7 on Portenta Breakout Board
- **Debugger**: ST-Link V3 Mini via SWD
- **PMIC**: NXP PF1550 on I2C1 (PB6/PB7, addr 0x08), called after HAL_Init + SystemClock_Config

## Toolchain

- **Compiler**: arm-none-eabi-gcc (-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard)
- **Build**: GNU Make (Makefile in project root)
- **Flash/debug**: OpenOCD with `interface/stlink-dap.cfg` and `target/stm32h7x.cfg`
- **IDE**: VS Code with Cortex-Debug extension (launch.json and tasks.json in .vscode/)
- **Host OS**: Windows. Makefile must use forward slashes for paths and `mkdir -p` must be replaced with PowerShell-compatible commands or use $(shell mkdir). Use `del /q` or `rmdir /s /q` for clean target, not `rm -rf`. Ensure Makefile uses tabs not spaces for recipe lines.

## Build commands

- `make` — build firmware, output to build/firmware.elf and build/firmware.bin
- `make flash` — build + flash via OpenOCD to 0x08000000
- `make clean` — remove build directory
- `make debug` — start OpenOCD GDB server for VS Code Cortex-Debug

## Project structure

- `src/` — application source files (main.c, pmic.c, peripheral drivers)
- `startup/` — startup_stm32h747xihx.s (vector table, reset handler)
- `linker/` — STM32H747XIHX_CM7.ld (linker script)
- `include/` — project headers and stm32h7xx_hal_conf.h
- `drivers/` — STM32 HAL driver source and CMSIS headers (do NOT modify these)
- `reference/` — skjafar's Portenta_Cube_Template repo (read-only, extract init code from here)
- `docs/` — hardware notes, peripheral driver docs, current config, schematic reference
- `build/` — compiler output (gitignored)

## Code rules

- Pure C (C11). No C++ files. File extensions: .c and .h only.
- Use STM32 HAL for peripheral init. Direct register access acceptable for performance-critical paths but must be commented with register name and RM0399 section number.
- Every function has a brief comment explaining purpose. No boilerplate filler comments.
- Linker script places code at 0x08000000 (no bootloader). Do not add bootloader offset unless explicitly asked.
- HAL driver files in drivers/ are never modified. Override behavior via stm32h7xx_hal_msp.c callbacks.
- Interrupt handlers go in stm32h7xx_it.c, not scattered across files.

## Workflow rules

### Planning

- For any task involving 3+ files or a new peripheral: state the plan and list affected files BEFORE writing code. Do not start coding until the plan is confirmed.
- If compilation fails unexpectedly or a flash attempt bricks the board: STOP. Re-read the relevant reference code in `reference/` before attempting a fix. Do not guess.

### Verification

- After writing or modifying code, ALWAYS run `make` to verify compilation succeeds.
- After adding a new source file, update the Makefile's SRC list and verify it compiles.
- Never report a task as done if `make` has not been run and passed in this session.

### Minimal impact

- Only touch files relevant to the current task. Embedded firmware has fragile init sequences — changing one peripheral's config can break another.
- If a fix feels like a workaround, say so. State what the clean solution would be and why you chose the workaround.

### Hardware knowledge — ask the user first

Before searching the web or guessing about hardware-specific details (pin assignments, bus routing, peripheral connections, schematic details, datasheet register values), **ask the user first**. They likely have the answer immediately or can provide the relevant datasheet/schematic page. Web searches for board-specific hardware info are unreliable and have already caused a wrong-bus bug (I2C4 vs I2C1 for PMIC). The user is the authoritative source for this board's hardware.

### Hardware debugging notes

- When I report hardware behavior (LED color, UART output, crash symptoms), record the finding in `docs/hardware-notes.md` with the date.
- If I report a bricked board (orange LED, no SWD connection), the recovery procedure is: flash Arduino bootloader at 0x08000000 via ST-Link, or use BOOT DIP switch on breakout board to enter STM32 ROM bootloader.

### Documentation after completing a peripheral or module

This project is a prototype workbench — each peripheral/module integration must be documented so it can be reused later. After completing and verifying a peripheral driver or external module integration:

1. Update `docs/peripheral-status.md` — mark the peripheral as VERIFIED with the date and a one-line summary.
2. Create `docs/drivers/<peripheral-name>.md` — document: what pins/bus it uses, initialization order, any gotchas or workarounds discovered, and a minimal usage example (function call sequence to init and use it).
3. Update `docs/current-config.md` if the new peripheral changes the init sequence, clock tree, or memory layout.
4. Do NOT add per-peripheral details to this file. The peripheral-status file and current-config doc are the living trackers.

## Reference material

- skjafar's Portenta_Cube_Template in `reference/` — working clock config, PMIC init, ETH setup, FreeRTOS integration. Extract and adapt, do not copy the CubeIDE project structure.
- `docs/portenta_h7_schematic_reference.md` — full pin map, power architecture, bus routing extracted from official Arduino schematic.
- `docs/` contains extracted sections from STM32H747 reference manual (RM0399) and PF1550 datasheet. Do not assume register layouts from memory — always check `docs/` or search the web for the specific register/peripheral before writing driver code.
- When a peripheral's register details are not in `docs/`, tell the user which RM0399 section or datasheet page is needed. Do not guess register addresses or bit fields.

## Project purpose

This is a hardware prototype workbench, not a product. The goal is to build a library of verified, reusable peripheral drivers and external module integrations on the Portenta H7. Each peripheral is brought up independently, tested, then kept in the codebase for future use. The main.c should be structured so that peripheral demos can be enabled/disabled with #define flags.
