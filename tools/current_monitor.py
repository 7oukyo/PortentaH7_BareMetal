"""
Real-time INA226 current monitor — plots mA from Portenta H7 VCP serial.

Parses "I=<value>mA" from both standard reports (~1Hz) and sofa status (~5Hz).
Shows adaptive baseline, dynamic threshold, settle state, and motor info.

Traces:
  - Blue:       instantaneous current (mA)
  - Green:      adaptive baseline EMA (mA) — only during CLOSING/RESETTING
  - Red dashed: effective threshold (baseline + offset)

Features:
  - Waits for [CFG] startup line before plotting (ensures correct config)
  - Auto-reconnects on serial disconnect / board reset
  - Clears graph data on reconnect for a clean view

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


def serial_reader():
    """Background thread: connect, read, parse, auto-reconnect on failure."""
    while True:
        # ---- Try to open port ----
        ser = None
        while ser is None:
            try:
                ser = serial.Serial(PORT, BAUD, timeout=0.5)
                connected[0] = True
                connect_status[0] = f"Connected to {PORT}"
                print(f"[OK] Connected to {PORT} @ {BAUD} baud")
            except serial.SerialException:
                connected[0] = False
                connect_status[0] = f"Waiting for {PORT}..."
                time.sleep(RECONNECT_INTERVAL)

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
        connect_status[0] = f"Connected — waiting for [CFG]..."
        print("[..] Waiting for firmware [CFG] startup line...")

        # ---- Read loop ----
        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue

                # Parse startup config — this gates data collection
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

                # Only collect data after config is received
                if not config_received.is_set():
                    continue

                t = time.monotonic() - start_time[0]

                # Parse current
                m = RE_CURRENT.search(line)
                if m:
                    ma = float(m.group(1))
                    timestamps.append(t)
                    currents.append(ma)

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
                    # Compute effective threshold based on current state
                    state = latest_state[0]
                    if state == "RESETTING":
                        offset = stall_offset_ma[0]
                    else:
                        offset = contact_offset_ma[0]
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
            connect_status[0] = f"Disconnected — reconnecting..."
            print(f"[!!] Serial lost. Reconnecting in {RECONNECT_INTERVAL}s...")
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(RECONNECT_INTERVAL)


def main():
    # Start serial reader thread
    t = threading.Thread(target=serial_reader, daemon=True)
    t.start()

    # ---- Set up plot ----
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
    ax.set_title("INA226 Current Monitor — Adaptive Baseline", color="#cdd6f4",
                 fontsize=13, fontweight="bold")
    ax.tick_params(colors="#6c7086")
    ax.spines["bottom"].set_color("#45475a")
    ax.spines["left"].set_color("#45475a")
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.grid(True, color="#313244", alpha=0.5)
    ax.legend(loc="upper left", facecolor="#313244", edgecolor="#45475a",
              labelcolor="#cdd6f4", fontsize=9)

    # Status text in top-right
    status_text = ax.text(
        0.98, 0.95, "", transform=ax.transAxes, fontsize=9, fontfamily="monospace",
        color="#a6e3a1", ha="right", va="top",
        bbox=dict(boxstyle="round,pad=0.4", facecolor="#313244", edgecolor="#45475a", alpha=0.9)
    )

    # Current value display (large, center-top)
    current_text = ax.text(
        0.5, 0.95, "-- mA", transform=ax.transAxes, fontsize=22, fontweight="bold",
        color="#f9e2af", ha="center", va="top"
    )

    # Center message for waiting state
    waiting_text = ax.text(
        0.5, 0.5, "Waiting for firmware config...\nReset board or open serial",
        transform=ax.transAxes, fontsize=14, color="#6c7086",
        ha="center", va="center", fontfamily="monospace"
    )

    def update(frame):
        # Show/hide waiting message
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

        # Auto-scale X to rolling window
        t_max = ts[-1]
        t_min = max(0, t_max - WINDOW_SEC)
        ax.set_xlim(t_min, t_max + 1)

        # Auto-scale Y with padding
        visible_c = [c for t, c in zip(ts, cs) if t >= t_min]
        visible_bl = [b for t, b in zip(bts, bls) if t >= t_min]
        visible_th = [h for t, h in zip(tts, ths) if t >= t_min]
        all_visible = visible_c + visible_bl + visible_th
        if all_visible:
            y_min = min(min(all_visible), 0)
            y_max = max(all_visible)
            margin = max((y_max - y_min) * 0.1, 50)
            ax.set_ylim(y_min - margin, y_max + margin)

        # Update current value display
        current_text.set_text(f"{cs[-1]:.1f} mA")
        # Color based on whether threshold is available
        if ths and cs[-1] > ths[-1]:
            current_text.set_color("#f38ba8")  # red = above threshold
        else:
            current_text.set_color("#f9e2af")  # yellow = normal

        # Build status text
        state = latest_state[0]
        settle_str = latest_settle[0]
        peak_str = latest_peak[0]
        bl_str = latest_baseline[0]

        lines = [
            f"State:   {state}",
            f"Motor:   {latest_motor[0]}",
            f"Person:  {latest_presence[0]}",
        ]

        # Show settle/baseline info when motor is running
        if state in ("CLOSING", "RESETTING"):
            lines.append(f"Settled: {settle_str}")
            lines.append(f"Peak:    {peak_str} mA")
            lines.append(f"Base:    {bl_str} mA")
            if bl_str != "--":
                offset = contact_offset_ma[0] if state == "CLOSING" else stall_offset_ma[0]
                eff_thresh = int(float(bl_str) + offset)
                lines.append(f"Thresh:  {eff_thresh} mA (+{int(offset)})")
        else:
            lines.append(f"Offset:  +{int(contact_offset_ma[0])} mA")

        status_text.set_text("\n".join(lines))

        return (line_current, line_baseline, line_thresh,
                status_text, current_text, waiting_text)

    ani = animation.FuncAnimation(fig, update, interval=UPDATE_MS,
                                  blit=False, cache_frame_data=False)

    plt.tight_layout()
    print("Graph window open. Reset board to start.")
    plt.show()


if __name__ == "__main__":
    main()
