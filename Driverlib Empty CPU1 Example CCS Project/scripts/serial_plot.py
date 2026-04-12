"""
serial_plot.py — Live telemetry plotter for open-loop spin test

Binary protocol (big-endian):
  RX from DSP:  [0xAA 0x55] [Ia:i16] [Ib:i16] [Ic:i16] [Vdc:u16] [theta:u16] [enc_angle:u16]  — 14 bytes
                scales: currents /100 -> A,  Vdc /100 -> V,  theta /10000 -> rad,  enc_angle /10000 -> rad
  TX to   DSP:  [0xBB 0xCC] [vd:i16] [vq:i16] [freq:u16] [enable:u8] [cal:u8]                 — 10 bytes
                scales: vd/vq *100,         freq *10,   cal 1=run calibration (only when enable=0)

Usage:
    python serial_plot.py
    python serial_plot.py --port COM5 --baud 3000000
"""

import argparse
import collections
import queue
import struct
import threading
import time
import csv
import os
import sys
from datetime import datetime

import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore, QtGui

# ── Protocol constants ────────────────────────────────────────────────────────

TELEM_SYNC  = bytes([0xAA, 0x55])
TELEM_FMT   = ">hhhHHHH"        # ia, ib, ic (signed), vdc, theta, enc_angle, biss_error (unsigned)
TELEM_BYTES = 2 + struct.calcsize(TELEM_FMT)   # 16

CMD_SYNC    = bytes([0xBB, 0xCC])
CMD_FMT     = ">hhHBB"          # vd, vq (signed), freq (unsigned), enable, cal
CMD_BYTES   = 2 + struct.calcsize(CMD_FMT)      # 10

CURRENT_SCALE = 1 / 100
VDC_SCALE     = 1 / 100
THETA_SCALE   = 1 / 10000

# ── Shared data ───────────────────────────────────────────────────────────────

MAX_SAMPLES = 10_000

buffers = {
    "ia":        collections.deque([0.0]  * MAX_SAMPLES, maxlen=MAX_SAMPLES),
    "ib":        collections.deque([0.0]  * MAX_SAMPLES, maxlen=MAX_SAMPLES),
    "ic":        collections.deque([0.0]  * MAX_SAMPLES, maxlen=MAX_SAMPLES),
    "vdc":       collections.deque([24.0] * MAX_SAMPLES, maxlen=MAX_SAMPLES),
    "theta":     collections.deque([0.0]  * MAX_SAMPLES, maxlen=MAX_SAMPLES),
    "enc_angle": collections.deque([0.0]  * MAX_SAMPLES, maxlen=MAX_SAMPLES),
}
data_lock  = threading.Lock()
new_data   = threading.Event()
biss_error = {"value": 0}   # latest raw error code from DSP (0=OK, 16=parity, 32=CRC, 1-15=encoder/FPGA)
biss_lock  = threading.Lock()

cmd       = {"vd": 0.0, "vq": 0.0, "freq": 0.0, "enable": False}
cmd_lock  = threading.Lock()

enc_offset     = {"value": 0.0}   # radians, applied locally to enc_angle before plotting
enc_offset_lock = threading.Lock()

# ── Serial I/O ────────────────────────────────────────────────────────────────

ser = None
serial_error = {"msg": ""}   # set from serial_thread if connection fails

def send_command(cal=0):
    if ser is None:
        return
    with cmd_lock:
        vd16   = int(max(-32768, min(32767, cmd["vd"]   * 100)))
        vq16   = int(max(-32768, min(32767, cmd["vq"]   * 100)))
        freq16 = int(max(0,      min(65535, cmd["freq"] *  10)))
        en     = 1 if cmd["enable"] else 0
    frame = CMD_SYNC + struct.pack(CMD_FMT, vd16, vq16, freq16, en, cal)
    ser.write(frame)


