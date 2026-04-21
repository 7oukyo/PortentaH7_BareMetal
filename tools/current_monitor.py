"""
Real-time INA226 current monitor + interactive C4001/Sofa control console.

Plots mA from the Portenta H7 USB VCP and gives a menu-driven console so you
can calibrate the C4001 mmWave sensor (sensitivity, range, mode, etc.) and
drive the sofa state machine without remembering any vendor command syntax.

Graph traces:
  - Blue:       instantaneous current (mA)
  - Green:      adaptive baseline EMA (mA), only during CLOSING/RESETTING
  - Red dashed: effective threshold (baseline + offset)

Console:
  - Press ? or h, then Enter, to print the menu
  - All commands are single letters; values are prompted interactively
  - Sensor / firmware replies are echoed to the terminal as they arrive

Usage:
    python current_monitor.py              # defaults: COM4, 9600 baud, 60s window
    python current_monitor.py COM5         # different port
    python current_monitor.py COM4 120     # 120-second rolling window
"""

import sys
import re
import time
import threading
import collections
import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ---- Configuration ----
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
BAUD = 9600
WINDOW_SEC = int(sys.argv[2]) if len(sys.argv) > 2 else 60
UPDATE_MS = 100  # graph refresh interval
RECONNECT_INTERVAL = 2  # seconds between reconnect attempts

# ---- Shared data (thread-safe via deque) ----
timestamps = collections.deque(maxlen=WINDOW_SEC * 10)
currents = collections.deque(maxlen=WINDOW_SEC * 10)
bl_timestamps = collections.deque(maxlen=WINDOW_SEC * 10)  # baseline trace
baselines = collections.deque(maxlen=WINDOW_SEC * 10)
thresh_timestamps = collections.deque(maxlen=WINDOW_SEC * 10)  # threshold trace
thresholds = collections.deque(maxlen=WINDOW_SEC * 10)

latest_state = ["--"]
latest_motor = ["--"]
latest_presence = ["--"]
latest_settle = ["--"]
latest_peak = ["--"]
latest_baseline = ["--"]
start_time = [None]

# Connection / config state
config_received = threading.Event()
connected = [False]
connect_status = ["Waiting for serial..."]
contact_offset_ma = [200.0]  # default until firmware reports
stall_offset_ma = [250.0]

# Shared serial handle so the input thread can write commands
ser_handle = [None]
ser_lock = threading.Lock()
quit_event = threading.Event()

# Regex patterns
RE_CURRENT = re.compile(r"I=(-?\d+\.?\d*)mA")
RE_STATE = re.compile(r"(?:sofa=|SOFA=)(\w+)")
RE_MOTOR = re.compile(r"MTR=(\w+)")
RE_PRESENCE = re.compile(r"PRS=([01])")
RE_SETTLE = re.compile(r"STL=([01])")
RE_PEAK = re.compile(r"PK=(\d+)")
RE_BASELINE = re.compile(r"BL=(\d+)")
RE_OFFSET_CMD = re.compile(r"offset=\+(\d+)mA")
RE_CFG_CONTACT = re.compile(r"\[CFG\] CONTACT=\+(\d+)mA")
RE_CFG_STALL = re.compile(r"STALL=\+(\d+)mA")

# Lines worth surfacing to stdout (firmware echoes, sensor responses, banners)
RE_PASSTHROUGH = re.compile(r"^(>|\[sensor\]|\[CFG\]|\[INA226\]|\[SOFA\]|\[OK\]|\[ERR\])")


# ============================================================================
# Serial I/O helpers
# ============================================================================

def send_serial(text):
    """Write a command string to the firmware's USB CDC. Adds CRLF if missing."""
    with ser_lock:
        ser = ser_handle[0]
        if ser is None:
            print("[!!] Not connected — cannot send.")
            return False
        if not text.endswith("\n"):
            text = text + "\r\n"
        try:
            ser.write(text.encode("utf-8"))
            ser.flush()
            return True
        except (serial.SerialException, OSError) as e:
            print(f"[!!] Send failed: {e}")
            return False


