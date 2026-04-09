# VCP Serial Output Format

**Current VCP serial format** (COM4, 9600 baud) — **DO NOT MODIFY** without updating this document.

This is the standard output format sent by `send_report()` in `src/main.c` (moved from c4001.c on 2026-04-09 as part of module decoupling). Any diagnostic output must be appended AFTER the final `\r\n` or use a separate line with a unique marker prefix.

## Standard C4001 Report Format

```
[<timestamp>] <status> | range=<min_m>-<max_m> spd=<speed_mps> e=<energy> | frames=<count> rx=<bytes>B | raw=$DFHPD,<0|1> | I=<current_mA>mA
```

### Field Breakdown

| Field | Example | Description | Source |
|-------|---------|-------------|--------|
| `[<timestamp>]` | `[12.345]` | Elapsed time in seconds (floating point, 3 decimals) | C4001 sensor data |
| `<status>` | `DETECTED` or `CLEAR` | Presence detection state | C4001 parsed presence bit |
| `range=` | `range=1.20-2.40` | Detection range min-max in meters | C4001 speed frame or defaults |
| `spd=` | `spd=0.50` | Object speed in m/s (2 decimals) | C4001 speed frame |
| `e=` | `e=42` | Signal energy value | C4001 speed frame |
| `frames=` | `frames=100` | Total valid sensor frames received | Firmware counter |
| `rx=` | `rx=2048B` | Total bytes received from sensor | Firmware counter |
| `raw=$DFHPD,<0\|1>` | `raw=$DFHPD,1` | Raw sensor presence line (for debug) | C4001 UART RX buffer |
| `I=` | `I=523.4mA` or `I=FAULT` | Current reading in mA, or FAULT if sensor error | ACS712 driver |

### Example Output (Presence Mode, Current Flowing)

```
[2.341] DETECTED | range=1.20-2.40 spd=0.50 e=45 | frames=3 rx=128B | raw=$DFHPD,1, , , * | I=523.4mA
```

### Example Output (Clear, No Current)

```
[5.123] CLEAR | range=1.20-2.40 spd=0.50 e=0 | frames=5 rx=256B | raw=$DFHPD,0, , , * | I=0.0mA
```

### Example Output (Current Sensor Fault)

```
[8.456] DETECTED | range=1.20-2.40 spd=0.50 e=42 | frames=8 rx=384B | raw=$DFHPD,1, , , * | I=FAULT
```

## Motor Status Format

During motor test (or when motor commands are active), `send_motor_status()` in `src/main.c` prints every 100ms:

```
[<timestamp>] MOTOR=<FWD|REV|STOP> I=<current_mA>mA [TRIPPED]
```

### Field Breakdown

| Field | Example | Description |
|-------|---------|-------------|
| `[<timestamp>]` | `[3.100]` | Elapsed time in seconds |
| `MOTOR=` | `MOTOR=FWD` | Current motor direction (FWD, REV, or STOP) |
| `I=` | `I=123.4mA` or `I=FAULT` | Current from ACS712 |
| `TRIPPED` | (optional) | Present if current exceeded 50mA threshold |

### Example Output

```
[3.100] MOTOR=STOP I=0.0mA
[4.200] MOTOR=FWD I=12.3mA
[4.500] MOTOR=STOP I=52.3mA TRIPPED
```

### Serial Commands

| Command | Action |
|---------|--------|
| `motor_fwd` | Set motor forward |
| `motor_rev` | Set motor reverse |
| `motor_stop` | Emergency stop + disable auto-test |
| `motor_test` | Start/restart test sequence |

## Diagnostic Output Convention

When adding debugging output (raw ADC samples, calibration values, etc.), use one of these approaches to keep the standard format intact:

### Option 1: Separate Line with Marker Prefix (Preferred)

Send diagnostic data on a new line with a unique prefix like `[DEBUG]`, `[ADC]`, `[ACS712]`:

```
[ADC_RAW] sample_count=128 avg=51234 min=50998 max=51456 mv=2434.5
[ADC_RAW] sample_count=128 avg=51200 min=50876 max=51512 mv=2432.1
[2.341] DETECTED | range=1.20-2.40 ... | I=523.4mA
```

### Option 2: Appended to Report Line (If Space Available)

Append diagnostic data after the final ACS712 field but before `\r\n`:

```
[2.341] DETECTED | range=1.20-2.40 spd=0.50 e=45 | frames=3 rx=128B | raw=$DFHPD,1 | I=523.4mA | [adc_bias=2500.3mV]
```

### Markers for Common Diagnostics

| Marker | Data | Purpose |
|--------|------|---------|
| `[ADC_CAL]` | Calibration results | Bias offset detection at startup |
| `[ADC_RAW]` | Raw sample data | Multi-sample ADC reads for noise analysis |
| `[ADC_DEBUG]` | Conversion details | Channel routing, reference voltage checks |
| `[I_DIAG]` | ACS712 status | Fault codes, bias drift detection |

## Implementation Note for Future Sessions

When adding diagnostic output:

1. **Read this file first** before modifying any VCP output code
2. **Preserve the standard format** — the C4001 report must remain unchanged to keep existing data parseable
3. **Use prefix markers** to distinguish debug output from standard telemetry
4. **Document the new format** in this file immediately after adding it
5. **Update latest_memory.md** with the diagnostic format for the next session

## Reading VCP Output

The user reads serial output directly from the COM port (9600 baud) and reports findings back. Expected output: lines matching the format above, one per C4001 frame (~1 Hz in presence mode).
