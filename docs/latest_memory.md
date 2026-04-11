# Latest Working Memory

**Session Date**: 2026-04-11

---

## INA226 Current Monitor — VERIFIED

I2C communication and current reading confirmed working with 150 mA dummy load.
Now wired to 29V sofa motor power supply for real operation.

### Hardware Setup

- INA226 on I2C3 (PH7=SCL, PH8=SDA), breakout I2C_0, addr 0x40
- Shunt: 0.1 ohm (100 mOhm), confirmed by user
- VBUS: 29V sofa motor supply
- Cal=512, Current LSB = 0.1 mA, 64x averaging, continuous mode

### What's Done

- `src/ina226.c` + `include/ina226.h` — full driver
- I2C3 MSP in `stm32h7xx_hal_msp.c` (PH7/PH8 AF4)
- `main.c`: INA226 replaces ACS712 everywhere, sofa state machine uses INA226
- Dummy load test commands (`load_on`/`load_off`) removed — unsafe with real 29V motor
- PD5 boot override removed — Motor_Init() handles relay state safely
- All docs cleaned of ACS712 references

## Sofa Auto-Adjust System — READY FOR REAL TEST

State machine: IDLE -> CLOSING -> CONTACT -> RESETTING.
User is wiring INA226 VBUS to 29V motor supply for end-to-end test.
Current threshold: 1000 mA default, adjustable via `sofa_thresh <mA>`.

## Serial Commands Reference

| Command | Action |
|---------|--------|
| `sofa_start` | Enable sofa auto-adjust |
| `sofa_stop` | Disable sofa mode |
| `sofa_status` | Print state snapshot |
| `sofa_thresh <mA>` | Set current trip (default 1000) |
| `motor_fwd/rev/stop` | Manual motor (disables sofa) |
| `ina_diag` | INA226 register dump + measurements |
| `ina_read` | Quick I + Vbus readout |
| Any other text | Forwarded to C4001 sensor |

## Build Status

Last successful build: 65788 text + 280 data + 24472 bss = 90540 total
