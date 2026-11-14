"""Assembles the SD-card folder for mw2mp from your own disc and title update 6:

  <out>/switch/mw2mp/mw2mp.nro   (mw2mp/build_switch.sh)
  <out>/switch/mw2sp/mw2sp.nro   the campaign (mw2sp/build_switch.sh) in its own folder: hbmenu shows one NRO per
                                 folder. With no game/ beside it, it uses sdmc:/switch/mw2mp's folders
                                 (mw2mp/src/mw2mp_app.h); mw2sp_shaders.spvpack/.spvidx go beside mw2sp.nro.
  <out>/switch/<p>/pipelines/    <p>/pipelines/*.bin, the per-level pipeline lists, if the repo has them
  <out>/switch/mw2mp/game/       the disc's files ($SystemUpdate left out) + the update's .xexp
  <out>/switch/mw2mp/update/     the rest of title update 6 (patch_mp.ff, dlc1_ui_mp.ff, dlc2_ui_mp.ff)

Copy <out>/switch to the root of the SD card. The TU6 files come from work/load_tu6 (tools/install/tu6_setup.py).
Disc files already there with the right size are kept, so a second run only refreshes the NRO and the update.

usage: python3 tools/stage_sd_mw2mp.py [--iso disc.iso] [--out DIR]   (defaults: the ISO in iso/, sd/ in the data folder)
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "install"))
import xiso  # noqa: E402

TU6 = os.path.join(ROOT, "work", "load_tu6")
TU_FILES = ("patch_mp.ff", "dlc1_ui_mp.ff", "dlc2_ui_mp.ff")
NROS = [os.path.join(ROOT, p, "out", "switch", p + ".nro") for p in ("mw2mp", "mw2sp")]


def main():
    data = subprocess.run(["bash", os.path.join(ROOT, "tools", "data_dir.sh")], capture_output=True, text=True,
                          check=True).stdout.strip()
    isos = sorted(f for f in os.listdir(os.path.join(ROOT, "iso")) if f.lower().endswith(".iso")) \
        if os.path.isdir(os.path.join(ROOT, "iso")) else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=os.path.join(ROOT, "iso", isos[0]) if isos else None)
    ap.add_argument("--out", default=os.path.join(data, "sd"))
    a = ap.parse_args()

    if not a.iso or not os.path.isfile(a.iso):
        sys.exit("no disc ISO: put it in iso/ or pass --iso")
    missing = [f for f in ("default_mp.xexp", "default.xexp") + TU_FILES if not os.path.isfile(os.path.join(TU6, f))]
    if missing:
        sys.exit(f"title update 6 is not in {TU6} (no {', '.join(missing)}): run tools/install/tu6_setup.py first")

    dst = os.path.join(a.out, "switch", "mw2mp")
    game, update = os.path.join(dst, "game"), os.path.join(dst, "update")
    os.makedirs(update, exist_ok=True)

    print(f"disc -> {game}")
    with xiso.Xiso(a.iso) as x:
        if "default_mp.xex" not in {n.lower() for n, _, _, _ in x.root()}:
            sys.exit(f"{a.iso} is not a Modern Warfare 2 disc (no default_mp.xex)")
        last = [-1]

        def progress(done, total, rel):
            pct = done * 100 // total
            if pct != last[0]:
                last[0] = pct
                print(f"\r  {pct:3d} %  {rel[:60]:<60}", end="", flush=True)
        n = x.extract(game, skip=lambda rel: rel.lower().startswith("$systemupdate/"), progress=progress)
        print(f"\n  {n} files")

    for f in sorted(os.listdir(TU6)):
        if f.lower().endswith(".xexp"):
            shutil.copy2(os.path.join(TU6, f), game)
    for f in TU_FILES:
        shutil.copy2(os.path.join(TU6, f), update)
    print(f"title update 6 -> {game} (.xexp), {update}")

    for nro in NROS:
        if os.path.isfile(nro):
            to = os.path.join(a.out, "switch", os.path.basename(nro)[:-4])
            os.makedirs(to, exist_ok=True)
            shutil.copy2(nro, to)
            print(f"{os.path.basename(nro)} -> {to}")
            # Per-level pipeline lists (mw2mp_native_record_map_lists): no game data, shipped with the port.
            lists = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(nro))), "pipelines")
            if os.path.isdir(lists):
                shutil.copytree(lists, os.path.join(to, "pipelines"), dirs_exist_ok=True)
                print(f"{lists} -> {os.path.join(to, 'pipelines')}")
        else:
            print(f"no {nro} yet (its build_switch.sh): run this again after the build")
    print(f"done: copy {os.path.join(a.out, 'switch')} to the root of the SD card")


if __name__ == "__main__":
    main()
