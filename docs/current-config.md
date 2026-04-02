# Current Hardware & Firmware Configuration

Living document of what's actually configured and running in the firmware right now.
For workflow rules and code conventions, see CLAUDE.md.
For per-peripheral details, see docs/drivers/*.md.

---

## MCU & Board

- **MCU**: STM32H747XIH6 — Cortex-M7 @ 480 MHz, Cortex-M4 @ 240 MHz
- **Active core**: CM7 only. CM4 boot is **disabled** (`HAL_RCCEx_EnableBootCore` commented out) for now.
- **Board**: Arduino Portenta H7 on Portenta Breakout Board
- **Debugger**: ST-Link V3 Mini via SWD through breakout 20-pin JTAG header
- **HSE crystal**: 25 MHz, gated by OSCEN (PH1 GPIO, must be HIGH before clock config)

## Memory Map (no Arduino bootloader — full flash)

| Region   | Start        | Size   | Current Usage                  |
|----------|--------------|--------|--------------------------------|
| Flash    | 0x08000000   | 2 MB   | Firmware code + const data     |
| DTCMRAM  | 0x20000000   | 128 KB | Fast variables (unused currently) |
| AXI SRAM | 0x24000000   | 512 KB | .data, .bss, heap, **stack** (_estack = 0x24080000) |
| SRAM D2  | 0x30000000   | 288 KB | DMA buffers, shared mem        |
| SRAM D3  | 0x38000000   | 64 KB  | Available                      |
| SDRAM    | 0xC0000000   | 8 MB   | External, requires FMC init    |

Stack is at the end of AXI SRAM per the linker script, not in DTCMRAM.

## Clock Tree

- **PLL1**: HSE 25 MHz / M=5 * N=192 / P=2 = **480 MHz SYSCLK**
- **HCLK**: 480 / 2 = **240 MHz**
- **APB1/2/3/4**: 240 / 2 = **120 MHz**
- **TIM6 clock**: APB1 x2 (prescaler doubling) = **240 MHz**
- **PLL2**: HSE 25 MHz / M=5 * N=72 = 360 MHz VCO; P=3 (120 MHz SPI1/2/3), Q=3, R=2 (180 MHz SPI4/5)
- **PLL3**: HSE 25 MHz / M=25 * N=192 = 192 MHz VCO; Q=4 -> **48 MHz USB clock**
- **Power**: VOS0 (highest), SMPS 1V8 supplies LDO
- **Flash latency**: 4 wait states
- **I-Cache**: Enabled (SCB_EnableICache). D-Cache disabled (breaks DMA without cache maintenance).

## PMIC (NXP PF1550)

- **Bus**: I2C1 (PB6=SCL, PB7=SDA, AF4, addr 0x08)
- **Timing**: 0x307075B1 (valid for D2PCLK1 = 120 MHz)
- **Control**: PJ0 LOW = PMIC RUN mode (must be first thing in main)
- **Called after**: HAL_Init() and SystemClock_Config()
- See [docs/drivers/pmic-pf1550.md](drivers/pmic-pf1550.md) for full register sequence

## Init Sequence (main.c)

1. PJ0 LOW (PMIC STANDBY -> RUN)
2. HAL_Init()
3. SCB_EnableICache()
4. PH1 HIGH + 10 ms delay (oscillator enable)
5. SystemClock_Config() (480 MHz)
6. PeriphCommonClock_Config() (PLL2 for SPI)
7. GPIO_LEDs_Init() (PK5/6/7 active LOW)
8. PMIC_Init() (HAL I2C1, all register writes)
9. PJ4 reset toggle + delays (USB3320 PHY reset)
10. MX_USB_DEVICE_Init() (USB CDC, PLL3 48 MHz clock configured in MSP)
11. LEDs OFF
12. LedPwm_Init() (TIM6 10kHz ISR for green LED RX blink timing)
13. C4001_Init() (UART4 9600 baud, presence mode, starts sensor)
14. Main loop: C4001_Poll() — parses sensor data, sends formatted status over CDC on every new frame, red LED = presence

## OpenOCD Flash

```
openocd -f openocd.cfg \
  -c "program build/firmware.bin 0x08000000 verify reset exit"
```

Config uses `interface/stlink-dap.cfg` + `target/stm32h7x.cfg` with `DUAL_CORE=0`.

## What Boots Without Firmware (OTP Defaults)

| Rail | State | Consequence |
|------|-------|-------------|
| SW3 (+3V1) | ON | MCU, oscillators, I2C pullups, LEDs powered |
| VSNVS (+VBAT) | ON 3.0V | MCU VBAT, always on |
| SW1, SW2, LDO1/2/3 | OFF | No SDRAM, Ethernet, USB3320, connectors, ANX7625 |

MCU can boot and run code on OTP defaults alone. PMIC_Init() brings up the rest.
