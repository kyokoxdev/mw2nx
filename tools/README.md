# tools

Run from the repository root on Linux or WSL2. Python tools that read game files run with `python -I`, and nothing
derived from the game is ever written inside the repository.

## Setup and build

| Tool | What it does |
|---|---|
| `install/tu6_setup.py` | Checks your ISO, then fetches Title Update 6 and fills `work/load_tu6/` |
| `install/mw2nx_setup.py`, `make_setup_zip.py` | The players' setup kit and the script that packs it |
| `fetch_thirdparty.py` | Fetches the SDK's third-party sources from the ReXGlue v0.10.0 release, keeping this project's changes |
| `build_host_linux.sh` | Builds `rexglue` (the code generator) and `xexdump` |
| `build_mesa_linux.sh` | Builds NVK for the Switch from mesa-switch plus the patch in `patches/` |
| `build_zstd_switch.sh` | A static zstd for the Switch, into the Mesa SDK |
| `toolchain.sh`, `data_dir.sh`, `switch/cmake/` | Tool locations, the data folder, the devkitA64 toolchain file |
| `stage_sd_mw2mp.py` | Lays out the SD card folders from your ISO and Title Update 6 |
| `make_nsp.py`, `nsp/` | The installable multi-program NSP |

## Code generation

`codegen_mw2mp.sh` and `codegen_mw2sp.sh` run the generator and these passes in order.

| Tool | What it does |
|---|---|
| `read_before_write.py` | Registers read before written; `--strict` stops on unknown cases |
| `setjmp_locals.py` | `setjmp` sites restore register locals (`--fix`, `--verify`) |
| `guest_barriers.py` | Compiler barriers for plain guest memory (`--check`, `--undo`) |
| `direct_calls.py` | Direct calls to unhooked functions (`--check`, `--undo`) |
| `codegen_stats.py` | Counts what makes generated code slow |
| `codegen_fix_unresolved.py` | Declares tail-branch targets that are in no function |
| `codegen_gaps.py` | Classifies code no function covers into a `gaps.toml` (`--program mp|sp`) |

## Shader library

| Tool | What it does |
|---|---|
| `mw2shaders/build_mw2mp_shaders.sh` | Builds `mw2mp_shaders.spvpack` and its index from your disc's fastfiles (`MW2_SP=1`: the Campaign's) |
| `xexdump/` | Writes the decrypted, patched executable image and a summary |
| `xenosrecomp/` | The shader translator (`mw2_hlsl`), its build and the library packer (`mw2_pack`) |
| `pipelines_list.py` | Pipeline prewarm lists |

## Measuring

| Tool | What it does |
|---|---|
| `pgo_check.py` | How many trained functions still read a PGO profile's counts (`--program mp|sp`) |
| `profile_sym.py` | Names the console profiler's addresses |
| `profile_inline.py` | One thread's samples by inlined function or source line |
| `memcpy_callers_sym.py` | A copy-callers build's calls by call site |

## Inspecting the game

These read your own files or the generated code and write nothing into the repository: `inspect/xex_header.py`,
`xexp_info.py` (XEX headers and versions), `stfs_extract.py` (STFS packages), `ppc_dis.py` and `ppc_xref.py` (PowerPC
disassembly and cross-references), `gen_index.py`, `genxref.py` and `genfn.sh` (the generated code by function),
`dvar_list.py` (dvars and flags), `grep_strings.py` (a pattern search in a binary).
