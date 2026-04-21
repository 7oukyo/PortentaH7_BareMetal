# Porting Notes: Portenta H7 -> BlackPill F411

## Hardware Differences

| Feature | Portenta H7 (STM32H747) | BlackPill (STM32F411) |
|---------|-------------------------|----------------------|
| Core | Cortex-M7 @ 480 MHz | Cortex-M4 @ 100 MHz |
| FPU | Double-precision (FPv5-D16) | Single-precision (FPv4-SP-D16) |
| Flash | 2 MB | 512 KB |
| RAM | 512 KB AXI + others | 128 KB SRAM |
| USB | OTG HS + ULPI PHY (USB3320) | OTG FS, internal PHY |
| I-Cache | Yes (SCB_EnableICache) | No |
| D-Cache | Yes (not used) | No |
| PMIC | NXP PF1550 on I2C1 | None (simple LDO) |
| HSE enable | PH1 GPIO gates oscillator | Always connected |
| LEDs | 3x RGB on GPIOK (active LOW) | 1x on PC13 (active LOW, sink) |
| TIM6 | Available (basic timer) | Not available |
| Dual core | CM7 + CM4 | Single core |

## Code Changes Summary

### Removed (H7-specific, not needed on F411)

- `pmic.c/h` — No PMIC on BlackPill
- PJ0 STANDBY pin init — No PMIC standby
- PH1 oscillator enable — HSE always connected
- CM4 boot disable — Single core MCU
- SCB_EnableICache() — M4 has no cache control
- PeriphCommonClock_Config() — No PLL2/PLL3
- USB3320 ULPI PHY reset (PJ4) — Internal USB PHY
- GPIOH/I/J/K — Not available on UFQFPN48

### Changed

| Component | H7 | F411 |
|-----------|-----|------|
| HAL include | `stm32h7xx_hal.h` | `stm32f4xx_hal.h` |
| HAL define | `STM32H747xx`, `CORE_CM7` | `STM32F411xE` |
| CPU flags | `-mcpu=cortex-m7 -mfpu=fpv5-d16` | `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16` |
| System clock | 480 MHz (PLL1 M=5 N=192 P=2) | 96 MHz (PLL M=25 N=192 P=2) |
| USB clock | PLL3 48 MHz | PLL Q=4 -> 48 MHz |
| Flash latency | 4 WS (VOS0) | 3 WS (Scale 1) |
| I2C (INA226) | I2C3 (PH7/PH8), Timing register | I2C1 (PB6/PB7), ClockSpeed/DutyCycle |
| UART (C4001) | UART4 (PA0/PI9, AF8) | USART1 (PA9/PA10, AF7) |
| Motor relays | PC15 + PD5 | PB0 + PB1 |
| LEDs | GPIOK 5/6/7 (3 LEDs) | PC13 (1 LED) |
| USB OTG | HS + ULPI (12 GPIO pins) | FS internal (PA11/PA12 only) |
| Timer (LED) | TIM6 (basic, APB1) | TIM3 (general purpose, APB1) |
| CDC transmit | `CDC_Transmit_HS()` | `CDC_Transmit_FS()` |
| USB FIFO | 512+128+372 words | 128+64+128 words |
| Stack | 0x24080000 (AXI SRAM end) | 0x20020000 (SRAM end) |

### I2C Timing Difference

STM32H7 uses a digital timing register:
```c
hi2c.Init.Timing = 0x307075B1U;  // 120 MHz kernel clock
```

STM32F4 uses analog clock speed:
```c
hi2c.Init.ClockSpeed = 400000;           // 400 kHz
hi2c.Init.DutyCycle = I2C_DUTYCYCLE_2;   // Fm mode
```

### Clock Tree

**H7**: HSE 25MHz -> PLL1 (M=5, N=192, P=2) -> SYSCLK 480MHz -> AHB/2 = 240MHz -> APBx/2 = 120MHz

**F411**: HSE 25MHz -> PLL (M=25, N=192, P=2, Q=4) -> SYSCLK 96MHz -> AHB = 96MHz -> APB1/2 = 48MHz, APB2 = 96MHz
- USB 48MHz from PLL Q output

### LED Strategy

H7 has 3 LEDs with distinct roles:
- Red = presence detected
- Green = USB RX blink
- Blue = motor active

F411 has 1 LED (PC13):
- Blinks on USB RX (primary feedback)
- Presence and motor status reported via VCP serial only

## Files Not Ported

- `acs712.c/h` — Abandoned in H7 project (replaced by INA226)
- `pmic.c/h` — H7-specific PMIC driver

## Behavioral Differences

- **Performance**: Sofa state machine timing should work identically. The 100ms sample intervals and debounce timers are HAL_GetTick()-based and MCU-speed-independent.
- **USB speed**: Full Speed (12 Mbps) vs High Speed through ULPI. VCP throughput is lower but more than sufficient for 9600-baud telemetry and command traffic.
- **I2C**: Both run at 400 kHz Fast Mode. INA226 readings should be identical.
- **UART**: Both run at 9600 baud. C4001 communication is identical.
