# VCP Serial Output Format

**Current VCP serial format** (COM4, 9600 baud) — **DO NOT MODIFY** without updating this document.

This is the standard output format sent by `send_report()` in `src/main.c`. Any diagnostic output must be appended AFTER the final `\r\n` or use a separate line with a unique marker prefix.

## Standard Report Format

On each C4001 sensor frame (~1 Hz), `send_report()` in `src/main.c` outputs:

```
[<timestamp>] <status> | sofa=<state> | frames=<count> rx=<bytes>B | raw=<sensor_line> | I=<mA>mA
```

### Field Breakdown

| Field | Example | Description | Source |
|-------|---------|-------------|--------|
| `[<timestamp>]` | `[12.345]` | Elapsed time in seconds (3 decimal digits) | HAL_GetTick |
| `<status>` | `DETECTED` or `CLEAR` | Presence detection state | C4001 parsed presence bit |
| `sofa=` | `sofa=IDLE` | Sofa state machine state | main.c sofa controller |
| `frames=` | `frames=100` | Total valid sensor frames received | Firmware counter |
| `rx=` | `rx=2048B` | Total bytes received from sensor | Firmware counter |
| `raw=` | `raw=$DFHPD,1` | Raw sensor presence line (for debug) | C4001 UART RX buffer |
| `I=` | `I=523.4mA` or `I=FAULT` | Current reading in mA, or FAULT if I2C error | INA226 driver |

### Example Output

```
[12.095] DETECTED | sofa=CLOSING | frames=10 rx=397B | raw=$DFHPD,1, , , * | I=523.4mA
[13.087] CLEAR | sofa=IDLE | frames=11 rx=414B | raw=$DFHPD,0, , , * | I=0.0mA
```

## Sofa Status Format

Every 200ms when sofa mode is enabled, `send_sofa_status()` outputs:

```
[<timestamp>] SOFA=<state> MTR=<FWD|REV|STOP> t=<time_in_state>s I=<mA>mA PRS=<0|1>
```

### Sofa Status Fields

| Field | Example | Description |
|-------|---------|-------------|
| `SOFA=` | `SOFA=CLOSING` | Current state: IDLE, CLOSING, CONTACT, RESETTING |
| `MTR=` | `MTR=FWD` | Motor direction |
| `t=` | `t=3.2s` | Time in current state |
| `I=` | `I=123.4mA` or `I=FAULT` | Current from INA226 |
| `PRS=` | `PRS=1` | C4001 presence (1=detected, 0=clear) |

### Serial Commands

| Command | Action |
|---------|--------|
| `sofa_start` | Enable sofa auto-adjust, reset to IDLE |
| `sofa_stop` | Disable sofa mode, emergency stop |
| `sofa_status` | Print current state snapshot |
| `sofa_thresh <mA>` | Set current trip threshold (default 1000) |
| `motor_fwd` | Manual motor forward (disables sofa mode) |
| `motor_rev` | Manual motor reverse (disables sofa mode) |
| `motor_stop` | Emergency stop (disables sofa mode) |
| `ina_diag` | Print INA226 config/cal registers + measurements |
| `ina_read` | Quick current + bus voltage readout |
| `ina_test` | Comprehensive self-test: register dump, cross-check, CAL test, verdict |

## Diagnostic Output Convention

When adding debugging output, use separate lines with unique prefix markers:

```
[INA226] CFG=0x4527 CAL=512 MFR=0x5449 DIE=0x2260
[INA226] Vbus=29012.5mV Vsh=15000uV I=150.0mA P=4353.0mW
```

### Markers for Common Diagnostics

| Marker | Data | Purpose |
|--------|------|---------|
| `[INA226]` | Register dump + measurements | INA226 diagnostics |
| `[SOFA]` | Threshold change | Sofa parameter updates |

## Reading VCP Output

The user reads serial output directly from the COM port (9600 baud) and reports findings back. Expected output: lines matching the format above, one per C4001 frame (~1 Hz in presence mode).
