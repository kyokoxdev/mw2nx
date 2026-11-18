# mw2nx: plan and status

Call of Duty: Modern Warfare 2 (Xbox 360) on the Nintendo Switch by static recompilation: ReXGlue v0.10.0 recompiles
the PowerPC executable to C++, a Horizon runtime layer runs it, and Vulkan on mesa-switch NVK draws it. Multiplayer is
`mw2mp/` (`default_mp.xex`), the Campaign is `mw2sp/` (`default.xex`). How it works: [technical.md](technical.md).
Building: [building.md](building.md).

The Switch is the only target: no desktop builds.

**Release 1.0.0** is out as an installable NSP (application `01F2D6C0A9E40000`: the Campaign is program 0, Multiplayer
program 1). Every milestone below is done; what is left is under [Known issues](#known-issues) and goes into 1.0.x.

## Inputs (yours, never committed)

- Xbox 360 disc ISO, version 1.0.557 (USA/Europe).
- Title update 6 (`TU_10LC20N_0000018000000.00000000001GA`, 1.6 MB), fetched at a pinned commit from the codxenon
  xbox360-title-updates archive. `python tools/install/tu6_setup.py` checks the ISO in `iso/`, then downloads TU6 (or
  uses one already in `iso/`) and fills `work/load_tu6/` with both executables from the disc plus the update's `.xexp`
  and `.ff` files. Game files are never redistributed.

## Host (WSL2, no sudo)

- clang/lld 21 from Ubuntu's debs, `dpkg -x`'d into `~/tools/clang`, used by `tools/build_host_linux.sh`.
- X11/Wayland dev files the same way into `~/tools/devroot`.
- devkitA64 in `~/devkitpro` (GCC 15.2).
- Mesa/NVK by `tools/build_mesa_linux.sh`: host LLVM 21 dev, libclc, SPIRV-LLVM-Translator and SPIRV-Tools debs, rustup
  target `aarch64-unknown-linux-gnu`, `cargo install bindgen-cli cbindgen`. Vulkan only.
- Link-time optimization fits a 15 GB WSL with 8 GB swap (the link takes about 7 minutes and uses about 2 GB).

## Milestones

1. **Host tools**: `tools/build_host_linux.sh` builds `rexglue` and `xexdump`.
2. **Load + codegen**: `tools/codegen_mw2mp.sh` (codegen, then the fix-up passes). Unresolved branches go in
   `mw2mp/overrides.toml`.
3. **Switch first light**: Horizon build (devkitA64), main menu on hardware.
4. **Native renderer**: PM4 to Vulkan with a build-time shader pack from the game's fastfiles.
5. **Perf**: LTO, PGO, the fourth core, direct calls, locals and native hot paths.
6. **Campaign** (`default.xex`).
7. **Packaging (installable NSP)**, done: one application, two programs (see below).

## Release 1.0.0

`python3 -I tools/make_nsp.py --version 1.0.0` packs the last LTO+PGO ELFs of both programs. No game data is in it.

- **One application, one title ID** `01F2D6C0A9E40000`: program 0 the Campaign (`mw2sp`), program 1 Multiplayer
  (`mw2mp`, `01F2D6C0A9E40001`), each an NSO with its own NPDM (application type 1, four cores, 39-bit). `hacbrewpack`
  v3.05 with `patches/hacbrewpack-extraprogram.patch` builds the multi-program NCAs, encrypted with the builder's own
  keys, which are never committed.
- **Mode switch** from the game's menus: `XamLoaderLaunchTitle` is `appletExecuteProgram` with the Switch user and the
  launch data (`mw2mp/src/mw2mp_launch.cpp`), so both programs share one save.
- **First start** (`mw2mp/src/mw2mp_first_start.cpp`): makes `game/` and `update/` in the game's folder, each with a "put
  X here" note, and shows a screen that lists the missing files until they are copied.
- **Paths**: the installed game uses only `sdmc:/atmosphere/contents/01F2D6C0A9E40000/` (game data, update, shader
  libraries, pipelines, caches, settings, logs) and never `sdmc:/switch/`. The shader libraries are built from the
  player's disc and stay on the SD card; per-level pipeline key lists (no game bytes) ship with the port.
- **Memory**: `__nx_heap_size = 0`, so the NSO gets all memory like a game started through `hbloader`; the profile shows
  about 3,189 MB. `application_type` must be 1: as an applet the process had 394 MB and the 31 MB image allocation failed.
- **Saves**: console save data of the application (32 MB, 16 MB journal); the pipeline caches stay on the SD card.
- **Recommended clocks** (sys-clk, profile for both program IDs): CPU 1785 / GPU 614 / memory 2133. SoC plateaus near
  45 C after 30 minutes; the ring thread is twice as fast per draw as at 1224 / 614 / 1666. Raise the GPU to 768 only
  where a log window shows the GPU's time per swap above the frame time (the invasion level).

## Known issues

- **Campaign performance.** The heaviest levels (invasion: 12-13.6k draws a frame) are limited by the renderer's ring
  thread at about 2.5 us a draw, not the GPU; frame times run 16-50 ms. Cutting draws is the lever; a 30 lock is not
  (off by default).
- **Hitches** on level loads and checkpoints in the Campaign, and a longer first load of every level while its pipelines
  compile.
- **Black textures** can show on the first map load of a session in Multiplayer, probably textures rather than shaders
  or pipelines.
- **Memory.** One heap backs GPU memory, stacks and caches; the Gulag load ran out in an early run. Watch the heap left
  in the profile log.
- **Texture write stamps** for the texture shortcuts are not finished (re-hash and re-upload of unchanged pages).

## Status

- [x] 1 Host tools (Linux/WSL).
- [x] 2 Codegen: 19,796 functions in 61 files; one tail-branch target declared; 6 read-before-write reports checked by
  hand; 14 of 14 `setjmp` sites protected.
- [x] 3 Switch first light: `mw2mp/build_switch.sh` builds `mw2mp.nro`; `tools/stage_sd_mw2mp.py` lays out
  `switch/mw2mp/{game,update}`. The stock Xenos path ran the menu at 5-6 fps, GPU-bound (about 70 draws a frame, the GPU
  commands thread waiting on fences ~820 ms of every second), which is why milestone 4 exists.
- [x] 4 Native renderer (`mw2mp_renderer = native`, the default). One open bug: black textures on the first map load of
  a session.
  - Shader library: `tools/mw2shaders/build_mw2mp_shaders.sh <work>` inflates the multiplayer zones (signed fastfiles
    interleave 8 KB of hashes every 2 MB), takes each shader's container and physical part, then XenosRecomp, `dxc`,
    `spirv-val` and the packer. 7,626 shaders (612 vertex, 7,014 pixel), all translated and validated, 315 MB.
  - Translator changes (marked in `tools/xenosrecomp`): constants the shader's table does not declare read the stage's
    raw constant file (also for relative addressing); booleans by their 0-255 number; 1D fetches through the 2D heap.
    1D textures are drawn as one-row 2D images.
  - `[rexcrt]`: MW2's `memcpy` (`0x8241A9F0`) and `memset` (`0x8241A890`) run as native code (`mw2mp_crt.cpp`, 1,050
    call sites).
  - Fixes from the first hardware runs: gamma-encoded targets (`k_8_8_8_8_GAMMA`, black scene and silhouettes);
    `FrontEnd()` answers false (the front-end shortcuts dropped the sun shadow maps); GPU 460 MHz handheld
    (`mw2mp_switch_gpu_mhz`); shadow clears keyed by base and samples per row (shadows slid with the player).
  - Run 028: the game threads sat 60-84 % of their samples in `mutexLock` on the codegen's global lock. `skip_msr` drops
    the MSR pair around the 330 compare-and-swap loops. Run 029: about 50 fps (was about 30).
  - Run 029-030: each shader met for the first time had its SPIR-V read on the ring, queued behind 256 KB image reads.
    Now read on its own thread and the draw skipped until it arrives; hitches fell to about 80-100 ms.
  - Run 030-032: the Server thread spent 3-4 s of every 10 s inside `recvfrom` on its idle socket. A watcher thread in
    `sdk/src/system/xsocket.cpp` (`switch_net_watch_ms`) answers empty sockets locally. The watcher thread was
    `detach()`ed at first, which throws on libnx and terminated the program; it is now leaked.
  - Runs 033-035: plays well, matches smooth, desync no longer felt. `ClientInGame()` reads the game's own `cl_ingame`
    dvar (`0x825A590C`). `mw2mp_native_load_wait_s` (5) reads missing SPIR-V at once outside a match and for 5 s after
    `cl_ingame`; `mw2mp_native_load_hold_s` (60) keeps the loading image up until the first complete frame.
  - Open: black textures still show on the first map load of a session, probably textures rather than shaders or
    pipelines. The 50-second freeze on the Xenos path needs stack samples. Texture write stamps (below).
