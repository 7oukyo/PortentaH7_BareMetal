# BlackPill F411 — Pin Assignment

Pin assignments chosen for the sofa auto-adjust controller port.

## Assigned Pins

| Function | Pin | Peripheral | AF | Notes |
|----------|-----|------------|-----|-------|
| C4001 TX (MCU->sensor) | PA9 | USART1_TX | AF7 | Header 2 pin 6 |
| C4001 RX (sensor->MCU) | PA10 | USART1_RX | AF7 | Header 2 pin 7 |
| INA226 SCL | PB6 | I2C1_SCL | AF4 | Header 2 pin 14, open-drain |
| INA226 SDA | PB7 | I2C1_SDA | AF4 | Header 2 pin 15, open-drain |
| Motor Relay 1 | PB0 | GPIO output | - | Header 1 pin 7, active LOW |
| Motor Relay 2 | PB1 | GPIO output | - | Header 1 pin 6, active LOW |
| USB D- | PA11 | USB_OTG_FS_DM | AF10 | Header 2 pin 8 / USB-C |
| USB D+ | PA12 | USB_OTG_FS_DP | AF10 | Header 2 pin 9 / USB-C |
| User LED | PC13 | GPIO output | - | Onboard, active LOW (sink) |
| User Button | PA0 | GPIO input | - | Onboard KEY, active LOW |
| SWD IO | PA13 | SWDIO | - | SWD header pin 2 |
| SWD CLK | PA14 | SWCLK | - | SWD header pin 3 |

## Mapping from Portenta H7

| Function | H7 Pin | H7 Peripheral | F411 Pin | F411 Peripheral |
|----------|--------|---------------|----------|-----------------|
| C4001 TX | PA0 | UART4_TX (AF8) | PA9 | USART1_TX (AF7) |
| C4001 RX | PI9 | UART4_RX (AF8) | PA10 | USART1_RX (AF7) |
| INA226 SCL | PH7 | I2C3_SCL (AF4) | PB6 | I2C1_SCL (AF4) |
| INA226 SDA | PH8 | I2C3_SDA (AF4) | PB7 | I2C1_SDA (AF4) |
| Relay 1 | PC15 | GPIO | PB0 | GPIO |
| Relay 2 | PD5 | GPIO | PB1 | GPIO |
| LED Red | PK5 | GPIO | PC13 | GPIO (single LED) |
| LED Green | PK6 | GPIO | PC13 | GPIO (shared) |
| LED Blue | PK7 | GPIO | PC13 | GPIO (shared) |
| USB | PA11/PA12 | OTG_HS + ULPI | PA11/PA12 | OTG_FS (internal PHY) |

## Free Pins (available for expansion)

| Pin | Header | Possible Uses |
|-----|--------|---------------|
| PA1 | H1-14 | ADC1_IN1, TIM5_CH2 |
| PA2 | H1-13 | ADC1_IN2, USART2_TX |
| PA3 | H1-12 | ADC1_IN3, USART2_RX |
| PA4 | H1-11 | ADC1_IN4, SPI1_NSS (flash) |
| PA5 | H1-10 | ADC1_IN5, SPI1_SCK (flash) |
| PA6 | H1-9 | ADC1_IN6, SPI1_MISO (flash) |
| PA7 | H1-8 | ADC1_IN7, SPI1_MOSI (flash) |
| PA8 | H2-5 | I2C3_SCL, TIM1_CH1 |
| PA15 | H2-10 | SPI3_NSS, TIM2_CH1 |
| PB2 | H1-5 | BOOT1 |
| PB3 | H2-11 | SPI3_SCK, I2C2_SDA |
| PB4 | H2-12 | SPI3_MISO, I2C3_SDA |
| PB5 | H2-13 | SPI3_MOSI |
| PB8 | H2-16 | I2C1_SCL, TIM4_CH3 |
| PB9 | H2-17 | I2C1_SDA, TIM4_CH4 |
| PB10 | H1-4 | I2C2_SCL, SPI2_SCK |
| PB12-15 | H2 1-4 | SPI2 |
| PC14 | H1-18 | OSC32_IN (LSE crystal) |
| PC15 | H1-17 | OSC32_OUT (LSE crystal) |

## Wiring Diagram

```
BlackPill F411           External Hardware
==============           =================

PA9  (USART1_TX) ------> C4001 RX
PA10 (USART1_RX) <------ C4001 TX
                          C4001 VCC -> 3.3V or 5V (check sensor spec)
                          C4001 GND -> GND

PB6  (I2C1_SCL) -------> INA226 SCL (with 4.7k pull-up to 3.3V)
PB7  (I2C1_SDA) <------> INA226 SDA (with 4.7k pull-up to 3.3V)
                          INA226 A0 -> GND (addr 0x40)
                          INA226 A1 -> GND
                          INA226 VS -> motor supply (measure bus voltage)
                          INA226 VCC -> 3.3V
                          INA226 GND -> GND

PB0  (GPIO) -----------> Relay Module 1 IN (active LOW)
PB1  (GPIO) -----------> Relay Module 2 IN (active LOW)
                          Relay VCC -> 5V
                          Relay GND -> GND

USB-C ------------------> Host PC (VCP serial)
SWD Header -------------> ST-Link V2/V3 debugger
```
