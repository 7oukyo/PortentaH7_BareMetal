# Host Tools

Python scripts that run on the host PC and talk to the firmware over the USB CDC virtual COM port.

## current_monitor.py

Real-time current plot **plus** a menu-driven console for calibrating the C4001 mmWave sensor and driving the sofa state machine. Replaces the need to remember any vendor command syntax.

### Requirements

- Python 3.8+
- `pyserial`, `matplotlib`

```bash
pip install pyserial matplotlib
```

### Run

```bash
python tools/current_monitor.py              # defaults: COM4, 60s window
python tools/current_monitor.py COM5         # different COM port
python tools/current_monitor.py COM4 120     # 120-second rolling window
```

The script:

1. Auto-connects to the firmware VCP and auto-reconnects on board reset
2. Opens a matplotlib graph (current, baseline EMA, threshold)
3. Starts an interactive command menu in the terminal — type `?` then **Enter** for help

### Console menu

Single-letter commands; each prompts for any values it needs. Sensor / firmware replies (`> ...`, `[sensor] ...`, `[CFG]`, `[INA226]`, `[SOFA]`) are echoed to the terminal as they arrive.

#### C4001 mmWave Sensor

| Key | Action | Translates to firmware command |
|-----|--------|--------------------------------|
| `s` | Sensitivity 0-9 (sets both trig + keep) | `setSensitivity 255 <v>` then `setSensitivity <v> 255` |
| `r` | Detection range in meters (e.g. `0.3 5.0`) | `setRange <min> <max>` |
| `m` | Mode: 1=presence, 2=speed | `setRunApp 0` or `setRunApp 1` |
| `d` | Trigger delay (10ms units) + keep timeout (0.5s units) | `setLatency <td> <kt>` |
| `u` | Micro-motion on/off | `setMicroMotion 1` or `setMicroMotion 0` |
| `G` | Start sensor (Go) | `sensorStart` |
| `S` | Stop sensor | `sensorStop` |
| `X` | Factory reset (requires typing `YES`) | `sensorStop` → `resetCfg` → `sensorStart` |

#### Sofa Controller

| Key | Action | Firmware command |
|-----|--------|------------------|
| `o` | Set contact offset above baseline (mA) | `sofa_thresh <mA>` |
| `P` | Print sofa status | `sofa_status` |
| `1` | Enable sofa auto-adjust | `sofa_start` |
| `0` | Disable sofa mode | `sofa_stop` |
| `F` | Motor forward | `motor_fwd` |
| `R` | Motor reverse | `motor_rev` |
| `N` | Motor stop (Neutral) | `motor_stop` |

#### INA226

| Key | Action | Firmware command |
|-----|--------|------------------|
| `i` | Quick I + Vbus readout | `ina_read` |
| `D` | Full register dump + measurements | `ina_diag` |
| `T` | Self-test with cross-check + verdict | `ina_test` |

#### Misc

| Key | Action |
|-----|--------|
| `c` | Send a raw command (CRLF appended) — escape hatch for anything not wrapped above |
| `?` / `h` | Reprint the menu |
| `q` | Quit |

### Tips

- The script gates plot data on the firmware's `[CFG]` startup banner, so reset the board after launching for a clean view
- Sensitivity wrapper sends both halves (trig + keep) to keep the sensor symmetric — adjust manually with `c) raw` if you need them split
- The C4001 vendor docs use centimeter input on some commands and meters on others; this wrapper consistently uses **meters** for `setRange` to match the most common DFRobot reference
- All graph state clears automatically on reconnect

### Architecture

- `serial_reader` thread: opens COM port, reads lines, parses regex, updates shared deques
- `cli_loop` thread: blocking `input()` for menu commands, dispatches via `DISPATCH` table
- `main` thread: matplotlib FuncAnimation (must be on the main thread on Windows)
- Shared `ser_handle[0]` + `ser_lock` for thread-safe writes
- `quit_event` lets the CLI shut the plot down cleanly on `q`
