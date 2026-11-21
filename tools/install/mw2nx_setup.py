"""mw2nx setup: makes everything the installed game needs on the SD card, from your own Modern Warfare 2 disc.

One run, on Windows or Linux, Python 3.11 or later and nothing else to install:
  1. checks your disc image (Xbox 360, version 1.0.557), then downloads Title Update 6;
  2. extracts the disc and the update;
  3. downloads Microsoft's shader compiler (dxc) and builds both shader libraries from your disc;
  4. lays it all out, with the pipeline lists and the bundled script mods, in
       <out>/atmosphere/contents/01F2D6C0A9E40000/
     Copy <out>/atmosphere to the root of the SD card.

Nothing is downloaded until the disc checks out, and nothing it makes may be shared: it is derived from your disc.
A second run keeps what is already done (extracted files of the right size, a downloaded update and compiler).

usage: python mw2nx_setup.py [--iso DISC.iso] [--out FOLDER] [--jobs N] [--no-shaders]
(on Windows, double-clicking it works too: it finds an .iso placed beside it, or asks for one)
"""
import argparse
import concurrent.futures
import hashlib
import os
import platform
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
import zlib

# Runs from the setup kit (tools/make_setup_zip.py), which has everything beside this script.
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xiso  # noqa: E402
from stfs_extract import Stfs  # noqa: E402

TITLE_ID = 0x41560817
APP_ID = "01F2D6C0A9E40000"
TU_NAME = "TU_10LC20N_0000018000000.00000000001GA"
# The CoD Xenon archive at a fixed commit, as tools/install/tu6_setup.py, so the file cannot change.
TU_URL = ("https://raw.githubusercontent.com/codxenon/xbox360-title-updates/25de2389ce214dae6159fcd45a9581cb3b640bd9/"
          "games/Call%20of%20Duty%20-%20Modern%20Warfare%202%20%28USA%2C%20Europe%29/TU6/" + TU_NAME)
TU_XEXP = ("default_mp.xexp", "default.xexp")
TU_FF = ("patch_mp.ff", "dlc1_ui_mp.ff", "dlc2_ui_mp.ff")
# DXC v1.9.2609, the version the released shader libraries are built with (same output on both systems).
DXC = {
    "windows": ("https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip",
                "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1", "bin/x64/dxc.exe"),
    "linux": ("https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/"
              "linux_dxc_2026_09_28.x86_x64.tar.gz",
              "96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1", "bin/dxc"),
}


def say(text=""):
    print(text, flush=True)


def system():
    name = {"Windows": "windows", "Linux": "linux"}.get(platform.system())
    if not name or platform.machine().lower() not in ("amd64", "x86_64"):
        sys.exit(f"this PC ({platform.system()} {platform.machine()}) is not supported: Windows or Linux on x86-64")
    return name


def download(url, dst, sha256=None, tries=4):
    """url to dst through dst.part, with a progress line; checks sha256 when given. A slow or broken connection is
    tried again from the start."""
    part = dst + ".part"
    for attempt in range(1, tries + 1):
        digest = hashlib.sha256()
        try:
            with urllib.request.urlopen(url, timeout=60) as r, open(part, "wb") as f:
                total, done = int(r.headers.get("Content-Length") or 0), 0
                while chunk := r.read(1 << 20):
                    f.write(chunk)
                    digest.update(chunk)
                    done += len(chunk)
                    print(f"\r  {done >> 20} MB" + (f" of {total >> 20} MB" if total else ""), end="", flush=True)
            print()
            break
        except OSError as e:  # URLError, timeouts, resets
            print()
            if attempt == tries:
                sys.exit(f"cannot download {url}: {e}\ncheck the internet connection and run this again")
            say(f"  the download stopped ({e}), trying again ({attempt + 1} of {tries})")
    if sha256 and digest.hexdigest() != sha256:
        os.remove(part)
        sys.exit(f"{os.path.basename(dst)}: the download is not the expected file (checksum differs)")
    os.replace(part, dst)


def find_iso(given):
    if given:
        return given
    for folder in (HERE, os.path.join(HERE, "iso"), os.getcwd()):
        if os.path.isdir(folder):
            for name in sorted(os.listdir(folder)):
                if name.lower().endswith(".iso"):
                    return os.path.join(folder, name)
    return input("Path to your Modern Warfare 2 disc image (.iso): ").strip().strip('"')


def check_disc(iso):
    try:
        with xiso.Xiso(iso) as x:
            if "default_mp.xex" not in {name.lower() for name, _, _, _ in x.root()}:
                sys.exit(f"{iso}: there is no default_mp.xex on it, it is not a Modern Warfare 2 disc")
    except (xiso.XisoError, OSError) as e:
        sys.exit(f"cannot read the disc {iso}: {e}")


