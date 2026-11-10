// The campaign's values for the game addresses and file names mw2mp/src names (forced into every mw2mp/src file
// mw2sp builds, mw2sp/CMakeLists.txt), so those files build for this program unchanged.
//
// What mw2mp/src takes from default_mp.xex by address: the cl_ingame dvar (below) and the CRT's memcpy/memset
// ([rexcrt] in mw2sp/overrides.toml). Its three D3D hooks (sub_8259BC90, sub_8259C038, sub_825A2FB8) are BO2
// addresses that fall in MW2's .data: no function of either program has them, so they stay as they are.

#pragma once

#define MW2_CAMPAIGN 1
#define MW2_APP_NAME "mw2sp"

// "Is a level loaded": the campaign has no cl_ingame, so sv_running (0x82033058) stands in. sub_822E15D8 (the
// campaign's Dvar_RegisterBool, r3 name, r4 default 0, r5 flags 0x2000) registers it and the caller stores the dvar_t*
// at 0x833FDB58 (lis r10,-31936; stw r3,-9384(r10)). Same dvar_t layout as mw2mp: the current value is the byte at +12.
#define MW2_IN_GAME_DVAR 0x833FDB58u

// Both programs share sdmc:/switch/mw2mp: the campaign's shader library and pipeline cache sit beside mw2mp's.
#define MW2MP_SHADER_LIBRARY_FILE "mw2sp_shaders.spvpack"
#define MW2MP_NATIVE_CACHE_FOLDER "cache_sp"
