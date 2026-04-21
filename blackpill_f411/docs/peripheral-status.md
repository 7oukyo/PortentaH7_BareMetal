# BlackPill F411 — Peripheral & Module Status

Tracks bring-up of the ported sofa controller on WeAct BlackPill STM32F411CEU6.

## Build System

| Item | Status | Date | Notes |
|------|--------|------|-------|
| Makefile + linker + startup | BUILD-READY | 2026-04-21 | Compiles clean, FLASH 9.9% / RAM 13.3% |
| STM32CubeF4 HAL drivers | INSTALLED | 2026-04-21 | Copied from `x:/stm32cube/STM32CubeF4` submodules into `drivers/` |
| STM32 USB Device Library (Core + CDC) | INSTALLED | 2026-04-21 | Same source; Class/CDC + Core |

## Board Bringup

| Peripheral | Status | Date | Notes |
|------------|--------|------|-------|
| Clock (96 MHz) | NOT FLASHED | | HSE 25 MHz → PLL M=25 N=192 P=2 → 96 MHz. Q=4 → 48 MHz USB |
| GPIO (LED PC13) | NOT FLASHED | | Active LOW, single LED; replaces 3x GPIOK on H7 |
| Flash latency 3 WS | NOT FLASHED | | Scale 1 voltage scaling |

## Onboard Peripherals

| Peripheral | Status | Date | Notes |
|------------|--------|------|-------|
| USB CDC (OTG_FS) | NOT FLASHED | | PA11/PA12 internal PHY, FS only. `CDC_Transmit_FS()` |
| I2C1 (INA226) | NOT FLASHED | | PB6=SCL, PB7=SDA (AF4). ClockSpeed=400000, DutyCycle_2 |
| USART1 (C4001) | NOT FLASHED | | PA9=TX, PA10=RX (AF7). 9600 baud |
| GPIO (Motor Relays) | NOT FLASHED | | PB0 + PB1, active-LOW |
| TIM3 (LED PWM) | NOT FLASHED | | Replaces H7's TIM6 (not available on F411) |

## External Modules

| Module | Status | Date | Notes |
|--------|--------|------|-------|
| C4001 mmWave | NOT FLASHED | | Driver ported verbatim from H7. Presence mode, 9600 baud |
| INA226 Current/Power | NOT FLASHED | | Clone-chip calibration carried over: Rsh=0.025, CAL=2048 |
| Motor Relay H-Bridge | NOT FLASHED | | Dead-time + safety invariants identical to H7 |
| Sofa Auto-Adjust | NOT FLASHED | | State machine + adaptive baseline EMA identical to H7 |

## Bring-up Order (recommended)

1. **Flash + blink PC13** — confirms clock tree, linker, startup
2. **USB CDC enumeration** — confirms PLL Q = 48 MHz and OTG_FS init
3. **VCP echo + commands** — confirms CDC bidirectional
4. **I2C1 / INA226** — run `ina_diag` to confirm clone calibration
5. **USART1 / C4001** — confirm presence frames parse
6. **Relays PB0/PB1** — manual `motor_fwd/rev/stop`
7. **Sofa end-to-end** — `sofa_start`, watch `STL=`, `BL=`, `PK=` fields in VCP

Status values: NOT FLASHED → IN PROGRESS → VERIFIED → BROKEN (with reason)
