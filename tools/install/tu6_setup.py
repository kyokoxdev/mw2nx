"""Sets up Modern Warfare 2 for mw2nx from your own disc: finds the ISO, then downloads title update 6 and lays out
both in one folder (default work/load_tu6/): default_mp.xex and default.xex (the campaign) from the disc, and the
update's files beside them.

Nothing is downloaded until the disc checks out, and a TU6 package already in iso/ or the output folder is used as is.

usage: python tools/install/tu6_setup.py
"""
import os
import queue
import sys
import threading
import tkinter as tk
import urllib.request
from tkinter import filedialog, ttk

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path[:0] = [HERE, os.path.join(ROOT, "tools", "inspect")]
import xiso  # noqa: E402
from stfs_extract import Stfs  # noqa: E402
from mw2nx_setup import wrong_xex  # noqa: E402

TITLE_ID = 0x41560817
TU_NAME = "TU_10LC20N_0000018000000.00000000001GA"
# The CoD Xenon archive at a fixed commit, as mw2-recompiled's launcher uses, so the file cannot change.
TU_URL = ("https://raw.githubusercontent.com/codxenon/xbox360-title-updates/25de2389ce214dae6159fcd45a9581cb3b640bd9/"
          "games/Call%20of%20Duty%20-%20Modern%20Warfare%202%20%28USA%2C%20Europe%29/TU6/" + TU_NAME)
TU_FILES = ("default_mp.xexp", "default.xexp", "patch_mp.ff", "dlc1_ui_mp.ff", "dlc2_ui_mp.ff")
ISO_DIR = os.path.join(ROOT, "iso")
DEFAULT_OUT = os.path.join(ROOT, "work", "load_tu6")


def find_iso():
    if os.path.isdir(ISO_DIR):
        for name in sorted(os.listdir(ISO_DIR)):
            if name.lower().endswith(".iso"):
                return os.path.join(ISO_DIR, name)
    return ""


def open_update(path):
    """The package's Stfs and its entries by name, or raises ValueError when it is not MW2's title update 6."""
    s = Stfs(path)
    if s.title_id != TITLE_ID:
        raise ValueError(f"{os.path.basename(path)} is for another game (title {s.title_id:08X})")
    entries = {e["name"].lower(): e for e in s.entries() if not e["dir"]}
    missing = [n for n in TU_FILES if n not in entries]
    if missing:
        raise ValueError(f"{os.path.basename(path)} is not title update 6: it has no {', '.join(missing)}")
    return s, entries


def download(dst, log):
    part = dst + ".part"
    with urllib.request.urlopen(TU_URL, timeout=60) as r, open(part, "wb") as f:
        total, done = int(r.headers.get("Content-Length") or 0), 0
        while chunk := r.read(1 << 16):
            f.write(chunk)
            done += len(chunk)
            log(f"  {done >> 10} KB" + (f" of {total >> 10} KB" if total else ""), replace=True)
    os.replace(part, dst)


def run(iso, out, log):
    log(f"Checking the disc: {iso}")
    try:
        with xiso.Xiso(iso) as x:
            if "default_mp.xex" not in {name.lower() for name, _, _, _ in x.root()}:
                raise ValueError("there is no default_mp.xex on it: it is not a Modern Warfare 2 disc")
            wrong = wrong_xex(x)
            if wrong:
                raise ValueError(f"{' and '.join(wrong)} on it are not the version Title Update 6 patches")
    except (xiso.XisoError, OSError) as e:
        raise ValueError(f"cannot read the disc: {e}") from e

    os.makedirs(out, exist_ok=True)
    tu = next((p for p in (os.path.join(ISO_DIR, TU_NAME), os.path.join(out, TU_NAME)) if os.path.isfile(p)), None)
    if tu:
        log(f"Using title update 6 already here: {tu}")
    else:
        tu = os.path.join(out, TU_NAME)
        log("Downloading title update 6 (1.6 MB)")
        log("")
        download(tu, log)
    s, entries = open_update(tu)

    log("Copying default_mp.xex and default.xex from the disc")
    with xiso.Xiso(iso) as x:
        x.extract(out, skip=lambda rel: rel.lower() not in ("default_mp.xex", "default.xex"))
    for e in entries.values():
        log(f"Writing {e['name']}")
        with open(os.path.join(out, e["name"]), "wb") as f:
            f.write(s.read(e))
    log(f"Done: {out}")


class App:
    def __init__(self, root):
        self.root, self.events = root, queue.Queue()
        root.title("mw2nx: game files and title update 6")
        frame = ttk.Frame(root, padding=10)
        frame.pack(fill="both", expand=True)
        self.iso, self.out = tk.StringVar(value=find_iso()), tk.StringVar(value=DEFAULT_OUT)
        for row, (label, var, pick) in enumerate((
                ("Disc ISO", self.iso, lambda: filedialog.askopenfilename(filetypes=[("Disc image", "*.iso"), ("All", "*")])),
                ("Output folder", self.out, filedialog.askdirectory))):
            ttk.Label(frame, text=label).grid(row=row, column=0, sticky="w")
            ttk.Entry(frame, textvariable=var, width=70).grid(row=row, column=1, sticky="ew", padx=5)
            ttk.Button(frame, text="Browse", command=lambda v=var, p=pick: v.set(p() or v.get())).grid(row=row, column=2)
        self.button = ttk.Button(frame, text="Set up", command=self.start)
        self.button.grid(row=2, column=0, columnspan=3, pady=8)
        self.text = tk.Text(frame, height=16, width=90, state="disabled")
        self.text.grid(row=3, column=0, columnspan=3, sticky="nsew")
        frame.columnconfigure(1, weight=1)
        frame.rowconfigure(3, weight=1)
        if not self.iso.get():
            self.write(f"No ISO found in {ISO_DIR}: put your disc image there or choose it with Browse.")
        root.after(100, self.poll)

    def write(self, line, replace=False):
        self.text.configure(state="normal")
        if replace:
            self.text.delete("end-2l linestart", "end-1c")
        self.text.insert("end", line + "\n")
        self.text.see("end")
        self.text.configure(state="disabled")

    def start(self):
        iso, out = self.iso.get().strip(), self.out.get().strip()
        if not os.path.isfile(iso):
            self.write("Choose your disc ISO first: nothing is downloaded without it.")
            return
        self.button.configure(state="disabled")
        log = lambda line, replace=False: self.events.put((line, replace))  # noqa: E731

        def work():
            try:
                run(iso, out, log)
            except Exception as e:  # shown to the player, not raised: the window stays usable
                log(f"Stopped: {e}")
            self.events.put(None)
        threading.Thread(target=work, daemon=True).start()

    def poll(self):
        while not self.events.empty():
            event = self.events.get()
            if event is None:
                self.button.configure(state="normal")
            else:
                self.write(*event)
        self.root.after(100, self.poll)


if __name__ == "__main__":
    App(tk.Tk()).root.mainloop()
