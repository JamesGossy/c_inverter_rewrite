"""
serial_plot.py — Live serial plotter + command sender for open-loop spin test

RX format from DSP: "Ia,Ib,Ic,Vdc,theta\r\n"
TX format to  DSP: "vd,vq,freq,enable\r\n"
"""

import collections
import threading
import serial
import matplotlib.pyplot as plt
import matplotlib.widgets as widgets

# --- Settings ---
PORT          = "COM12"
BAUD          = 3_000_000
WINDOW        = 200       # samples shown at once
REFRESH       = 0.05      # seconds between redraws
MAX_PER_FRAME = 500       # max lines drained per frame

# --- Open port ---
ser = serial.Serial(PORT, BAUD, timeout=0.0)

# --- Rolling buffers ---
ia    = collections.deque([0.0] * WINDOW, maxlen=WINDOW)
ib    = collections.deque([0.0] * WINDOW, maxlen=WINDOW)
ic    = collections.deque([0.0] * WINDOW, maxlen=WINDOW)
vdc   = collections.deque([0.0] * WINDOW, maxlen=WINDOW)
theta = collections.deque([0.0] * WINDOW, maxlen=WINDOW)

# --- Command state (written by widgets, read by send thread) ---
cmd_lock    = threading.Lock()
cmd_vd      = 0.0
cmd_vq      = 3.0
cmd_freq    = 5.0
cmd_enable  = False

def send_command():
    with cmd_lock:
        en = 1 if cmd_enable else 0
        line = f"{cmd_vd:.3f},{cmd_vq:.3f},{cmd_freq:.3f},{en}\r\n"
    ser.write(line.encode("ascii"))

# --- Layout ---
# 3 data rows + control strip at bottom
fig = plt.figure(figsize=(10, 8))
fig.subplots_adjust(left=0.1, right=0.97, top=0.95, bottom=0.32, hspace=0.35)

ax_i     = fig.add_subplot(3, 1, 1)
ax_v     = fig.add_subplot(3, 1, 2)
ax_theta = fig.add_subplot(3, 1, 3)

x = list(range(WINDOW))

line_ia,    = ax_i.plot(x, ia,    label="Ia (A)")
line_ib,    = ax_i.plot(x, ib,    label="Ib (A)")
line_ic,    = ax_i.plot(x, ic,    label="Ic (A)")
line_vdc,   = ax_v.plot(x, vdc,   label="Vdc (V)", color="orange")
line_theta, = ax_theta.plot(x, theta, label="θ (rad)", color="purple")

for ax, ylabel in [(ax_i, "Current (A)"), (ax_v, "Voltage (V)"), (ax_theta, "Angle (rad)")]:
    ax.legend(loc="upper right", fontsize=8)
    ax.grid(True)
    ax.set_ylabel(ylabel)
ax_theta.set_xlabel("Sample")

# --- Control widgets ---
ax_vd   = fig.add_axes([0.10, 0.20, 0.55, 0.03])
ax_vq   = fig.add_axes([0.10, 0.14, 0.55, 0.03])
ax_freq = fig.add_axes([0.10, 0.08, 0.55, 0.03])
ax_btn  = fig.add_axes([0.75, 0.08, 0.15, 0.10])

sl_vd   = widgets.Slider(ax_vd,   "Vd (V)",   -20.0, 20.0, valinit=cmd_vd,   valstep=0.5)
sl_vq   = widgets.Slider(ax_vq,   "Vq (V)",   -20.0, 20.0, valinit=cmd_vq,   valstep=0.5)
sl_freq = widgets.Slider(ax_freq, "Freq (Hz)",   0.0, 60.0, valinit=cmd_freq, valstep=1.0)
btn_en  = widgets.Button(ax_btn, "Enable\nOFF", color="0.85")

def on_vd(val):
    global cmd_vd
    with cmd_lock:
        cmd_vd = val
    send_command()

def on_vq(val):
    global cmd_vq
    with cmd_lock:
        cmd_vq = val
    send_command()

def on_freq(val):
    global cmd_freq
    with cmd_lock:
        cmd_freq = val
    send_command()

def on_enable(event):
    global cmd_enable
    with cmd_lock:
        cmd_enable = not cmd_enable
        enabled = cmd_enable
    btn_en.label.set_text("Enable\nON" if enabled else "Enable\nOFF")
    btn_en.color = "limegreen" if enabled else "0.85"
    btn_en.hovercolor = "lime" if enabled else "0.95"
    send_command()

sl_vd.on_changed(on_vd)
sl_vq.on_changed(on_vq)
sl_freq.on_changed(on_freq)
btn_en.on_clicked(on_enable)

# --- Main loop ---
rxbuf = b""
try:
    while plt.fignum_exists(fig.number):
        # Drain incoming bytes
        rxbuf += ser.read(ser.in_waiting or 1)

        count = 0
        while b"\n" in rxbuf and count < MAX_PER_FRAME:
            line, rxbuf = rxbuf.split(b"\n", 1)
            text  = line.decode("ascii", errors="replace").strip()
            parts = text.split(",")
            if len(parts) == 5:
                try:
                    ia.append(float(parts[0]))
                    ib.append(float(parts[1]))
                    ic.append(float(parts[2]))
                    vdc.append(float(parts[3]))
                    theta.append(float(parts[4]))
                except ValueError:
                    pass
            count += 1

        # Update current plot
        line_ia.set_ydata(ia)
        line_ib.set_ydata(ib)
        line_ic.set_ydata(ic)
        all_i = list(ia) + list(ib) + list(ic)
        ax_i.set_ylim(min(all_i) - 1, max(all_i) + 1)

        # Update Vdc plot
        line_vdc.set_ydata(vdc)
        all_v = list(vdc)
        ax_v.set_ylim(min(all_v) - 5, max(all_v) + 5)

        # Update theta plot
        line_theta.set_ydata(theta)
        ax_theta.set_ylim(-0.5, 7.0)   # 0 to 2π with margins

        fig.canvas.flush_events()
        plt.pause(REFRESH)

finally:
    ser.close()
