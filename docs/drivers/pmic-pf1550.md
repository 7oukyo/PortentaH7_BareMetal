# PMIC — NXP PF1550

**Status**: VERIFIED (cold boot working 2026-03-27)

## Hardware

| Item       | Value                              |
|------------|------------------------------------|
| IC         | NXP PF1550                         |
| Bus        | I2C1 (NOT I2C4 — corrected 03-26)  |
| SCL        | PB6 (AF4, open-drain)              |
| SDA        | PB7 (AF4, open-drain)              |
| 7-bit addr | 0x08                               |
| STANDBY    | PJ0 (push-pull, drive LOW for RUN) |
| Speed      | Standard Mode 100 kHz              |

## Initialization order

PMIC is initialized **after** `HAL_Init()` and `SystemClock_Config()`. The MCU boots on PMIC OTP
default voltages — HAL and clock config work fine without PMIC being configured first. This matches
the reference project (Portenta_Cube_Template).

**Critical prerequisite**: `SystemInit()` must perform a full RCC reset (see
`system_stm32h7xx_dualcore_boot_cm4_cm7.c`). Without this, cold boot fails with a reset loop.

Call sequence in `main()`:
```c
// PJ0 LOW (PMIC STANDBY pin → RUN mode)
HAL_Init();
// PH1 HIGH (oscillator enable), HAL_Delay(10)
SystemClock_Config();
PeriphCommonClock_Config();
GPIO_LEDs_Init();
PMIC_Init();              // I2C1 HAL, sets all voltage rails
```

## Implementation

`src/pmic.c` / `include/pmic.h` — uses HAL I2C1.

**I2C timing register**: `0x307075B1U`
- For D2PCLK1 = 120 MHz (after SystemClock_Config, APB1 = HCLK/2 = 120MHz)
- Reference: Portenta_Cube_Template/CM7/Core/Src/i2c.c

**I2C1 MSP init** (`stm32h7xx_hal_msp.c`):
- Peripheral clock: I2C1 from D2PCLK1 via `RCC_I2C123CLKSOURCE_D2PCLK1`
- GPIO: PB6 (SCL) and PB7 (SDA), AF4, open-drain, no pull, very high speed
- Clock enable: `__HAL_RCC_GPIOB_CLK_ENABLE()`, `__HAL_RCC_I2C1_CLK_ENABLE()`

## Gotchas

- **WRONG BUS (fixed 2026-03-26)**: Original code used I2C4 on PD12/PD13 — those pins are NOT
  connected to the PMIC. The PMIC is on I2C1 (PB6/PB7). This was masked by the Arduino bootloader
  having already configured the PMIC.
- **SystemInit must do full RCC reset (fixed 2026-03-27)**: A minimal SystemInit (FPU+VTOR only)
  causes cold boot reset loops. The full reference SystemInit resets all RCC state, which is required
  for the PMIC/MCU power sequencing to work correctly.
- **CM4 boot with stale firmware**: `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)` will run whatever is in
  flash bank 2. If old Arduino CM4 code is there, it blinks blue LED and may interfere. Disable CM4
  boot until CM4 firmware is written.
- **SW_VOLT writes only effective once**: Per reference, SW1/SW2 voltage registers can only be set
  during the PMIC startup window (first power-on). MCU reset without power cycle has no effect on
  these registers.
- **Register 0x50 must be written last**: Writing LDO2 enable (0x50) while other I2C transactions
  are pending can kill I2C responsiveness. Always write it as the final register.
- **PJ0 STANDBY pin**: Must be driven LOW before HAL_Init() for PMIC RUN mode. STANDBYINV=0 (OTP
  default) means LOW=RUN, HIGH/float=Standby.

## Key register writes

| Register | Value | Purpose |
|----------|-------|---------|
| 0x4F | 0x00 | LDO2 voltage (1.8V) |
| 0x4C | 0x05 | LDO1 voltage (1.0V) |
| 0x4D | 0x0F | LDO1 enable |
| 0x52 | 0x09 | LDO3 voltage (1.2V) |
| 0x53 | 0x0F | LDO3 enable |
| 0x9C | 0x80 | Charger LED off (duty cycle) |
| 0x9E | 0x20 | Charger LED disable |
| 0x42 | 0x02 | SW3 current limit (2A) |
| 0x94 | 0xA0 | VBUS input current limit (1.5A) |
| 0x38 | 0x07 | SW2 RUN voltage → 3.3V |
| 0x39 | 0x06 | SW2 STANDBY voltage → 3.0V |
| 0x3A | 0x06 | SW2 SLEEP voltage → 3.0V |
| 0x3B | 0x0F | SW2_CTRL enable |
| 0x32 | 0x07 | SW1 RUN voltage → 3.3V |
| 0x33 | 0x06 | SW1 STANDBY voltage → 3.0V |
| 0x34 | 0x06 | SW1 SLEEP voltage → 3.0V |
| 0x35 | 0x0F | SW1_CTRL enable |
| 0x50 | 0x0F | LDO2 enable (WRITE LAST) |

## Minimal usage

```c
#include "pmic.h"

// Called after HAL_Init() and SystemClock_Config() in main()
PMIC_Init();
```
