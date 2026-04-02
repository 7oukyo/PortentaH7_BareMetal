# C4001 mmWave Presence Sensor (UART4)

**Status**: IN PROGRESS (compiles, not yet verified on hardware)
**Date**: 2026-04-02

## Hardware

- **Sensor**: DFRobot C4001 24 GHz mmWave human presence sensor
- **Bus**: UART4 at 9600 baud, 8N1
- **TX pin**: PA0 (AF8 = GPIO_AF8_UART4)
- **RX pin**: PI9 (AF8 = GPIO_AF8_UART4)
- **Breakout header**: UART0 (J1-34 TX, J1-36 RX)
- **Power**: 3.3V from breakout board

## Protocol

ASCII text over UART at 9600 baud. Sensor streams data lines, MCU sends command strings terminated with `\r\n`. Sensor responds to queries with `Response <val1> [<val2>] [<val3>]`.

### Sensor Output Frames

| Mode | Frame format | Example |
|------|-------------|---------|
| Presence | `$DFHPD,<0\|1>` | `$DFHPD,1` (human detected) |
| Speed | `$DFDMD,<targets>,0,<range_m>,<speed_mps>,<energy>,0,0` | `$DFDMD,1,0,2.45,0.50,450,0,0` |

### Command Write Sequence

Config commands must follow this sequence (our `write_config_cmd()` does this automatically):

1. `sensorStop` — wait 200ms
2. Send command — wait 100ms
3. `saveConfig` — wait 100ms
4. `sensorStart` — wait 100ms

### Commands Reference

#### System Commands

| Command | Description | Delay after |
|---------|-------------|-------------|
| `sensorStart` | Start data collection. Sensor begins streaming frames. | 200ms |
| `sensorStop` | Stop data collection. Sensor stops streaming. | 200ms |
| `saveConfig` | Persist current settings to sensor flash. | 500ms |
| `resetCfg` | Factory reset all parameters to defaults. | 1500ms |
| `resetSystem` | Full system reset of the sensor module. | 1500ms |

#### Mode Selection

| Command | Description | Applies to |
|---------|-------------|------------|
| `setRunApp 0` | Switch to **presence mode**. Sensor outputs `$DFHPD` frames — binary presence only (0 or 1). Simpler, lower latency. Best for "is someone there?" detection. | Both (changes mode) |
| `setRunApp 1` | Switch to **speed mode**. Sensor outputs `$DFDMD` frames — includes target count, range (m), speed (m/s), and signal energy. More data but slightly higher latency. Best for tracking distance/movement. | Both (changes mode) |

Mode change requires 1500ms settle time.

#### Sensitivity — `setSensitivity <keep> <trig>`

Controls how easily the sensor triggers and maintains detection. Use `255` as a sentinel to leave one parameter unchanged.

| Command | Effect | Mode |
|---------|--------|------|
| `setSensitivity 255 <0-9>` | Set **trigger sensitivity** only. Higher = triggers on weaker signals, detects sooner. | Presence |
| `setSensitivity <0-9> 255` | Set **keep sensitivity** only. Higher = holds detection longer on subtle movement (breathing, stillness). | Presence |

- **Trig sensitivity**: how easily a *new* presence is first detected. 0 = least sensitive (needs strong motion), 9 = most sensitive (detects faint signals).
- **Keep sensitivity**: how easily detection is *maintained* once triggered. 0 = drops quickly when motion stops, 9 = holds detection even with very subtle movement (breathing through a sofa).
- **Cannot set both in one command** — `setSensitivity 5 5` causes an error. Must send two separate commands.
- **Query**: `getSensitivity` — returns `Response <trig> <keep>`.

#### Detection Range — `setRange <min_m> <max_m>`

Sets the minimum and maximum detection range in meters. Objects outside this range are ignored.

