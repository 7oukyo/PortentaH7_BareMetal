# Peripheral & Module Status

## Board Bringup

| Peripheral     | Status   | Date       | Notes                                                        |
|----------------|----------|------------|--------------------------------------------------------------|
| PMIC (PF1550)  | VERIFIED | 2026-03-27 | HAL I2C1 post-HAL_Init; cold boot fixed via SystemInit reset |
| Clock (480MHz) | VERIFIED | 2026-03-25 | HSE 25MHz BYPASS -> PLL1 -> 480MHz CM7                       |
| GPIO (LED)     | VERIFIED | 2026-03-25 | PK5/PK6/PK7 active LOW; green blink confirmed                |

## Onboard Peripherals

| Peripheral       | Status      | Date       | Notes                                             |
|------------------|-------------|------------|---------------------------------------------------|
| UART (USB-C VCP) | NOT STARTED |            |                                                   |
| SPI              | NOT STARTED |            |                                                   |
| I2C (user)       | NOT STARTED |            |                                                   |
| SDRAM (8MB)      | NOT STARTED |            | FMC, 0xC0000000                                   |
| ETH              | NOT STARTED |            |                                                   |
| USB              | NOT STARTED |            |                                                   |
| QSPI Flash       | NOT STARTED |            |                                                   |
| SD Card          | NOT STARTED |            |                                                   |
| PWM (TIM6/LED)   | VERIFIED    | 2026-03-25 | Software PWM via TIM6 ISR; RGB rainbow on PK5/6/7 |
| ADC              | NOT STARTED |            |                                                   |
| DAC              | NOT STARTED |            |                                                   |
| CAN              | NOT STARTED |            |                                                   |

## External Modules

| Module | Status | Date | Notes |
|--------|--------|------|-------|
|        |        |      |       |

Status values: NOT STARTED -> IN PROGRESS -> VERIFIED -> BROKEN (with reason)
