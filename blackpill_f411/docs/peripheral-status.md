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
| Sofa Auto-Adjust | BUILD-READY | 2026-05-12 | State machine + adaptive baseline EMA identical to H7. CLEAR debounce extended to 900 s (15 min). AUTO close gated by `sofa_armed` flag — fires once per fresh presence rising edge. |
| Manual Override (toggle + buttons) | BUILD-READY | 2026-05-12 | PB12 toggle (PU, switch-to-GND). Press = AUTO + force-close one-shot; release = MANUAL-only (motor/state preserved). Buttons (PB2 fwd / PB10 bwd, PD active HIGH from cap-touch IC) override motor in BOTH modes; release = Motor_EmergencyStop only (no state reset). Park-at-IDLE only via the 15-min retract path or `sofa_start`. AUTO close fires once per fresh presence rising edge from IDLE. See `docs/drivers/manual-buttons.md`. |
| Direction Inversion (USER_KEY) | BUILD-READY | 2026-05-12 | `dir_inverted` flag (default true) maps "close/retract" intent to MOTOR_REV/MOTOR_FWD or vice versa. PA0 USER_KEY press flips the flag + Motor_EmergencyStop. State machine and manual buttons both route through `dir_close()`/`dir_retract()` helpers; raw `motor_fwd`/`motor_rev` serial commands are unaffected. VCP status shows `DIR=NORM` or `DIR=INV`. |

## Bring-up Order (recommended)

1. **Flash + blink PC13** — confirms clock tree, linker, startup
2. **USB CDC enumeration** — confirms PLL Q = 48 MHz and OTG_FS init
3. **VCP echo + commands** — confirms CDC bidirectional
4. **I2C1 / INA226** — run `ina_diag` to confirm clone calibration
5. **USART1 / C4001** — confirm presence frames parse
6. **Relays PB0/PB1** — manual `motor_fwd/rev/stop`
7. **Sofa end-to-end** — `sofa_start`, watch `STL=`, `BL=`, `PK=` fields in VCP
8. **Mode toggle (PB12) + manual buttons (PB2/PB10)** — verify:
   (a) toggle released (open) at boot → `sofa=MANUAL/IDLE`, no motor activity;
   (b) toggle press (pin falling) → motor goes FWD into CLOSING (force-close);
   (c) toggle release mid-CLOSING → motor keeps going forward, mode flips to MANUAL, state machine paused. Sofa is "frozen" in CLOSING until re-pressed or until manual buttons intervene;
   (d) PB2 alone in either mode → relay 1 only; release stops motor; sofa_state unchanged;
   (e) PB10 alone in either mode → relay 2 only; release stops motor; sofa_state unchanged;
   (f) AUTO + fresh sit-down (radar clear → detected, 500 ms debounce) → CLOSING fires once;
   (g) AUTO + continuous presence after a manual button override release → state machine resumes from preserved state; no spontaneous re-CLOSE on continuous presence (sofa_armed already consumed);
   (h) Full natural cycle: CLOSING → CONTACT (current spike) → 15 min clear on radar → RESETTING → IDLE. Only this path parks the sofa at IDLE

Status values: NOT FLASHED → BUILD-READY → IN PROGRESS → VERIFIED → BROKEN (with reason)
