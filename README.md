# mw2nx

Call of Duty: Modern Warfare 2 (Xbox 360) on the Nintendo Switch, by static recompilation. The game's PowerPC
executable is recompiled ahead of time to C++ with ReXGlue, built natively for the Switch's ARM cores, and drawn with
Vulkan through Mesa's NVK driver. Nothing is emulated at run time.

**Status:** early bring-up, no playable build yet. Plan and progress: [docs/MW2.md](docs/MW2.md).

## What you need

- Your own Xbox 360 disc of Modern Warfare 2, version 1.0.557 (USA/Europe), as an ISO.
- Title update 6.
- A Switch that runs homebrew.

No game code or data is included in this repository, and none is ever committed: every step that reads game files
writes into ignored folders.

## Credits

- The Switch runtime, renderer and tooling are based on [bo2-360-nx](https://github.com/AlvaroEPena/bo2-360-nx).
- MW2 addresses, engine fixes and rendering notes come from
  [mw2-recompiled](https://github.com/PaulCombal/mw2-recompiled).
- Built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), Xenia and mesa-switch; see
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

GPL-3.0, see [LICENSE](LICENSE). Call of Duty and Modern Warfare are trademarks of Activision, Nintendo Switch of
Nintendo, Xbox 360 of Microsoft. This project is not affiliated with any of them.
