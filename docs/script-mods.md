# Script mods (GSC loader)

Multiplayer loads GSC mods made for **CoD Xe** on MW2 TU6 (the Xbox 360 / Xenia mod loader). MW2 compiles its scripts
from source when a map loads, so a mod is plain `.gsc` text laid out like the game's own script paths. The loader
replaces or adds scripts. It does not sandbox what they do. Campaign does not load script mods.

## Install a mod

1. Put the mod's folder in `sdmc:/switch/mw2mp/mods/`, next to `mw2mp.nro`. For a CoD Xe release, copy
   `_codxe/mods/<name>/` from its zip. Leave out `_codxe/codxe.json` and `default_mp.xex`, which Switch does not need.
2. Name the mod in `sdmc:/switch/mw2mp/mw2mp.toml`:

   ```toml
   mw2mp_mod = "<name>"
   ```

3. Start multiplayer. The log (`logs/`) shows `[gsc] mod "<name>"`, then one `[gsc] ... from the mod` line for each
   script the mod supplies.

Set `mw2mp_mod = ""` to play without mods; with the line removed, the bundled Bot Warfare loads. Only one mod is active at a time.

A file at `mods/<name>/maps/mp/gametypes/_callbacksetup.gsc` replaces the game's script with that path. A new path,
such as `scripts/mp/mymod.gsc`, adds a script. A script runs only when another script calls it, so mods usually
replace a stock script (often `_callbacksetup.gsc`) to start their own.

## Script functions

These functions are added for mods, with the same names and behaviour as in CoD Xe:

| Function | What it does |
| --- | --- |
| `printconsole(text)` | Writes to the log |
| `fileread(file)`, `filewrite(file, text, "write"\|"append")`, `fileexists(file)` | Read and write files. Paths are relative to the mod folder; absolute paths and `..` are refused |
| `openfile(file, "read")`, `readstream()`, `closefile()` | Read a file line by line, one file at a time |
| `addtestclient([name])` | Adds a bot, with an optional name |
| `<bot> botaction("+fire")` and so on | Holds (`+`) or releases (`-`) a button: `fire`, `ads`, `melee`, `frag`, `smoke`, `reload`, `sprint`, `gostand`, `gocrouch`, `goprone`, `leanleft`, `leanright`, `holdbreath`, `use`, `activate`, `usereload`, `remote` |
| `<bot> botmovement(forward, right)` | Moves the bot (-127 to 127) |
| `<bot> botangles(pitch, yaw, roll)` | Turns the bot to these view angles |
| `<bot> botremoteangles(pitch, yaw)`, `<bot> botmeleeparams(yaw, dist)` | Controls killstreaks such as the Predator, and melee lunges |
| `<bot> botstop()` | Releases every button and stops all movement |

A bot stands still until a script controls it. Bots can also hold Use to plant or defuse bombs.

## Bot Warfare

[IW4 Bot Warfare](https://codxenon.dev/guides/iw4/bot-warfare/) by INeedGames, in CoD Xenon's Xbox 360 port, adds
bots to private matches and System Link games. It ships with the port and is on by default: `mw2mp/mods/bot_warfare/`
(unmodified `v2.4.0-xenon`, credits in its `CREDITS.md`) is staged to `sdmc:/switch/mw2mp/mods/bot_warfare/` by
`tools/stage_sd_mw2mp.py`, and `mw2mp_mod` defaults to `"bot_warfare"`.

Start a private match and press D-pad Down to open the bot menu. Set `mw2mp_mod = ""` in `mw2mp.toml` to play
without bots.

Bot names are read from `mods/bot_warfare/scriptdata/botnames.txt`, one per line. Waypoints come from
`scriptdata/waypoints/`.
