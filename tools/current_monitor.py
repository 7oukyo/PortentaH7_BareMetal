"""
Real-time INA226 current monitor — plots mA from Portenta H7 VCP serial.

Parses "I=<value>mA" from both standard reports (~1Hz) and sofa status (~5Hz).
Also shows sofa state and motor direction when available.

Features:
  - Waits for [CFG] startup line before plotting (ensures correct threshold)
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
latest_state = ["--"]
latest_motor = ["--"]
latest_presence = ["--"]
start_time = [None]

# Connection / config state
config_received = threading.Event()  # set when [CFG] line is parsed
connected = [False]
connect_status = ["Waiting for serial..."]
threshold_ma = [1500.0]  # default until firmware reports

# Regex patterns
RE_CURRENT = re.compile(r"I=(-?\d+\.?\d*)mA")
RE_STATE = re.compile(r"(?:sofa=|SOFA=)(\w+)")
RE_MOTOR = re.compile(r"MTR=(\w+)")
RE_PRESENCE = re.compile(r"PRS=([01])")
RE_THRESH = re.compile(r"threshold=(\d+)mA")
RE_CFG_CONTACT = re.compile(r"\[CFG\] CONTACT=(\d+)mA")


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
        latest_state[0] = "--"
        latest_motor[0] = "--"
        latest_presence[0] = "--"
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
                    threshold_ma[0] = float(m.group(1))
                    if not config_received.is_set():
                        config_received.set()
                        start_time[0] = time.monotonic()
                        connect_status[0] = f"Live — {PORT}"
                        print(f"[OK] Config received: threshold={int(threshold_ma[0])}mA")

                # Only collect data after config is received
                if not config_received.is_set():
                    continue

                # Parse current
                m = RE_CURRENT.search(line)
                if m:
                    ma = float(m.group(1))
                    t = time.monotonic() - start_time[0]
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

                # Parse runtime threshold change
                m = RE_THRESH.search(line)
                if m:
                    threshold_ma[0] = float(m.group(1))

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
            # Loop back to reconnect


def main():
    # Start serial reader thread
    t = threading.Thread(target=serial_reader, daemon=True)
    t.start()

    # ---- Set up plot ----
    fig, ax = plt.subplots(figsize=(12, 5))
    fig.patch.set_facecolor("#1e1e2e")
    ax.set_facecolor("#1e1e2e")

    (line_current,) = ax.plot([], [], color="#89b4fa", linewidth=1.5, label="Current (mA)")
    line_thresh = ax.axhline(
        y=threshold_ma[0], color="#f38ba8", linestyle="--", linewidth=1, alpha=0.7,
        label=f"Threshold ({int(threshold_ma[0])}mA)"
    )

    ax.set_xlabel("Time (s)", color="#cdd6f4", fontsize=10)
    ax.set_ylabel("Current (mA)", color="#cdd6f4", fontsize=10)
    ax.set_title("INA226 Current Monitor", color="#cdd6f4", fontsize=13, fontweight="bold")
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
            status_text.set_text(f"Status: {connect_status[0]}")
            return (line_current, status_text, current_text, waiting_text)

        waiting_text.set_visible(False)

        if not timestamps:
            return (line_current, status_text, current_text, waiting_text)

        ts = list(timestamps)
        cs = list(currents)

        line_current.set_data(ts, cs)

        # Update threshold line
        line_thresh.set_ydata([threshold_ma[0]])
        line_thresh.set_label(f"Threshold ({int(threshold_ma[0])}mA)")
        ax.legend(loc="upper left", facecolor="#313244", edgecolor="#45475a",
                  labelcolor="#cdd6f4", fontsize=9)

        # Auto-scale X to rolling window
        t_max = ts[-1]
        t_min = max(0, t_max - WINDOW_SEC)
        ax.set_xlim(t_min, t_max + 1)

        # Auto-scale Y with padding
        if cs:
            visible = [c for t, c in zip(ts, cs) if t >= t_min]
            if visible:
                y_min = min(min(visible), 0)
                y_max = max(max(visible), threshold_ma[0] * 1.1)
                margin = max((y_max - y_min) * 0.1, 50)
                ax.set_ylim(y_min - margin, y_max + margin)

        # Update current value display
        current_text.set_text(f"{cs[-1]:.1f} mA")
        if cs[-1] > threshold_ma[0]:
            current_text.set_color("#f38ba8")
        else:
            current_text.set_color("#f9e2af")

        # Update status text
        status_text.set_text(
            f"State: {latest_state[0]}\n"
            f"Motor: {latest_motor[0]}\n"
            f"Person: {latest_presence[0]}\n"
            f"Thresh: {int(threshold_ma[0])}mA"
        )

        return (line_current, status_text, current_text, waiting_text)

    ani = animation.FuncAnimation(fig, update, interval=UPDATE_MS, blit=False, cache_frame_data=False)

    plt.tight_layout()
    print("Graph window open. Reset board to start.")
    plt.show()


if __name__ == "__main__":
    main()
