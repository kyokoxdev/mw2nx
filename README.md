<p align="center">
  <b>Call of Duty: Modern Warfare 2 for the Nintendo Switch, recompiled from the Xbox 360.</b><br>
  Multiplayer and Campaign, running as native code.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Nintendo%20Switch-E60012?style=for-the-badge&logo=nintendoswitch&logoColor=white" alt="Nintendo Switch">
  <img src="https://img.shields.io/badge/status-in%20development-orange?style=for-the-badge" alt="Status: in development">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue?style=for-the-badge" alt="License: GPL-3.0"></a>
</p>

---

mw2nx is a static recompilation of Modern Warfare 2 (Xbox 360, version 1.0.557 with Title Update 6). The game's
PowerPC code is translated ahead of time into C++ and compiled for the Switch's ARM cores, so nothing is emulated while
you play, and a Vulkan renderer of its own draws the game through Mesa's NVK driver.

> [!IMPORTANT]
> **No game data is included, and none ever will be.** You need your own Xbox 360 disc of Modern Warfare 2 as an ISO.
> Every step that reads your files writes into folders the repository ignores; what it produces is yours alone and must
> not be shared.

> [!NOTE]
> **In development, not released.** Multiplayer and the Campaign both run on hardware (the Campaign has been played
> through to Gulag). Performance, memory headroom and load hitches are the open work, and installable packaging comes
> last. Progress and plan: [docs/MW2.md](docs/MW2.md).

## Contents

[Features](#features) · [Installing](#installing) · [Script mods](#script-mods) · [How it works](#how-it-works) ·
[Warnings](#warnings) · [Credits](#credits) · [License](#license) · [Technical documentation](#technical-documentation)

## Features

### Game modes

| Mode | What you get |
|---|---|
| **Multiplayer** | Private matches and System Link, with bots from the bundled Bot Warfare mod |
| **Campaign** | Its own program (`mw2sp.nro`) that shares the game files with Multiplayer; played through to Gulag |

### Graphics and performance

- **Presets** `high`, `medium`, `low` and `very_low` (the default), which set the game's own quality settings, a draw
  distance and the detail of distant models. Each can be overridden in the settings file.
- **Native code**: about 19,800 game functions compiled to ARM64 with `-O3`, direct calls between them, and hot game
  code (memory copies, culling) replaced by hand-written native code.
- **Pipeline cache**: each level compiles its Vulkan pipelines the first time it is played and starts warm after that.
- **Adaptive frame pacing** in the Campaign, and FXAA on by default there.
- **Optional fourth CPU core**, from a forwarder with four cores enabled (see [Warnings](#warnings)).

### Mods

- **Script mods**: plain `.gsc` mods made for CoD Xe load in Multiplayer; IW4 Bot Warfare ships with the port and is on by
  default. See [docs/script-mods.md](docs/script-mods.md).

Settings and their file: [docs/features.md](docs/features.md).

## Installing

**You need:**

- Your Xbox 360 disc of Modern Warfare 2, version 1.0.557 (USA/Europe), as an ISO.
- **Title Update 6.** The setup tool downloads it after it has checked your ISO.
- A Switch that runs homebrew (custom firmware such as Atmosphère).
- A Linux or WSL2 PC to build on. There is no prebuilt release yet.

Steps, from the repository root (every tool and build option: [docs/building.md](docs/building.md)):

1. Put your ISO in `iso/` and run `python tools/install/tu6_setup.py`. It checks the disc, fetches Title Update 6 and
   fills `work/load_tu6/`.
2. Build the code generator with `tools/build_host_linux.sh`, then generate the game's C++ with
   `tools/codegen_mw2mp.sh` and `tools/codegen_mw2sp.sh`.
3. Build the Switch programs with `mw2mp/build_switch.sh` and `mw2sp/build_switch.sh`.
4. Run `python tools/stage_sd_mw2mp.py`: it lays out the finished folders from your ISO and Title Update 6.
5. Build the shader libraries from your own disc with `tools/mw2shaders/build_mw2mp_shaders.sh <work folder>`
   (`MW2_SP=1` for the Campaign's); they land next to the NROs in the staged folders.
6. Copy the staged `switch` folder to the root of the SD card and start `mw2mp` from the Homebrew Menu opened through a game
   (hold R while starting one), so it gets the console's full memory.

The finished folders on the SD card:

```text
sdmc:/switch/mw2mp/
├── mw2mp.nro                  Multiplayer
├── mw2mp.toml                 its settings (optional, you write it)
├── mw2mp_shaders.spvpack      Multiplayer shaders, and its index
├── mw2mp_shaders.spvidx
├── game/                      your disc files
├── update/                    Title Update 6
└── mods/bot_warfare/          the bundled script mod

sdmc:/switch/mw2sp/
├── mw2sp.nro                  Campaign
├── mw2sp.toml                 its settings (optional)
└── mw2sp_shaders.spvpack      Campaign shaders, and its index
```

The Campaign reads `game/`, `update/` and `user/` from the `mw2mp` folder.

## Script mods

Multiplayer loads GSC mods made for CoD Xe. Put a mod's folder in `sdmc:/switch/mw2mp/mods/`, name it in `mw2mp.toml`
with `mw2mp_mod = "<name>"`, and start Multiplayer. `mw2mp_mod = ""` plays without mods. Folder layouts, the functions
added for mods and Bot Warfare's menu: [docs/script-mods.md](docs/script-mods.md).

## How it works

The Xbox 360's executable is decrypted and patched to Title Update 6, then ReXGlue's code generator turns all of its
PowerPC code into C++, which is compiled into a native Switch program. A runtime built on the ReXGlue SDK stands in
for the 360's kernel and system libraries: threads, memory, files, audio, controllers and networking. The game's own
Direct3D layer still writes its commands for the Xbox 360's GPU; a native Vulkan renderer reads them and draws each
frame on the Switch's GPU. The game's shaders are translated to SPIR-V when you build, from your own disc. The details:
[docs/technical.md](docs/technical.md).

## Warnings

- **Memory.** One heap backs the GPU's memory, the stacks and the caches, so running out of it can crash the game.
  Start from a game-opened Homebrew Menu, not the applet one.
- **Four CPU cores.** The fourth core belongs to the system, so in theory using it can make the console unstable. It is
  off by default; a forwarder with four cores enabled turns it on.
- **Sleep.** Leaving the game in sleep mode for a long time might cause problems. Close it first if you will be away.

## Credits

- The Switch runtime, renderer and tooling are based on [bo2-360-nx](https://github.com/AlvaroEPena/bo2-360-nx) by
  AlvaroEPena.
- MW2 addresses, engine fixes and rendering notes come from
  [mw2-recompiled](https://github.com/PaulCombal/mw2-recompiled) by PaulCombal.
- Multiplayer bots are [IW4 Bot Warfare](https://github.com/ineedbots/iw4_bot_warfare) by INeedGames, in the
  Xbox 360 port by [CoD Xenon](https://github.com/codxenon/iw4-bot-warfare-xenon), shipped unmodified.
- Built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), Xenia and mesa-switch. Every upstream project and
  library with its license is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

GPL-3.0 ([LICENSE](LICENSE)); bundled upstream code keeps its own notices, listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Call of Duty and Modern Warfare are trademarks of Activision Publishing, Inc. Nintendo Switch is a trademark of
Nintendo. Xbox 360 is a trademark of Microsoft. This project is not affiliated with or endorsed by any of them, and it
contains no game data.

## Technical documentation

How the port works, what was optimized and what it gained, and building from source:
**[docs/technical.md](docs/technical.md)** and **[docs/building.md](docs/building.md)**.