def serial_thread(port, baud):
    global ser
    try:
        import serial
        ser = serial.Serial(port, baud, timeout=0.01)
    except Exception as e:
        serial_error["msg"] = str(e)
        new_data.set()   # wake the UI so it can show the error
        return

    buf = bytearray()
    while True:
        buf.extend(ser.read(ser.in_waiting or 1))

        # Scan for sync pattern and extract complete frames
        while len(buf) >= TELEM_BYTES:
            idx = buf.find(TELEM_SYNC)
            if idx == -1:
                buf = buf[-1:]   # keep last byte in case it's the first sync byte
                break
            if idx + TELEM_BYTES > len(buf):
                buf = buf[idx:]  # not enough bytes yet
                break

            frame = buf[idx : idx + TELEM_BYTES]
            buf   = buf[idx + TELEM_BYTES:]

            ia, ib, ic, vdc, theta, enc_angle, err = struct.unpack_from(TELEM_FMT, frame, 2)

            vals = (
                ia        * CURRENT_SCALE,
                ib        * CURRENT_SCALE,
                ic        * CURRENT_SCALE,
                vdc       * VDC_SCALE,
                theta     * THETA_SCALE,
                enc_angle * THETA_SCALE,
            )

            with data_lock:
                for key, val in zip(buffers, vals):
                    buffers[key].append(val)
            with biss_lock:
                biss_error["value"] = err
            new_data.set()

            if logger["writer"]:   # fast unsynchronised check — benign race
                log_queue.put_nowait([f"{time.time():.6f}"] + [f"{v:.5f}" for v in vals] + [str(err)])

# ── CSV logging ───────────────────────────────────────────────────────────────

LOG_DIR  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
log_lock   = threading.Lock()
logger     = {"file": None, "writer": None}
log_queue  = queue.Queue()   # serial thread enqueues rows; writer thread flushes to disk

def _log_writer_thread():
    while True:
        row = log_queue.get()
        if row is None:
            break
        with log_lock:
            if logger["writer"]:
                logger["writer"].writerow(row)

threading.Thread(target=_log_writer_thread, daemon=True).start()

def start_log():
    os.makedirs(LOG_DIR, exist_ok=True)
    path = os.path.join(LOG_DIR, datetime.now().strftime("log_%Y%m%d_%H%M%S.csv"))
    f = open(path, "w", newline="")
    w = csv.writer(f)
    w.writerow(["t_s", "Ia", "Ib", "Ic", "Vdc", "theta", "enc_angle", "biss_error"])
    with log_lock:
        logger["file"]   = f
        logger["writer"] = w
    return path

def stop_log():
    with log_lock:
        if logger["file"]:
            logger["file"].close()
        logger["file"]   = None
        logger["writer"] = None

# ── UI helpers ────────────────────────────────────────────────────────────────

pg.setConfigOptions(background="w", foreground="k", antialias=True)

COLORS = ["#e63946", "#2a9d8f", "#457b9d", "#e76f51", "#6a4c93", "#f4a261"]
MONO   = QtGui.QFont("Consolas", 9)
BOLD   = QtGui.QFont("Consolas", 9, QtGui.QFont.Bold)
SS_INPUT = ("QLineEdit{background:#fff;border:1px solid #d0d0d0;border-radius:3px;"
            "padding:2px 5px;font-family:Consolas;font-size:8pt;color:#1a1a1a;}"
            "QLineEdit:focus{border:1px solid #888;}")

def label(text):
    w = QtWidgets.QLabel(text)
    w.setFont(MONO)
    w.setStyleSheet("color:#888;")
    return w

def line_edit(value, width=58):
    w = QtWidgets.QLineEdit(str(value))
    w.setFixedWidth(width)
    w.setStyleSheet(SS_INPUT)
    return w


