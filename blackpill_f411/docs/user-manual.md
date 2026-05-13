# Smart Sofa — User Manual

*A plain-language guide to how the automatic sofa backrest works.*

---

## 1. What this sofa does

This is a **smart reclining sofa**. Its backrest moves by itself using a motor.

It has three jobs:

1. **Wait** for someone to sit down.
2. **Hug** their back gently by closing the backrest forward until it touches them.
3. **Reset** itself flat again after they leave.

You don't need to push a button to use it. The sofa watches the seat with an invisible radar, and decides what to do on its own. Buttons are there if you want to take over manually.

---

## 2. What's on the sofa (the parts you can see/touch)

| Part | What it looks like | What it does |
|---|---|---|
| **Radar sensor (C4001)** | A small flat module hidden in the frame | "Sees" if a person is sitting on the sofa — through fabric, no camera. |
| **Motor + relay** | A motor on the backrest mechanism | Pushes the backrest forward (close) or pulls it back (open). |
| **Current sensor (INA226)** | A small board on the motor wires | Feels how hard the motor is pushing. When the backrest touches your back, the motor pushes harder — the sensor notices and stops the motor. |
| **Mode switch** | A small toggle switch | Picks between **AUTO** (sofa does everything) and **MANUAL** (you do everything with buttons). |
| **Two manual buttons** | Two push-buttons: **FORWARD** and **BACKWARD** | Move the backrest by hand while you hold them down. |
| **USER button** | A tiny button on the controller board | Reverses the motor direction. Only used once during setup if the motor wires were connected backwards. You can ignore this in normal use. |
| **Status LED** | A blue light on the controller | Blinks slowly when the controller is alive. |

---

## 3. How it normally feels to use

**The everyday experience — no buttons needed:**

1. You walk over and sit down.
2. After about half a second, you hear the motor start. The backrest moves forward and lightly meets your back.
3. The motor stops as soon as it touches you. Sit as long as you want.
4. You stand up and walk away.
5. After 15 minutes of no one sitting there, the motor runs again and opens the backrest back to flat. Ready for the next person.

That's it. AUTO mode handles everything.

---

## 4. The two modes — AUTO and MANUAL

### AUTO mode (toggle switch ON / pressed)

The sofa is in charge. The radar watches for people, and the motor moves by itself.

### MANUAL mode (toggle switch OFF / released)

You are in charge. The radar still watches, but the sofa **will not move on its own**. The motor only moves while you are holding one of the two manual buttons:

- Hold **FORWARD** → backrest closes (toward you) as long as you hold.
- Hold **BACKWARD** → backrest opens (away) as long as you hold.
- Let go → motor stops immediately. The backrest stays where it is.

You can use MANUAL anytime you want full control, like during cleaning, repositioning a cushion, or showing the sofa to a customer.

### Switching modes — what happens at the moment you flip the toggle

| You do this | What happens |
|---|---|
| Flip toggle ON (AUTO) | Sofa **immediately starts closing** the backrest once. This is the "demo button" — useful for showing off. After that it behaves like normal AUTO. |
| Flip toggle OFF (MANUAL) | Motor freezes wherever it is. Sofa won't move until you press a manual button. |

The "demo close" on flipping into AUTO only happens **once per flip**, not continuously.

---

## 5. The state diagram — what the sofa is doing at any moment

This is the full picture. There are four AUTO states and one MANUAL state.

```
                       ┌─────────────────────────────────────┐
                       │                                     │
                       │           SOFA POWER ON             │
                       │                                     │
                       └────────────────┬────────────────────┘
                                        │
                                        ▼
                       ┌─────────────────────────────────────┐
                       │                                     │
              ┌───────►│              IDLE                   │◄──────┐
              │        │   Backrest is fully open & flat.    │       │
              │        │   Radar is watching the seat.       │       │
              │        │                                     │       │
              │        └────────────────┬────────────────────┘       │
              │                         │                            │
              │                         │ Someone sits down          │
              │                         │ (radar confirms for 0.5s)  │
              │                         ▼                            │
              │        ┌─────────────────────────────────────┐       │
              │        │                                     │       │
              │        │             CLOSING                 │       │
              │        │  Motor running — backrest moving    │       │
              │        │  forward toward the person.         │       │
              │        │                                     │       │
              │        └────────────────┬────────────────────┘       │
              │                         │                            │
              │                         │ Motor pushes harder        │
              │                         │ → INA226 sees current      │
              │                         │   spike = contact made.    │
              │                         │ (or 10-second safety stop) │
              │                         ▼                            │
              │        ┌─────────────────────────────────────┐       │
              │        │                                     │       │
              │        │             CONTACT                 │       │
              │        │  Motor stopped. Backrest is         │       │
              │        │  resting gently on the person.      │       │
              │        │  Sit as long as you like.           │       │
              │        │                                     │       │
              │        └────────────────┬────────────────────┘       │
              │                         │                            │
              │                         │ Person leaves AND          │
              │                         │ seat stays empty for       │
              │                         │ 15 minutes.                │
              │                         ▼                            │
              │        ┌─────────────────────────────────────┐       │
              │        │                                     │       │
              │        │            RESETTING                │       │
              │        │  Motor runs in reverse — backrest   │       │
              │        │  opens back to flat.                │       │
              │        │  Stops when it reaches the end      │       │
              │        │  (motor stalls) or after 10s.       │       │
              │        │                                     │       │
              │        └────────────────┬────────────────────┘       │
              │                         │                            │
              │                         │ Back to flat.              │
              └─────────────────────────┴────────────────────────────┘


  At any time, flipping the toggle OFF puts the sofa into:

                       ┌─────────────────────────────────────┐
                       │                                     │
                       │           MANUAL MODE               │
                       │  AUTO behaviour paused.             │
                       │  Motor moves ONLY while you hold    │
                       │  FORWARD or BACKWARD button.        │
                       │  Release → motor stops, position    │
                       │  is kept.                           │
                       │                                     │
                       └─────────────────────────────────────┘
```

