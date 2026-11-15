# Technical documentation

How mw2nx works, what was optimized, and where to go to build it.

| Page | What it covers |
|---|---|
| This page | The pipeline, code generation, hooks, the runtime, the renderer, optimizations, testing |
| [building.md](building.md) | Building from source, every step and option |
| [features.md](features.md) | Every setting and the TOML files |
| [script-mods.md](script-mods.md) | Script mods: folders, functions, Bot Warfare |
| [MW2.md](MW2.md) | The plan, the run-by-run log and what is still open |

Elsewhere: [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) lists every upstream project and its license;
`sdk/README.md`, `tools/README.md` and `patches/README.md` describe their folders.

## The pipeline

The Xbox 360's PowerPC code is translated ahead of time into C++, compiled for the Switch's ARM cores and linked with a
runtime that provides what the 360's kernel and hardware did. The GPU is not emulated: a renderer of its own reads the
game's GPU commands and draws them with Vulkan.

```
 your disc + TU6          build time (PC)                              run time (Switch)
 default_mp.xex  ──▶  loader: decrypt, decompress, patch        mw2mp.nro
 default_mp.xexp      code generator: 19,796 functions   ──▶    ├ recompiled game code
                      fix-up passes → C++ → GCC (LTO, PGO)       ├ runtime: kernel, memory, threads, files, audio
 fastfiles (.ff) ──▶  shaders → XenosRecomp → dxc         ──▶   ├ native renderer → Vulkan (NVK)
                      → mw2mp_shaders.spvpack                    └ Switch layer: threads, cores, display
```

Multiplayer lives in `default_mp.xex`; the Campaign is `default.xex` (19,108 functions), recompiled the same way as a
second program (`mw2sp`) that builds the same sources. Both target Title Update 6.

**No Direct3D translation.** On the Xbox 360 the game's Direct3D layer is part of the game and drives the Xenos GPU with
PM4 command packets and shader microcode. That layer is recompiled like the rest; the renderer reads the stream it
writes and calls Vulkan itself, through NVK. The SDK's exact Xenos emulation (`mw2mp_renderer = xenos`) remains as a
reference image, but it ran the menu at 5-6 fps and is not used.

## Code generation

The SDK's loader (from Xenia) decrypts the XEX2 image, decompresses it and applies the update's delta patch. ReXGlue's
code generator (`sdk/src/codegen`, driven by `mw2mp/mw2mp_manifest.toml`) analyzes the image from the entry point,
exports, call targets and jump tables, then emits one C++ function per PowerPC function: 19,796 in 61 files for
Multiplayer. Guest memory is one base pointer with byte swaps; VMX128 maps to SIMDe (NEON on ARM).

`mw2mp/overrides.toml` holds what the analysis cannot find: function boundaries, tail-branch thunks, the `setjmp` and
`longjmp` addresses and register options. `mw2mp/gaps.toml` lists code no function covers. The non-volatile registers
are C++ locals, so the compiler keeps them in host registers.

**Fix-up passes**, run in order by `tools/codegen_mw2mp.sh`:

| Pass | Why |
|---|---|
| `read_before_write.py --strict` | Finds functions that read a register before writing it; they must share registers |
| `setjmp_locals.py --fix` | `longjmp` error recovery must restore the register locals |
| `guest_barriers.py` | Compiler barriers that keep plain guest memory accesses correct |
| `direct_calls.py` | Direct calls to unhooked functions, so the compiler can inline them |

**Atomics.** The game wraps every `lwarx`/`stwcx.` loop in `mfmsr`/`mtmsrd`. The generator turned each into a global
recursive lock, and the game's threads spent 60-84 % of their samples waiting on it. `skip_msr = true` in the overrides
drops the MSR pair around those 330 pure compare-and-swap loops, which took matches from about 30 to about 50 fps.

## Hooks

Each generated function is `__imp__sub_<address>`, called through a weak alias `sub_<address>`. A hook is a C++
function of that name, which the linker prefers; it can still call the original. `direct_calls.py` rewrites calls to
unhooked functions to the `__imp__` name, because GCC never inlines through a weak alias, not even with LTO.
`[rexcrt]` entries in the overrides replace the game's `memcpy` and `memset` with native code at their 1,050 call
sites. A rewrite of game code first runs beside the original and compares their outputs; a difference turns it off.

