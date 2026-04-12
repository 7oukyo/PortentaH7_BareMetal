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
14. Motor_Init() (PC15 + PD5 relay GPIOs, both OFF/HIGH)
15. INA226_Init() (I2C3, PH7/PH8, addr 0x40, 64x averaging, continuous mode)
16. Sofa controller starts in IDLE state
17. Main loop: C4001_Poll() + sofa_tick(). send_report() on new C4001 frame. send_sofa_status() every 200ms. Red LED = presence, Blue LED = motor active.

## INA226 Current/Power Monitor

- **Bus**: I2C3 (PH7=SCL, PH8=SDA, AF4), breakout I2C_0
- **Address**: 0x40 (A0=GND, A1=GND)
- **I2C timing**: 0x307075B1 (120 MHz D2PCLK1, ~400 kHz), same as PMIC I2C1
- **Config**: 64x averaging, 1.1ms conversion, continuous shunt+bus mode
- **Shunt**: 10 mohm physical. Effective INA226_SHUNT_OHM = 0.025 (compensates clone 2.5x Vsh overread)
- **Calibration**: Cal=2048, Current LSB = 0.1 mA, Power LSB = 2.5 mW
- **Clone chip**: Vsh ADC LSB ~1.0 uV (not TI's 2.5 uV). MFR/DIE IDs spoofed (0x5449/0x2260).
- **Application**: Motor current sensing for sofa backplane auto-adjust. VBUS = 29V motor supply.
- See [docs/drivers/ina226-current.md](drivers/ina226-current.md) for driver details

## Motor Relay H-Bridge

- **Relay 1**: PC15 (breakout GPIO_1) — Motor+ polarity
- **Relay 2**: PD5 (breakout GPIO_3) — Motor- polarity
- **Logic**: Active-LOW (pin LOW = relay energized, COM→NO = +29V)
- **Safety**: Both HIGH = motor stopped. NEVER both LOW. 100ms dead time on direction change.
- **Application**: Sofa backplane auto-adjust (see [docs/sofa-mechanism-flowchart.md](sofa-mechanism-flowchart.md))
- See [docs/drivers/motor-relay.md](drivers/motor-relay.md) for driver details

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
