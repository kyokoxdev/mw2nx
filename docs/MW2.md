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
- [x] 3 Switch first light: `mw2mp/build_switch.sh` builds `mw2mp.nro` (38.5 MB; stock ReXApp, Xenos GPU, -O2, no
  LTO); `tools/stage_sd_mw2mp.py` lays out `switch/mw2mp/{game,update}`. Multiplayer menu on hardware (run 014).
  - Menu at 5-6 fps, GPU-bound: ~70 draws a frame, the GPU Commands thread waits on fences ~820 ms of every second,
    CPU 100-150 % of 300 %, handheld clocks (CPU 1020, GPU 307 MHz). bo2 measured the same on its Xenos path (title
    screen 5-10 fps, GPU 99.7 %); the Xenos emulation is the reference, not the Switch path. Fix = milestone 4.
  - Open: after ~50 s the game stops presenting (0 fps), `Main XThread` alone spins at 100 %, everything else idle.
    Next run with `logs/rex/profile_stacks.flag` on the SD (stack samples in `rex_profile.log`) to find the loop.
- [ ] 4 Native renderer. Port order:
  1. Copy `app/src/native/render` + `bo2zm_native_system`, `bo2zm_page_writes`, `bo2zm_memcpy_callers`,
     `bo2zm_gpu_skin`, `bo2zm_d3d_native` into `mw2mp/src/native` (rename `bo2zm_` -> `mw2mp_`), build-time shader
     pack from MW2's fastfiles (XenosRecomp, `dxc`).
  2. Re-find the ~30 BO2 D3D functions the hooks name (`sub_82169DE0` SetVertexShader, `sub_82169A00`
     SetPixelShader, `sub_825A2FB8`, `sub_825A3AF0`/`sub_825A37D8` microcode, memcpy/fill callers) in MW2MP; known MP
     addresses from mw2-recompiled `docs/d3d9-seam.md`: Present `820DFD10`, ArenaWait `820E1E70`, InitPixelShader
     `820D7268`, PatchVertexFetches `820D5220`, D3D library around `820D5xxx..820Fxxxx`.
  3. Unhooked features (GPU skinning, menu blur reuse, preset, benchmark) off until the menu draws right.
  - Done: `mw2mp/src/native` (renamed copy) builds and links into `mw2mp.nro`; `mw2mp_renderer` (default `xenos`;
    `native`, `skeleton`) picks it. BO2-only callees answered in `mw2mp/src/mw2mp_native_stubs.cpp`; every write-stamp
    shortcut is off (no MW2 copy-routine hooks mark pages yet). BO2's shader-constructor hooks stay compiled out.
  - Shaders: `tools/mw2shaders/build_mw2mp_shaders.sh <work>` builds `mw2mp_shaders.spvpack` + `.spvidx` (copy next
    to the NRO; game-derived, never commit): inflates the MP zones (signed fastfiles interleave 8 KB of hashes every
    2 MB), `zone_containers` takes each shader's container + physical part with mw2-recompiled's zone walker (read
    from its checkout, `MW2_RECOMPILED`), then XenosRecomp, dxc 1.9.2609 (`~/tools/dxc`), spirv-val, bo2zm_pack.
    7,626 shaders (612 vertex, 7,014 pixel), all translated and validated, 315 MB.
  - Translator (tools/xenosrecomp, "mw2nx:" comments): constants the CTAB does not declare (MW2's engine constants)
    read the stage's raw constant file (`g_VsC`/`g_PsC`, also for relative addressing); booleans by their 0-255
    number from the full copy (`g_BooleanBit`); 1D fetches through the 2D heap (`tfetch1D`). Renderer: the shared
    block carries all 8 boolean registers (bytes 688-719).
  - 1D textures: drawn as one-row 2D images (width in the size word's low 24 bits).
  - `[rexcrt]`: MW2MP's CRT memcpy `0x8241A9F0` and memset `0x8241A890` run as native code (`mw2mp/src/mw2mp_crt.cpp`,
    1,050 call sites); they stamp pages, but the write-stamp shortcuts stay off until MW2's other vertex/texture
    writers are known.
  - Open: not run on a console; the 50 s freeze (Xenos run) needs stack samples; D3D-level hooks (perf).
