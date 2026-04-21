# WeAct Black Pill V3.0 — Pinout Reference

Source: https://stm32-base.org/boards/STM32F401CEU6-WeAct-Black-Pill-V3.0

## MCU

| Parameter | Value |
|-----------|-------|
| MCU | STM32F411CEU6 |
| Core | ARM Cortex-M4 with FPU |
| Max Clock | 100 MHz |
| Flash | 512 KB |
| SRAM | 128 KB |
| Package | UFQFPN48 |

## Oscillators

| Source | Frequency | Notes |
|--------|-----------|-------|
| HSI | 16 MHz | Internal RC |
| LSI | 32 kHz | Internal RC |
| HSE | 25 MHz | External crystal (metal shell) |
| LSE | 32.768 kHz | External crystal (metal shell) |

## Power

- Regulator: Diodes Inc. AP7343 (3.3V @ 300mA)
- Input: 3.52V to 5.25V (via 5V pins or USB)

## Onboard Peripherals

| Device | Pin | Active | Notes |
|--------|-----|--------|-------|
| User LED | PC13 | LOW (sink) | Drive LOW to turn ON |
| User Button (KEY) | PA0 | LOW | Has pull-up |
| Reset Button | NRST | LOW | Hardware reset |
| BOOT0 Button | BOOT0 | HIGH | Enter ROM bootloader |
| Power LED | 3.3V rail | Always on | |

## SPI Flash Footprint (U3, SOP-8)

If populated, shares SPI1 pins. If empty, pins are free.

| Flash Pin | MCU Pin | Function |
|-----------|---------|----------|
| /CS | PA4 | SPI1_NSS |
| DO (MISO) | PA6 | SPI1_MISO |
| DI (MOSI) | PA7 | SPI1_MOSI |
| CLK | PA5 | SPI1_SCK |
| /WP | 3.3V | Write protect (disabled) |
| /HOLD | 3.3V | Hold (disabled) |

## USB Type-C Connector

| Function | MCU Pin | Notes |
|----------|---------|-------|
| D- (DM) | PA11 | Via 10 ohm (R9) |
| D+ (DP) | PA12 | Via 10 ohm (R7) |
| VBUS | 5V rail | Power input |
| CC1/CC2 | GND via 5.1k | Device mode pull-downs |

## SWD Header (4-pin)

| Pin | Name | MCU Pin |
|-----|------|---------|
| 1 | VCC | 3.3V |
| 2 | SWDIO | PA13 |
| 3 | SWCLK | PA14 |
| 4 | GND | GND |

## Header 1 — Left Side (top to bottom)

| # | Silk | MCU Pin | Alternate Functions |
|---|------|---------|---------------------|
| 1 | 5V | 5V rail | Power |
| 2 | G | GND | Ground |
| 3 | 3.3 | 3.3V | Power |
| 4 | B10 | PB10 | I2C2_SCL, TIM2_CH3, SPI2_SCK |
| 5 | B2 | PB2 | BOOT1 |
| 6 | B1 | PB1 | ADC1_IN9, TIM3_CH4 |
| 7 | B0 | PB0 | ADC1_IN8, TIM3_CH3 |
| 8 | A7 | PA7 | ADC1_IN7, SPI1_MOSI, TIM1_CH1N |
| 9 | A6 | PA6 | ADC1_IN6, SPI1_MISO, TIM3_CH1 |
| 10 | A5 | PA5 | ADC1_IN5, SPI1_SCK, DAC_OUT2 |
| 11 | A4 | PA4 | ADC1_IN4, SPI1_NSS, DAC_OUT1 |
| 12 | A3 | PA3 | ADC1_IN3, USART2_RX, TIM5_CH4 |
| 13 | A2 | PA2 | ADC1_IN2, USART2_TX, TIM5_CH3 |
| 14 | A1 | PA1 | ADC1_IN1, TIM5_CH2, TIM2_CH2 |
| 15 | A0 | PA0 | ADC1_IN0, TIM5_CH1, USER KEY |
| 16 | R | NRST | Reset |
| 17 | C15 | PC15 | OSC32_OUT |
| 18 | C14 | PC14 | OSC32_IN |
| 19 | C13 | PC13 | USER LED |
| 20 | VB | VBAT | Battery backup |

## Header 2 — Right Side (top to bottom)

| # | Silk | MCU Pin | Alternate Functions |
|---|------|---------|---------------------|
| 1 | B12 | PB12 | SPI2_NSS, I2C2_SMBA |
| 2 | B13 | PB13 | SPI2_SCK, TIM1_CH1N |
| 3 | B14 | PB14 | SPI2_MISO, TIM1_CH2N |
| 4 | B15 | PB15 | SPI2_MOSI, TIM1_CH3N |
| 5 | A8 | PA8 | I2C3_SCL, TIM1_CH1, MCO1 |
| 6 | A9 | PA9 | USART1_TX, TIM1_CH2, USB_VBUS |
| 7 | A10 | PA10 | USART1_RX, TIM1_CH3 |
| 8 | A11 | PA11 | USB_DM, TIM1_CH4 |
| 9 | A12 | PA12 | USB_DP |
| 10 | A15 | PA15 | SPI3_NSS, TIM2_CH1 |
| 11 | B3 | PB3 | SPI3_SCK, TIM2_CH2, I2C2_SDA |
| 12 | B4 | PB4 | SPI3_MISO, I2C3_SDA |
| 13 | B5 | PB5 | SPI3_MOSI, I2C3_SMBA |
| 14 | B6 | PB6 | I2C1_SCL, TIM4_CH1, USART1_TX |
| 15 | B7 | PB7 | I2C1_SDA, TIM4_CH2, USART1_RX |
| 16 | B8 | PB8 | I2C1_SCL, TIM4_CH3, TIM10_CH1 |
| 17 | B9 | PB9 | I2C1_SDA, TIM4_CH4, TIM11_CH1 |
| 18 | 5V | 5V rail | Power |
| 19 | G | GND | Ground |
| 20 | 3.3 | 3.3V | Power |

## Available Peripherals Summary

| Peripheral | Instances | Notes |
|------------|-----------|-------|
| SPI | SPI1, SPI2, SPI3, SPI4, SPI5 | SPI1 shared with flash footprint |
| I2C | I2C1, I2C2, I2C3 | I2C1: PB6/PB7 on header |
| USART | USART1, USART2, USART6 | USART6 on PA11/PA12 conflicts with USB |
| USB | OTG FS | PA11/PA12, internal PHY |
| Timers | TIM1-5, TIM9-11 | No TIM6/7/8 |
| ADC | ADC1 (16 ch) | PA0-PA7, PB0, PB1 |
| DMA | 2 controllers, 16 streams | |
| GPIO | Up to 36 pins | UFQFPN48 package |

## Bootloader Entry

- **HID Flash**: Hold KEY (PA0) during power-on. PC13 LED blinks. App at 0x08004000.
- **STM32 ROM (ISP)**: Press BOOT0 + Reset simultaneously, release Reset, then BOOT0 after 0.5s.
- **DFU**: Via USB, enters DFU mode with BOOT0.
