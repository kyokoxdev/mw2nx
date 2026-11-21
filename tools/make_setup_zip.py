"""Makes the setup kit players run on their PC: <data>/release/mw2nx-setup-<version>.zip (and its folder).

  mw2nx_setup.py, xiso.py, stfs_extract.py   the setup script (tools/install/mw2nx_setup.py) and what it imports
  bin/linux/, bin/windows/                   the shader tools, static x86-64 builds: zone_containers, bo2zm_hlsl,
                                             bo2zm_pack (as tools/mw2shaders/build_zone_containers.sh and
                                             tools/xenosrecomp/build_tools.sh build them), and shader_common.h
  pipelines/                                 the Campaign's pipeline lists (mw2sp/pipelines)
  mods/                                      the bundled script mods (mw2mp/mods)
  LICENSE, THIRD_PARTY_NOTICES.md

No game data goes in it. Needs g++ (static libstdc++) and x86_64-w64-mingw32-g++ (13 or later), and a checkout of
mw2-recompiled for the zone reader (MW2_RECOMPILED, default ~/projects/mw2-recompiled).

usage: python3 -I tools/make_setup_zip.py --version 1.0.0
"""
import argparse
import os
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
XR = ROOT / "tools" / "xenosrecomp"
TR = XR / "translator"
TP = ROOT / "sdk" / "thirdparty"
RENDER = ROOT / "app" / "src" / "native" / "render"


def zone_defs(zone, out):
    """mw2-recompiled's loading rules as one .cpp, as tools/mw2shaders/build_zone_containers.sh makes it."""
    text, table, n = ["// Generated from launcher/zone/defs/ and defs_pc/.\n"], [], 0
    for folder, prefix in (("defs", ""), ("defs_pc", "pc/")):
        base = zone / folder
        for path in sorted(p for pattern in ("*.h", "*.txt", "assets/*.txt") for p in base.glob(pattern)):
            data = path.read_bytes()
            text.append(f"static const unsigned char kDef{n}[] = {{ {','.join(str(b) for b in data)} }};\n")
            table.append(f'    {{ "{prefix}{path.relative_to(base).as_posix()}", kDef{n}, {len(data)} }},\n')
            n += 1
    text.append("struct ZoneDefinition { const char* name; const unsigned char* data; unsigned size; };\n"
                f"extern const ZoneDefinition kZoneDefinitions[] = {{\n{''.join(table)}}};\n"
                f"extern const unsigned kZoneDefinitionCount = {n};\n")
    out.write_text("".join(text))


def build(cxx, out, exe, zone, defs):
    out.mkdir(parents=True, exist_ok=True)
    common = [cxx, "-static", "-w", f"-I{TP / 'xxHash'}"]
    jobs = [
        common + ["-std=c++20", "-O2", f"-I{zone}", str(ROOT / "tools" / "mw2shaders" / "zone_containers.cpp"),
                  *(str(zone / f) for f in ("walker.cpp", "schema.cpp", "defs.cpp", "expr.cpp", "rules.cpp")),
                  str(defs), "-o", str(out / f"zone_containers{exe}")],
        common + ["-std=c++23", "-O1", f"-I{TR}", f"-I{TP / 'fmt' / 'include'}", "-include", str(TR / "pch_min.h"),
                  "-include", str(XR / "assert_throw.h"), "-DFMT_HEADER_ONLY", "-DXXH_INLINE_ALL", "-DBO2ZM_RECOMP",
                  "-D_CRT_SECURE_NO_WARNINGS", str(XR / "bo2zm_hlsl.cpp"), str(TR / "shader_recompiler.cpp"),
                  "-o", str(out / f"bo2zm_hlsl{exe}")],
        common + ["-std=c++23", "-O2", f"-I{RENDER}", str(XR / "bo2zm_pack.cpp"),
                  str(RENDER / "bo2zm_shader_library.cpp"), str(RENDER / "bo2zm_shader_index.cpp"),
                  "-o", str(out / f"bo2zm_pack{exe}")],
    ]
    for p in [subprocess.Popen(j) for j in jobs]:
        if p.wait():
            raise SystemExit(f"{cxx}: build failed")
    shutil.copy2(TR / "shader_common.h", out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    a = ap.parse_args()
    data = pathlib.Path(subprocess.run(["bash", str(ROOT / "tools" / "data_dir.sh")], capture_output=True, text=True,
                                       check=True).stdout.strip())
    zone = pathlib.Path(os.environ.get("MW2_RECOMPILED", pathlib.Path.home() / "projects" / "mw2-recompiled")) \
        / "launcher" / "zone"
    if not (zone / "walker.cpp").is_file():
        raise SystemExit(f"no mw2-recompiled checkout at {zone.parent.parent} (set MW2_RECOMPILED)")
    name = f"mw2nx-setup-{a.version}"
    kit = data / "release" / name
    shutil.rmtree(kit, ignore_errors=True)
    kit.mkdir(parents=True)
    defs = data / "release" / "zone_defs.cpp"
    zone_defs(zone, defs)
    build("g++", kit / "bin" / "linux", "", zone, defs)
    build("x86_64-w64-mingw32-g++", kit / "bin" / "windows", ".exe", zone, defs)
    defs.unlink()
    for src in (ROOT / "tools" / "install" / "mw2nx_setup.py", ROOT / "tools" / "install" / "xiso.py",
                ROOT / "tools" / "inspect" / "stfs_extract.py", ROOT / "LICENSE", ROOT / "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(src, kit)
    shutil.copytree(ROOT / "mw2sp" / "pipelines", kit / "pipelines")
    shutil.copytree(ROOT / "mw2mp" / "mods", kit / "mods")
    archive = shutil.make_archive(str(kit), "zip", kit.parent, name)
    print(f"{archive} ({os.path.getsize(archive) >> 20} MB)")


if __name__ == "__main__":
    main()
