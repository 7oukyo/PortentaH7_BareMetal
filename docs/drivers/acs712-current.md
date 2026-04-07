# ACS712 5A Current Sensor

**Status**: VERIFIED (pending hardware test)
**Date added**: 2026-04-06
**Source file**: `src/acs712.c`, `include/acs712.h`

## Hardware Connection

| Signal | MCU Pin | Breakout Pin | Notes |
|--------|---------|--------------|-------|
| Analog output | PA0_C | Analog A0 | Dedicated ADC pin, no GPIO conflict |

- **Sensor model**: ACS712-05B (5A bidirectional)
- **Sensitivity**: 185 mV/A
- **Zero-current output**: Vcc / 2 (nominally 1.65V at 3.3V supply)
- **ADC**: ADC1 channel INP0, 16-bit resolution, single-ended

## Pin Conflict Note

PA0_C is a **dedicated analog pin** separate from PA0 in the STM32H747 BGA package.
PA0 is used for UART4_TX (C4001 mmWave sensor). Both work simultaneously because
SYSCFG_PMCR.PA0SO = 0 (default) keeps PA0_C routed directly to the ADC while PA0
remains available for GPIO/AF functions.

## Initialization Order

1. PMIC must be up first (stable 3.3V for VREF+)
2. `ACS712_Init()` called in main.c after `PMIC_Init()`
3. ADC1 clock: ADC12 async clock / 6
4. ADC self-calibration runs (single-ended, offset)
5. 64-sample bias calibration — averages zero-current reading
6. Bias sanity check: must be within 1650 ± 450 mV, else fault flag set

## API

```c
ACS712_Init();                    // Init ADC1 + calibrate bias
float ma = ACS712_ReadCurrent_mA(); // Read current in milliamps
bool bad = ACS712_IsFault();      // True if bias calibration failed
float bias = ACS712_GetBias_mV(); // Calibrated bias for diagnostics
```

## Output Format

Current reading is appended to the C4001 mmWave serial report:
```
[12.345] DETECTED | range=1.20m spd=0.50m/s e=42 | frames=100 rx=2048B | raw=$DFHPD,1 | I=523.4mA
```

If bias calibration failed:
```
[12.345] DETECTED | ... | I=FAULT
```

## Gotchas

- The ACS712 outputs Vcc/2 at zero current. If Vcc is noisy, readings will be noisy.
- The sensor is bidirectional — negative current gives values below Vcc/2.
- With 185 mV/A sensitivity and 16-bit ADC (0.05 mV/count), theoretical resolution
  is ~0.27 mA/count. In practice, expect ±10-20 mA noise without averaging.
- If the sensor is not connected at startup, bias calibration will read ~0V or rail
  voltage and flag as FAULT — this is by design.
