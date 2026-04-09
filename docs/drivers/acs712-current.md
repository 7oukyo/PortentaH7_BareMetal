# ACS712 5A Current Sensor

**Status**: IN PROGRESS (wiring change pending)
**Date added**: 2026-04-06
**Last updated**: 2026-04-09
**Source files**: `src/acs712.c`, `include/acs712.h`

## Hardware Connection

| Signal | MCU Pin | Breakout Pin | Notes |
|--------|---------|--------------|-------|
| Analog output | PA0_C | Analog A0 | ADC1_INP0, dedicated analog pin |

- **Sensor model**: ACS712-05B (5A bidirectional)
- **Sensitivity**: 185 mV/A
- **Zero-current output**: Vcc / 2 (nominally 2.5V at 5V supply, measured 2.43V)
- **ADC**: ADC1 channel INP0, 16-bit resolution, single-ended
- **VREF+**: 3.1V from PMIC SW3 (measured)

## Pin History

Originally wired to PA0_C (Analog A0) and worked correctly. Haiku session (2026-04-09) moved to PC2 (Analog A4, ADC1_INP18) but no channel number (4/12/18) produced correct readings:
- CH18 was actually PA4/ULPI_D0 (read USB noise)
- CH12 and CH4 both read VREF+ (3.1V) regardless of input
- Root cause unresolved — may be SYSCFG analog switch issue or PCB routing on PC2

**Resolution**: Reverted back to PA0_C / ADC_CHANNEL_0 which reads correctly. PA0_C is a dedicated analog pin with a direct ADC path — no GPIO init or SYSCFG switch needed.

**Action required**: User must move ACS712 output wire from breakout ANALOG_A4 back to ANALOG_A0.

## Initialization Order

1. PMIC must be up first (stable 3.1V for VREF+ via SW3)
2. `ACS712_Init()` called in main.c after `PMIC_Init()`
3. ADC1 clock: ADC12 async clock / 6
4. ADC offset calibration (`HAL_ADCEx_Calibration_Start`, single-ended)
5. PA0_C needs no GPIO init (dedicated analog, SYSCFG PA0SO=0 default)
6. 128-sample bias calibration — averages zero-current reading
7. Bias sanity check: must be within 2500 +/- 450 mV, else fault flag set
8. **Critical**: `HAL_ADC_Stop()` must be called after every single conversion on STM32H7

## API

```c
ACS712_Init();                       // Init ADC1 + calibrate bias
float ma = ACS712_ReadCurrent_mA();  // Read current in mA (16-sample avg)
bool bad = ACS712_IsFault();         // True if bias calibration failed
float bias = ACS712_GetBias_mV();    // Calibrated bias for diagnostics
ACS712_PrintRawSamples();            // Print 16 raw ADC samples over USB VCP
```

## Output Format

Current reading is appended to the C4001 mmWave serial report (see `docs/vcp-serial-format.md`):

```
[12.345] DETECTED | range=1.20m spd=0.50m/s e=42 | frames=100 rx=2048B | raw=$DFHPD,1 | I=523.4mA
```

If bias calibration failed:

```
[12.345] DETECTED | ... | I=FAULT
```

## Diagnostic Command

Send `adc_diag` via USB VCP to get raw ADC samples:

```
[ADC_RAW] samples=16 avg_raw=52800 min_raw=52600 max_raw=53100 avg_mV=2498.1
```

## Gotchas

- The ACS712 outputs Vcc/2 at zero current. If Vcc is noisy, readings will be noisy.
- Motor wiring: current flow DECREASES output below bias. Code uses `(bias - mv)` for positive mA.
- With 185 mV/A sensitivity and 16-bit ADC (0.047 mV/count at 3.1V VREF), theoretical resolution is ~0.25 mA/count. In practice, expect +/-10-20 mA noise without averaging.
- If the sensor is not connected at startup, bias calibration will read ~0V or rail voltage and flag as FAULT — this is by design.
- ADC offset calibration is re-enabled as of 2026-04-09. Was disabled during PA0_C debugging but now works correctly.
- PC2 (ANALOG_A4) does NOT work for this sensor — see Pin History above.
