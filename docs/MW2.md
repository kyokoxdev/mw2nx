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
- Title update 6 (`TU_10LC20N_0000018000000.00000000001GA`, 2 MB), the package mw2-recompiled's launcher fetches
  from the codxenon xbox360-title-updates archive. Extract with `tools/inspect/stfs_extract.py`, put
  `default_mp.xexp` next to `default_mp.xex` in `work/load_tu6/`.

## Host (WSL2, no sudo)

- clang/lld 21: `apt-get download` of Ubuntu's `clang-21 lld-21 llvm-21 ...` debs, `dpkg -x` into
  `~/tools/clang`. Used by `tools/build_host_linux.sh` (bo2's `build_host.sh` is Windows-only; the SDK itself
  supports linux-amd64).
- X11/Wayland dev files: same trick into `~/tools/devroot` (`libx11-xcb-dev`, `libwayland-dev` and deps, `.pc`
  prefixes rewritten). Outputs: `sdk/out/linux-amd64/rexglue`, `out/host-linux/switch_bo2/xexdump`.
- devkitA64 in `~/devkitpro` (GCC 15.2; bo2 used 16.1, watch for that).

## Milestones

1. **Host tools**: `tools/build_host_linux.sh` builds `rexglue` and `xexdump`.
2. **Load + codegen**: `xexdump` on TU6 `default_mp.xex`; `rexglue codegen mw2mp/mw2mp_manifest.toml`; then bo2's
   fix-up passes (`tools/codegen.sh` order: read_before_write, setjmp_locals, direct_calls, guest_barriers).
   Resolve unresolved branches into `mw2mp/overrides.toml`.
3. **Desktop first light**: build for linux-amd64 with the SDK's generic Xenos GPU (exact emulation). Main menu.
4. **Switch first light**: Horizon build with the Xenos path or a minimal native path. Main menu on hardware.
5. **Native renderer**: port bo2's `app/src/native/render` (PM4 -> Vulkan, build-time shader pack) to MW2's D3D
   layer; mw2-recompiled `docs/d3d9-seam.md`, `rendering.md`, `shaders.md` describe MW2's side.
6. **Perf**: LTO, PGO, 4th core, as bo2 `docs/technical.md`.
7. **Campaign** (`default.xex`, `mw2-recompiled/config/MW2.toml`).

## Status

- [x] Repo, manifest, overrides seeded from mw2-recompiled `config/MW2MP.toml`
- [x] 1 host tools (Linux/WSL)
- [ ] 2 codegen (blocked: needs the disc ISO)