| Command | Effect | Mode |
|---------|--------|------|
| `setRange <min> <max>` | Set min/max detection boundary in meters. | Presence |
| `setTrigRange <trig_m>` | Set trigger range (max range for initial detection). | Presence |

- **min**: 0.3 to max (meters). Objects closer than this are ignored.
- **max**: 2.4 to 20.0 (meters). Objects farther than this are ignored.
- **trig**: 2.4 to 20.0 (meters). Maximum distance for initial trigger.
- Values are floats with one decimal, e.g. `setRange 0.3 2.0`.
- **Query**: `getRange` — returns `Response <min> <max>`. `getTrigRange` — returns `Response <trig>`.

#### Speed Mode Range — `setRange` (in speed mode)

When in speed mode, `setRange` and `setThrFactor` configure the speed detection zone.

| Command | Effect | Mode |
|---------|--------|------|
| `setRange <min_m> <max_m>` | Set speed detection zone boundaries. | Speed |
| `setThrFactor <threshold>` | Set CFAR detection threshold. Lower = more sensitive. | Speed |

- **Query**: `getRange` — returns `Response <min> <max>`. `getThrFactor` — returns `Response <threshold>`.

#### Latency — `setLatency <trig_s> <keep_s>`

Controls timing of detection transitions.

| Command | Effect | Mode |
|---------|--------|------|
| `setLatency <trig_s> <keep_s>` | Set trigger delay and keep timeout. | Presence |

- **trig_s**: Trigger delay in seconds (0.0 to 2.0, resolution 0.01s). How long a target must be present before detection triggers. 0 = instant.
- **keep_s**: Keep timeout in seconds (2.0 to 1500.0, resolution 0.5s). How long detection stays active after target disappears. Higher = stays "DETECTED" longer after person leaves.
- Values are floats, e.g. `setLatency 0.0 100.0` (no trigger delay, 100s keep).
- **Query**: `getLatency` — returns `Response <trig_s> <keep_s>`.

Note: The library internally uses register units (trig: 0-200 in 0.01s steps, keep: 4-3000 in 0.5s steps) but the UART command takes seconds as floats.

#### Micromotion Detection — `setMicroMotion <0|1>`

Enables detection of very subtle movement (breathing, fidgeting).

| Command | Effect | Mode |
|---------|--------|------|
| `setMicroMotion 0` | Disable fretting/micromotion detection. Only detects gross movement. | Presence |
| `setMicroMotion 1` | Enable fretting/micromotion detection. Detects breathing, subtle body shifts. Critical for detecting a stationary seated/lying person. | Presence |

- **Query**: `getMicroMotion` — returns `Response <0\|1>`.

#### GPIO Output — `setGpioMode <pin> <polarity>`

Controls the sensor's onboard GPIO output pin behavior.

| Command | Effect | Mode |
|---------|--------|------|
| `setGpioMode 1 0` | GPIO LOW when target present, HIGH when clear. | Both |
| `setGpioMode 1 1` | GPIO HIGH when target present, LOW when clear (default). | Both |

- **Query**: `getGpioMode 1` — returns `Response <pin> <polarity>`.

#### PWM Output — `setPwm <pwm1> <pwm2> <timer>`

Controls the sensor's onboard PWM output.

| Command | Effect | Mode |
|---------|--------|------|
| `setPwm <pwm1> <pwm2> <timer>` | Set PWM duty cycles and transition time. | Both |

- **pwm1**: Duty cycle when no target (0-100%).
- **pwm2**: Duty cycle when target present (0-100%).
- **timer**: Transition time between states (0-255, unit 64ms). e.g. 16 = ~1 second transition.
- **Query**: `getPwm` — returns `Response <pwm1> <pwm2> <timer>`.

## Init Sequence

1. PMIC must be up (sensor powered from breakout 3.3V rail)
2. USB CDC must be initialized (sensor data is forwarded over USB)
3. `C4001_Init()` — configures UART4, arms RX interrupt, sets presence mode, starts sensor

