"""
serial_plot.py — Live serial plotter + command sender for open-loop spin test

RX format from DSP: "Ia,Ib,Ic,Vdc,theta\r\n"
TX format to  DSP: "vd,vq,freq,enable\r\n"

Usage:
    python serial_plot.py
    python serial_plot.py --port COM5 --baud 115200
"""

import sys, argparse, collections, threading, time
import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore, QtGui

parser = argparse.ArgumentParser()
parser.add_argument("--port", default="COM12")
parser.add_argument("--baud", type=int, default=3_000_000)
args = parser.parse_args()

MAX_SAMPLES = 5000

ia    = collections.deque([0.0]  * MAX_SAMPLES, maxlen=MAX_SAMPLES)
ib    = collections.deque([0.0]  * MAX_SAMPLES, maxlen=MAX_SAMPLES)
ic    = collections.deque([0.0]  * MAX_SAMPLES, maxlen=MAX_SAMPLES)
vdc   = collections.deque([24.0] * MAX_SAMPLES, maxlen=MAX_SAMPLES)
theta = collections.deque([0.0]  * MAX_SAMPLES, maxlen=MAX_SAMPLES)
data_lock = threading.Lock()

cmd = {"vd": 0.0, "vq": 0.0, "freq": 0.0, "enable": False}
cmd_lock = threading.Lock()
ser = None

def send_command():
    if ser is None: return
    with cmd_lock:
        en = 1 if cmd["enable"] else 0
        line = f"{cmd['vd']:.3f},{cmd['vq']:.3f},{cmd['freq']:.3f},{en}\r\n"
    ser.write(line.encode("ascii"))

def serial_thread():
    global ser
    import serial
    ser = serial.Serial(args.port, args.baud, timeout=0.0)
    buf = b""
    while True:
        buf += ser.read(ser.in_waiting or 1)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            parts = line.decode("ascii", errors="replace").strip().split(",")
            if len(parts) == 5:
                try:
                    with data_lock:
                        ia.append(float(parts[0])); ib.append(float(parts[1]))
                        ic.append(float(parts[2])); vdc.append(float(parts[3]))
                        theta.append(float(parts[4]))
                except ValueError: pass
        time.sleep(0.001)

pg.setConfigOption("background", "w")
pg.setConfigOption("foreground", "k")
pg.setConfigOption("antialias", True)

app = QtWidgets.QApplication(sys.argv)
app.setStyle("Fusion")

MONO   = QtGui.QFont("Consolas", 9)
BOLD   = QtGui.QFont("Consolas", 9, QtGui.QFont.Bold)
COLORS = ["#e63946", "#2a9d8f", "#457b9d", "#e76f51", "#6a4c93"]

INPUT_SS = """QLineEdit {
    background:#fff; border:1px solid #d0d0d0; border-radius:3px;
    padding:2px 5px; font-family:Consolas; font-size:8pt; color:#1a1a1a;
} QLineEdit:focus { border:1px solid #888; }"""

def small_lbl(txt):
    l = QtWidgets.QLabel(txt); l.setFont(MONO); l.setStyleSheet("color:#888;"); return l

def inp(val, w=58):
    e = QtWidgets.QLineEdit(str(val)); e.setFixedWidth(w); e.setStyleSheet(INPUT_SS); return e

win = QtWidgets.QWidget()
win.setWindowTitle(f"Motor Plotter [{args.port}]")
win.resize(1100, 860)
win.setStyleSheet("QWidget { background:#f5f5f5; }")

root = QtWidgets.QVBoxLayout(win)
root.setContentsMargins(10, 10, 10, 10)
root.setSpacing(6)

hdr = QtWidgets.QHBoxLayout()
tl = QtWidgets.QLabel("OPEN-LOOP SPIN TEST")
tl.setFont(QtGui.QFont("Consolas", 12, QtGui.QFont.Bold))
tl.setStyleSheet("color:#1a1a1a; letter-spacing:2px;")
ml = QtWidgets.QLabel(args.port); ml.setFont(MONO); ml.setStyleSheet("color:#888;")
hdr.addWidget(tl); hdr.addStretch(); hdr.addWidget(ml)
root.addLayout(hdr)

