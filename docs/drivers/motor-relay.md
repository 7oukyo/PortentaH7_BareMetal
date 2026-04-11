# Motor Relay H-Bridge

**Status**: VERIFIED (code complete, hardware test pending)
**Date added**: 2026-04-09
**Source files**: `src/motor_relay.c`, `include/motor_relay.h`

## Hardware Connection

| Signal | MCU Pin | Breakout Pin | Notes |
|--------|---------|--------------|-------|
| Relay 1 (Motor+) | PC15 | GPIO_1 | Active-LOW: LOW = energized (COM to NO = +29V) |
| Relay 2 (Motor-) | PD5  | GPIO_3 | Active-LOW: LOW = energized (COM to NO = +29V) |

- **Relay type**: Active-LOW relay modules
- **Motor supply**: 29V through relay NO contacts
- **Current sensing**: INA226 on I2C3 monitors motor current (see `docs/drivers/ina226-current.md`)

## Operation

| State | Relay 1 (PC15) | Relay 2 (PD5) | Motor |
|-------|----------------|----------------|-------|
| STOP  | HIGH (off)     | HIGH (off)     | Both terminals on GND, braked |
| FWD   | LOW (on)       | HIGH (off)     | Motor+ = +29V, Motor- = GND |
| REV   | HIGH (off)     | LOW (on)       | Motor+ = GND, Motor- = +29V |
| **FORBIDDEN** | LOW (on) | LOW (on) | **Dead short at 29V — NEVER DO THIS** |

## Safety

- **Dead time**: 100ms enforced between direction changes (FWD to REV or REV to FWD)
- **Direction change**: Always goes through STOP first (both relays OFF), then waits dead time, then sets new direction
- **Emergency stop**: `Motor_EmergencyStop()` — both relays OFF immediately, no dead time wait
- **Current trip**: Sofa controller monitors INA226 current, emergency stops if above threshold (default 1000 mA, adjustable via `sofa_thresh`)

## Initialization

1. `Motor_Init()` called in main.c after `C4001_Init()`
2. Both relays set HIGH (OFF) before GPIO configured as output (prevents glitch)
3. GPIO: push-pull, no pull, low speed

## API

```c
Motor_Init();                        // Init GPIO, both relays OFF
Motor_SetDir(MOTOR_FWD);             // Set forward (relay 1 ON)
Motor_SetDir(MOTOR_REV);             // Set reverse (relay 2 ON)
Motor_SetDir(MOTOR_STOP);            // Both relays OFF
Motor_EmergencyStop();               // Immediate stop, no dead time
MotorDir_t dir = Motor_GetDir();     // Get current direction
```

## Motor Test Sequence

Auto-starts 3 seconds after boot. Cycles continuously:

1. **STOP** — 1 second pause
2. **FWD** — 2 seconds forward
3. **STOP** — 1 second pause
4. **REV** — 2 seconds reverse
5. Back to step 1

During test, motor status is printed every 100ms over USB VCP:

```
[3.100] MOTOR=STOP I=0.0mA
[4.100] MOTOR=FWD I=123.4mA
[6.200] MOTOR=STOP I=0.0mA
[7.200] MOTOR=REV I=456.7mA
```

If current exceeds 50mA threshold:

```
[4.500] MOTOR=STOP I=52.3mA TRIPPED
```

## Serial Commands

| Command | Action |
|---------|--------|
| `motor_fwd` | Set motor forward |
| `motor_rev` | Set motor reverse |
| `motor_stop` | Emergency stop + disable test |
| `motor_test` | Start/restart test sequence |

## Gotchas

- Relays are ACTIVE-LOW. Pin HIGH = relay OFF = motor terminal on GND.
- Both relays LOW simultaneously = dead short at 29V. Code prevents this but if GPIO init fails or glitches, hardware fuse/protection is recommended.
- Relay switching has mechanical delay (~5-10ms). The 100ms dead time accounts for this with margin.
- Motor test auto-starts on boot. Send `motor_stop` to disable if not wanted.
- Current trip threshold (50mA) may need adjustment based on actual motor characteristics.
