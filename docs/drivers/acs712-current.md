# ACS712 5A Current Sensor

**Status**: ABANDONED — replaced by INA226 (I2C) on 2026-04-11. See `docs/drivers/ina226-current.md`.
**Date added**: 2026-04-06
**Last updated**: 2026-04-11
**Source files**: `src/acs712.c`, `include/acs712.h` (kept but NOT compiled)

## Hardware Connection

| Signal | MCU Pin | Breakout Pin | Notes |
|--------|---------|--------------|-------|
| Analog output | PA1_C | Analog A1 | ADC1_INP1, dedicated analog pin |

- **Sensor model**: ACS712-05B (5A bidirectional)
- **Sensitivity**: 185 mV/A
- **Zero-current output**: Vcc / 2 (nominally 2.5V at 5V supply, measured 2.45V with multimeter)
- **ADC**: ADC1 channel INP1, 16-bit resolution, single-ended
- **VREF+**: 3.1V from PMIC SW3 (measured)

## Pin History

### PA0_C / ANALOG_A0 (ADC1_INP0)
Original pin. Reads ~50 mV consistently instead of expected ~2.45V. UART4_TX (PA0) shares the PA0/PA0_C naming but the analog switch PMCR.PA0SO was confirmed OPEN (bit 24=1). Suspected trace-level issue or unexpected UART leakage. Abandoned.

### PC2 / ANALOG_A4
Tested channels 4, 12, 18 — none read correctly. CH18 was PA4/ULPI_D0 (USB noise). CH12 and CH4 read VREF+. Root cause unresolved. Abandoned.

### PA1_C / ANALOG_A1 (ADC1_INP1) — CURRENT
Selected after PA0_C failure. ADC hardware reads per-channel correctly (A0 and A1 return different voltages in `adc_scan`). Currently reads floating (~3100 mV = VREF+) which means the physical wire from ACS712 OUT to breakout ANALOG_A1 needs verification.

### CubeMX Reference
Generated code in `CubeMX_Output/PortentaH7_MX/CM7/` confirms:
- `HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PA1, SYSCFG_SWITCH_PA1_OPEN)` required for `_C` pins
- ADC clock source: PLL2P at 90 MHz (PLL2 M=5, N=72, P=4). Our code uses per_ck / 6 = 10.7 MHz to avoid PLL2 conflict with SPI.
- GPIOA clock enable needed in MSP

## Initialization Order

1. PMIC must be up first (stable 3.1V for VREF+ via SW3)
2. `ACS712_Init()` called in main.c after `PMIC_Init()`
3. ADC1 clock: per_ck (HSI 64 MHz) / 6 = 10.7 MHz
4. ADC offset calibration (`HAL_ADCEx_Calibration_Start`, single-ended)
5. SYSCFG analog switch: PA1 switch opened (isolates PA1_C for ADC)
6. 128-sample bias calibration — averages zero-current reading
7. Bias sanity check: must be within 2500 +/- 450 mV, else fault flag set
8. **Critical**: `HAL_ADC_Stop()` must be called after every single conversion on STM32H7

## API

```c
ACS712_Init();                       // Init ADC1 + calibrate bias
float ma = ACS712_ReadCurrent_mA();  // Read current in mA (16-sample avg)
bool bad = ACS712_IsFault();         // True if bias calibration failed
float bias = ACS712_GetBias_mV();    // Calibrated bias for diagnostics
ACS712_PrintRawSamples();            // Print 16 raw ADC samples + PMCR + bias
ACS712_ScanChannels();               // Scan CH0-CH5 and print mV readings
ACS712_Recalibrate();                // Re-run 128-sample bias calibration
```

## Serial Commands

| Command | Action |
|---------|--------|
| `adc_diag` | Print 16-sample stats: raw, mV, bias, PMCR hex, fault status |
| `adc_scan` | Read CH0-CH5 side by side (pin identification) |
| `adc_recal` | Re-run 128-sample bias calibration and report result |

## Gotchas

- The ACS712 outputs Vcc/2 at zero current. If Vcc is noisy, readings will be noisy.
- Motor wiring: current flow DECREASES output below bias. Code uses `(bias - mv)` for positive mA.
- With 185 mV/A sensitivity and 16-bit ADC (0.047 mV/count at 3.1V VREF), theoretical resolution is ~0.25 mA/count. In practice, expect +/-10-20 mA noise without averaging.
- If the sensor is not connected at startup, bias calibration will read rail voltage and flag as FAULT — this is by design. Use `adc_recal` after connecting.
- STM32H747 `_C` pins (PA0_C, PA1_C, PC2_C, PC3_C) have analog switches to their GPIO counterparts. Default after reset is OPEN (PMCR bits 24-27 = 1), but CubeMX explicitly sets them. Our code does the same.
- Do NOT reconfigure PLL2 in ADC MSP — it's shared with SPI. Use per_ck (HSI) for ADC clock instead.
- PC2 (ANALOG_A4) does NOT work for this sensor — see Pin History above.
- PA0_C (ANALOG_A0) reads ~50 mV regardless of input — see Pin History above.
