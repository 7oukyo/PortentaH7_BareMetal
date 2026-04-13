# Latest Working Memory

**Session Date**: 2026-04-13

---

## Two-Layer Adaptive Current Detection (JUST BUILT, NOT FLASHED)

### Problem 1: Variable startup spike

Motor startup current spike varies each time, sometimes exceeding contact threshold. Fixed blanking window was insufficient.

### Problem 2: Motor warm-up drift

Running current drifts from ~1450mA (cold) to ~1000mA (warm, after minutes). Fixed threshold can't handle both.

### Solution: Slope-based settle + Adaptive baseline EMA

**Layer 1 — Settle detection** (handles startup spike):

- Samples current every 100ms, compares to previous
- If `prev - current > 30mA` (noise margin) → still falling → still in spike
- Once flat/rising for 300ms → settled, seeds baseline EMA
- Safety timeout: 3s forces settle

**Layer 2 — Adaptive baseline** (handles warm-up drift):

- After settling, maintains EMA of running current (`alpha=0.99`, tau ~10s)
- Contact threshold = `baseline + offset` (default +200mA)
- Stall threshold = `baseline + offset` (default +250mA)
- Baseline frozen during overcurrent to prevent drift from spike
- `sofa_thresh` command now sets the contact offset (not absolute threshold)

### Defines (main.c)

| Define | Value | Purpose |
|--------|-------|---------|
| `SOFA_CONTACT_OFFSET_MA` | 200 | Contact = baseline + this |
| `SOFA_STALL_OFFSET_MA` | 250 | Stall = baseline + this |
| `SOFA_BASELINE_ALPHA` | 0.99 | EMA smoothing, tau ~10s |
| `SOFA_MONITOR_SAMPLE_MS` | 100 | Current sample interval after settling |
| `SOFA_SETTLE_SAMPLE_MS` | 100 | Sample interval during settling |
| `SOFA_SETTLE_NOISE_MA` | 30 | Flat vs falling threshold |
| `SOFA_SETTLE_STABLE_MS` | 300 | Must be flat this long |
| `SOFA_SETTLE_TIMEOUT_MS` | 3000 | Safety fallback |

### VCP Status Fields (CLOSING/RESETTING)

`STL=0|1` settle state, `PK=<mA>` spike peak, `BL=<mA>` baseline EMA

### Needs Real-World Testing

- Watch `BL=` value drift as motor warms up — should track from ~1450 down to ~1000
- Does contact/stall detection trigger correctly at both cold and warm states?
- Is +200mA contact offset appropriate? Use `sofa_thresh <mA>` to tune
- Watch `STL=0→1` transition — does settle detection correctly ignore startup spike?

---

## Serial Commands Reference

| Command | Action |
|---------|--------|
| `sofa_start` | Enable sofa auto-adjust |
| `sofa_stop` | Disable sofa mode |
| `sofa_status` | Print state snapshot |
| `sofa_thresh <mA>` | Set contact offset above baseline (default 200) |
| `motor_fwd/rev/stop` | Manual motor (disables sofa) |
| `ina_diag` | INA226 register dump + measurements |
| `ina_read` | Quick I + Vbus readout |
| `ina_test` | Comprehensive self-test with cross-check |

## VCP Read Method (Working)

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

Note: COM4 = Portenta VCP. COM3 = ST-Link. Port must not be open in another app.