def make_plot(title, ylabel, curve_specs, y_min=0.0, y_max=10.0, auto_y=True, window=1000):
    """Build a PlotWidget with a control bar underneath. Returns (widget, curves, state)."""
    container = QtWidgets.QWidget()
    container.setStyleSheet("QWidget{background:#fff;border:1px solid #e0e0e0;border-radius:5px;}")
    layout = QtWidgets.QVBoxLayout(container)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    # Plot
    plot = pg.PlotWidget(background="w")
    plot.setTitle(title, color="#333", size="9pt")
    plot.setLabel("left", ylabel, **{"font-size": "9pt", "color": "#555"})
    plot.showGrid(x=True, y=True, alpha=0.4)
    for ax in (plot.getAxis("left"), plot.getAxis("bottom")):
        ax.setPen(pg.mkPen("#aaa", width=1))
        ax.setTextPen(pg.mkPen("#666"))
    plot.getViewBox().setMouseEnabled(x=False, y=True)
    plot.addLegend(offset=(5, 5), labelTextColor="#333").setLabelTextSize("8pt")
    layout.addWidget(plot)

    curves = [
        plot.plot(pen=pg.mkPen(COLORS[ci], width=1.5), name=name,
                  downsampleMethod="peak", autoDownsample=True, clipToView=True)
        for name, ci in curve_specs
    ]

    # Control bar
    bar = QtWidgets.QWidget()
    bar.setFixedHeight(28)
    bar.setStyleSheet("QWidget{background:#f8f8f8;border-top:1px solid #eee;}")
    row = QtWidgets.QHBoxLayout(bar)
    row.setContentsMargins(8, 2, 8, 2)
    row.setSpacing(8)

    chk   = QtWidgets.QCheckBox("Auto Y")
    chk.setFont(MONO)
    chk.setStyleSheet("color:#555;")
    chk.setChecked(auto_y)
    e_min = line_edit(y_min)
    e_max = line_edit(y_max)
    e_min.setEnabled(not auto_y)
    e_max.setEnabled(not auto_y)
    e_win = line_edit(window, width=62)

    for w in (chk, label("min"), e_min, label("max"), e_max):
        row.addWidget(w)
    row.addSpacing(12)
    row.addWidget(label("window"))
    row.addWidget(e_win)
    row.addStretch()
    layout.addWidget(bar)

    state = {"auto_y": auto_y, "y_min": y_min, "y_max": y_max, "window": window, "plot": plot}

    def apply_y():
        try:
            state["y_min"], state["y_max"] = float(e_min.text()), float(e_max.text())
            plot.setYRange(state["y_min"], state["y_max"], padding=0)
        except ValueError:
            pass

    def on_auto(checked):
        state["auto_y"] = bool(checked)
        e_min.setEnabled(not state["auto_y"])
        e_max.setEnabled(not state["auto_y"])
        if not state["auto_y"]:
            apply_y()

    def on_window():
        try:
            state["window"] = max(10, int(float(e_win.text())))
        except ValueError:
            pass

    chk.stateChanged.connect(on_auto)
    e_min.returnPressed.connect(apply_y)
    e_max.returnPressed.connect(apply_y)
    e_win.returnPressed.connect(on_window)

    return container, curves, state


def autoscale(plot, arrays, pad_abs=0.5):
    mn = min(a.min() for a in arrays)
    mx = max(a.max() for a in arrays)
    pad = max((mx - mn) * 0.15, pad_abs)
    plot.setYRange(mn - pad, mx + pad, padding=0)

# ── Main window ───────────────────────────────────────────────────────────────

