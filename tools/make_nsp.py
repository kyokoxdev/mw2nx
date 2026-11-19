"""Packs the installable mw2nx NSP: one application, title ID 01F2D6C0A9E40000, with no game data.

It is laid out as retail multi-program applications are:
  Program NCA, index 0   ExeFS: main (the Campaign, mw2sp, as an NSO) + main.npdm   title 01F2D6C0A9E40000
  Program NCA, index 1   ExeFS: main (Multiplayer, mw2mp)              + main.npdm   title 01F2D6C0A9E40001
  Control NCAs, 0 and 1  control.nacp (name, version, a user picked at start, the application's save) and the icon
  Meta NCA               the content list naming both programs and their controls (id offset 0 and 1)
The game switches between them with appletExecuteProgram (mw2mp/src/mw2mp_launch.cpp), the way the 360 executables
start each other.

Players' files go next to it on the SD card, in sdmc:/atmosphere/contents/01F2D6C0A9E40000/ (game/, update/, shader
libraries); the game makes that folder and lists what is missing. Saves are the console's save data.

Needs devkitPro (DEVKITPRO, default ~/devkitpro: elf2nso, npdmtool, nacptool) and hacbrewpack with the extra program
option (patches/hacbrewpack-extraprogram.patch; --hacbrewpack, default <data>/tools/hacbrewpack/hacbrewpack), which
encrypts the NCAs with the builder's own console keys (--keys). The keys and the icon are local files, never in the
repository. The programs are the last LTO+PGO builds' ELFs (mw2mp/build_switch.sh, mw2sp/build_switch.sh).

usage: python3 -I tools/make_nsp.py [--keys docs/prod.keys] [--icon docs/images/icon.jpg] [--version 0.1.0]
                                     [--mp-elf F] [--sp-elf F] [--out <data>/nsp]
"""
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys

TITLE_ID = 0x01F2D6C0A9E40000
NAME = "Call of Duty®: Modern Warfare® 2"
AUTHOR = "kyokoxdev"
# The save the game creates (mw2mp/src/mw2mp_launch.cpp, kSaveDataSize/kSaveJournalSize): the same sizes here.
SAVE_SIZE = 32 << 20
SAVE_JOURNAL = 16 << 20

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NPDM_TEMPLATE = os.path.join(ROOT, "tools", "nsp", "mw2nx.npdm.json")

# NacpStruct offsets (libnx nacp.h, checked with offsetof).
NACP_SIZE = 0x4000
OFF_STARTUP_USER_ACCOUNT = 0x3025
OFF_SUPPORTED_LANGUAGES = 0x302C
OFF_SCREENSHOT = 0x3034
OFF_VIDEO_CAPTURE = 0x3035
OFF_PRESENCE_GROUP_ID = 0x3038
OFF_SAVE_OWNER_ID = 0x3078
OFF_USER_SAVE_SIZE = 0x3080
OFF_USER_SAVE_JOURNAL = 0x3088


def data_dir():
    return subprocess.run(["bash", os.path.join(ROOT, "tools", "data_dir.sh")], check=True, capture_output=True,
                          text=True).stdout.strip()


def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def patch_nacp(nacp):
    """The fields nacptool leaves at zero: the user, the save, the languages; video capture off."""
    b = bytearray(nacp)
    if len(b) != NACP_SIZE:
        sys.exit(f"control.nacp is {len(b)} bytes, expected {NACP_SIZE}")
    b[OFF_STARTUP_USER_ACCOUNT] = 1  # pick a user at start: the save belongs to that user
    struct.pack_into("<I", b, OFF_SUPPORTED_LANGUAGES, 1 << 1)  # American English (the title entry nacptool fills)
    b[OFF_SCREENSHOT] = 0  # allowed
    b[OFF_VIDEO_CAPTURE] = 0  # off: automatic recording reserves memory the game needs
    struct.pack_into("<Q", b, OFF_PRESENCE_GROUP_ID, TITLE_ID)
    struct.pack_into("<Q", b, OFF_SAVE_OWNER_ID, TITLE_ID)
    struct.pack_into("<q", b, OFF_USER_SAVE_SIZE, SAVE_SIZE)
    struct.pack_into("<q", b, OFF_USER_SAVE_JOURNAL, SAVE_JOURNAL)
    return bytes(b)


