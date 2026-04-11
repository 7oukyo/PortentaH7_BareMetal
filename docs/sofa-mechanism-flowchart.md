# Automated Sofa Backplane Controller

## Overview

An automated sofa backplane system that uses mmWave presence detection to close a motor-driven backplane toward the seated user, and retracts it when they leave.

## Hardware

| Component | Role | Connection |
|-----------|------|------------|
| C4001 mmWave sensor | Presence detection | UART4 (PA0 TX, PI9 RX) 9600 baud |
| INA226 current/power monitor | Motor load detection | I2C3 (PH7/PH8, breakout I2C_0), addr 0x40 |
| 2x relay module (H-bridge) | Motor direction control | PC15 (relay 1), PD5 (relay 2), active-LOW |
| DC motor + sofa backplane | Actuator | Connected via relay H-bridge, 29V supply |

## State Machine

```
                       +--------+
                       |  IDLE  |<--------------------------+
                       +--------+                           |
                           |                                |
                 person detected                  reset complete
                 (debounced 500ms)                (10s reverse done)
                           |                                |
                           v                                |
                       +--------+                     +-----------+
             +-------->|CLOSING |--person leaves----->| RESETTING |
             |         +--------+                     +-----------+
             |              |                               ^
             |    current spike OR                          |
             |    15s timeout                               |
             |              |                               |
             |              v                               |
             |         +---------+                          |
             |         | CONTACT |---person leaves----------+
             |         +---------+
             |              |
             +-person sits back during reset
```

## State Descriptions

### IDLE

- **Motor**: OFF (both relays HIGH)
- **Trigger**: C4001 reports `DETECTED` for >= 500ms (debounce)
- **LED**: Red = presence, Blue = OFF
- **Transition**: -> CLOSING (motor starts forward)

### CLOSING

- **Motor**: FORWARD (relay 1 ON, relay 2 OFF) - drives backplane toward user
- **Stop conditions** (whichever comes first):
  1. INA226 current exceeds threshold (default 1000 mA) -> CONTACT
  2. Timeout at 15 seconds -> CONTACT (safety, assumes contact even without current feedback)
  3. Person leaves (CLEAR for >= 2s) -> RESETTING (abort and retract)
- **LED**: Red = presence, Blue = ON (motor active)

### CONTACT

- **Motor**: OFF - backplane resting against person's back
- **Hold**: Stays in this state as long as person is present
- **Trigger**: C4001 reports `CLEAR` for >= 2 seconds (debounce)
- **Transition**: -> RESETTING (motor reverses to retract)
- **LED**: Red = presence, Blue = OFF

### RESETTING

- **Motor**: REVERSE (relay 1 OFF, relay 2 ON) for exactly 10 seconds
- **Purpose**: Return backplane to fully retracted position (absolute max open)
- **Interruption**: If person sits back down (DETECTED for >= 500ms), stops reverse and goes to CLOSING
- **Transition**: After 10 seconds -> IDLE
- **LED**: Red = OFF, Blue = ON (motor active)

## Safety Features

| Feature | Description |
|---------|-------------|
| Current trip | INA226 detects motor stall/overload, emergency stop |
| Close timeout | 15s max motor-on time prevents runaway if current sensing fails |
| Dead time | 100ms pause between FWD<->REV direction changes (relay protection) |
| Emergency stop | `motor_stop` command kills motor and disables sofa mode |
| INA226 FAULT mode | If I2C comm fails, system uses timeout-only (no current trip) |

## Serial Commands

### Sofa Control

| Command | Action |
|---------|--------|
| `sofa_start` | Enable sofa auto-adjust mode, reset to IDLE |
| `sofa_stop` | Disable sofa mode, emergency stop motor |
| `sofa_status` | Print current state, motor dir, elapsed time, current, presence |
| `sofa_thresh <mA>` | Set current trip threshold (default 1000 mA) |

### Motor Manual Override

These disable sofa mode and give direct motor control:

| Command | Action |
|---------|--------|
| `motor_fwd` | Motor forward (disables sofa mode) |
| `motor_rev` | Motor reverse (disables sofa mode) |
| `motor_stop` | Emergency stop (disables sofa mode) |

### C4001 Sensor (pass-through)

All C4001 commands are forwarded via UART4. Examples:

| Command | Action |
|---------|--------|
| `sensorStart` | Start sensor data collection |
| `sensorStop` | Stop sensor data collection |
| `setRunApp 0` | Set presence mode |
| `setRange <min> <max>` | Set detection range (meters) |
| `setSensitivity <keep> <trig>` | Set detection sensitivity (0-9) |
| `setLatency <trig_delay> <keep_timeout>` | Set detection timing |
| `resetCfg` | Factory reset sensor |

### INA226 Diagnostics

| Command | Action |
|---------|--------|
| `ina_diag` | Print config/cal registers + Vbus/Vsh/I/P measurements |
| `ina_read` | Quick current + bus voltage readout |

## VCP Output Format

### Sofa Status Line (every 200ms when sofa enabled)

```
[<time>] SOFA=<state> MTR=<FWD|REV|STOP> t=<time_in_state>s I=<mA>mA PRS=<0|1>
```

### C4001 Report (every ~1s on new sensor frame)

```
[<time>] <DETECTED|CLEAR> | sofa=<state> | frames=<n> rx=<bytes>B | raw=<sensor_line> | I=<mA>mA
```

## Tunable Parameters (compile-time, in main.c)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `SOFA_CLOSE_TIMEOUT_MS` | 15000 | Max motor-on time when closing (ms) |
| `SOFA_RESET_DURATION_MS` | 10000 | Reverse time to fully retract (ms) |
| `SOFA_REPORT_INTERVAL_MS` | 200 | Status output interval (ms) |
| `SOFA_CLEAR_DEBOUNCE_MS` | 2000 | Presence CLEAR debounce before reset (ms) |
| `SOFA_DETECT_DEBOUNCE_MS` | 500 | Presence DETECTED debounce before close (ms) |
| `sofa_current_threshold_ma` | 1000.0 | Current trip point (mA), adjustable via `sofa_thresh` |
