# Latest Working Memory

**Session Date**: 2026-04-12

---

## Sofa State Machine — Sustained Overcurrent (JUST FLASHED)

### Changes Made
1. **CLOSING**: Single-sample threshold replaced with sustained overcurrent.
   - Current must exceed `SOFA_CONTACT_THRESH_MA` (1500mA) for `SOFA_CONTACT_SUSTAIN_MS` (500ms) continuously.
   - Timer resets if current drops below threshold.
2. **RESETTING**: Re-close-on-presence logic REMOVED. Full 10s retraction always completes.
   - Stall detection added: sustained current > `SOFA_STALL_THRESH_MA` (1500mA) for `SOFA_STALL_SUSTAIN_MS` (300ms) triggers safety stop.
3. New `#define` constants in main.c (lines 52-57) for easy tuning.
4. `sofa_thresh` command still works (sets contact threshold at runtime).

### Motor Current Characteristics (user measured)
- Nominal running: 1.2-1.3A swinging
- Contact/stall: 1.5-1.7A

### Needs Real-World Testing
- Does 1500mA / 500ms sustain window correctly detect contact without false triggers?
- Does stall detection trip correctly during retraction?
- User may need to tune thresholds based on actual motor behavior.

---

## INA226 — Clone Chip (RESOLVED 2026-04-12)

Fix: `INA226_SHUNT_OHM = 0.025f`, CAL=2048. Verified 1009mA with 1A load.
Details in memory file `project_ina226_overread.md`.

---

## Serial Commands Reference

| Command | Action |
|---------|--------|
| `sofa_start` | Enable sofa auto-adjust |
| `sofa_stop` | Disable sofa mode |
| `sofa_status` | Print state snapshot |
| `sofa_thresh <mA>` | Set current trip (default 1500) |
| `motor_fwd/rev/stop` | Manual motor (disables sofa) |
| `ina_diag` | INA226 register dump + measurements |
| `ina_read` | Quick I + Vbus readout |
| `ina_test` | Comprehensive self-test with cross-check |

## VCP Read Method (Working)
```python
C:/Users/Vor7e/AppData/Local/Programs/Python/Python310/python.exe -c "
import serial, time
s = serial.Serial('COM4', 9600, timeout=0)
s.write(b'ina_test\r\n')
time.sleep(1.5)
data = s.read(s.in_waiting)
s.close()
print(data.decode('utf-8', errors='replace'))
"
```
Note: COM4 = Portenta VCP. COM3 = ST-Link. Port must not be open in another app.