def make_plot_block(title, ylabel, curves_spec, auto_y=True, y_min=0.0, y_max=10.0, win_default=1000):
    outer = QtWidgets.QWidget()
    outer.setStyleSheet("QWidget{background:#fff;border:1px solid #e0e0e0;border-radius:5px;}")
    vbox = QtWidgets.QVBoxLayout(outer)
    vbox.setContentsMargins(0, 0, 0, 0); vbox.setSpacing(0)

    pw = pg.PlotWidget(background="w")
    pw.setTitle(title, color="#333", size="9pt")
    pw.setLabel("left", ylabel, **{"font-size":"9pt","color":"#555"})
    pw.showGrid(x=True, y=True, alpha=0.4)
    for ax in [pw.getAxis("left"), pw.getAxis("bottom")]:
        ax.setPen(pg.mkPen("#aaa", width=1)); ax.setTextPen(pg.mkPen("#666"))
    pw.getViewBox().setMouseEnabled(x=False, y=True)
    pw.addLegend(offset=(5,5), labelTextColor="#333").setLabelTextSize("8pt")
    vbox.addWidget(pw)

    curves = [pw.plot(pen=pg.mkPen(COLORS[ci], width=1.5), name=name,
                      downsampleMethod="peak", autoDownsample=True, clipToView=True)
              for name, ci in curves_spec]

    bar = QtWidgets.QWidget(); bar.setFixedHeight(28)
    bar.setStyleSheet("QWidget{background:#f8f8f8;border-top:1px solid #eee;}")
    brow = QtWidgets.QHBoxLayout(bar)
    brow.setContentsMargins(8,2,8,2); brow.setSpacing(8)

    chk = QtWidgets.QCheckBox("Auto Y"); chk.setFont(MONO)
    chk.setStyleSheet("color:#555;"); chk.setChecked(auto_y)
    e_min = inp(y_min); e_max = inp(y_max)
    e_min.setEnabled(not auto_y); e_max.setEnabled(not auto_y)
    e_win = inp(win_default, 62)

    brow.addWidget(chk)
    brow.addWidget(small_lbl("min")); brow.addWidget(e_min)
    brow.addWidget(small_lbl("max")); brow.addWidget(e_max)
    brow.addSpacing(12)
    brow.addWidget(small_lbl("window")); brow.addWidget(e_win)
    brow.addStretch()
    vbox.addWidget(bar)

    state = {"auto_y": auto_y, "y_min": y_min, "y_max": y_max, "window": win_default, "plot": pw}

    def apply_y():
        try:
            mn, mx = float(e_min.text()), float(e_max.text())
            state["y_min"] = mn; state["y_max"] = mx; pw.setYRange(mn, mx, padding=0)
        except ValueError: pass

    def on_auto(s):
        a = bool(s); state["auto_y"] = a
        e_min.setEnabled(not a); e_max.setEnabled(not a)
        if not a: apply_y()

    def on_window():
        try: state["window"] = max(10, int(float(e_win.text())))
        except ValueError: pass

    chk.stateChanged.connect(on_auto)
    e_min.returnPressed.connect(apply_y); e_max.returnPressed.connect(apply_y)
    e_win.returnPressed.connect(on_window)
    return outer, curves, state

blk_i, (c_ia,c_ib,c_ic), st_i = make_plot_block("Phase Currents",   "A",   [("Ia",0),("Ib",1),("Ic",2)], auto_y=False, y_min=-10.0, y_max=10.0)
blk_v, (c_vdc,),          st_v = make_plot_block("DC Bus Voltage",   "V",   [("Vdc",3)],                  auto_y=False, y_min=0.0,   y_max=30.0)
blk_t, (c_theta,),        st_t = make_plot_block("Electrical Angle", "rad", [("θ",4)],                    auto_y=False, y_min=-0.3,  y_max=6.6)