def strip_jpeg(jpeg):
    """The JPEG without metadata segments (APP1-APP15, comments): Horizon can hang installing an icon with EXIF."""
    if jpeg[:2] != b"\xff\xd8":
        sys.exit("the icon is not a JPEG")
    out = bytearray(jpeg[:2])
    i = 2
    while i + 4 <= len(jpeg):
        if jpeg[i] != 0xFF:
            sys.exit("the icon's JPEG segments are damaged")
        marker = jpeg[i + 1]
        if marker == 0xDA:  # start of scan: the image data to the end
            out += jpeg[i:]
            return bytes(out)
        length = struct.unpack(">H", jpeg[i + 2:i + 4])[0]
        segment = jpeg[i:i + 2 + length]
        if marker in (0xC2, 0xC6, 0xCA, 0xCE):
            sys.exit("the icon is a progressive JPEG; save it as baseline")
        if marker in (0xC0, 0xC1):
            height, width = struct.unpack(">HH", segment[5:9])
            if (width, height) != (256, 256):
                sys.exit(f"the icon is {width}x{height}; it must be 256x256")
        if not (0xE1 <= marker <= 0xEF or marker == 0xFE):
            out += segment
        i += 2 + length
    sys.exit("the icon's JPEG has no image data")


def make_exefs(folder, elf, program_id, env):
    """ExeFS of one program: the game as an NSO, and its NPDM (tools/nsp/mw2nx.npdm.json) with its own title ID."""
    os.makedirs(folder)
    run(["elf2nso", elf, os.path.join(folder, "main")], env=env)
    with open(NPDM_TEMPLATE) as f:
        npdm = json.load(f)
    tid = f"0x{program_id:016X}"
    npdm.update(title_id=tid, title_id_range_min=tid, title_id_range_max=tid)
    npdm["filesystem_access"]["content_owner_ids"] = [f"0x{TITLE_ID:016X}"]
    npdm_json = folder + ".npdm.json"
    with open(npdm_json, "w") as f:
        json.dump(npdm, f, indent=4)
    run(["npdmtool", npdm_json, os.path.join(folder, "main.npdm")], env=env, stdout=subprocess.DEVNULL)


def hacbrewpack(args, work, program_id, exefs, control, extra=()):
    """One hacbrewpack run in its own folder; returns its NSP and the paths of its program and control NCAs."""
    os.makedirs(work)
    cmd = [os.path.abspath(args.hacbrewpack), "-k", os.path.abspath(args.keys), "--titleid", f"{program_id:016x}",
           "--exefsdir", exefs, "--nologo", "--controldir", control, "--keepncadir",
           "--nspdir", os.path.join(work, "nsp"), "--ncadir", os.path.join(work, "nca"),
           "--tempdir", os.path.join(work, "temp"), "--backupdir", os.path.join(work, "backup")]
    cmd += list(extra) if "--romfsdir" in extra else ["--noromfs"] + list(extra)
    out = run(cmd, cwd=work, capture_output=True, text=True).stdout
    with open(os.path.join(work, "hacbrewpack.log"), "w") as f:
        f.write(out)
    found = [re.search(pattern, out) for pattern in
             (r"----> Created NSP: (.+\.nsp)", r"Created Program NCA: (.+\.nca)", r"Created Control NCA: (.+\.nca)")]
    if not all(found):
        sys.exit(f"hacbrewpack output not understood, see {work}/hacbrewpack.log")
    return [m.group(1).strip() for m in found]


