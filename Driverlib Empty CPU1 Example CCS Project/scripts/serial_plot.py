"""
serial_plot.py — Live serial plotter (beginner version)
Reads lines like:  Ia,Ib,Ic,Vdc
"""

import collections
import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# --- Settings ---
PORT    = "COM3"
BAUD    = 3_000_000
WINDOW  = 200          # how many samples to show at once
REFRESH = 50           # redraw every 50ms

# --- Open the serial port ---
ser = serial.Serial(PORT, BAUD, timeout=0.1)

# --- Storage: one rolling list per channel ---
ia  = collections.deque([0.0] * WINDOW, maxlen=WINDOW)
ib  = collections.deque([0.0] * WINDOW, maxlen=WINDOW)
ic  = collections.deque([0.0] * WINDOW, maxlen=WINDOW)
vdc = collections.deque([0.0] * WINDOW, maxlen=WINDOW)

# --- Set up the plot ---
fig, ax = plt.subplots()
x = list(range(WINDOW))   # x-axis is just 0, 1, 2, ... 199

line_ia,  = ax.plot(x, ia,  label="Ia")
line_ib,  = ax.plot(x, ib,  label="Ib")
line_ic,  = ax.plot(x, ic,  label="Ic")
line_vdc, = ax.plot(x, vdc, label="Vdc")

ax.legend()
ax.grid(True)
ax.set_xlabel("Sample")
ax.set_ylabel("Value")

# --- This runs every REFRESH ms to update the plot ---
def update(frame):
    # Read all waiting lines from the serial port
    while ser.in_waiting:
        raw  = ser.readline()
        text = raw.decode("ascii", errors="replace").strip()

        # Split "1.0,2.0,3.0,4.0" into ["1.0", "2.0", "3.0", "4.0"]
        parts = text.split(",")

        # Skip the line if it's not 4 numbers
        if len(parts) != 4:
            print("Skipped:", text)
            continue

        try:
            a, b, c, d = float(parts[0]), float(parts[1]), float(parts[2]), float(parts[3])
        except ValueError:
            print("Skipped:", text)
            continue

        # Push new values into the rolling buffers
        ia.append(a)
        ib.append(b)
        ic.append(c)
        vdc.append(d)

    # Update each line with the latest buffer data
    line_ia.set_ydata(ia)
    line_ib.set_ydata(ib)
    line_ic.set_ydata(ic)
    line_vdc.set_ydata(vdc)

    # Rescale y-axis to fit whatever values are on screen
    all_values = list(ia) + list(ib) + list(ic) + list(vdc)
    ax.set_ylim(min(all_values) - 1, max(all_values) + 1)

    return line_ia, line_ib, line_ic, line_vdc

# --- Start the animation and keep the window open ---
ani = animation.FuncAnimation(fig, update, interval=REFRESH)

try:
    plt.show()
finally:
    ser.close()   # always close the port when the window is shut