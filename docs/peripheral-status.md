# Peripheral & Module Status

## Board Bringup

| Peripheral     | Status   | Date       | Notes                                                        |
|----------------|----------|------------|--------------------------------------------------------------|
| PMIC (PF1550)  | VERIFIED | 2026-03-27 | HAL I2C1 post-HAL_Init; cold boot fixed via SystemInit reset |
| Clock (480MHz) | VERIFIED | 2026-03-25 | HSE 25MHz BYPASS -> PLL1 -> 480MHz CM7                       |
| GPIO (LED)     | VERIFIED | 2026-04-01 | PK5/6/7 active LOW; green=RX blink, blue=alive pulse         |

## Onboard Peripherals

| Peripheral       | Status      | Date       | Notes                                             |
|------------------|-------------|------------|---------------------------------------------------|
| UART (USB-C VCP) | VERIFIED    | 2026-04-01 | USB CDC via USB3320 ULPI PHY; echo + 5s alive msg  |
| SPI              | NOT STARTED |            |                                                   |
| I2C3 (user)      | IN PROGRESS | 2026-04-11 | PH7=SCL, PH8=SDA (breakout I2C_0). INA226 connected. |
| SDRAM (8MB)      | NOT STARTED |            | FMC, 0xC0000000                                   |
| ETH              | NOT STARTED |            |                                                   |
| USB              | VERIFIED    | 2026-04-01 | OTG_HS + USB3320 ULPI, CDC class, echo-back tested |
| QSPI Flash       | NOT STARTED |            |                                                   |
| SD Card          | NOT STARTED |            |                                                   |
| PWM (TIM6/LED)   | VERIFIED    | 2026-04-01 | TIM6 10kHz ISR; green LED RX blink timing           |
| ADC              | ABANDONED   | 2026-04-10 | ACS712 replaced by INA226 (I2C). ADC code kept in src/acs712.c but not compiled. |
| DAC              | NOT STARTED |            |                                                   |
| CAN              | NOT STARTED |            |                                                   |

## External Modules

| Module | Status | Date | Notes |
|--------|--------|------|-------|
| C4001 mmWave | IN PROGRESS | 2026-04-02 | UART4 (PA0/PI9) 9600 baud, presence mode, CDC output. Decoupled from other modules (2026-04-09). |
| ACS712 5A Current | ABANDONED   | 2026-04-10 | Replaced by INA226 (I2C). ADC wiring issues made analog approach unreliable. Code kept in src/acs712.c. |
| INA226 Current/Power | VERIFIED | 2026-04-12 | I2C3 (PH7/PH8, breakout I2C_0), addr 0x40. Clone chip: Vsh LSB ~1.0uV (not 2.5uV). 10mohm shunt, effective Rsh=0.025 compensates. CAL=2048. Verified 1009mA with 1A ref load. |
| Motor Relay H-Bridge | VERIFIED | 2026-04-09 | PC15 + PD5, active-LOW relays, 29V motor. Dead time enforced. |
| Sofa Auto-Adjust | IN PROGRESS | 2026-04-11 | C4001 + motor + INA226 integrated. State machine: IDLE→CLOSING→CONTACT→RESETTING. INA226 replaces ACS712 for current sensing. See docs/sofa-mechanism-flowchart.md. |

Status values: NOT STARTED -> IN PROGRESS -> VERIFIED -> BROKEN (with reason)