In `main.c` this comes after `LedPwm_Init()` and before the main loop.

## Architecture

```
C4001_Poll() [main loop]     -- drains ring buffer, parses lines, sends CDC report
  |
C4001 UART driver (c4001.c)  -- UART4 init, command TX, ISR RX -> ring buffer
  |
HAL UART + UART4 IRQ         -- byte-at-a-time receive interrupt
  |
Hardware                      -- UART4 peripheral -> C4001 sensor
```

## Files

| File | Purpose |
|------|---------|
| `src/c4001.c` | UART4 MSP init, RX ISR ring buffer, line parser, command API, CDC reporter |
| `include/c4001.h` | API declarations, pin/baud defines, data structures |

## USB CDC Output Format

A line is printed on every new sensor frame (not polled — real-time).

**Presence mode** (`$DFHPD`):
```
[12.345] DETECTED | frames=42 rx=504B | raw=$DFHPD,1
[12.678] CLEAR | frames=43 rx=517B | raw=$DFHPD,0
```

**Speed mode** (`$DFDMD`) — adds range, speed, and energy:
```
[12.345] DETECTED | range=2.45m spd=0.50m/s e=450 | frames=11 rx=220B | raw=$DFDMD,1,0,2.45,0.50,450,0,0
```

Fields:
- `DETECTED/CLEAR` — current presence state
- `range` — target distance in meters (speed mode only)
- `spd` — target speed in m/s, negative = approaching (speed mode only)
- `e` — signal energy / detection strength (speed mode only)
- `frames` — total valid sensor frames received since boot
- `rx` — total bytes received from UART4 (0 = wiring/baud issue)
- `raw` — last raw sensor line verbatim

Non-data sensor responses (e.g. command acks) are forwarded with `[sensor]` prefix.
User commands are echoed back with `>` prefix.

## LED Indicators

| LED | Function |
|-----|----------|
| Red | ON while presence detected, OFF when clear |
| Green | Blinks ~50ms on USB serial RX (existing behavior) |
| Blue | Unused |

## USB Command Pass-through

Any text sent from the USB host is forwarded directly to the C4001 sensor.
This allows live configuration from a serial terminal:
```
setSensitivity 255 7   # set trig=7, keep unchanged
setSensitivity 7 255   # set keep=7, trig unchanged
setRange 0.3 2.0
setMicroMotion 1
```

## Configuration for Under-Sofa Detection

For detecting humans through a sofa from underneath (close range):
```
setRange 0.3 1.5         # 30cm to 1.5m range
setSensitivity 255 7     # trig sensitivity 7
setSensitivity 7 255     # keep sensitivity 7
setMicroMotion 1         # detect subtle breathing/movement
setLatency 0 200         # no trigger delay, 100s keep timeout
```

## Gotchas

- **9600 baud is fixed**: The C4001 does not support changing baud rate over UART.
- **setSensitivity format**: `setSensitivity <keep> <trig>` — use `255` as sentinel to leave one unchanged. e.g. `setSensitivity 255 7` sets trig=7 only. Sending both as real values (like `setSensitivity 5 5`) causes an error.
- **Command timing**: Must stop sensor before changing config, save, then restart. Each step needs delays (100-1500ms depending on command).
- **Mode change delay**: Switching between presence/speed mode needs 1500ms settle time.
- **RX interrupt**: Uses byte-at-a-time `HAL_UART_Receive_IT` into a ring buffer. Not the most efficient but simple and reliable at 9600 baud.
- **UART4 clock**: Derived from APB1 (120 MHz). Standard baud rate generator handles 9600 fine.

## HAL Modules Required

Add to HAL_SRC in Makefile:
- `stm32h7xx_hal_uart.c`
- `stm32h7xx_hal_uart_ex.c`

Enable in stm32h7xx_hal_conf.h:
```c
#define HAL_UART_MODULE_ENABLED
```
