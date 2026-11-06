# mw2nx: plan

Call of Duty: Modern Warfare 2 (Xbox 360) on the Nintendo Switch by static recompilation: ReXGlue v0.10.0
recompiles the PowerPC executable to C++, a Horizon runtime layer runs it, and Vulkan on mesa-switch NVK draws it.
MW2 lives in `mw2mp/` (multiplayer, `default_mp.xex`) and later `mw2sp/`.

## Credits

- The Switch runtime, renderer and tooling are based on [bo2-360-nx](https://github.com/AlvaroEPena/bo2-360-nx)
  (GPL-3.0); its files keep their notices, see `THIRD_PARTY_NOTICES.md`.
- MW2 addresses, engine fixes and D3D notes come from
  [mw2-recompiled](https://github.com/PaulCombal/mw2-recompiled) (GPL-3.0).

## Inputs (yours, never committed)

- Xbox 360 disc ISO, version 1.0.557 (USA/Europe). mw2-recompiled `launcher/setup.cpp` lists the SHA-256s.
- Title update 6 (`TU_10LC20N_0000018000000.00000000001GA`, 1.6 MB), from the codxenon xbox360-title-updates
  archive at a pinned commit (as mw2-recompiled's launcher). `python tools/install/tu6_setup.py` (tkinter GUI) checks
  the ISO in `iso/`, then downloads TU6 (or uses one already in `iso/`) and fills `work/load_tu6/`: `default_mp.xex`
  from the disc plus the update's `.xexp` and `.ff` files. Game files are never redistributed.

## Host (WSL2, no sudo)

- clang/lld 21: `apt-get download` of Ubuntu's `clang-21 lld-21 llvm-21 ...` debs, `dpkg -x` into
  `~/tools/clang`. Used by `tools/build_host_linux.sh` (bo2's `build_host.sh` is Windows-only; the SDK itself
  supports linux-amd64).
- X11/Wayland dev files: same trick into `~/tools/devroot` (`libx11-xcb-dev`, `libwayland-dev` and deps, `.pc`
  prefixes rewritten). Outputs: `sdk/out/linux-amd64/rexglue`, `out/host-linux/switch_bo2/xexdump`.
- devkitA64 in `~/devkitpro` (GCC 15.2; bo2 used 16.1, watch for that).
- Mesa/NVK: `tools/build_mesa_linux.sh` (bo2's `build_mesa.sh` and mesa-switch's `build-unified.sh` are MSYS2-only).
  Host LLVM 21 dev, libclc, SPIRV-LLVM-Translator, SPIRV-Tools debs `dpkg -x`'d into `~/tools/clang`; rustup target
  `aarch64-unknown-linux-gnu`, `cargo install bindgen-cli cbindgen`. Vulkan only. Output is `MESA_SDK`'s default.

## Milestones

1. **Host tools**: `tools/build_host_linux.sh` builds `rexglue` and `xexdump`.
2. **Load + codegen**: `xexdump` on TU6 `default_mp.xex`; `tools/codegen_mw2mp.sh` (codegen, then bo2's fix-up
   passes: read_before_write, setjmp_locals, guest_barriers).
   Resolve unresolved branches into `mw2mp/overrides.toml`.
3. **Switch first light**: Horizon build (devkitA64) with the Xenos path or a minimal native path. Main menu on
   hardware. Switch is the only target: no desktop builds.
4. **Native renderer**: port bo2's `app/src/native/render` (PM4 -> Vulkan, build-time shader pack) to MW2's D3D
   layer; mw2-recompiled `docs/d3d9-seam.md`, `rendering.md`, `shaders.md` describe MW2's side.
5. **Perf**: LTO, PGO, 4th core, as bo2 `docs/technical.md`.
6. **Campaign** (`default.xex`, `mw2-recompiled/config/MW2.toml`).
7. **Packaging (installable NSP)**, only once the whole game (multiplayer and campaign) is done: the program ships
   as an NSP with no game data in it. On first start it creates its folders on the SD card (`game/`, `update/`, `user/`, each with a short "put X here" note) and, while files are
   missing, shows a screen listing what to copy where. Players stage from their own disc and TU6 on the PC
   (`tools/install/tu6_setup.py`, `tools/stage_sd_mw2mp.py`) and paste onto the SD card, as the GTA V Switch port
   handles its game files. Open points: title ID, NPDM (`npdmtool`) and NSP build (`hacbrewpack`, which needs the
   builder's own console keys); a full application gets more memory than an NRO run as an applet.

## Status

- [x] Repo, manifest, overrides seeded from mw2-recompiled `config/MW2MP.toml`
- [x] 1 host tools (Linux/WSL)
- [x] 2 codegen: `tools/codegen_mw2mp.sh`. 19,796 functions, 61 files; one tail-branch target declared; 6
  read-before-write reports checked by hand (KNOWN); 14/14 setjmp sites protected. direct_calls/localize wait for hooks.
- [ ] 3 Switch first light: `mw2mp/build_switch.sh` builds `mw2mp.nro` (38.5 MB; stock ReXApp, Xenos GPU, -O2, no
  LTO); `tools/stage_sd_mw2mp.py` lays out `switch/mw2mp/{game,update}`. Not yet run on a console.
