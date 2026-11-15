# Settings

mw2nx has no in-game settings pages of its own. Its options are settings in a TOML file next to the NRO:
`sdmc:/switch/mw2mp/mw2mp.toml` for Multiplayer and `sdmc:/switch/mw2sp/mw2sp.toml` for the Campaign. Both files are
optional; a missing file means every default below. Keys are written as `name = value`, strings in quotes. The same
keys work as command-line options.

```toml
mw2mp_preset = "low"
mw2mp_show_fps = true
```

## Graphics

| Key | Default | What it does |
|---|---|---|
| `mw2mp_preset` | `"very_low"` | `high`, `medium`, `low` or `very_low`. Sets the game's own quality settings (shadow maps, dynamic lights, depth of field, distortion, clouds, decals, brass, corpses, physics objects), a draw distance, the detail of distant models and the texture detail. Held every frame below `high` |
| `mw2mp_draw_distance` | `-1` | Draw distance in game units. `-1` uses the preset's, `0` the level's own |
| `mw2mp_model_detail` | `0` | Multiplier for the distance at which models drop to a simpler version. `0` uses the preset's (1, 1.5, 2 and 4 from `high` to `very_low`) |
| `mw2mp_antialiasing` | Campaign: FXAA, Multiplayer: off | Post-process anti-aliasing; the renderer has no MSAA |
| `mw2mp_internal_resolution` | `"auto"` | The resolution the scene is drawn at before it is scaled to the screen |
| `mw2mp_fps_limit` | `"60"` | Frame rate cap |
| `mw2mp_frame_pacing` | Campaign: `auto` | Adaptive pacing: drops to 30 where most frames miss 60 but fit 30, and back where 60 fits |
| `mw2mp_adaptive_shadows` | `true` | Lowers shadow cost when frames run long |
| `mw2mp_sky_glow`, `mw2mp_visual_treatment` | `"natural"`, `"original"` | Look of the sky and of the image |
| `mw2mp_postprocess` and `mw2mp_postprocess_*` | `"off"` | Brightness, contrast, saturation, vibrance, temperature, gamma, vignette and scanlines |
| `mw2mp_show_fps` | `false` | FPS counter |

## Switch

| Key | Default | What it does |
|---|---|---|
| `mw2mp_cpu_cores` | `4` | Cores to use. The fourth is only there when the program is started from a forwarder with four cores enabled; otherwise it runs on three |
| `mw2mp_switch_gpu_mhz` | `0` | GPU clock when the console is handheld; `0` is the built-in profile (460 MHz) |
| `mw2mp_switch_ram_1600` | `false` | Raises the memory clock |

## Multiplayer

| Key | Default | What it does |
|---|---|---|
| `mw2mp_mod` | `"bot_warfare"` | The script mod to load; `""` for none. See [script-mods.md](script-mods.md) |
| `mw2mp_user_name` | `""` | The player's name. Empty uses the Switch user's name |

## Measuring

| Key | What it does |
|---|---|
| `mw2mp_benchmark` | Runs a timed window in a private match and writes the frame statistics |
| `mw2mp_preset_test_s` | Steps through the presets every N seconds, for an A/B in one run |
| `mw2mp_native_per_draw_stats_s` | Draw statistics every N seconds |

Logs go to `logs/` next to the NRO. Anything under `mw2mp_native_*` is a renderer switch for development and is
documented in the source beside its definition.