- [x] 5 Perf (1.0.0). Order = payoff per effort; each step measured with `mw2mp_benchmark` before the next.
  - [x] `-O3`, the fourth core.
  - [x] LTO (`MW2_LTO=1`): `mw2mp.nro` 45.5 MB.
  - [x] PGO: `MW2_PGO=generate` dumps gcov at the end of the `mw2mp_benchmark` window; `MW2_PGO=use MW2_LTO=1` is the
    release build. A profile goes stale whenever the generated code changes: retrain, then build.
  - [x] Direct calls (`tools/direct_calls.py`, run by `tools/codegen_mw2mp.sh` and `tools/codegen_mw2sp.sh`, checked by
    both `build_switch.sh`): 54k calls to unhooked functions are no longer opaque.
  - [ ] Locals (`tools/localize.py`): more guest registers as C++ locals in the hottest functions, from a list made from
    profiles.
  - [~] Profiles on hardware (`tools/profile_sym.py`, `tools/pc_profile_report.py`): stack samples of every game
    thread, symbolised to `sub_X`. A campaign profile found the Mesa pipeline-cache stalls; a match profile for
    multiplayer is still to do.
  - [x] Native hot paths: CPU skinning was tried and dropped (the NEON kernels ran slower than the game's and skinning is
    in no thread's top 25 samples). Shipped: the bounds-tree frustum cull (`mw2mp_native_cull.cpp`), checked against
    the game's at run time. Candidates from a multiplayer profile: the VMX bounds and plane culling loop (`sub_8240DDA0`
    in multiplayer, `sub_823D1598` in the campaign, 3.5 % on Main and Backend) and the worker-command dispatcher's
    polling (`sub_823F0870`, 3.3 % on Worker2; the campaign's three Worker threads took about 60 % in run mw2sp_004).
  - [ ] Texture write stamps: hooks on MW2's VMX copy routines and plain-store writers mark changed pages, so the texture
    shortcuts (re-hash and re-upload skipped for unchanged pages) can come on.
  - [-] GPU skinning: dropped, skinning is negligible on MW2's CPU.
  - [-] Direct3D-level hooks: dropped (2026-11-12). A Direct3D-level renderer gained about 2 % at best.
- [x] 6 Campaign (1.0.0) (`mw2sp/`, TU6 `default.xex`): the same program as mw2mp (native renderer, native CRT, app, settings),
  in `sdmc:/switch/mw2sp/` with its own shader library, using `sdmc:/switch/mw2mp`'s `game/`, `update/` and `user/`.
  Runs on hardware (run mw2sp_025 played through Oilrig to the start of Gulag). Open: load and checkpoint hitches, heap
  headroom (out of memory at the Gulag load in run 022).
  - `default.xex` comes off the same disc as `default_mp.xex`; `xexdump work/load_tu6 default.xex work/dump_tu6` applies
    `default.xexp` (0.0.0.10 to 0.0.6.10): base `0x82000000`, entry `0x82370940`, 32.5 MB image.
  - `mw2sp/overrides.toml`: `setjmp` `0x823DFF10`, `longjmp` `0x823E08C0`, 12 function extents, `skip_msr = true`,
    `[rexcrt]` `memcpy` `0x823DE1F0` and `memset` `0x823DE090`, and two tail branches declared standalone (`0x82091758`,
    `0x82267FC8`).
  - `tools/codegen_mw2sp.sh`: 19,108 functions (37 from `mw2sp/gaps.toml`), 59 files; 14 of 14 `setjmp` sites protected.
  - Sharing: `mw2mp/mw2mp_sources.cmake` lists mw2mp's sources; `mw2sp/CMakeLists.txt` builds them with
    `-include mw2sp/src/mw2sp_game_remap.h`. The campaign has no `cl_ingame`: `MW2_IN_GAME_DVAR` (`sv_running`, dvar at
    `0x833FDB58`) stands in, and `MW2_CGAME_FLAG` (`0x82648A8A`, set by CL_InitCGame `sub_821208D8`, cleared by
    CL_ShutdownCGame `sub_8211FF88`) gates `ClientInGame` per level.
  - Run mw2sp_003: no `mw2sp.toml` meant the Xenos emulation drew at 15 fps; `mw2mp_renderer` now defaults to `native`.
  - Run mw2sp_004: the first level about 30 fps, the second 15-20 fps (10k draws, 953k triangles, GPU 40 ms a frame at
    768 MHz). The second level compiled about 470 pipelines while running (613 ms each, 11,707 draws skipped per 10 s).
    A held frame now waits for its compiles in the campaign (`WaitCompilesHeld`), with two compile threads.
  - FXAA on by default in the campaign (the native renderer has no MSAA).
  - Graphics presets (`mw2mp/src/mw2mp_graphics_preset.cpp`, `mw2mp_preset` = `high`, `medium`, `low`, `very_low`,
    default `very_low` in both programs): the game's own quality dvars found by name in the dvar pool (`0x8393DAB0`,
    72-byte `dvar_t`, count at `0x83938A90` in the campaign; `0x837A5890`, count `0x837A0868` in multiplayer) and written
    in place, held every frame below `high`; texture detail through the renderer's mip floor. `mw2mp_preset_test_s` steps
    through them for an A/B in one run.
  - Draw distance: `r_zfar` at 10000, 6000 and 4000 units for `medium`, `low` and `very_low`, the level's own at `high`;
    `mw2mp_draw_distance` overrides it.
  - Model detail: `XModelGetLodForDist` is `sub_82300B50` (campaign) and `sub_8232E1C0` (multiplayer). The presets hook
    it: the ramp distance times 1, 1.25, 1.5 and 2.5 from `high` to `very_low` (`mw2mp_model_detail` overrides), never better
    than the game's pick, never hiding a model the game draws, long models measured from their nearest point.
  - Run mw2sp_006 (LTO, preset `low`, three cores, GPU 768 MHz): playable, frame times 16-50 ms inside every 20 s
    window. Light stretches hold 60; the heaviest run 18-25 fps GPU-bound (7-8k draws, 0.7-1.3 M triangles in the
    1024x600 scene pass). Added adaptive pacing (`mw2mp_frame_pacing`); it is off by default since 2026-11-13: every lock let go within 2-10 s and 7-40 % of the locked frames showed for 50 ms.
  - LTO and PGO for mw2sp use the same variables (`MW2_LTO=1`, `MW2_PGO=generate|use`, profile in `data/pgo-sp`).
  - Runs mw2sp_012 to mw2sp_020 (the release candidates), in order:
    - Run 012 (invasion): ring thread at 100 %, 12-13.6k draws a frame at about 2.5 us each at `very_low`. `very_low` now
      hides small static props (`mw2mp_model_cull_reach`, `XModelGetLodForDist` called from the static model loops only;
      characters, weapons, vehicles, scene and brush models never). Vertex ranges in big buffers are cached in 64 KB
      chunks (`mw2mp_native_vertex_cache_chunk_kb`).
    - Run 014 (`favela_escape`): the cull at reach 64 hid about 1 % of the draws with no visible pop-in; Worker2 spent
      49 % of its busy time in the job-queue scan `sub_823B7430`, whose stwcx. write-back of an unchanged hint is a full
      atomic on a hot line once recompiled. It runs natively without it (`mw2mp_native_job_scan`); the multiplayer
      twin of the model-cull callers is `sub_823ECDD8` and its three neighbours. MW2's unnamed XAudio mixer thread is
      raised to the audio priority by its guest start address.
    - Run 016: model detail 3 with reach 128 showed pop-in, so the `very_low` factor is 2.5 (1, 1.25, 1.5, 2.5 from
      `high`) and the Campaign's scene scale 80 %. Run 018 (gulag, dcburning): 55-63 fps mostly; the scene scale
      settled at 75 % (Campaign) and 70 % (Multiplayer).
    - Run 018-020: the lighting volume's pages come from the one writer, `sub_8239F480` (multiplayer `sub_823F7DB8`),
      hooked in `mw2mp_volume_writes.cpp`, instead of hashing the volume every update (XXH3 was 4.2 % of the ring thread
      in the Campaign, 2.1 % in Multiplayer). The first 300 updates, then one in 64, also hash every page; a page
      changed without a mark switches the marks off for the run. Found with the `MW2_WATCH_STORES` build.
    - Run mw2mp_004 (installed): about 30 fps GPU-bound at the stock 1020 / 307 / 1331 clocks, because sys-clk had no
      profile for Multiplayer's program ID.
