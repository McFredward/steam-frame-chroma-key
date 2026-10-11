"""MR Chroma control panel (PC side).

Sends key settings to the Steam Frame over the existing `ssh frame` alias,
where ~/mr-chroma/mrkey.sh regenerates the compositor shader and hot-reloads
it.  No network service is opened; nothing runs on the PC besides this window.
"""
import ctypes
import json
import os
import queue
import subprocess
import threading
import time
import tkinter as tk
from tkinter import colorchooser, ttk

HOST = "frame"
SETTINGS = os.path.join(os.environ.get("APPDATA", "."), "mrchroma_panel.json")
DEFAULTS = {"mode": "chroma", "rgb": [0, 255, 0], "threshold": 0.32, "softness": 0.12, "spill": 0.5}

try:  # physical pixel coordinates for the eyedropper on scaled displays
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass


def screen_pixel(x, y):
    hdc = ctypes.windll.user32.GetDC(0)
    c = ctypes.windll.gdi32.GetPixel(hdc, x, y)
    ctypes.windll.user32.ReleaseDC(0, hdc)
    return [c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF]


class Panel:
    def __init__(self, root):
        self.root = root
        root.title("MR Chroma Key")
        root.resizable(False, False)
        self.s = dict(DEFAULTS)
        try:
            self.s.update(json.load(open(SETTINGS)))
        except Exception:
            pass
        self.jobs = queue.Queue()
        self.pending = None
        threading.Thread(target=self.worker, daemon=True).start()

        f = ttk.Frame(root, padding=12)
        f.grid()
        self.mode = tk.StringVar(value=self.s["mode"])
        mf = ttk.Frame(f)
        mf.grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 8))
        for i, (lbl, val) in enumerate([("Off", "off"), ("Chroma key", "chroma"), ("Rect test", "rect")]):
            ttk.Radiobutton(mf, text=lbl, value=val, variable=self.mode, command=self.changed).grid(row=0, column=i, padx=4)

        ttk.Label(f, text="Key colour").grid(row=1, column=0, sticky="w")
        self.swatch = tk.Label(f, width=6, relief="sunken")
        self.swatch.grid(row=1, column=1, sticky="w", padx=6)
        bf = ttk.Frame(f)
        bf.grid(row=1, column=2, sticky="e")
        ttk.Button(bf, text="Eyedropper", command=self.eyedropper).grid(row=0, column=0, padx=2)
        ttk.Button(bf, text="Pick…", command=self.pick).grid(row=0, column=1, padx=2)
        self.rgb_lbl = ttk.Label(f, text="")
        self.rgb_lbl.grid(row=2, column=1, columnspan=2, sticky="w", padx=6)

        self.vars = {}
        rows = [("threshold", "Similarity", 0.02, 0.70), ("softness", "Edge softness", 0.005, 0.30),
                ("spill", "Spill removal", 0.0, 1.0)]
        for i, (k, lbl, lo, hi) in enumerate(rows):
            ttk.Label(f, text=lbl).grid(row=3 + i, column=0, sticky="w", pady=4)
            v = tk.DoubleVar(value=self.s[k])
            self.vars[k] = v
            sc = ttk.Scale(f, from_=lo, to=hi, variable=v, length=240, command=lambda _=None: self.changed())
            sc.grid(row=3 + i, column=1, columnspan=2, sticky="we")
            val = ttk.Label(f, width=6)
            val.grid(row=3 + i, column=3)
            v.trace_add("write", lambda *_, v=v, val=val: val.config(text=f"{v.get():.3f}"))
            val.config(text=f"{v.get():.3f}")

        self.status = ttk.Label(f, text="ready", foreground="#555", wraplength=360)
        self.status.grid(row=7, column=0, columnspan=4, sticky="w", pady=(10, 0))
        self.refresh_swatch()

    # ---------------------------------------------------------------- ui
    def refresh_swatch(self):
        r, g, b = self.s["rgb"]
        self.swatch.config(bg=f"#{r:02x}{g:02x}{b:02x}")
        self.rgb_lbl.config(text=f"RGB {r}, {g}, {b}")

    def pick(self):
        r, g, b = self.s["rgb"]
        res = colorchooser.askcolor(color=f"#{r:02x}{g:02x}{b:02x}", parent=self.root)
        if res and res[0]:
            self.s["rgb"] = [int(x) for x in res[0]]
            self.refresh_swatch()
            self.changed()

    def eyedropper(self):
        """Full-screen, almost invisible window; click anywhere to sample that pixel."""
        top = tk.Toplevel(self.root)
        top.attributes("-fullscreen", True)
        top.attributes("-alpha", 0.01)
        top.attributes("-topmost", True)
        top.config(cursor="crosshair")
        self.status.config(text="Eyedropper: click the background colour in your game's window (Esc cancels)")

        def done(e):
            x, y = top.winfo_pointerx(), top.winfo_pointery()
            top.destroy()
            self.root.update()
            time.sleep(0.08)
            self.s["rgb"] = screen_pixel(x, y)
            self.refresh_swatch()
            self.changed()

        top.bind("<Button-1>", done)
        top.bind("<Escape>", lambda e: (top.destroy(), self.status.config(text="cancelled")))
        top.focus_force()

    def changed(self):
        for k, v in self.vars.items():
            self.s[k] = round(float(v.get()), 4)
        self.s["mode"] = self.mode.get()
        if self.pending:
            self.root.after_cancel(self.pending)
        self.pending = self.root.after(350, self.apply)  # debounce slider drags

    def apply(self):
        self.pending = None
        try:
            json.dump(self.s, open(SETTINGS, "w"))
        except Exception:
            pass
        r, g, b = (c / 255.0 for c in self.s["rgb"])
        m = self.s["mode"]
        if m == "chroma":
            args = f"chroma {r:.4f} {g:.4f} {b:.4f} {self.s['threshold']} {self.s['softness']} {self.s['spill']}"
        elif m == "rect":
            args = "rect 0.3 0.3 0.7 0.7"
        else:
            args = "off"
        self.jobs.put(args)
        self.status.config(text=f"applying: {args}")

    # ---------------------------------------------------------------- ssh worker
    def worker(self):
        while True:
            args = self.jobs.get()
            while not self.jobs.empty():  # only the newest setting matters
                args = self.jobs.get()
            t0 = time.time()
            try:
                p = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", HOST,
                                    f"~/mr-chroma/mrkey.sh {args}"],
                                   capture_output=True, text=True, timeout=30,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                ok = p.returncode == 0
                msg = (p.stdout.strip().splitlines() or [""])[-1] if ok else (p.stderr.strip() or p.stdout.strip())[-200:]
            except Exception as e:
                ok, msg = False, str(e)
            dt = time.time() - t0
            text = f"{'OK' if ok else 'ERROR'} ({dt:.1f}s): {msg}"
            self.root.after(0, lambda t=text, ok=ok: self.status.config(text=t, foreground="#060" if ok else "#a00"))


if __name__ == "__main__":
    root = tk.Tk()
    Panel(root)
    root.mainloop()
