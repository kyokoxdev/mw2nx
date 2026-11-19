// The campaign's values for the game addresses and file names mw2mp/src names (forced into every mw2mp/src file
// mw2sp builds, mw2sp/CMakeLists.txt), so those files build for this program unchanged.
//
// What mw2mp/src takes from default_mp.xex by address: the cl_ingame dvar (below) and the CRT's memcpy/memset
// ([rexcrt] in mw2sp/overrides.toml). Its three reference D3D hooks (sub_8259BC90, sub_8259C038, sub_825A2FB8) are
// addresses no function of either program has (compiled out); the campaign's own microcode writers (sub_820B6B10,
// sub_820B72C0) are hooked in mw2mp_native_hooks.cpp under MW2_CAMPAIGN.

#pragma once

#define MW2_CAMPAIGN 1
#define MW2_APP_NAME "mw2sp"

// XAudio's mixer thread: sub_82095990 starts it with ExCreateThread at one of these two entries (r7, picked by a flag;
// mw2mp's twins sub_820A56B0/sub_820A5778 in mw2mp_native_stubs.cpp). Unnamed, so switch_audio_guest_threads matches
// the start address.
#define MW2_XAUDIO_THREADS "0x82095818,0x820958E0"

// "Is a level loaded": the campaign has no cl_ingame, so sv_running (0x82033058) stands in. sub_822E15D8 (the
// campaign's Dvar_RegisterBool, r3 name, r4 default 0, r5 flags 0x2000) registers it and the caller stores the dvar_t*
// at 0x833FDB58 (lis r10,-31936; stw r3,-9384(r10)). Same dvar_t layout as mw2mp: the current value is the byte at +12.
#define MW2_IN_GAME_DVAR 0x833FDB58u
// sv_running alone is not enough: it turns on when the first level starts loading and stays on across mission changes
// (run mw2sp_004: on at the start of trainer.ff's load, never off between S.S.D.D. and the next mission), so the load
// hold ended during the loading movie and never ran for later missions. The client's "cgame initialized" byte is the
// per-level edge: CL_InitCGame (sub_821208D8, "CL_InitCGame: %5.2f seconds") sets 0x825B70A0+0x919EA/0x919EB to 1
// before CG_Init, CL_ShutdownCGame (sub_8211FF88) tests +0x919EA and clears +0x919E9..0x919EB.
#define MW2_CGAME_FLAG 0x82648A8Au

// The dvar pool, for the graphics presets (mw2mp_graphics_preset.cpp). Dvar_RegisterNew (sub_822E12A0) takes dvar_t
// #count, 72 bytes each, from 0x8393DAB0 (lis r11,-31852; addi r28,r11,-9552) and the count from 0x83938A90
// (lwz r11,-30064(r30); cmpwi cr6,r11,4096).
#define MW2_DVAR_POOL 0x8393DAB0u
#define MW2_DVAR_COUNT 0x83938A90u

// XModelGetLodForDist (sub_82300B50): the presets' model detail hooks it (mw2mp_graphics_preset.cpp).
#define MW2_XMODEL_LOD_FOR_DIST 1

// The campaign's shader library and pipeline cache sit beside mw2sp.nro (sdmc:/switch/mw2sp), named apart from mw2mp's.
#define MW2MP_SHADER_LIBRARY_FILE "mw2sp_shaders.spvpack"
#define MW2MP_NATIVE_CACHE_FOLDER "cache_sp"