### Plain-English explanation of each state

- **IDLE** — Sofa is resting flat, waiting. Nothing moving. Radar is looking.
- **CLOSING** — Motor is on, pushing the backrest forward. Started because radar saw a person.
- **CONTACT** — Motor is off. The backrest is gently touching the person's back.
- **RESETTING** — Motor is on in reverse, opening the backrest because the person has been gone for 15 minutes.
- **MANUAL** — Auto behaviour is paused; you drive the motor with the two buttons.

---

## 6. Important safety details (built into the firmware)

- **Gentle touch.** The sofa stops as soon as the motor feels resistance (about 130 mA above its idle current). It does not keep pushing.
- **10-second safety limit on closing.** If something is wrong and the motor never feels contact, it stops anyway after 10 seconds. It will never run forever.
- **10-second safety limit on opening.** Same idea when going back to flat.
- **15-minute "are you really gone?" wait.** Before opening the backrest, the sofa checks that the seat has been empty continuously for a full 15 minutes. This stops the sofa from suddenly opening if you stand up briefly to reach for the remote.
- **One-shot AUTO close.** Once the sofa has closed onto a person, it will **not** close again on that same person if they shift around or get up briefly. It only re-closes on a brand-new sit-down event. This prevents annoying repeated nudges.
- **Manual override is always available.** Even in the middle of an automatic close, flipping the toggle to MANUAL freezes the motor. Pressing a manual button always wins over AUTO.
- **Boot is safe.** When you turn the controller on, it remembers whichever mode the toggle is currently in and does **not** trigger a spurious close.

---

## 7. Quick reference — what each control does

| Control | Action | Result |
|---|---|---|
| Mode toggle → AUTO | Flip ON | Sofa enters AUTO. Backrest closes once as a "demo," then automatic from then on. |
| Mode toggle → MANUAL | Flip OFF | Sofa pauses AUTO. Motor stays where it is. |
| FORWARD button | Hold | Backrest closes while held. Release = stop. |
| BACKWARD button | Hold | Backrest opens while held. Release = stop. |
| Both buttons pressed | n/a | Hardware does not allow both at the same time; the active one wins. |
| USER button (on-board) | Press once | Reverses motor direction. Only used during initial install if the motor was wired backwards. |
| Power off | Cut power | All movement stops. Backrest stays where it is until power returns. |

---

## 8. Troubleshooting (everyday issues)

| You see this | Likely reason | What to do |
|---|---|---|
| Sofa doesn't close when you sit down | Toggle is in MANUAL position | Flip toggle to AUTO. |
| Sofa keeps closing onto an empty seat | Radar misreading (rare) | Flip to MANUAL, then back to AUTO to reset. |
| Backrest moves the wrong way (closes when it should open) | Motor wired backwards | Press the on-board USER button once. Direction flips. |
| Motor stops too early on the way in | Sofa thinks it touched the person | Increase the contact threshold (technician task — needs the serial command `sofa_thresh`). |
| Won't open after person leaves | The 15-minute wait isn't over yet | Wait. Or flip toggle OFF then ON for an immediate forced close (then later release the toggle to manually open with the BACKWARD button). |
| Nothing works, no LED blink | No power | Check power supply and USB connection. |

---

## 9. For the technically curious

If you want to know what's happening under the hood, the controller speaks over USB serial (9600 baud). Plug a laptop into the USB port and you'll see one line every 0.2 seconds describing the sofa's current state, the radar reading, and the motor current. Serial commands `sofa_start`, `sofa_stop`, `sofa_status`, and `sofa_thresh <mA>` are also available — see `docs/vcp-serial-format.md` for the protocol. None of this is needed for normal use.

---

*End of manual.*
