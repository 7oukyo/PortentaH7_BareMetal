# Latest Working Memory

**Session Date**: 2026-04-21

---

## BlackPill F411 Port — BUILD-READY, NOT FLASHED

Port of the Portenta H7 sofa controller firmware to STM32F411CEU6 BlackPill lives under `blackpill_f411/`. As of 2026-04-21 the port compiles cleanly end-to-end.

### State

- **Sources, headers, Makefile, linker, startup**: all in place, adapted from H7 versions
- **HAL drivers**: installed under `blackpill_f411/drivers/` from STM32CubeF4 submodules (cloned to `x:/stm32cube/STM32CubeF4`, three submodules initialized: CMSIS device F4, STM32F4xx_HAL_Driver, STM32_USB_Device_Library)
- **Build**: `cd blackpill_f411 && make` succeeds — FLASH 51,908 B (9.9%) / RAM 17,392 B (13.3%)
- **Warnings**: only benign ST HAL `Banks` unused-param + newlib-nano `_close/_read/_write` stubs. Not real issues.

### What was adapted from H7

- Clock: 480 MHz PLL1 (H7) → 96 MHz PLL M=25 N=192 P=2 Q=4 with 48 MHz USB from Q (F411)
- USB: OTG_HS + USB3320 ULPI PHY → OTG_FS internal PHY on PA11/PA12
- I2C: I2C3 PH7/PH8 Timing=0x307075B1 → I2C1 PB6/PB7 ClockSpeed=400000 + DutyCycle_2
- UART: UART4 PA0/PI9 AF8 → USART1 PA9/PA10 AF7
- LEDs: 3x GPIOK (PK5/6/7 active LOW) → single PC13 (active LOW sink)
- Motor relays: PC15 + PD5 → PB0 + PB1
- Timer: TIM6 (H7 only) → TIM3 (general-purpose on F411)
- Removed entirely: PMIC (PF1550 not on BlackPill), HSE gating (PH1), CM4 boot disable, I-cache enable, ULPI PHY reset

See `blackpill_f411/docs/porting-notes.md` for the full adaptation table.

### Next steps (hardware bring-up order)

1. Flash via ST-Link: `cd blackpill_f411 && make flash`
2. PC13 blink sanity check
3. USB CDC enumerates on USB-C connector
4. I2C1 + INA226 (verify clone-chip calibration carries over: Rsh=0.025, CAL=2048)
5. USART1 + C4001 (presence parsing)
6. Relay GPIO (PB0/PB1, active-LOW)
7. Sofa state machine end-to-end

### Gotchas to watch on F411

- USB CDC on F411 uses `CDC_Transmit_FS()` (not `_HS()` like the Portenta)
- USB FIFO sizes scaled down: 128+64+128 words (was 512+128+372 on H7)
- Single-precision FPU only (`-mfpu=fpv4-sp-d16`). Any `double` math in H7 code will be soft-float and slow.
- 3 WS flash latency (Scale 1) vs H7's 4 WS (VOS0)

---

## H7 Sofa Controller — STABLE (reference project)

Two-layer adaptive current detection working on H7 (2026-04-13 session). All peripherals VERIFIED. Sofa state machine IDLE→CLOSING→CONTACT→RESETTING with adaptive baseline EMA. Kept here for reference while the F411 port comes up.

### Serial Commands Reference (H7, carried forward to F411 verbatim)

| Command | Action |
|---------|--------|
| `sofa_start` | Enable sofa auto-adjust |
| `sofa_stop` | Disable sofa mode |
| `sofa_status` | Print state snapshot |
| `sofa_thresh <mA>` | Set contact offset above baseline (default 200) |
| `motor_fwd/rev/stop` | Manual motor (disables sofa) |
| `ina_diag` | INA226 register dump + measurements |
| `ina_read` | Quick I + Vbus readout |
| `ina_test` | Self-test with cross-check |

### Host Tool — `tools/current_monitor.py`

Updated 2026-04-21. Now bundles a menu-driven console alongside the live current plot. Single-letter keys wrap every C4001 / INA226 / sofa / motor command — no DFRobot syntax to remember. Run with `python tools/current_monitor.py [COM port] [window seconds]`. Press `?` then Enter for the menu. See [tools/README.md](../tools/README.md) for the full key map.

For ad-hoc one-shot reads without launching the tool:

```python
C:/Users/Vor7e/AppData/Local/Programs/Python/Python310/python.exe -c "
import serial, time
s = serial.Serial('COM4', 9600, timeout=0)
s.write(b'sofa_status\r\n')
time.sleep(1.5)
data = s.read(s.in_waiting)
s.close()
print(data.decode('utf-8', errors='replace'))
"
```

COM4 = Portenta VCP, COM3 = ST-Link. F411 will enumerate on its own COM port — re-detect when flashed.
