"""
view_log.py — Offline viewer for CSV logs produced by serial_plot.py

Usage:
    python view_log.py                        # opens file picker
    python view_log.py logs/log_20260320.csv  # load directly
"""

import sys, os, argparse
import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore, QtGui

parser = argparse.ArgumentParser()
parser.add_argument("file", nargs="?", default=None)
args = parser.parse_args()

# ── load file ──────────────────────────────────────────────────────────────
app = QtWidgets.QApplication(sys.argv)
app.setStyle("Fusion")

csv_path = args.file
if not csv_path:
    csv_path, _ = QtWidgets.QFileDialog.getOpenFileName(
        None, "Open log file",
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs"),
        "CSV files (*.csv)")
    if not csv_path:
        sys.exit(0)

data = np.genfromtxt(csv_path, delimiter=",", skip_header=1)
if data.ndim == 1:
    data = data[np.newaxis, :]

t     = data[:, 0] - data[0, 0]   # relative time (s)
r_ia  = data[:, 1]
r_ib  = data[:, 2]
r_ic  = data[:, 3]
r_vdc = data[:, 4]
r_th  = data[:, 5]
N     = len(t)

# ── style ──────────────────────────────────────────────────────────────────
pg.setConfigOption("background", "w")
pg.setConfigOption("foreground", "k")
pg.setConfigOption("antialias", True)

MONO   = QtGui.QFont("Consolas", 9)
BOLD   = QtGui.QFont("Consolas", 9, QtGui.QFont.Bold)
COLORS = ["#e63946", "#2a9d8f", "#457b9d", "#e76f51", "#6a4c93"]

INPUT_SS = """QLineEdit {
    background:#fff; border:1px solid #d0d0d0; border-radius:3px;
    padding:2px 5px; font-family:Consolas; font-size:8pt; color:#1a1a1a;
} QLineEdit:focus { border:1px solid #888; }"""

def small_lbl(txt):
    l = QtWidgets.QLabel(txt); l.setFont(MONO)
    l.setStyleSheet("color:#888;"); return l

def inp(val, w=58):
    e = QtWidgets.QLineEdit(str(val)); e.setFixedWidth(w)
    e.setStyleSheet(INPUT_SS); return e

# ── window ─────────────────────────────────────────────────────────────────
win = QtWidgets.QWidget()
win.setWindowTitle(f"Log Viewer — {os.path.basename(csv_path)}")
win.resize(1100, 900)
win.setStyleSheet("QWidget { background:#f5f5f5; }")

root = QtWidgets.QVBoxLayout(win)
root.setContentsMargins(10, 10, 10, 10)
root.setSpacing(6)

# header
hdr = QtWidgets.QHBoxLayout()
tl = QtWidgets.QLabel("LOG VIEWER")
tl.setFont(QtGui.QFont("Consolas", 12, QtGui.QFont.Bold))
tl.setStyleSheet("color:#1a1a1a; letter-spacing:2px;")
ml = QtWidgets.QLabel(os.path.basename(csv_path)); ml.setFont(MONO)
ml.setStyleSheet("color:#888;")
dur_lbl = QtWidgets.QLabel(f"{t[-1]:.2f} s  |  {N} samples"); dur_lbl.setFont(MONO)
dur_lbl.setStyleSheet("color:#888;")
hdr.addWidget(tl); hdr.addStretch(); hdr.addWidget(dur_lbl)
hdr.addSpacing(16); hdr.addWidget(ml)
root.addLayout(hdr)

