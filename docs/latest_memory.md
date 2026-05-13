# Latest Working Memory

**Session Date**: 2026-05-13

---

## BlackPill F411 — Plain-Language User Manual Added (2026-05-13)

End-user documentation produced for the sofa controller, aimed at readers
with zero technical background.

- [blackpill_f411/docs/user-manual.md](../blackpill_f411/docs/user-manual.md) — markdown source
- [blackpill_f411/docs/user-manual.pdf](../blackpill_f411/docs/user-manual.pdf) — rendered PDF (5 pages)

Covers: what the sofa does, parts list (radar/motor/current sensor/switches),
AUTO vs MANUAL mode, ASCII state diagram (IDLE → CLOSING → CONTACT → RESETTING),
safety behaviour, controls quick-reference, and everyday troubleshooting.

**Toolchain installed this session** (Windows host, via winget):

- Pandoc 3.9.0.2 → `C:\Users\Vor7e\AppData\Local\Pandoc\pandoc.exe`
- MiKTeX 25.12 → `C:\Users\Vor7e\AppData\Local\Programs\MiKTeX\miktex\bin\x64\`

**PDF rebuild command** (run from `blackpill_f411/docs/`):

```powershell
pandoc user-manual.md -o user-manual.pdf --pdf-engine=xelatex `
  -V geometry:margin=2cm -V mainfont="Segoe UI" -V monofont="Cascadia Mono"
```

xelatex is required (not pdflatex) because the state diagram uses Unicode
box-drawing characters. Known minor warning: *"Infinite glue shrinkage in
box being split"* on the diagram page — cosmetic, the PDF still renders.

---

## BlackPill F411 — Toggle + Buttons Redesign — BUILD-READY, NOT FLASHED (2026-05-12)

### What changed this session

Reworked the manual-override layer from "exclusive mode select" to
"AUTO arm + force-close gesture, with buttons overriding in both modes".

**Hardware change**:

- Toggle pin moved PA1 → **PB12** (pull-up, switch-to-GND; asserted = LOW).
- Polarity flipped: asserted (closed) = AUTO armed; deasserted (open) = MANUAL-only.

**Firmware semantic changes** in [blackpill_f411/src/main.c](../blackpill_f411/src/main.c):

- `sofa_mode` no longer exclusive: AUTO/MANUAL gates only `sofa_tick`. Manual buttons override the motor in both modes.
- **Force-close**: toggle press (pin falling edge) drives `Motor_SetDir(FWD) + sofa_enter(SOFA_CLOSING)` from any state. Suppressed if a manual button is held.
- **Toggle release** (pin rising edge): only flips `sofa_mode = MANUAL`. Motor, `sofa_state`, and `sofa_armed` are preserved. The state machine just pauses.
- **Manual button release**: `Motor_EmergencyStop` only. `sofa_state` and `sofa_armed` are preserved. The state machine resumes from where it was when the button was pressed.
- **`sofa_armed` flag** (new): AUTO close (IDLE → CLOSING) gated on a fresh presence rising edge. Cleared inside `sofa_enter(SOFA_IDLE)`, which is now called only from the natural retract path (`RESETTING` timeout), boot init, and `sofa_start`. No button can force disarm or park-at-IDLE.
- **15-min retract**: `SOFA_CLEAR_DEBOUNCE_MS` 600000 → 900000.
- **Boot-time edge suppression**: toggle pin sampled once at init to seed `mode_sw_state`; no force-close fires on power-up even if toggle is already pressed.

**Hardware verified** on bench: toggle press = force close, release = MANUAL-only, manual buttons override correctly. Closing direction was physically reversed → fixed in software.

**Direction inversion + USER_KEY** (added after first flash):

- New `dir_inverted` flag (default `true` — matches as-wired sofa) decouples "close/retract" semantic from raw `MOTOR_FWD/MOTOR_REV`.
- `dir_close()` / `dir_retract()` helpers used everywhere the state machine and manual buttons drive the motor. Raw `motor_fwd`/`motor_rev` serial commands bypass the helpers intentionally.
- PA0 onboard USER_KEY (PU, active LOW) press flips the flag and issues `Motor_EmergencyStop`. Lets us correct motor lead polarity in the field without reflashing.
- VCP status line now appends a `DIR=NORM` or `DIR=INV` field; an inline `[DIR] inverted=N` is printed on every flip.

**Build**: `cd blackpill_f411 && make` succeeds. FLASH 10.23% / RAM 13.30%.

### Predictable trigger rule (user-confirmed design)

AUTO close fires only when **sofa is in IDLE (fully retracted) AND a fresh
presence rising edge occurs**. The 15-min retract path is the only route
back to IDLE, so the rule "no previous sequence + no person for 15 min"
emerges naturally from the state machine — no separate cooldown.

Buttons cannot park-at-IDLE or disarm. Only the natural retract cycle or
an explicit `sofa_start` resets state. This makes button usage purely
"override the motor in the moment" — they never leave a permanent mark
on the state machine.

### Docs updated this session

- [blackpill_f411/docs/pin-assignment.md](../blackpill_f411/docs/pin-assignment.md) — PA1 → PB12 with new polarity; PA1 returned to Free Pins; PB12 removed from Free Pins
- [blackpill_f411/docs/peripheral-status.md](../blackpill_f411/docs/peripheral-status.md) — Manual Override + Sofa Auto-Adjust rows updated; verification step 8 rewritten
- [blackpill_f411/docs/drivers/manual-buttons.md](../blackpill_f411/docs/drivers/manual-buttons.md) — full rewrite for new semantics, force-close path, `sofa_armed` gate

### What's pending (next session — hardware verification)

1. Wire toggle: PB12 ↔ switch ↔ GND. No external pull resistor needed.
2. Flash: `cd blackpill_f411 && make flash`.
3. Verification checklist in `docs/drivers/manual-buttons.md`. Key new tests:
   - [ ] Toggle press (pin falling) → force-close fires even with no one on radar
   - [ ] Toggle release mid-CLOSING → motor keeps going forward, state stays CLOSING (paused)
   - [ ] Manual button release mid-CLOSING → motor stops, state stays CLOSING (paused); sofa_tick resumes from CLOSING
   - [ ] Boot with toggle already pressed → no force-close (gesture-only)

---

## H7 Sofa Controller — STABLE (reference project)

Two-layer adaptive current detection working on H7 (2026-04-13 session).
All peripherals VERIFIED. Sofa state machine IDLE→CLOSING→CONTACT→RESETTING
with adaptive baseline EMA. The manual-override layer is **BlackPill-only**;
H7 main.c is unchanged.

### Serial Commands Reference (H7, carried forward to F411 verbatim)

| Command | Action |
|---------|--------|
| `sofa_start` | Enable sofa auto-adjust, reset to disarmed IDLE (F411: toggle still arbitrates mode) |
| `sofa_stop` | Disable sofa state machine; manual buttons still work on F411 |
| `sofa_status` | Print state snapshot |
| `sofa_thresh <mA>` | Set contact offset above baseline (default 200) |
| `motor_fwd/rev/stop` | Manual motor (disables sofa_tick; F411 manual buttons still work) |
| `ina_diag` | INA226 register dump + measurements |
| `ina_read` | Quick I + Vbus readout |
| `ina_test` | Self-test with cross-check |

### Host Tool — `tools/current_monitor.py`

Menu-driven console alongside live current plot. Run with
`python tools/current_monitor.py [COM port] [window seconds]`. Press `?` then
Enter for the menu. F411 enumerates on its own COM port — re-detect when flashed.
