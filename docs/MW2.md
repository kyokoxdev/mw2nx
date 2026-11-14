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
5. **Perf**: LTO, PGO, 4th core, as bo2 `docs/technical.md`; then direct calls, locals, native hot paths,
   D3D-level hooks (plan in Status, milestone 5).
6. **Campaign** (`default.xex`, `mw2-recompiled/config/MW2.toml`).
7. **Packaging (installable NSP)**, only once the whole game (multiplayer and campaign) is done: the program ships
   as an NSP with no game data in it. On first start it creates its folders on the SD card (`game/`, `update/`, `user/`, each with a short "put X here" note) and, while files are
   missing, shows a screen listing what to copy where. Players stage from their own disc and TU6 on the PC
   (`tools/install/tu6_setup.py`, `tools/stage_sd_mw2mp.py`) and paste onto the SD card, as the GTA V Switch port
   handles its game files. Open points: title ID, NPDM (`npdmtool`) and NSP build (`hacbrewpack`, which needs the
   builder's own console keys); a full application gets more memory than an NRO run as an applet. NPDM allows cores
   0-3 (`highest_cpu_id: 3`, as sphaira's "CPU Cores 4" forwarder), so the fourth core (`mw2mp_cpu_cores`) works
   from the NSP without a forwarder.
   - One app, one title ID, for both modes: it asks Multiplayer or Campaign at start (today two NROs, `mw2mp.nro`
     and `mw2sp.nro`, each in its own `sdmc:/switch/` folder because hbmenu shows one NRO per folder). Needs both
     recompiled images in one program (mw2sp builds the same sources with `mw2sp_game_remap.h` today).
   - Mode switch from inside the game: on the 360 the campaign menu's Multiplayer (and back) launches the other xex
     through `XamLoaderLaunchTitle`, which the SDK lists but does not implement. Candidate: the app restarts itself
     in the other mode (`appletRestartProgram` with the mode as the launch parameter) instead of tearing down the
     guest in place.
   - Paths: no executable folder on the SD card for an installed app. Game data already falls back to
     `sdmc:/switch/mw2mp` (`mw2mp/src/mw2mp_app.h`); the shader packs (`mw2mp_shaders.*`, `mw2sp_shaders.*`) are
     still read from `GetExecutableFolder()` (`mw2mp_native_render.cpp`) and must move to that fixed folder. They are
     built from the disc's zones, so they stay on the SD card, staged by the player, never in the NSP.

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
- [x] 4 Native renderer (`mw2mp_renderer = native`; one open bug, black textures on the first map load). Port order:
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
  - On hardware (runs 023-026, `mw2mp_renderer = native`): menu and matches draw. Fixes from those runs:
    - `k_8_8_8_8_GAMMA` targets: the pixel shader encodes Xenos's piecewise gamma (`SPEC_CONSTANT_GAMMA_OUTPUT`,
      `xenosLinearToGamma` in the translator) and the target shares plain 8_8_8_8 storage, so effects drawn over the
      scene at the same EDRAM base see it (black scene / silhouettes, run 023). Color resolves are created
      `MUTABLE_FORMAT` so they can be sampled through their sRGB twin.
    - `FrontEnd()` answers false: BO2's campaign front-end shortcuts dropped MW2's sun shadow maps in matches (run 024).
    - `RequestBootClocks()` on `OnPostInitLogging`: GPU 460 MHz handheld (`mw2mp_switch_gpu_mhz`), was 307.2.
    - Build: plain guest loads/stores by default (`MW2_GUEST_MEMORY_PLAIN=0` for volatile); `MW2_LTO=1` opt-in (link
      needs more RAM than a 9 GB WSL).
  - Run 026 perf (menu, ~140 draws): GPU ~7 ms a frame, but frames of 60-180 ms; the ring thread is the cost. BO2's
    CPU memexport path ran 112 M elements in 35.5 s since boot; two 1280x720 resolves a frame nobody reads.
    Matches ran 22-42 fps (swaps per 20 s), lowest during texture streaming.
    - Memexport: MW2's streaming copy shaders hash differently from BO2's, so all ran on the interpreter (0.32 us per
      element). Unknown memexport VS are now tried as BO2's direct 64-/128-byte copy and adopted after 16 draws equal
      to the interpreter (log: `memexport VS ...: direct N-byte copy`). Not yet run.
  - Run 027: shadows slid with the player. D3D clears the 1024x2048 sun map as `[960,1024)` on the 1040-wide 1X
    surface plus `[0,480)` on a 520-wide 4X alias (spot atlas: 560 1X + 280 4X), all at depth base 0. `ClearD3D`'s
    alias lookup kept one surface per base, so each map's alias met the other map's surface, failed the samples-per-row
    test and cleared nothing: old casters stayed at their shadow texels. Now keyed by base and samples per row.
    Open: hitches when turning and the movement desync/replay (listen server starved?) need a `rex_profile.log` with
    stacks from the current build (the one in `debug/rex/` is older).
  - Run 028 (mp_rust ~30 fps, mp_terminal 18-26 fps): with stacks, the game threads (Main, Worker0-2, Backend,
    Server) sat 60-84% of their samples in `mutexLock` on the codegen's global lock: `mfmsr`/`mtmsrd r13` around
    every lwarx/stwcx. atomic (330 sites, all pure CAS loops) became `REX_ENTER/LEAVE/CHECK_GLOBAL_LOCK`, one
    recursive mutex. bo2 hit the same and set `skip_msr = true`; now in `mw2mp/overrides.toml`. Not yet run.
    mp_terminal's first visit also compiled ~386 new pipelines over ~85 s (compile thread 40-64% CPU on 3 saturated
    cores); they are saved (prewarm list 511 -> 897), so later visits start warm.
  - Run 029 (`skip_msr`): ~50 fps (was ~30); the ring is now the busiest thread (75-100 %). The remaining hitches
    (~250 ms frames, felt as 300+ ms ping) had the ring in `ShaderLibrary::ReadAt` -> `fsdev_read`: each shader met for
    the first time in a session had its SPIR-V read from the SD card on the ring, queued behind the game's 256 KB
    image reads (~10 ms each). Now read on a "BO2 SPIR-V reads" thread (`SpirvOf`/`SpirvLoop` in
    `mw2mp_native_draws.cpp`) and the draw is skipped until it arrives, as for a compiling pipeline (log:
    `C6 SPIR-V read off the ring`). Run 030: 2-5 ms per read, no ring stalls; hitches down to ~80-100 ms.
  - Run 030, desync still felt: the Server thread spent 3-4 s of every 10 s inside `recvfrom` on its idle socket
    (`[net]`: ~100 calls/s, all empty, 2-5 ms each in the socket service; 0.5 ms at boot). BO2's 1 ms idle window
    answered ~4 % (MW2 polls >1 ms apart). New watch in `sdk/src/system/xsocket.cpp` (`switch_net_watch_ms`, 5): an
    empty socket answers EWOULDBLOCK locally until a watcher thread blocked in `poll()` sees data. The `[net]` line's
    "answered from the idle window" should now be nearly every call. Run 032 crashed at start: the watcher thread was
    `detach()`ed, which throws `std::system_error` on libnx (std::terminate at the first empty recvfrom); now leaked
    instead, and a failed thread falls back to the idle window. If the desync stays, BO2's other
    finding applies: its Server thread needed ~46 % of a core and fell behind on three busy cores (core 3 forwarder,
    `bo2zm_core3_threads`).
  - Runs 033-035: plays well (matches smooth, desync no longer felt). Changes:
    - `ClientInGame()` reads MW2's own `cl_ingame` dvar (dvar_t* at `0x825A590C`, value byte at +12), no hook.
    - Vertex/index caches and the constants shortcut on (`CachesOff`/`ConstantsShortcutOff` false); texture
      write-stamp shortcuts stay off until MW2's VMX copies and plain-store writers stamp pages. Page stamps moved to
      `mw2mp/src/mw2mp_page_writes.cpp` (bo2's, renamed).
    - First map load came out black: draws skipped while their shader's SPIR-V was read (run 033: 200 shaders, 1,175
      draws) or their pipeline compiled (run 035: ~100-160 pipelines at 300-470 ms, 12,000-39,000 draws skipped per
      10 s). `mw2mp_native_load_wait_s` (5): outside a match and for 5 s after `cl_ingame`, the ring reads missing
      SPIR-V at once. `mw2mp_native_load_hold_s` (60): after `cl_ingame`, frames with skipped draws are submitted but
      not shown (`HoldOutput`), so the loading image stays up until the first complete frame. Prewarm hands its
      shader modules to the ring (`HandModule`) instead of the ring re-reading them.
  - Open: black textures still show on the first map load of a session. Load hold isn't enough on its own, possibly
    textures rather than shaders/pipelines (texture shortcuts off, so not a stale stamp). The 50 s freeze (Xenos run)
    needs stack samples. D3D-level hooks (perf).