def title_update(iso, cache):
    """Title Update 6's Stfs and its files by name: a copy beside the ISO or the script, else downloaded."""
    candidates = [os.path.join(os.path.dirname(os.path.abspath(iso)), TU_NAME), os.path.join(HERE, TU_NAME),
                  os.path.join(cache, TU_NAME)]
    tu = next((p for p in candidates if os.path.isfile(p)), None)
    if tu:
        say(f"Title Update 6: {tu}")
    else:
        tu = candidates[-1]
        say("Downloading Title Update 6 (1.6 MB)")
        download(TU_URL, tu)
    s = Stfs(tu)
    entries = {e["name"].lower(): e for e in s.entries() if not e["dir"]}
    missing = [n for n in TU_XEXP + TU_FF if n not in entries]
    if s.title_id != TITLE_ID or missing:
        sys.exit(f"{tu} is not Modern Warfare 2's Title Update 6: delete it and run this again")
    return s, entries


def extract(iso, dst):
    game = os.path.join(dst, "game")
    say(f"Extracting the disc to {game}")
    last = [-1]

    def progress(done, total, rel):
        pct = done * 100 // total
        if pct != last[0]:
            last[0] = pct
            print(f"\r  {pct:3d} %  {rel[:60]:<60}", end="", flush=True)
    with xiso.Xiso(iso) as x:
        n = x.extract(game, skip=lambda rel: rel.lower().startswith("$systemupdate/"), progress=progress)
    say(f"\n  {n} files")


def dxc(cache, name):
    url, sha, exe = DXC[name]
    folder = os.path.join(cache, "dxc-1.9.2609")
    path = os.path.join(folder, *exe.split("/"))
    if os.path.isfile(path):
        return path
    archive = os.path.join(cache, url.rsplit("/", 1)[1])
    if not os.path.isfile(archive):
        say("Downloading the shader compiler (Microsoft's dxc)")
        download(url, archive, sha)
    say("Unpacking the shader compiler")
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            z.extractall(folder)
    else:
        with tarfile.open(archive) as t:
            t.extractall(folder, **({"filter": "data"} if hasattr(tarfile, "data_filter") else {}))
    os.chmod(path, 0o755)
    os.remove(archive)
    return path


def inflate(path, out):
    """A 360 fastfile's zone; a signed one interleaves 8 KB of hashes every 2 MB of its zlib stream."""
    data = open(path, "rb").read()
    if data[:8] not in (b"IWffu100", b"IWff0100"):
        sys.exit(f"{path}: not an Xbox 360 fastfile")
    body = 0x1D + struct.unpack_from(">I", data, 0x19)[0] * 12 + 8  # header, picture table, two sizes
    if data[body:body + 8] == b"IWffs100":
        parts, at = [], body + 0x2000
        while at + 0x2000 < len(data):
            at += 0x2000
            parts.append(data[at:at + 0x200000])
            at += 0x200000
        stream = b"".join(parts)
    else:
        stream = data[body:]
    d = zlib.decompressobj()
    zone = d.decompress(stream)
    if not d.eof:
        sys.exit(f"{path}: the fastfile does not decompress to its end")
    open(os.path.join(out, os.path.basename(path)[:-3] + ".zone"), "wb").write(zone)


