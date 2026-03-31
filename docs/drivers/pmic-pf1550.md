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

---

## OTP (One-Time Programmable) Registers

The PF1550 has OTP fuse registers (addresses 0x1C–0x36) that store factory-programmed defaults for
voltage rails, enable states, and STANDBYINV polarity. These are the values the PMIC uses at
power-on before any I2C configuration.

### OTP properties

- **Read-only in practice**: programming requires 7.7V on an external pin, not available on the
  Portenta H7 board. Even if attempted, OTP fuses can only be burned (0→1), never cleared (1→0).
- Arduino likely programs the OTP themselves during manufacturing (NXP does not do custom OTP
  for small volumes).
- Our verified cold boot measurements confirm OTP defaults: SW1=0V, SW2=0V, SW3=3.1V, LDOs=0V.
  The Arduino bootloader then configures SW1/SW2/LDOs via I2C to bring up remaining rails.

### Reading OTP registers (indirect access)

OTP values are not directly readable as normal registers. They must be accessed via the OTP
controller using an indirect read sequence:

```c
// Step 1: Initialize OTP controller (unlock keys + config)
PMIC_WriteReg(0x6F, 0x15);
PMIC_WriteReg(0x9F, 0x50);
PMIC_WriteReg(0xDF, 0xAB);
PMIC_WriteReg(0x6B, 0x01);

// Step 2: Write desired OTP register address (0x1C..0x36) to FMRADDR
PMIC_WriteReg(0xC4, otp_address);

// Step 3: Wait for OTP controller to fetch the value
HAL_Delay(2);

// Step 4: Read result from FMRDATA register
uint8_t value = PMIC_ReadReg(0xC5);
```

### OTP register dump (**NOT VERIFIED** — from external research, may not match our board)

Addresses 0x1C–0x35, read via indirect OTP access on a different Portenta H7 unit:

```
0x1C: 00  0x1D: 06  0x1E: 1A  0x1F: D8
0x20: 02  0x21: 00  0x22: 00  0x23: 00
0x24: 00  0x25: 00  0x26: 00  0x27: 00
0x28: 00  0x29: 00  0x2A: 00  0x2B: 02
0x2C: 00  0x2D: 84  0x2E: 2B  0x2F: 00
0x30: 14  0x31: 00  0x32: 00  0x33: 00
0x34: 00  0x35: 00
```

To verify on our board: implement the indirect read sequence above and dump all OTP registers.

---

## Standby Mode and Low-Power Operation

### Overview

The PF1550 supports RUN and STANDBY power modes. The MCU controls the mode via the **PJ0**
(PMIC_STBY) pin. Each switching regulator (SW1, SW2) has separate voltage registers for RUN,
STANDBY, and SLEEP modes, allowing different voltages per mode.

**SW3 is the exception**: SW3 voltage is set from OTP for all modes (always 3.1V on our board,
not configurable). SW3 powers the MCU — it does not change between RUN and STANDBY.

### Pin control

| PJ0 Level | PMIC Mode | Notes |
|-----------|-----------|-------|
| LOW       | RUN       | Normal operation, must be driven LOW before HAL_Init() |
| HIGH      | STANDBY   | Reduced voltages on SW1/SW2 per STANDBY registers |

STANDBYINV = 0 (OTP default): LOW = RUN, HIGH = STANDBY.

### PMIC status register

Register **0x67** reflects the current power state:

- Value **0x0C** → RUN mode
- Value **0x0D** → STANDBY mode

### Rail behavior per mode (our board's verified wiring)

| PMIC Output | Net    | Powers                                    | RUN/STANDBY configurable? |
|-------------|--------|-------------------------------------------|---------------------------|
| SW1         | +3V1SW | SDRAM, Ethernet, USB3320                  | Yes — SW1_VOLT / SW1_STBY registers |
| SW2         | +3V3   | External VCC (MKR/HD connectors), JTAG    | Yes — SW2_VOLT / SW2_STBY registers |
| SW3         | +3V1   | MCU VDD, oscillators, LDO inputs, LEDs    | **No** — OTP-fixed at 3.1V for all modes |

### Arduino default behavior

The Arduino bootloader programs the same voltage for both RUN and STANDBY modes on SW1/SW2
(both set to 3.0V). This means toggling PJ0 has **no visible effect** on output voltages with
stock Arduino PMIC configuration.

### Our bare-metal configuration

Our `PMIC_Init()` sets different voltages per mode:

| Register | Value | Meaning |
|----------|-------|---------|
| 0x32     | 0x07  | SW1 RUN = 3.3V |
| 0x33     | 0x06  | SW1 STANDBY = 3.0V |
| 0x38     | 0x07  | SW2 RUN = 3.3V |
| 0x39     | 0x06  | SW2 STANDBY = 3.0V |

This means toggling PJ0 will switch SW1 and SW2 between 3.3V and 3.0V.

### Low-power mode strategy (future reference)

To implement a real system low-power mode using the PMIC:

1. **Configure PMIC**: set lower STANDBY voltages for SW1 and SW2 (e.g., 3.0V or lower).
   SW3 (MCU core) stays at 3.1V regardless — it's OTP-fixed.

2. **Enter low-power**: shut down peripherals, lower MCU clock, then set PJ0 HIGH to switch
   PMIC to STANDBY mode. SW1/SW2 voltages drop to STANDBY values.

3. **Peripheral impact**: peripherals on SW1 (SDRAM, Ethernet, USB3320) and SW2 (external
   VCC) will see reduced voltage. Some may stop functioning — this is expected and acceptable
   if they're not needed during low-power.

4. **Wake-up**: MCU waits for a wake-up event (external interrupt, RTC alarm, etc.), then sets
   PJ0 LOW to return PMIC to RUN mode. Voltages rise back to RUN values. Re-initialize
   peripherals as needed.

5. **Caution**: dropping SW1 too low (e.g., below 2.5V) can kill USB-C UART and other 3.3V
   peripherals. The MCU itself stays alive on SW3 (3.1V) but may need clock reduction to
   remain stable at lower peripheral voltages.

6. **PMIC interrupt (PK0)**: the PMIC INT pin can notify the MCU of power events. Could be
   used to confirm mode transitions or detect faults.

### Why Arduino defaults to 3.0V

The Arduino bootloader configures SW1/SW2 to 3.0V (not 3.3V). Likely reason: compatibility
with 3.0V coin cell battery operation. At 3.3V the coin cell would drain rapidly and provide
unreliable voltage. Our bare-metal firmware overrides this to 3.3V since we run from USB-C
power only and want maximum peripheral reliability.

### PMIC interrupt pin

| Signal   | MCU Pin | Direction    | Notes |
|----------|---------|--------------|-------|
| PMIC_INT | PK0     | PMIC → MCU   | Open-drain, active LOW, pullup to +3V1. Asserted on any PMIC interrupt event. |

Not currently used in our firmware. Could be configured to detect over-current, thermal
shutdown, or power state transitions.