- [~] 5 Perf. Order below = payoff per effort; each step measured with `mw2mp_benchmark` before the next.
  - [x] -O3 (`build_switch.sh`), 4th core (`mw2mp_cpu_cores` 4 from a 4-core forwarder or the NSP).
  - [x] LTO: `MW2_LTO=1` builds and links in 15 GB WSL (+8 GB swap; link ~7 min, no swap used): `mw2mp.nro`
    45.5 MB. Not yet measured on hardware (bo2: +10 %).
  - [~] PGO: `MW2_PGO=generate` training build (`out/switch-pgo`, 46.5 MB) dumps gcov at the end of the
    `mw2mp_benchmark` window to `sdmc:/switch/mw2mp/pgo`; `MW2_PGO=use MW2_LTO=1` is the final build. Train only after
    the codegen-level steps below: a profile goes stale whenever the generated code changes.
  - [ ] Direct calls (`tools/direct_calls.py`, not yet run for MW2). Every `sub_X` is a weak alias of `__imp__sub_X`
    so a hook can replace it, and GCC never inlines through a weak alias, not even with LTO: the 54k direct calls in
    the generated code are all opaque today. The tool rewrites calls to unhooked functions to `__imp__sub_X`. mw2mp
    has no live hooks (the three BO2 shader hooks are dead), so nearly every call qualifies. Add to
    `tools/codegen_mw2mp.sh` and `tools/codegen_mw2sp.sh`, rebuild, measure.
  - [ ] Locals (`tools/localize.py`): more guest registers as C++ locals in the hottest functions, from a list made
    from profiles (bo2's `app/locals.txt`). Needs the profiles from the next step.
  - [ ] Profiles on hardware (`pc_profiler`, `tools/profile_sym.py`, `tools/pc_profile_report.py`): stack samples of
    every game thread in a match, symbolised to `sub_X`. Drives the next three steps.
  - [ ] Native hot paths, as bo2 (culling, entity scan, sound mixer, LZX decoder, CPU skinning, limiter): MW2's
    equivalents from the profile, replaced with `[rexcrt]` or `REX_HOOK_RAW` as memcpy/memset are.
  - [ ] Texture write stamps: hooks on MW2's VMX copy routines and plain-store writers mark changed pages, so the
    texture shortcuts (re-hash/re-upload skipped for unchanged pages) can come on.
  - [ ] GPU skinning: bo2's port (stubbed off in `mw2mp_native_stubs.cpp`).
  - [-] D3D-level hooks: DROPPED (2026-11-12): bo2's full Direct3D-level renderer gained about +2 % at best and was
    not adopted (bo2 `docs/technical.md`, "Tried and not adopted"). Was: the ring thread is the busiest (75-100 %, run 029). bo2's D3D marker hands the renderer
    what the game's D3D calls set instead of decoding it back out of PM4. Needs MW2's D3D functions mapped (device
    state setters near `0x82169A00`/`0x82169DE0`, constants `0x82174868`, flush `0x821752D0`; the campaign's
    `0x8211AFC0`, `0x82122E60`, `0x821238C8` by instruction shape). Biggest job, biggest lever on the renderer.
- [~] 6 Campaign (`mw2sp/`, TU6 `default.xex`): same program as mw2mp (native renderer, native CRT, app, settings),
  `sdmc:/switch/mw2sp/mw2sp.nro` + `mw2sp_shaders.*` in their own folder (hbmenu shows one NRO per folder), using
  `sdmc:/switch/mw2mp`'s game/, update/, user/ and `mw2sp.toml`; not yet run on hardware
  - `default.xex` comes off the same disc as `default_mp.xex`; `tools/install/tu6_setup.py` now copies both xex and
    requires `default.xexp` in the update, `tools/stage_sd_mw2mp.py` stages `mw2sp.nro` too. `xexdump work/load_tu6
    default.xex work/dump_tu6` applies `default.xexp` (0.0.0.10 -> 0.0.6.10): base 0x82000000, entry 0x82370940,
    32.5 MB image, `.text` 0x82090000 + 3,546,484.
  - `mw2sp/overrides.toml` seeded from mw2-recompiled `config/MW2.toml`: setjmp `0x823DFF10`, longjmp `0x823E08C0`,
    12 function extents (one branch ladder, 11 jump-table leaves), same register-local options and `skip_msr = true` as
    mw2mp (328 mfmsr / 327 lwarx sites, same CAS-loop shape). Two unresolved tail branches declared standalone:
    `0x82091758` (thunk table, same as mw2mp's `0x820A1640` family) and `0x82267FC8` (the campaign's twin of mw2mp's
    `0x82368AE8`). `[rexcrt]`: memcpy `0x823DE1F0`, memset `0x823DE090` (same shapes as mw2mp's; keys stay
    `mw2mp_*` so `mw2mp_crt.cpp` builds unchanged).
  - `tools/codegen_mw2sp.sh`: 19,108 functions (37 from `mw2sp/gaps.toml`, `tools/codegen_gaps.py --text
    82090000:823F1D74 --image-size 32505856`, report in `work/dump_tu6/codegen_gaps_mw2sp.txt`), 59 files; 6
    read-before-write reports, all KNOWN (two `subfe rN,rN,rN`, three VMX copy kernels, setjmp itself); 14/14 setjmp
    sites protected.
  - Sharing: `mw2mp/mw2mp_sources.cmake` lists mw2mp's sources (all but `main.cpp`); `mw2sp/CMakeLists.txt` builds
    them with `-include mw2sp/src/mw2sp_game_remap.h`. What mw2mp/src takes from `default_mp.xex` by address is only
    the `cl_ingame` dvar and the CRT pair: its three D3D shader hooks (`sub_8259BC90`, `sub_8259C038`, `sub_825A2FB8`)
    are BO2 addresses inside MW2's `.data` (no function in either program), so nothing to remap. The campaign has no
    `cl_ingame`: `sv_running` stands in (`MW2_IN_GAME_DVAR`, dvar_t* at `0x833FDB58`, registered by `sub_822E15D8`).
    Its own shader library `mw2sp_shaders.spvpack` (`MW2_SP=1 tools/mw2shaders/build_mw2mp_shaders.sh <work>`: every
    disc zone that is not multiplayer) and pipeline cache folder `cache_sp`; `mw2sp.toml` for its settings.
  - To check on hardware: the main menu (does `sv_running` stay 0 there, or does the menu run a level?), first level
    load with the load hold, then the level itself.
  - Runs mw2sp_001-003: 001 crashed on `sdmc:/` paths (now rooted `/switch/...`), 002 on `0x8209CFC8` (overrides).
    003 reached the menu and the first level's intro, all at 15 fps (title intro too): no `mw2sp.toml`, so
    `mw2mp_renderer` stayed at its default `xenos` and the Xenos emulation drew (53-61 emulated draws a frame, fences
    waited ~840 ms of every second, 230k emulated read faults/s while the Bink "Cinematic" thread ran at 85 %).
    `mw2mp_renderer` now defaults to `native` (a fresh install has no toml either). Open: the Cinematic thread's cost
    (stack samples: `sdmc:/switch/mw2sp/logs/rex/profile_stacks.flag`), and whether `sv_running` turns on at the
    start of a load (the load hold would then end during the loading movie, before the level's pipelines compile).
  - Run mw2sp_004 (native renderer, first run: empty `cache_sp`): S.S.D.D. ~30 fps (ring thread 100 %, 2.3-3.5 us a
    draw, ~6,400 draws a frame against mp_afghan's ~3,400; GPU ~28 ms a frame), the next mission 15-20 fps (10k
    draws, 953k triangles, GPU 40 ms a frame at 768 MHz, CPU 330 %). `sv_running` did turn on at the start of
    trainer.ff's load and never went off between missions: the load hold ended inside the loading movie, never ran
    for mission 2, and pipelines were saved only at start and on quit. Mission 2 then compiled its ~470 new pipelines
    with the game running: 613 ms each, 17 per 10 s, 11,707 draws skipped per 10 s. Fixes: `MW2_CGAME_FLAG`
    (`0x82648A8A`, CL_InitCGame `sub_821208D8` sets it, CL_ShutdownCGame `sub_8211FF88` clears it) gates
    `ClientInGame` per level; in the campaign a held frame waits for its compiles (`WaitCompilesHeld`: the game
    stops behind the ring, a longer first-visit load) with two compile threads; the load window covers the hold.
    Open: what fills mission 2's GPU frame (`mw2mp_native_per_draw_stats_s` in `mw2sp.toml`), the light flicker in
    S.S.D.D. (needs captures), and whether the game tolerates the ring held for tens of seconds (60 s cap).
  - FXAA on by default in the campaign (`mw2mp_antialiasing`; the native renderer has no MSAA).
  - Graphics presets (`mw2mp/src/mw2mp_graphics_preset.cpp`, `mw2mp_preset` = high/medium/low/very_low, campaign
    default very_low): the game's own quality dvars (sm_*, r_dlightLimit, r_dof_enable, r_distortion, fx_drawClouds,
    fx_marks*, ai_corpseCount, cg_brass, dynEnt_explodeMaxEnts) found by name in the dvar pool (`MW2_DVAR_POOL`
    `0x8393DAB0`, 72-byte dvar_t, count at `MW2_DVAR_COUNT` `0x83938A90`) and written in place, held every frame below
    high; texture detail through the renderer's mip floor. Multiplayer names no pool yet (no-op there).
    `mw2mp_preset_test_s` steps through them for an A/B in one run.
  - LTO and PGO for mw2sp (`MW2_LTO=1`, `MW2_PGO=generate|use`, profile in `data/pgo-sp`, written to
    `sdmc:/switch/mw2sp/pgo` at the end of the `mw2mp_benchmark` window). Native rewrites wait for a profile of the
    LTO build with its ELF (`debug/elf/`); candidates from mp run 040: the VMX bounds/plane culling loop (mp
    `sub_8240DDA0`, sp `sub_823D1598`, 3.5 % on Main+Backend) and the worker-command dispatcher's polling (mp
    `sub_823F0870`, 3.3 % on Worker2; sp's three Worker threads took ~60 % in run mw2sp_004).
  - Run mw2sp_006 (LTO, preset low, 3 cores, GPU 768 MHz): playable, but frame times spread over 16-50 ms inside every
    20 s window. Light stretches hold 60 (ring ~590 ms/s, GPU ~12 ms a frame); mid stretches mix 60 with 20-30
    (ring ~980 ms/s, GPU 16-19 ms); the heaviest run 18-25 fps GPU-bound (real GPU 36-51 ms a frame, nearly all of it
    the 1024x600 scene pass: 7-8k draws, 0.7-1.3 M triangles; the ring at ~1000 ms/s too). CPU ~300 % of 3 cores.
    The r_*LodDist dvars are read only in `sub_82392EE0`, which hands each to `sub_82300BD0` per LOD
    level; not confirmed as a LOD control, so not in the presets.
    Added adaptive pacing (`mw2mp_frame_pacing`, campaign default auto, `[pacing]` log lines): interval 2 where most
    frames miss 60 but fit 30 and the frame's own cost (ring without its waits, or GPU) says so, back to 0 where 60
    fits (`mw2mp_frame_pacing_60_ms`, default 15) or 30 does not (over 15 % of frames past 36 ms).
  - Campaign preset default now very_low, and the presets set a draw distance: `r_zfar` ("the distance at which
    culling fog reaches 100% opacity; 0 is off", float 0 to FLT_MAX, registered in `sub_823A2F98`), 10000 / 6000 /
    4000 units for medium / low / very_low, the level's own at high, as BO2 cut its draws with r_zfar.
    `mw2mp_draw_distance` overrides it (-1 the preset's, 0 the level's own). The preset writer takes float dvars now.
  - Model detail (BO2's other draw lever): TU6 registers no r_lodScale dvars, and r_highLodDist and its siblings
    feed `sub_82300BD0`, an empty function. XModelGetLodForDist is `sub_82300B50` (campaign) / `sub_8232E1C0` (mp):
    model r3, ramp distance f1, plain distance f2; versions from maxLoadedLod (+224) to numLods (+225), switch
    distances at +64 (40-byte XModelLodInfo, numsurfs u16 at +68), radius +244, bounds +248 (checked against the getters
    beside it). The presets hook it (`mw2mp_graphics_preset.cpp`) with BO2's rules: the ramp distance x 1 / 1.5 / 2 / 4
    (high ... very_low, `mw2mp_model_detail` overrides), never better than the game's pick, never hiding a model the
    game draws, long models measured from their nearest point, the second simplest kept when the simplest has fewer
    surfaces. `[preset] model detail` logs picks every 10 s.
  - Multiplayer presets: its dvar pool is `0x837A5890` (count `0x837A0868`, Dvar_RegisterNew `sub_8230EAA0`), so
    `mw2mp_preset` now works there too (default still high: nothing changes unless chosen).