def serial_reader():
    """Background thread: connect, read, parse, auto-reconnect on failure."""
    while not quit_event.is_set():
        # ---- Try to open port ----
        ser = None
        while ser is None and not quit_event.is_set():
            try:
                ser = serial.Serial(PORT, BAUD, timeout=0.5)
                with ser_lock:
                    ser_handle[0] = ser
                connected[0] = True
                connect_status[0] = f"Connected to {PORT}"
                print(f"[OK] Connected to {PORT} @ {BAUD} baud")
            except serial.SerialException:
                connected[0] = False
                connect_status[0] = f"Waiting for {PORT}..."
                time.sleep(RECONNECT_INTERVAL)

        if quit_event.is_set():
            return

        # ---- Clear state for fresh connection ----
        config_received.clear()
        timestamps.clear()
        currents.clear()
        bl_timestamps.clear()
        baselines.clear()
        thresh_timestamps.clear()
        thresholds.clear()
        latest_state[0] = "--"
        latest_motor[0] = "--"
        latest_presence[0] = "--"
        latest_settle[0] = "--"
        latest_peak[0] = "--"
        latest_baseline[0] = "--"
        start_time[0] = None
        connect_status[0] = "Connected — waiting for [CFG]..."
        print("[..] Waiting for firmware [CFG] startup line...")

        # ---- Read loop ----
        try:
            while not quit_event.is_set():
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue

                # Surface firmware/sensor messages to the user's terminal
                if RE_PASSTHROUGH.match(line):
                    # newline prefix keeps an active input prompt readable
                    print(f"\n{line}")

                # Parse startup config — gates data collection
                m = RE_CFG_CONTACT.search(line)
                if m:
                    contact_offset_ma[0] = float(m.group(1))
                    m2 = RE_CFG_STALL.search(line)
                    if m2:
                        stall_offset_ma[0] = float(m2.group(1))
                    if not config_received.is_set():
                        config_received.set()
                        start_time[0] = time.monotonic()
                        connect_status[0] = f"Live — {PORT}"
                        print(f"[OK] Config: contact=+{int(contact_offset_ma[0])}mA"
                              f" stall=+{int(stall_offset_ma[0])}mA")

                if not config_received.is_set():
                    continue

                t = time.monotonic() - start_time[0]

                # Parse current
                m = RE_CURRENT.search(line)
                if m:
                    timestamps.append(t)
                    currents.append(float(m.group(1)))

                # Parse state info
                m = RE_STATE.search(line)
                if m:
                    latest_state[0] = m.group(1)
                m = RE_MOTOR.search(line)
                if m:
                    latest_motor[0] = m.group(1)
                m = RE_PRESENCE.search(line)
                if m:
                    latest_presence[0] = "YES" if m.group(1) == "1" else "NO"

                # Parse settle / baseline (only in CLOSING/RESETTING)
                m = RE_SETTLE.search(line)
                if m:
                    latest_settle[0] = "YES" if m.group(1) == "1" else "NO"
                m = RE_PEAK.search(line)
                if m:
                    latest_peak[0] = m.group(1)
                m = RE_BASELINE.search(line)
                if m:
                    bl_val = float(m.group(1))
                    latest_baseline[0] = m.group(1)
                    bl_timestamps.append(t)
                    baselines.append(bl_val)
                    state = latest_state[0]
                    offset = stall_offset_ma[0] if state == "RESETTING" else contact_offset_ma[0]
                    thresh_timestamps.append(t)
                    thresholds.append(bl_val + offset)

                # Parse runtime offset change via sofa_thresh command
                m = RE_OFFSET_CMD.search(line)
                if m:
                    contact_offset_ma[0] = float(m.group(1))
                    print(f"[OK] Offset updated: +{int(contact_offset_ma[0])}mA")

        except (serial.SerialException, OSError):
            connected[0] = False
            config_received.clear()
            connect_status[0] = "Disconnected — reconnecting..."
            print(f"[!!] Serial lost. Reconnecting in {RECONNECT_INTERVAL}s...")
            with ser_lock:
                ser_handle[0] = None
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(RECONNECT_INTERVAL)