## The runtime

The ReXGlue SDK (`sdk/`) carries Xenia's implementation of the 360's kernel and libraries, plus a Horizon layer.

- **Memory**: Horizon has no sparse memory, so the guest window is reserved empty, committed in chunks and mapped on
  first access; the exception handler emulates accesses to unmapped pages. One newlib heap backs the GPU's memory, the
  stacks and the caches, so memory left is watched.
- **Threads**: each guest thread is a Horizon thread. libnx cannot detach a `std::thread`, so helper threads are joined
  or leaked.
- **Cores**: three by default; the fourth from a forwarder with four cores enabled (`mw2mp_cpu_cores`).
- **Audio**: the game mixes on its own threads; the runtime provides XAudio and the XMA decoder, out through `audout`.
- **Files**: `game/`, `update/` and `user/`, falling back to `sdmc:/switch/mw2mp`. Loading screens and menus read from
  the SD card, so reads that block a render thread are moved off it.
- **Sockets**: an empty socket answers `EWOULDBLOCK` locally until a watcher thread blocked in `poll()` sees data, so
  the game's server thread does not spend its time inside the socket service.
- **Input**: HID controllers as Xbox 360 controllers, with gyro aiming; the player's name comes from the Switch user.
- **Logs**: written by their own thread, so logging never waits for the SD card.

## The native renderer

The renderer's ring thread reads the PM4 packets the game writes, keeps the register state, and for every draw
identifies the shaders by hash in the shader library, finds the Vulkan pipeline, resolves the textures, uploads
vertices, indices and constants and records the draw. Resolves, fences, occlusion queries and memexport are handled the
same way.

- **Shader library**: every shader is translated at build time (XenosRecomp, then `dxc`) into one pack: 7,626 shaders
  for Multiplayer, a larger pack for the Campaign. A shader's SPIR-V is read on its own thread the first time it is
  met; the draw is skipped until it arrives, as for a pipeline that is still compiling.
- **Pipelines**: compiled on threads, cached on the SD card, saved per level and precompiled at start. Outside a match,
  and for a few seconds after the level starts, missing shaders are read at once; frames that still have skipped draws
  are held back so the loading image stays up until the first complete frame. The Campaign waits for its compiles.
- **Gamma and 1D textures**: the pixel shader encodes Xenos's piecewise gamma into the shared 8888 target; 1D textures
  are drawn as one-row 2D images.
- **Memexport**: streaming copy shaders run on the CPU; the ones that are plain copies are recognized after matching the
  interpreter 16 times and then run as direct copies.
- **Mesa patch**: the changes to NVK for this port are in `patches/` (pipeline binds, copies, texture memory,
  descriptors, SM50 shader compiler fixes).

## Optimizations

| Change | Result |
|---|---|
| Native renderer over the Xenos path | Menu from 5-6 fps to a drawn, playable game |
| `skip_msr` | Matches from about 30 to about 50 fps |
| Shader SPIR-V read off the ring | Hitches from ~250 ms to ~80-100 ms |
| Socket watcher | The server thread no longer starves; the movement desync is gone |
| `-O3`, direct calls | Every call between game functions can inline |
| LTO | Builds in 15 GB of RAM; the NRO is 45.5 MB |
| Graphics presets with draw distance and model detail | Cuts draws on every preset below `high` |
| Adaptive frame pacing (Campaign) | Holds 30 where 60 cannot be kept |

Tried and not adopted: GPU skinning and CPU NEON skinning (skinning is negligible on MW2's CPU), and Direct3D-level
renderer hooks (about +2 % at best).

In progress: PGO training (`MW2_PGO`), the bounds-tree frustum cull in native code, the worker-command dispatcher's
polling, texture write stamps and register locals for the hottest functions. The run-by-run detail is in
[MW2.md](MW2.md).

## Testing and measuring

- **Benchmark**: `mw2mp_benchmark` runs a timed window in a private match for PGO training and for comparing builds.
- **Profiler**: stack samples of every game thread (`logs/rex/profile_stacks.flag` on the SD card), symbolised with
  `tools/profile_sym.py`.
- **Logs**: each run writes a numbered console log; the renderer reports draws, pipelines compiled and skipped draws
  every ten seconds.
- **Crashes**: Atmosphère's crash report is matched to the ELF by build ID.
