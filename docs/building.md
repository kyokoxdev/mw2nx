# Building from source

A build from start to finish: your own game files in, Switch programs out. Nothing in this repository contains the
game. Every step that reads game data runs on your machine and writes into folders git ignores.

The build host is **Linux or WSL2** (no sudo needed). The Switch is the only target. A full build needs about 15 GB of
RAM (WSL2 with extra swap works); each generated file takes 2-3 GB to compile and the LTO link about 2 GB more.

Steps 1-5 build Multiplayer (`default_mp.xex`); the Campaign (`default.xex`) repeats them with its own scripts, as
[below](#campaign). The release is the NSP of [the last section](#the-installable-nsp); the NROs from steps 5 and 7 are
the development layout, run from the Homebrew Menu.

Players who only install the released NSP need none of this: the setup kit of [the last section](#the-setup-kit) does
steps 1, 6 and 7 for them on Windows or Linux. The rest builds the program itself.

## What you need

**Your own copy of the game:** the Modern Warfare 2 disc for the Xbox 360, version 1.0.557 (USA/Europe), as an ISO in
`iso/`. Title Update 6 is fetched for you in step 1.

**Tools:**

| Tool | Used for |
|---|---|
| Python 3.11 or later (with tkinter) | setup tool, code generation fix-ups, staging |
| CMake and Ninja | every C++ build |
| clang/lld 21 | the host tools and the shader translator |
| devkitPro: devkitA64 and libnx | the Switch build |
| Rust, Meson and the LLVM, libclc and SPIRV packages named in `tools/build_mesa_linux.sh` | Mesa's NVK driver for the Switch |
| DirectXShaderCompiler (`dxc`) and `spirv-val` | the shader library |

`tools/toolchain.sh` and the build scripts say which environment variable points at each tool.

## Step 1: set up the game files

```bash
python tools/install/tu6_setup.py
```

Pick your ISO (or leave it in `iso/`). The tool checks the disc, and only then downloads Title Update 6 (or uses a copy
already in `iso/`). It fills `work/load_tu6/` with `default_mp.xex` and `default.xex` from the disc and the update's
`.xexp` and `.ff` files. Game files are never redistributed.

## Step 2: build the host tools

```bash
tools/build_host_linux.sh
```

Builds `sdk/out/linux-amd64/rexglue` (the code generator) and `xexdump`. The SDK's third-party sources come from
`python tools/fetch_thirdparty.py`.

## Step 3: generate the game's C++

```bash
tools/codegen_mw2mp.sh
```

Decrypts and patches the executable, generates the C++ into `mw2mp/generated/` and runs the fix-up passes in order:
registers read before written, `setjmp` sites, guest-memory barriers and direct calls. What the analysis cannot find is
in `mw2mp/overrides.toml` and `mw2mp/gaps.toml`. The builds refuse to start without the fix-ups.

## Step 4: build the Mesa driver

```bash
tools/build_mesa_linux.sh
```

Builds NVK for the Switch from mesa-switch plus the patch in `patches/`. Its output is the default `MESA_SDK` of the
build script.

## Step 5: build the Switch program

```bash
mw2mp/build_switch.sh
```

Writes `mw2mp/out/switch/mw2mp.nro` and the stripped ELF the NSP is made from. Options, as environment variables:

| Variable | What it does |
|---|---|
| `JOBS` | Parallel compiles (default 2) |
| `MW2_LTO=1` | Link-time optimization |
| `MW2_PGO=generate` | A training build in `out/switch-pgo`. Run it with `mw2mp_benchmark = true` in a private match; it writes the profile to `sdmc:/switch/mw2mp/pgo` |
| `MW2_PGO=use` | The final build with that profile (`MW2_PGO_DIR`, default `data/pgo`). Retrain whenever the generated code or flags change; use together with `MW2_LTO=1` |
| `MW2_GUEST_MEMORY_PLAIN=0` | Volatile guest loads and stores instead of plain ones |
| `MW2_WATCH_STORES=1` | A diagnostic build that watches stores into a guest range, to find a page's writers (Campaign build too) |

A release build is `MW2_PGO=use MW2_LTO=1`; a plain run gives the smaller `-O3` program without the profile.

## Step 6: build the shader library

```bash
tools/mw2shaders/build_mw2mp_shaders.sh <work folder>
```

Translates every shader in your disc's fastfiles with XenosRecomp, compiles them with `dxc`, validates them with
`spirv-val` and packs them into `mw2mp_shaders.spvpack` and `.spvidx` (about 7,600 shaders for Multiplayer). The output
is derived from the game and must never be committed or shared.

## Step 7: stage the SD card

```bash
python tools/stage_sd_mw2mp.py
```

Extracts the disc's files into `switch/mw2mp/game/`, the update into `update/`, copies the NROs, the pipeline lists and
the script mods, and prints the folder to copy to the root of the SD card. Files already there with the right size are
kept, so a second run only refreshes the NRO and the update. Copy the shader library next to its NRO.

## Campaign

```bash
tools/codegen_mw2sp.sh
mw2sp/build_switch.sh
MW2_SP=1 tools/mw2shaders/build_mw2mp_shaders.sh <work folder>
```

The Campaign builds the same sources as Multiplayer with `mw2sp/src/mw2sp_game_remap.h` forced in, which names the
campaign's counterpart of each Multiplayer address. Its program lives in `sdmc:/switch/mw2sp/` (the Homebrew Menu shows
one NRO per folder) and reads the game files from `sdmc:/switch/mw2mp/`.

## The installable NSP

The release is one application, title ID `01F2D6C0A9E40000`, built as a retail multi-program game: program 0 is the
Campaign (`01F2D6C0A9E40000`), program 1 is Multiplayer (`01F2D6C0A9E40001`), each with its own `main` (an NSO) and
NPDM (`tools/nsp/mw2nx.npdm.json`: application type 1, four cores, 39-bit address space). The game menus switch between
them with `appletExecuteProgram`, passing the Switch user along so both programs share one save.

```bash
# the last LTO+PGO builds of both programs, then:
python3 -I tools/make_nsp.py --version 1.0.1
```

| Needs | Where |
|---|---|
| `elf2nso`, `npdmtool`, `nacptool` | devkitPro (`DEVKITPRO`, default `~/devkitpro`) |
| `hacbrewpack` with `--extraprogram` | v3.05 plus `patches/hacbrewpack-extraprogram.patch`; `--hacbrewpack` points at it (`make clean` after header changes) |
| Your console's keys | `--keys` (default `docs/prod.keys`): the NCAs are encrypted with the builder's own keys |
| An icon | `--icon` (default `docs/images/icon.jpg`) |

The output is `<data>/nsp/mw2nx-1.0.1-01F2D6C0A9E40000.nsp`. The keys and the icon are local files and are never
committed (both paths are git-ignored); the NSP contains no game data.

**Installed, the program uses its own folder**, `sdmc:/atmosphere/contents/01F2D6C0A9E40000/`, for `game/`, `update/`,
the shader libraries, `pipelines/`, caches, settings and logs, and never reads `sdmc:/switch/`. The profile and saves
are the console's save data of the application (32 MB with a 16 MB journal). `application_type` in the NPDM must stay
`1`: as an applet the process got only about 394 MB.

## The setup kit

The release's second file, `mw2nx-setup-1.0.1.zip`, is what players run on their PC (Windows or Linux, Python 3.11
or later): `mw2nx_setup.py` checks their ISO, downloads Title Update 6 and DXC, extracts the disc and builds both shader
libraries into `sd/atmosphere/contents/01F2D6C0A9E40000/`, the installed game's folder.

```bash
python3 -I tools/make_setup_zip.py --version 1.0.1
```

It builds the shader tools (`zone_containers`, `mw2_hlsl`, `mw2_pack`) as static programs for Linux with `g++` and
for Windows with `x86_64-w64-mingw32-g++` (13 or later), and packs them with the script, the Campaign's pipeline lists
and the bundled mods into `<data>/release/mw2nx-setup-1.0.1.zip`. The zone reader comes from a checkout of
mw2-recompiled (`MW2_RECOMPILED`, default `~/projects/mw2-recompiled`). The kit contains no game data.