# ============================================================================
# Interactive console — friendly wrappers around firmware/sensor commands
# ============================================================================

MENU = """
========================== C4001 mmWave Sensor ==========================
  s) Sensitivity (0-9)        higher = more sensitive (trig + keep)
  r) Detection range (m)      e.g. 0.3 to 5.0
  m) Mode                     1 = presence, 2 = speed
  d) Delays                   trig delay (10ms units) + keep timeout (0.5s units)
  u) Micro-motion             on / off
  G) Start sensor (Go)
  S) Stop sensor
  X) Factory reset (all C4001 params)
============================ Sofa Controller ============================
  o) Set contact offset (mA above baseline)
  P) Print sofa_status
  1) sofa_start
  0) sofa_stop
  F) Motor forward
  R) Motor reverse
  N) Motor stop (Neutral)
================================ INA226 =================================
  i) ina_read    quick I + Vbus
  D) ina_diag    full register dump
  T) ina_test    self-test
================================ Misc ===================================
  c) Send raw command (advanced — for un-wrapped commands)
  ?) or h)  Show this menu again
  q) Quit
=========================================================================
"""


def prompt(text, default=None):
    """Wrap input() with a default-on-empty option."""
    suffix = f" [{default}]" if default is not None else ""
    try:
        s = input(f"  {text}{suffix}: ").strip()
    except (EOFError, KeyboardInterrupt):
        return None
    if not s and default is not None:
        return str(default)
    return s


def parse_int_in_range(s, lo, hi):
    try:
        v = int(s)
    except (TypeError, ValueError):
        return None
    return v if lo <= v <= hi else None


def cmd_sensitivity():
    s = prompt("Sensitivity 0-9 (sets trig and keep)", "5")
    v = parse_int_in_range(s, 0, 9)
    if v is None:
        print("  ! invalid (must be 0-9)")
        return
    # Set both trig and keep to the same value via two separate commands
    # Format used by C4001: setSensitivity <keep> <trig>; 255 = "don't change"
    send_serial(f"setSensitivity 255 {v}")
    time.sleep(0.4)
    send_serial(f"setSensitivity {v} 255")


def cmd_range():
    s = prompt("Range MIN max in meters (e.g. '0.3 5.0')", "0.3 5.0")
    if not s:
        return
    parts = s.split()
    if len(parts) != 2:
        print("  ! need two numbers separated by space")
        return
    try:
        lo = float(parts[0])
        hi = float(parts[1])
    except ValueError:
        print("  ! invalid floats")
        return
    if not (0.3 <= lo < hi <= 20.0):
        print("  ! out of range (0.3 <= min < max <= 20.0)")
        return
    send_serial(f"setRange {lo:.2f} {hi:.2f}")


def cmd_mode():
    s = prompt("Mode: 1=presence, 2=speed", "1")
    if s == "1":
        send_serial("setRunApp 0")
    elif s == "2":
        send_serial("setRunApp 1")
    else:
        print("  ! pick 1 or 2")


def cmd_delays():
    td = prompt("Trigger delay (10ms units, 0-200)", "0")
    kt = prompt("Keep timeout (0.5s units, 4-3000)", "10")
    td_v = parse_int_in_range(td, 0, 200)
    kt_v = parse_int_in_range(kt, 4, 3000)
    if td_v is None or kt_v is None:
        print("  ! invalid range")
        return
    send_serial(f"setLatency {td_v} {kt_v}")


def cmd_micromotion():
    s = prompt("Micro-motion on/off", "on")
    if s.lower() in ("on", "1", "y", "yes"):
        send_serial("setMicroMotion 1")
    elif s.lower() in ("off", "0", "n", "no"):
        send_serial("setMicroMotion 0")
    else:
        print("  ! say on or off")


def cmd_start_sensor():
    send_serial("sensorStart")


