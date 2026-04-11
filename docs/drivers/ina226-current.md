# INA226 Current/Power Monitor

**Status**: VERIFIED (I2C comm + current reading confirmed 2026-04-11)
**Date added**: 2026-04-11
**Source files**: `src/ina226.c`, `include/ina226.h`

## Hardware Connection

| Signal | MCU Pin | Breakout Pin | Notes |
|--------|---------|--------------|-------|
| SCL    | PH7     | I2C_0 SCL (J1-46) | AF4, I2C3, open-drain |
| SDA    | PH8     | I2C_0 SDA (J1-44) | AF4, I2C3, open-drain |

- **Sensor**: INA226 (TI), I2C current/voltage/power monitor
- **I2C address**: 0x40 (A0=GND, A1=GND)
- **Shunt resistor**: 0.1 ohm (100 mOhm), confirmed by user
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
5. Calibration register = 512 (Current LSB = 0.1 mA with 0.1 ohm shunt)

## Calibration Math

```
Current_LSB = Max_Current / 2^15 = 3.2768 / 32768 = 0.0001 A = 0.1 mA
Cal = 0.00512 / (Current_LSB * R_shunt) = 0.00512 / (0.0001 * 0.1) = 512
Power_LSB = 25 * Current_LSB = 0.0025 W = 2.5 mW
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

## Gotchas

- The INA226 needs the shunt resistor value to be correct for accurate readings. If `INA226_SHUNT_OHM` doesn't match your module, current/power will scale linearly wrong.
- I2C3 shares the `I2c123ClockSelection` register with I2C1 (PMIC). Both use D2PCLK1 so no conflict.
- GPIOH clock is already enabled early in main() for PH1 (oscillator enable), so I2C3 MSP doesn't need to worry about clock order.
- The INA226 runs in continuous mode (shunt + bus). Reads return the latest completed conversion — no need to trigger or wait.
- With 64x averaging and 1.1ms conversion, each measurement takes ~70ms. This is fine for the sofa state machine (200ms tick rate).