def build_ui(port):
    app = QtWidgets.QApplication(sys.argv)
    app.setStyle("Fusion")

    win = QtWidgets.QWidget()
    win.setWindowTitle(f"Motor Plotter  [{port}]")
    win.resize(1100, 860)
    win.setStyleSheet("QWidget{background:#f5f5f5;}")

    root = QtWidgets.QVBoxLayout(win)
    root.setContentsMargins(10, 10, 10, 10)
    root.setSpacing(6)

    # Header
    hdr = QtWidgets.QHBoxLayout()
    title = QtWidgets.QLabel("OPEN-LOOP SPIN TEST")
    title.setFont(QtGui.QFont("Consolas", 12, QtGui.QFont.Bold))
    title.setStyleSheet("color:#1a1a1a;letter-spacing:2px;")
    port_lbl = QtWidgets.QLabel(port)
    port_lbl.setFont(MONO)
    port_lbl.setStyleSheet("color:#888;")
    hdr.addWidget(title)
    hdr.addStretch()
    hdr.addWidget(port_lbl)
    root.addLayout(hdr)

    # Plots
    blk_i, (c_ia, c_ib, c_ic), st_i = make_plot(
        "Phase Currents", "A", [("Ia", 0), ("Ib", 1), ("Ic", 2)],
        auto_y=False, y_min=-10.0, y_max=10.0)
    blk_v, (c_vdc,), st_v = make_plot(
        "DC Bus Voltage", "V", [("Vdc", 3)],
        auto_y=False, y_min=0.0, y_max=30.0)
    blk_t, (c_theta, c_enc), st_t = make_plot(
        "Electrical Angle", "rad", [("θ_sim", 4), ("θ_enc", 5)],
        auto_y=False, y_min=-0.3, y_max=6.6)

    splitter = QtWidgets.QSplitter(QtCore.Qt.Vertical)
    for blk in (blk_i, blk_v, blk_t):
        splitter.addWidget(blk)
    splitter.setSizes([280, 140, 140])
    splitter.setStyleSheet("QSplitter::handle{background:#e0e0e0;height:4px;}")
    root.addWidget(splitter, stretch=1)

    # Command bar
    cmd_frame = QtWidgets.QFrame()
    cmd_frame.setFixedHeight(46)
    cmd_frame.setStyleSheet("QFrame{background:#fff;border:1px solid #e0e0e0;border-radius:5px;}")
    cmd_row = QtWidgets.QHBoxLayout(cmd_frame)
    cmd_row.setContentsMargins(12, 6, 12, 6)
    cmd_row.setSpacing(8)

    ed_vd   = line_edit("0.0")
    ed_vq   = line_edit("0.0")
    ed_freq = line_edit("0.0")
    for lbl_text, ed in [("Vd (V)", ed_vd), ("Vq (V)", ed_vq), ("Freq (Hz)", ed_freq)]:
        cmd_row.addWidget(label(lbl_text))
        cmd_row.addWidget(ed)
    cmd_row.addSpacing(8)

    btn_apply = QtWidgets.QPushButton("APPLY")
    btn_apply.setFixedSize(65, 26)
    btn_apply.setFont(BOLD)
    btn_apply.setStyleSheet("QPushButton{background:#1a1a1a;color:#fff;border:none;border-radius:4px;}"
                            "QPushButton:hover{background:#333;}")

    btn_en = QtWidgets.QPushButton("ENABLE")
    btn_en.setCheckable(True)
    btn_en.setFixedSize(75, 26)
    btn_en.setFont(BOLD)
    btn_en.setStyleSheet("QPushButton{background:#ccc;color:#555;border:none;border-radius:4px;}"
                         "QPushButton:checked{background:#2dc653;color:#fff;}"
                         "QPushButton:hover{border:1px solid #aaa;}")

    btn_log = QtWidgets.QPushButton("LOG")
    btn_log.setCheckable(True)
    btn_log.setFixedSize(60, 26)
    btn_log.setFont(BOLD)
    btn_log.setStyleSheet("QPushButton{background:#ccc;color:#555;border:none;border-radius:4px;}"
                          "QPushButton:checked{background:#e63946;color:#fff;}"
                          "QPushButton:hover{border:1px solid #aaa;}")

    log_lbl = QtWidgets.QLabel("")
    log_lbl.setFont(MONO)
    log_lbl.setStyleSheet("color:#888;font-size:8pt;")

    btn_cal = QtWidgets.QPushButton("CAL CURRENTS")
    btn_cal.setFixedSize(110, 26)
    btn_cal.setFont(BOLD)
    btn_cal.setStyleSheet("QPushButton{background:#6a4c93;color:#fff;border:none;border-radius:4px;}"
                          "QPushButton:hover{background:#8a6cb3;}"
                          "QPushButton:disabled{background:#bbb;color:#888;}")

    ed_enc_offset = line_edit("0.0", width=62)
    btn_offset = QtWidgets.QPushButton("SET")
    btn_offset.setFixedSize(45, 26)
    btn_offset.setFont(BOLD)
    btn_offset.setStyleSheet("QPushButton{background:#457b9d;color:#fff;border:none;border-radius:4px;}"
                             "QPushButton:hover{background:#5a9abf;}")

    for w in (btn_apply, btn_en):
        cmd_row.addWidget(w)
    cmd_row.addSpacing(12)
    cmd_row.addWidget(btn_log)
    cmd_row.addWidget(log_lbl)
    cmd_row.addSpacing(12)
    cmd_row.addWidget(btn_cal)
    cmd_row.addSpacing(12)
    cmd_row.addWidget(label("Enc Offset (rad)"))
    cmd_row.addWidget(ed_enc_offset)
    cmd_row.addWidget(btn_offset)
    cmd_row.addSpacing(16)
    cmd_row.addWidget(label("BiSS:"))
    biss_lbl = QtWidgets.QLabel("OK")
    biss_lbl.setFont(BOLD)
    biss_lbl.setFixedWidth(90)
    biss_lbl.setAlignment(QtCore.Qt.AlignCenter)
    biss_lbl.setStyleSheet("background:#2dc653;color:#fff;border-radius:3px;padding:1px 4px;")
    cmd_row.addWidget(biss_lbl)
    cmd_row.addStretch()
    root.addWidget(cmd_frame)

    # ── Callbacks ─────────────────────────────────────────────────────────────

    def on_apply():
        try:
            with cmd_lock:
                cmd["vd"]   = float(ed_vd.text())
                cmd["vq"]   = float(ed_vq.text())
                cmd["freq"] = float(ed_freq.text())
            send_command()
        except ValueError:
            pass

    def on_enable(checked):
        with cmd_lock:
            cmd["enable"] = checked
        send_command()

    def on_log(checked):
        if checked:
            log_lbl.setText(os.path.basename(start_log()))
        else:
            stop_log()
            log_lbl.setText("")

    def on_set_offset():
        try:
            with enc_offset_lock:
                enc_offset["value"] = float(ed_enc_offset.text())
        except ValueError:
            pass

    def on_calibrate():
        send_command(cal=1)

    btn_apply.clicked.connect(on_apply)
    btn_en.toggled.connect(on_enable)
    btn_log.toggled.connect(on_log)
    btn_offset.clicked.connect(on_set_offset)
    btn_cal.clicked.connect(on_calibrate)
    ed_enc_offset.returnPressed.connect(on_set_offset)
    for ed in (ed_vd, ed_vq, ed_freq):
        ed.returnPressed.connect(on_apply)

    # ── Plot update timer ─────────────────────────────────────────────────────

    plot_groups = [
        (st_i, [c_ia, c_ib, c_ic],  ["ia", "ib", "ic"],          0.5),
        (st_v, [c_vdc],              ["vdc"],                      1.0),
        (st_t, [c_theta, c_enc],     ["theta", "enc_angle"],       0.3),
    ]

    def update():
        if not new_data.is_set():
            return
        new_data.clear()

        # Compute the maximum window needed per key so we only copy what we'll use
        needed: dict[str, int] = {}
        for st, _, keys, _ in plot_groups:
            w = min(st["window"], MAX_SAMPLES)
            for k in keys:
                if needed.get(k, 0) < w:
                    needed[k] = w

        with data_lock:
            snapshot = {k: np.array(list(buffers[k])[-w:]) for k, w in needed.items()}

        with enc_offset_lock:
            offset = enc_offset["value"]
        snapshot["enc_angle"] = (snapshot["enc_angle"] + offset) % (2 * np.pi)

        for st, curves, keys, pad in plot_groups:
            w      = min(st["window"], MAX_SAMPLES)
            slices = [snapshot[k][-w:] for k in keys]
            for curve, data in zip(curves, slices):
                curve.setData(data)   # x omitted — pyqtgraph uses integer indices
            if st["auto_y"]:
                autoscale(st["plot"], slices, pad)
            else:
                st["plot"].setYRange(st["y_min"], st["y_max"], padding=0)

        with biss_lock:
            err = biss_error["value"]
        if err == 0:
            biss_lbl.setText("OK")
            biss_lbl.setStyleSheet("background:#2dc653;color:#fff;border-radius:3px;padding:1px 4px;")
        elif err == 16:
            biss_lbl.setText("PARITY ERR")
            biss_lbl.setStyleSheet("background:#e63946;color:#fff;border-radius:3px;padding:1px 4px;")
        elif err == 32:
            biss_lbl.setText("CRC ERR")
            biss_lbl.setStyleSheet("background:#e76f51;color:#fff;border-radius:3px;padding:1px 4px;")
        else:
            biss_lbl.setText(f"ERR {err:#04x}")
            biss_lbl.setStyleSheet("background:#e63946;color:#fff;border-radius:3px;padding:1px 4px;")

    timer = QtCore.QTimer()
    timer.timeout.connect(update)
    timer.start(16)  # ~60 Hz
    win._timer = timer  # keep reference so GC doesn't collect and stop it

    new_data.set()  # trigger an initial render from the pre-filled zero buffers

    return app, win


# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM12")
    parser.add_argument("--baud", type=int, default=3_000_000)
    args = parser.parse_args()

    threading.Thread(target=serial_thread, args=(args.port, args.baud), daemon=True).start()

    app, win = build_ui(args.port)
    win.show()
    sys.exit(app.exec_())