def cmd_stop_sensor():
    send_serial("sensorStop")


def cmd_factory_reset():
    s = prompt("Type 'YES' to factory-reset C4001", "")
    if s == "YES":
        send_serial("sensorStop")
        time.sleep(0.3)
        send_serial("resetCfg")
        time.sleep(1.5)
        send_serial("sensorStart")
        print("  [OK] factory reset issued")
    else:
        print("  cancelled")


def cmd_offset():
    s = prompt("Contact offset above baseline (mA)", str(int(contact_offset_ma[0])))
    try:
        v = int(float(s))
    except (TypeError, ValueError):
        print("  ! invalid integer")
        return
    if not (0 <= v <= 5000):
        print("  ! out of sensible range (0-5000 mA)")
        return
    send_serial(f"sofa_thresh {v}")


def cmd_raw():
    s = prompt("Raw command (sent verbatim, CRLF appended)", "")
    if s:
        send_serial(s)


# Single-key dispatcher
DISPATCH = {
    "s": cmd_sensitivity,
    "r": cmd_range,
    "m": cmd_mode,
    "d": cmd_delays,
    "u": cmd_micromotion,
    "G": cmd_start_sensor,
    "S": cmd_stop_sensor,
    "X": cmd_factory_reset,
    "o": cmd_offset,
    "P": lambda: send_serial("sofa_status"),
    "1": lambda: send_serial("sofa_start"),
    "0": lambda: send_serial("sofa_stop"),
    "F": lambda: send_serial("motor_fwd"),
    "R": lambda: send_serial("motor_rev"),
    "N": lambda: send_serial("motor_stop"),
    "i": lambda: send_serial("ina_read"),
    "D": lambda: send_serial("ina_diag"),
    "T": lambda: send_serial("ina_test"),
    "c": cmd_raw,
}


def cli_loop():
    """Background thread: read single-letter commands from stdin and dispatch."""
    print(MENU)
    while not quit_event.is_set():
        try:
            choice = input("cmd> ").strip()
        except (EOFError, KeyboardInterrupt):
            quit_event.set()
            return
        if not choice:
            continue
        if choice in ("q", "Q", "quit", "exit"):
            quit_event.set()
            print("Bye.")
            # Closing the plot window will end the program.
            try:
                plt.close("all")
            except Exception:
                pass
            return
        if choice in ("?", "h", "H", "help"):
            print(MENU)
            continue
        action = DISPATCH.get(choice)
        if action is None:
            print(f"  unknown: {choice!r}  (press ? for menu)")
            continue
        try:
            action()
        except Exception as e:
            print(f"  ! command failed: {e}")


# ============================================================================
# Plot
# ============================================================================

