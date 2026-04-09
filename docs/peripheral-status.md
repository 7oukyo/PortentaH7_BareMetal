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
| I2C (user)       | NOT STARTED |            |                                                   |
| SDRAM (8MB)      | NOT STARTED |            | FMC, 0xC0000000                                   |
| ETH              | NOT STARTED |            |                                                   |
| USB              | VERIFIED    | 2026-04-01 | OTG_HS + USB3320 ULPI, CDC class, echo-back tested |
| QSPI Flash       | NOT STARTED |            |                                                   |
| SD Card          | NOT STARTED |            |                                                   |
| PWM (TIM6/LED)   | VERIFIED    | 2026-04-01 | TIM6 10kHz ISR; green LED RX blink timing           |
| ADC              | VERIFIED    | 2026-04-09 | ADC1 CH0 (PA0_C/ANALOG_A0), 16-bit, ACS712 current sensor |
| DAC              | NOT STARTED |            |                                                   |
| CAN              | NOT STARTED |            |                                                   |

## External Modules

| Module | Status | Date | Notes |
|--------|--------|------|-------|
| C4001 mmWave | IN PROGRESS | 2026-04-02 | UART4 (PA0/PI9) 9600 baud, presence mode, CDC output. Decoupled from other modules (2026-04-09). |
| ACS712 5A Current | IN PROGRESS | 2026-04-09 | PA0_C (ADC1_INP0/ANALOG_A0). Code verified, ADC reads correctly. Pending: user must wire sensor to ANALOG_A0 and verify bias calibration with motor load. PC2/A4 path tested and failed (see driver doc). |
| Motor Relay H-Bridge | VERIFIED | 2026-04-09 | PC15 + PD5, active-LOW relays, 29V motor. Dead time enforced. Auto-test cycle + current monitoring. Serial output pending verification. |

Status values: NOT STARTED -> IN PROGRESS -> VERIFIED -> BROKEN (with reason)
