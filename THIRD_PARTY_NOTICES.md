# Third-party notices

mw2nx (Call of Duty: Modern Warfare 2 for the Nintendo Switch) is licensed under the GNU General Public License v3.0
(`LICENSE`). That license covers everything this project wrote: `mw2mp/`, `mw2sp/`, its changes in `sdk/`, `tools/`, the
Mesa patch and the documentation. The port is built on the work of the projects and libraries below. Their code keeps its own
copyright and license notices, exactly as they are, and every one of those licenses is compatible with GPL-3.0.

## Projects this port is derived from

| Project | Used for | License |
|---|---|---|
| [bo2-360-nx](https://github.com/AlvaroEPena/bo2-360-nx) by AlvaroEPena | The Switch runtime, the native renderer (`mw2mp/src/native`), the SDK's Switch layer and much of `tools/`, renamed and adapted to MW2 | GPL-3.0 |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) v0.10.0 (commit `c94f5eb`) by Tom Clay, with portions from [Xenia](https://xenia.jp) (Ben Vanik and the Xenia contributors) | `sdk/`: code generator, Xbox 360 kernel and runtime, GPU command processing. See `sdk/README.md` for which files are the official release and which are changed | BSD-3-Clause (`sdk/LICENSE`) |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) by hedge-dev and contributors | `tools/xenosrecomp/translator/`: Xenos microcode to HLSL translator | MIT (`tools/xenosrecomp/translator/LICENSE.md`) |
| [mesa-switch](https://github.com/danfromtico/mesa-switch) by danfromtico (commit `1a8c1a66d6f`), on top of [Mesa](https://mesa3d.org) | The Vulkan driver (NVK) and shader compiler (NAK) for Horizon, linked into the NRO. The official base is fetched by `tools/build_mesa_linux.sh`; this project's changes are the patch in `patches/` | MIT (Mesa's licenses per file) |

Parts of `mw2mp/`, `mw2sp/` and `tools/` are derived from GPL-3.0 code, so GPL-3.0 would apply to them in any case; each such file
says so in its header. The upstream code in this repository keeps its own license: the ReXGlue SDK with its Xenia
portions and the Switch layer in `sdk/` under BSD-3-Clause (`sdk/LICENSE`), the translator under MIT
(`tools/xenosrecomp/translator/LICENSE.md`), and Mesa with the changes to it under its per-file licenses (MIT).

## Libraries

Libraries used by the SDK, the NRO and the code generator, fetched from the submodules of ReXGlue SDK v0.10.0
(`tools/fetch_thirdparty.py`):

| Library | License |
|---|---|
| [FFmpeg](https://ffmpeg.org) (libavcodec, libavutil: the WMV3 decoder for movies, the VP8 decoder for the loading videos), from `wmarti/FFmpeg` at `0604b464c7cb` | LGPL-2.1-or-later (configured without GPL parts) |
| [libmspack](https://www.cabextract.org.uk/libmspack/) by Stuart Caie (LZX decompression) | LGPL-2.1 |
| [glslang](https://github.com/KhronosGroup/glslang) | BSD-3-Clause and others (see its LICENSE.txt) |
| [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | Apache-2.0 |
| [SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers), [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | MIT, Apache-2.0 |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | MIT |
| [fmt](https://github.com/fmtlib/fmt), [spdlog](https://github.com/gabime/spdlog) | MIT |
| [xxHash](https://github.com/Cyan4973/xxHash) | BSD-2-Clause |
| [SIMDe](https://github.com/simd-everywhere/simde) | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT |
| [o1heap](https://github.com/pavel-kirienko/o1heap) | MIT |
| [utfcpp](https://github.com/nemtrif/utfcpp) | BSL-1.0 |
| [CLI11](https://github.com/CLIUtils/CLI11) | BSD-3-Clause |
| AES-128 by LuoPeng (`thirdparty/aes_128`) | MIT |
| [zstd](https://github.com/facebook/zstd) (`tools/build_zstd_switch.sh`) | BSD-3-Clause |
| [libnx](https://github.com/switchbrew/libnx) and the devkitA64 runtime (newlib, libstdc++) | ISC; newlib and GCC runtime licenses |

### FFmpeg and libmspack (LGPL)

These files of the SDK's third-party tree are modified; the modified copies are in `sdk/thirdparty`:

- `FFmpeg/libavcodec/fft_template.c` and `FFmpeg/libavcodec/mdct_template.c`: cache of the permutation and cosine tables
- `FFmpeg/config.h`, plus the new `FFmpeg/config_switch_aarch64.h` and the decoder-specific configuration headers:
  the configuration for AArch64 Horizon and for a build limited to the WMV3 and VP8 decoders
- `ffmpeg-overlay/codec_list.c`: registers the WMV3 and VP8 decoders
- `CMakeLists.txt`: builds the libraries for the Switch

The rest of FFmpeg and libmspack is the unmodified upstream source at the commits pinned by ReXGlue SDK v0.10.0.

## Script mods shipped with the port

| Mod | Source | Terms |
|---|---|---|
| IW4 Bot Warfare 2.4.0 (`mw2mp/mods/bot_warfare/`, unmodified) | [Bot Warfare](https://github.com/ineedbots/iw4_bot_warfare) by INeedGames / INeedBot(s), Xbox 360 port by [CoD Xenon](https://github.com/codxenon/iw4-bot-warfare-xenon) (release `v2.4.0-xenon`, commit `2f5e9fc`). Credits in `mw2mp/mods/bot_warfare/CREDITS.md` | The author's grant in the Bot Warfare README: use, host, modify and merge freely with credit |

## The setup kit and the shader tools

The shader library is built on the user's machine from their own game data. The setup kit
(`mw2nx-setup-<version>.zip`, `tools/make_setup_zip.py`) ships the tools for it as static programs: the translator
above, the packer (`tools/xenosrecomp/mw2_pack.cpp`) and `zone_containers`, which is built from
[mw2-recompiled](https://github.com/PaulCombal/mw2-recompiled)'s fastfile reader (`launcher/zone`, commit `b3aa7d8`,
GPL-3.0), with fmt and xxHash from `sdk/thirdparty`. The setup script downloads
[DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler) v1.9.2609 from Microsoft's release
(University of Illinois/NCSA Open Source License, with LLVM's license terms); it is not in the kit.

## Trademarks

Call of Duty and Modern Warfare are trademarks of Activision Publishing, Inc. Nintendo Switch is a
trademark of Nintendo. Xbox 360 is a trademark of Microsoft. This project is not affiliated with or endorsed by any of them.