def main():
    threading.Thread(target=serial_reader, daemon=True).start()
    threading.Thread(target=cli_loop, daemon=True).start()

    fig, ax = plt.subplots(figsize=(12, 5))
    fig.patch.set_facecolor("#1e1e2e")
    ax.set_facecolor("#1e1e2e")

    (line_current,) = ax.plot([], [], color="#89b4fa", linewidth=1.5, label="Current (mA)")
    (line_baseline,) = ax.plot([], [], color="#a6e3a1", linewidth=1.5, alpha=0.8,
                               linestyle="-", label="Baseline EMA")
    (line_thresh,) = ax.plot([], [], color="#f38ba8", linewidth=1, alpha=0.7,
                             linestyle="--", label="Threshold (BL+offset)")

    ax.set_xlabel("Time (s)", color="#cdd6f4", fontsize=10)
    ax.set_ylabel("Current (mA)", color="#cdd6f4", fontsize=10)
    ax.set_title("INA226 Current Monitor — Adaptive Baseline (use console to control)",
                 color="#cdd6f4", fontsize=12, fontweight="bold")
    ax.tick_params(colors="#6c7086")
    ax.spines["bottom"].set_color("#45475a")
    ax.spines["left"].set_color("#45475a")
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.grid(True, color="#313244", alpha=0.5)
    ax.legend(loc="upper left", facecolor="#313244", edgecolor="#45475a",
              labelcolor="#cdd6f4", fontsize=9)

    status_text = ax.text(
        0.98, 0.95, "", transform=ax.transAxes, fontsize=9, fontfamily="monospace",
        color="#a6e3a1", ha="right", va="top",
        bbox=dict(boxstyle="round,pad=0.4", facecolor="#313244", edgecolor="#45475a", alpha=0.9)
    )
    current_text = ax.text(
        0.5, 0.95, "-- mA", transform=ax.transAxes, fontsize=22, fontweight="bold",
        color="#f9e2af", ha="center", va="top"
    )
    waiting_text = ax.text(
        0.5, 0.5, "Waiting for firmware config...\nReset board or open serial",
        transform=ax.transAxes, fontsize=14, color="#6c7086",
        ha="center", va="center", fontfamily="monospace"
    )

    def update(_frame):
        if not config_received.is_set():
            waiting_text.set_visible(True)
            waiting_text.set_text(connect_status[0])
            current_text.set_text("-- mA")
            current_text.set_color("#6c7086")
            line_current.set_data([], [])
            line_baseline.set_data([], [])
            line_thresh.set_data([], [])
            status_text.set_text(f"Status: {connect_status[0]}")
            return (line_current, line_baseline, line_thresh,
                    status_text, current_text, waiting_text)

        waiting_text.set_visible(False)

        if not timestamps:
            return (line_current, line_baseline, line_thresh,
                    status_text, current_text, waiting_text)

        ts = list(timestamps)
        cs = list(currents)
        bts = list(bl_timestamps)
        bls = list(baselines)
        tts = list(thresh_timestamps)
        ths = list(thresholds)

        line_current.set_data(ts, cs)
        line_baseline.set_data(bts, bls)
        line_thresh.set_data(tts, ths)

        t_max = ts[-1]
        t_min = max(0, t_max - WINDOW_SEC)
        ax.set_xlim(t_min, t_max + 1)

        visible_c = [c for t, c in zip(ts, cs) if t >= t_min]
        visible_bl = [b for t, b in zip(bts, bls) if t >= t_min]
        visible_th = [h for t, h in zip(tts, ths) if t >= t_min]
        all_visible = visible_c + visible_bl + visible_th
        if all_visible:
            y_min = min(min(all_visible), 0)
            y_max = max(all_visible)
            margin = max((y_max - y_min) * 0.1, 50)
            ax.set_ylim(y_min - margin, y_max + margin)

        current_text.set_text(f"{cs[-1]:.1f} mA")
        if ths and cs[-1] > ths[-1]:
            current_text.set_color("#f38ba8")  # red = above threshold
        else:
            current_text.set_color("#f9e2af")  # yellow = normal

        state = latest_state[0]
        lines = [
            f"State:   {state}",
            f"Motor:   {latest_motor[0]}",
            f"Person:  {latest_presence[0]}",
        ]
        if state in ("CLOSING", "RESETTING"):
            lines.append(f"Settled: {latest_settle[0]}")
            lines.append(f"Peak:    {latest_peak[0]} mA")
            lines.append(f"Base:    {latest_baseline[0]} mA")
            if latest_baseline[0] != "--":
                offset = contact_offset_ma[0] if state == "CLOSING" else stall_offset_ma[0]
                eff_thresh = int(float(latest_baseline[0]) + offset)
                lines.append(f"Thresh:  {eff_thresh} mA (+{int(offset)})")
        else:
            lines.append(f"Offset:  +{int(contact_offset_ma[0])} mA")

        status_text.set_text("\n".join(lines))

        return (line_current, line_baseline, line_thresh,
                status_text, current_text, waiting_text)

    ani = animation.FuncAnimation(fig, update, interval=UPDATE_MS,
                                  blit=False, cache_frame_data=False)

    plt.tight_layout()
    print("Graph window open. Type ? in this terminal for the command menu.")
    try:
        plt.show()
    finally:
        quit_event.set()


if __name__ == "__main__":
    main()