def main():
    data = data_dir()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--keys", default=os.path.join(ROOT, "docs", "prod.keys"))
    ap.add_argument("--icon", default=os.path.join(ROOT, "docs", "images", "icon.jpg"))
    ap.add_argument("--version", default="0.1.0", help="the version shown on the HOME menu")
    ap.add_argument("--sp-elf", default=os.path.join(ROOT, "mw2sp", "out", "switch", "mw2sp.stripped.elf"))
    ap.add_argument("--mp-elf", default=os.path.join(ROOT, "mw2mp", "out", "switch", "mw2mp.stripped.elf"))
    ap.add_argument("--hacbrewpack", default=os.path.join(data, "tools", "hacbrewpack", "hacbrewpack"))
    ap.add_argument("--out", default=os.path.join(data, "nsp"))
    args = ap.parse_args()

    for what, path in (("keys", args.keys), ("icon", args.icon), ("Campaign ELF", args.sp_elf),
                       ("Multiplayer ELF", args.mp_elf), ("hacbrewpack", args.hacbrewpack)):
        if not os.path.isfile(path):
            sys.exit(f"{what} not found: {path}")
    usage = subprocess.run([args.hacbrewpack, "--help"], capture_output=True)
    if b"--extraprogram" not in usage.stdout + usage.stderr:
        sys.exit(f"{args.hacbrewpack} lacks --extraprogram: apply patches/hacbrewpack-extraprogram.patch and rebuild")
    devkitpro = os.environ.get("DEVKITPRO", os.path.expanduser("~/devkitpro"))
    env = dict(os.environ, DEVKITPRO=devkitpro,
               PATH=os.pathsep.join([os.path.join(devkitpro, "tools", "bin"), os.environ["PATH"]]))

    stage = os.path.join(args.out, "stage")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    exefs_sp = os.path.join(stage, "exefs_campaign")
    exefs_mp = os.path.join(stage, "exefs_multiplayer")
    make_exefs(exefs_sp, args.sp_elf, TITLE_ID, env)
    make_exefs(exefs_mp, args.mp_elf, TITLE_ID + 1, env)

    control = os.path.join(stage, "control")
    os.makedirs(control)
    nacp = os.path.join(control, "control.nacp")
    run(["nacptool", "--create", NAME, AUTHOR, args.version, nacp], env=env)
    with open(nacp, "rb") as f:
        patched = patch_nacp(f.read())
    with open(nacp, "wb") as f:
        f.write(patched)
    with open(args.icon, "rb") as f:
        icon = strip_jpeg(f.read())
    with open(os.path.join(control, "icon_AmericanEnglish.dat"), "wb") as f:
        f.write(icon)
    # hacbrewpack rewrites the title IDs in the control folder it is given: program 1's run gets its own copy.
    control_mp = os.path.join(stage, "control_multiplayer")
    shutil.copytree(control, control_mp)

    # Program 1 first (its program and control NCAs are kept; its NACP keeps the application's IDs, so both programs
    # share one save), then the application around program 0 with both added as index 1.
    _, program_mp, control_nca_mp = hacbrewpack(args, os.path.join(stage, "pack_multiplayer"), TITLE_ID + 1, exefs_mp,
                                                control_mp, ["--nacptitleid", f"{TITLE_ID:016x}"])
    # The Campaign's per-level pipeline lists (state keys + shader fingerprints, no game data) go in its RomFS.
    romfs = os.path.join(stage, "romfs_campaign")
    shutil.copytree(os.path.join(ROOT, "mw2sp", "pipelines"), os.path.join(romfs, "pipelines"))
    nsp, _, _ = hacbrewpack(args, os.path.join(stage, "pack_application"), TITLE_ID, exefs_sp, control,
                            ["--romfsdir", romfs, "--extraprogram", program_mp, "--extracontrol", control_nca_mp])
    final = os.path.join(args.out, f"mw2nx-{args.version}-[{TITLE_ID:016X}].nsp")
    os.replace(nsp, final)
    print(f"{final}: {os.path.getsize(final) / (1 << 20):.1f} MB")


if __name__ == "__main__":
    main()