# ── plot helper ────────────────────────────────────────────────────────────
def make_plot(title, ylabel, curves_spec):
    outer = QtWidgets.QWidget()
    outer.setStyleSheet("QWidget{background:#fff;border:1px solid #e0e0e0;border-radius:5px;}")
    vbox = QtWidgets.QVBoxLayout(outer)
    vbox.setContentsMargins(0, 0, 0, 0); vbox.setSpacing(0)

    pw = pg.PlotWidget(background="w")
    pw.setTitle(title, color="#333", size="9pt")
    pw.setLabel("left", ylabel, **{"font-size": "9pt", "color": "#555"})
    pw.setLabel("bottom", "Time (s)", **{"font-size": "9pt", "color": "#555"})
    pw.showGrid(x=True, y=True, alpha=0.4)
    for ax in [pw.getAxis("left"), pw.getAxis("bottom")]:
        ax.setPen(pg.mkPen("#aaa", width=1)); ax.setTextPen(pg.mkPen("#666"))
    pw.addLegend(offset=(5, 5), labelTextColor="#333").setLabelTextSize("8pt")

    curves = [pw.plot(pen=pg.mkPen(COLORS[ci], width=1.5), name=name,
                      downsampleMethod="peak", autoDownsample=True, clipToView=True)
              for name, ci in curves_spec]
    vbox.addWidget(pw)
    return outer, pw, curves

blk_i, pw_i, (c_ia, c_ib, c_ic) = make_plot("Phase Currents",   "A",   [("Ia",0),("Ib",1),("Ic",2)])
blk_v, pw_v, (c_vdc,)            = make_plot("DC Bus Voltage",   "V",   [("Vdc",3)])
blk_t, pw_t, (c_theta,)          = make_plot("Electrical Angle", "rad", [("θ",4)])

# link x axes
pw_v.setXLink(pw_i)
pw_t.setXLink(pw_i)

splitter = QtWidgets.QSplitter(QtCore.Qt.Vertical)
for blk in [blk_i, blk_v, blk_t]: splitter.addWidget(blk)
splitter.setSizes([280, 140, 140])
splitter.setStyleSheet("QSplitter::handle{background:#e0e0e0;height:4px;}")
root.addWidget(splitter, stretch=1)

# ── range slider bar ───────────────────────────────────────────────────────
rng_frame = QtWidgets.QFrame()
rng_frame.setFixedHeight(46)
rng_frame.setStyleSheet("QFrame{background:#fff;border:1px solid #e0e0e0;border-radius:5px;}")
rng_row = QtWidgets.QHBoxLayout(rng_frame)
rng_row.setContentsMargins(12, 6, 12, 6); rng_row.setSpacing(8)

e_t0 = inp("0.0", 70); e_t1 = inp(f"{t[-1]:.3f}", 70)
btn_full = QtWidgets.QPushButton("FULL"); btn_full.setFixedSize(55, 26); btn_full.setFont(BOLD)
btn_full.setStyleSheet("QPushButton{background:#1a1a1a;color:#fff;border:none;border-radius:4px;}"
                       "QPushButton:hover{background:#333;}")

rng_row.addWidget(small_lbl("t_start (s)")); rng_row.addWidget(e_t0)
rng_row.addWidget(small_lbl("t_end (s)"));   rng_row.addWidget(e_t1)
rng_row.addSpacing(8); rng_row.addWidget(btn_full)
rng_row.addStretch()
root.addWidget(rng_frame)

# ── populate plots ─────────────────────────────────────────────────────────
def plot_range(t0, t1):
    mask = (t >= t0) & (t <= t1)
    ts = t[mask]
    for c, arr in [(c_ia, r_ia), (c_ib, r_ib), (c_ic, r_ic)]:
        c.setData(ts, arr[mask])
    c_vdc.setData(ts, r_vdc[mask])
    c_theta.setData(ts, r_th[mask])
    pw_i.setXRange(t0, t1, padding=0)

def apply_range():
    try:
        t0 = float(e_t0.text()); t1 = float(e_t1.text())
        plot_range(max(t0, 0.0), min(t1, t[-1]))
    except ValueError: pass

def full_range():
    e_t0.setText("0.0"); e_t1.setText(f"{t[-1]:.3f}")
    plot_range(0.0, t[-1])

e_t0.returnPressed.connect(apply_range)
e_t1.returnPressed.connect(apply_range)
btn_full.clicked.connect(full_range)

# initial draw
plot_range(0.0, t[-1])

win.show()
sys.exit(app.exec_())
