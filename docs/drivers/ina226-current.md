# INA226 Current/Power Monitor

**Status**: VERIFIED (calibration confirmed with 1A reference load 2026-04-12)
**Date added**: 2026-04-11
**Source files**: `src/ina226.c`, `include/ina226.h`

## Hardware Connection

| Signal | MCU Pin | Breakout Pin | Notes |
|--------|---------|--------------|-------|
| SCL    | PH7     | I2C_0 SCL (J1-46) | AF4, I2C3, open-drain |
| SDA    | PH8     | I2C_0 SDA (J1-44) | AF4, I2C3, open-drain |

- **Sensor**: INA226 **clone** (fake TI markings), I2C current/voltage/power monitor
- **I2C address**: 0x40 (A0=GND, A1=GND)
- **Shunt resistor**: 10 mohm (0.01 ohm), user-soldered, multimeter-verified 10mV at 1A
- **Clone issue**: Vsh ADC LSB ~1.0 uV instead of TI's 2.5 uV. MFR/DIE IDs spoofed (0x5449/0x2260). Reads 2.5x too many shunt voltage counts. Effective `INA226_SHUNT_OHM = 0.025` compensates.
- **VBUS**: Connected to 29V sofa motor power supply
- **Application**: Measures motor current for sofa backplane auto-adjust current trip detection

## Replaces ACS712

The ACS712 analog current sensor was abandoned due to persistent ADC wiring issues on the Portenta H7 breakout board (`_C` pin floating reads). The INA226 uses I2C (digital) — no analog signal path issues.

Old ACS712 code is still in `src/acs712.c` but not compiled (removed from Makefile).

## Initialization

1. I2C3 init: PH7/PH8, AF4, 120 MHz D2PCLK1, timing 0x307075B1
2. Read Manufacturer ID (0x5449) to verify device present
3. Reset device (config bit 15)
4. Configure: 64x averaging, 1.1ms conversion, continuous shunt+bus
5. Calibration register = 2048 (Current LSB = 0.1 mA with effective 0.025 ohm shunt)

## Calibration Math

```
Current_LSB = Max_Current / 2^15 = 3.2768 / 32768 = 0.0001 A = 0.1 mA
Cal = 0.00512 / (Current_LSB * R_shunt_eff) = 0.00512 / (0.0001 * 0.025) = 2048
Power_LSB = 25 * Current_LSB = 0.0025 W = 2.5 mW

NOTE: Physical shunt is 10mohm. Effective R_shunt = 0.025 because the clone
chip's Vsh ADC has ~1.0uV/count LSB (not TI's 2.5uV). Factor = 2.5x.
```

## API

```c
bool  INA226_Init(void);              // Init I2C3 + configure INA226
float INA226_ReadCurrent_mA(void);    // Signed current (+ = into VIN+)
float INA226_ReadBusVoltage_mV(void); // Bus voltage (1.25 mV LSB)
float INA226_ReadShuntVoltage_uV(void); // Shunt voltage (2.5 uV LSB, signed)
float INA226_ReadPower_mW(void);      // Power (25 * current_lsb * bus_v)
bool  INA226_IsFault(void);           // True if I2C comm failed
void  INA226_PrintDiag(void);         // Print registers + measurements over VCP
```

## Serial Commands

| Command | Action |
|---------|--------|
| `ina_diag` | Print config/cal registers + Vbus/Vsh/I/P measurements |
| `ina_read` | Quick current + bus voltage readout |
| `ina_test` | Comprehensive self-test: register dump, cross-check chip math, CAL test |

## Gotchas

- **This is a clone chip.** The `INA226_SHUNT_OHM` value (0.025) is NOT the physical shunt resistance (0.01). It includes a 2.5x correction for the clone's non-standard ADC LSB. If replacing the chip with a genuine TI INA226, change back to 0.01.
- I2C3 shares the `I2c123ClockSelection` register with I2C1 (PMIC). Both use D2PCLK1 so no conflict.
- GPIOH clock is already enabled early in main() for PH1 (oscillator enable), so I2C3 MSP doesn't need to worry about clock order.
- The INA226 runs in continuous mode (shunt + bus). Reads return the latest completed conversion — no need to trigger or wait.
- With 64x averaging and 1.1ms conversion, each measurement takes ~70ms. This is fine for the sofa state machine (200ms tick rate).