splitter = QtWidgets.QSplitter(QtCore.Qt.Vertical)
for blk in [blk_i, blk_v, blk_t]: splitter.addWidget(blk)
splitter.setSizes([280, 140, 140])
splitter.setStyleSheet("QSplitter::handle{background:#e0e0e0;height:4px;}")
root.addWidget(splitter, stretch=1)

cmd_frame = QtWidgets.QFrame()
cmd_frame.setFixedHeight(46)
cmd_frame.setStyleSheet("QFrame{background:#fff;border:1px solid #e0e0e0;border-radius:5px;}")
cmd_row = QtWidgets.QHBoxLayout(cmd_frame)
cmd_row.setContentsMargins(12,6,12,6); cmd_row.setSpacing(8)

ed_vd = inp("0.0"); ed_vq = inp("0.0"); ed_freq = inp("0.0")
for lbl, ed in [("Vd (V)",ed_vd),("Vq (V)",ed_vq),("Freq (Hz)",ed_freq)]:
    cmd_row.addWidget(small_lbl(lbl)); cmd_row.addWidget(ed)
cmd_row.addSpacing(8)

btn_apply = QtWidgets.QPushButton("APPLY")
btn_apply.setFixedSize(65,26); btn_apply.setFont(BOLD)
btn_apply.setStyleSheet("QPushButton{background:#1a1a1a;color:#fff;border:none;border-radius:4px;}"
                        "QPushButton:hover{background:#333;}")

btn_en = QtWidgets.QPushButton("ENABLE")
btn_en.setCheckable(True); btn_en.setFixedSize(75,26); btn_en.setFont(BOLD)
btn_en.setStyleSheet("QPushButton{background:#ccc;color:#555;border:none;border-radius:4px;}"
                     "QPushButton:checked{background:#2dc653;color:#fff;}"
                     "QPushButton:hover{border:1px solid #aaa;}")

cmd_row.addWidget(btn_apply); cmd_row.addWidget(btn_en); cmd_row.addStretch()
root.addWidget(cmd_frame)

def on_apply():
    try:
        with cmd_lock:
            cmd["vd"] = float(ed_vd.text())
            cmd["vq"] = float(ed_vq.text())
            cmd["freq"] = float(ed_freq.text())
    except ValueError: pass
    send_command()

def on_enable(checked):
    with cmd_lock: cmd["enable"] = checked
    send_command()

btn_apply.clicked.connect(on_apply)
btn_en.toggled.connect(on_enable)
for ed in [ed_vd, ed_vq, ed_freq]: ed.returnPressed.connect(on_apply)

def autoscale(plot, arrays, pad_abs=0.5):
    all_d = np.concatenate(arrays); mn, mx = all_d.min(), all_d.max()
    pad = max((mx - mn) * 0.15, pad_abs)
    plot.setYRange(mn - pad, mx + pad, padding=0)

def update():
    with data_lock:
        r_ia=np.array(ia); r_ib=np.array(ib); r_ic=np.array(ic)
        r_v=np.array(vdc); r_th=np.array(theta)

    for st, curves, arrays, pad in [
        (st_i, [c_ia,c_ib,c_ic], [r_ia,r_ib,r_ic], 0.5),
        (st_v, [c_vdc],          [r_v],             1.0),
        (st_t, [c_theta],        [r_th],            0.3),
    ]:
        w = min(st["window"], MAX_SAMPLES)
        sliced = [a[-w:] for a in arrays]
        xs = np.arange(len(sliced[0]))
        for c, s in zip(curves, sliced): c.setData(xs, s)
        if st["auto_y"]: autoscale(st["plot"], sliced, pad)
        else: st["plot"].setYRange(st["y_min"], st["y_max"], padding=0)

timer = QtCore.QTimer()
timer.timeout.connect(update)
timer.start(50)

threading.Thread(target=serial_thread, daemon=True).start()
win.show()
sys.exit(app.exec_())