def run(*cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"{os.path.basename(cmd[0])} failed ({r.returncode}):\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    return r.stdout


def compile_one(dxc_exe, hlsl, spv):
    stage = ["-T", "vs_6_6", "-fvk-invert-y"] if os.path.basename(hlsl).startswith("v_") else ["-T", "ps_6_6"]
    r = subprocess.run([dxc_exe, "-spirv", *stage, "-E", "main", "-HV", "2021", "-fspv-target-env=vulkan1.2",
                        "-fvk-use-dx-layout", "-Fo", spv, hlsl], capture_output=True)
    return r.returncode == 0


def shaders(pack, dst, work, tools, dxc_exe, jobs):
    """dst/<pack>.spvpack and .spvidx from the fastfiles in dst/game and dst/update, as
    tools/mw2shaders/build_mw2mp_shaders.sh builds them. mw2mp: the multiplayer's zones; mw2sp: every other one."""
    game, update = os.path.join(dst, "game"), os.path.join(dst, "update")
    if pack == "mw2sp_shaders":
        zones = [os.path.join(game, f) for f in sorted(os.listdir(game))
                 if f.endswith(".ff") and not (f.endswith("_mp.ff") or f.startswith("mp_"))]
    else:
        zones = [os.path.join(game, f) for f in ("code_post_gfx_mp.ff", "common_mp.ff", "ui_mp.ff")]
        zones += [os.path.join(game, f) for f in sorted(os.listdir(game)) if f.startswith("mp_") and f.endswith(".ff")]
        zones += [os.path.join(update, f) for f in sorted(os.listdir(update)) if f.endswith(".ff")]
    say(f"Building {pack} from {len(zones)} fastfiles")
    shutil.rmtree(work, ignore_errors=True)
    dirs = {k: os.path.join(work, k) for k in ("zones", "containers", "hlsl", "spirv")}
    os.makedirs(dirs["zones"])
    os.makedirs(dirs["spirv"])
    for z in zones:
        inflate(z, dirs["zones"])
    exe = ".exe" if os.name == "nt" else ""
    zone_files = sorted(os.path.join(dirs["zones"], f) for f in os.listdir(dirs["zones"]))
    say("  " + run(os.path.join(tools, "zone_containers" + exe), dirs["containers"], *zone_files).strip()
        .splitlines()[-1])
    shutil.rmtree(dirs["zones"])
    say("  " + run(os.path.join(tools, "bo2zm_hlsl" + exe), dirs["containers"], dirs["hlsl"],
                   os.path.join(tools, "shader_common.h")).strip().splitlines()[-1])
    hlsl = sorted(os.path.join(r, f) for r, _, fs in os.walk(dirs["hlsl"]) for f in fs if f.endswith(".hlsl"))
    failed, done = [], 0
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        futures = {pool.submit(compile_one, dxc_exe, h,
                               os.path.join(dirs["spirv"], os.path.basename(h)[:-5] + ".spv")): h for h in hlsl}
        for f in concurrent.futures.as_completed(futures):
            done += 1
            if not f.result():
                failed.append(os.path.basename(futures[f])[:-5])
            if done % 100 == 0 or done == len(hlsl):
                print(f"\r  dxc: {done} of {len(hlsl)}", end="", flush=True)
    say(f"\n  dxc: {len(hlsl) - len(failed)} compiled, {len(failed)} left out (the game draws without them)")
    spvpack, spvidx = os.path.join(dst, pack + ".spvpack"), os.path.join(dst, pack + ".spvidx")
    pack_exe = os.path.join(tools, "bo2zm_pack" + exe)
    run(pack_exe, dirs["containers"], dirs["spirv"], spvpack + ".part")
    os.replace(spvpack + ".part", spvpack)
    run(pack_exe, "--index", spvpack, spvidx)
    shutil.rmtree(work)
    say(f"  {spvpack} ({os.path.getsize(spvpack) >> 20} MB)")


def main():
    name = system()
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--iso", help="your disc image (default: the first .iso beside this script or in iso/)")
    ap.add_argument("--out", default=os.path.join(HERE, "sd"),
                    help="where the SD card's folders are made (default: sd/ beside this script)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2, help="shaders compiled at once")
    ap.add_argument("--no-shaders", action="store_true", help="skip the shader libraries")
    a = ap.parse_args()

    iso = find_iso(a.iso)
    if not os.path.isfile(iso):
        sys.exit(f"no disc image at {iso}")
    say(f"Checking the disc: {iso}")
    check_disc(iso)
    cache = os.path.join(a.out, "cache")
    os.makedirs(cache, exist_ok=True)
    s, entries = title_update(iso, cache)

    dst = os.path.join(a.out, "atmosphere", "contents", APP_ID)
    extract(iso, dst)
    os.makedirs(os.path.join(dst, "update"), exist_ok=True)
    for n in TU_XEXP + TU_FF:
        with open(os.path.join(dst, "game" if n in TU_XEXP else "update", entries[n]["name"]), "wb") as f:
            f.write(s.read(entries[n]))
    say("Title Update 6 written to game/ and update/")

    # Shipped with the port, no game data: the Campaign's pipeline lists and the bundled script mods.
    for to in ("pipelines", "mods"):
        shutil.copytree(os.path.join(HERE, to), os.path.join(dst, to), dirs_exist_ok=True)
    say("pipelines/ and mods/ copied")

    if not a.no_shaders:
        tools = os.path.join(HERE, "bin", name)
        if name == "linux":  # a zip unpacked without its file modes
            for f in ("zone_containers", "bo2zm_hlsl", "bo2zm_pack"):
                os.chmod(os.path.join(tools, f), 0o755)
        dxc_exe = dxc(cache, name)
        for pack in ("mw2mp_shaders", "mw2sp_shaders"):
            shaders(pack, dst, os.path.join(a.out, "work"), tools, dxc_exe, a.jobs)

    say()
    say(f"Done. Copy {os.path.join(a.out, 'atmosphere')} to the root of your SD card (merge with the folder")
    say("already there), install the NSP and start Call of Duty: Modern Warfare 2 from the HOME menu.")
    say("These files come from your disc: never share them.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit("\nstopped: run it again to carry on")
    finally:
        if os.name == "nt" and sys.stdin.isatty():  # double-clicked on Windows: keep the window open
            try:
                input("Press Enter to close")
            except EOFError:
                pass
