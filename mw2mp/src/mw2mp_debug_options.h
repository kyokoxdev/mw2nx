#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mw2mp::debug {

// Switches that help find the cause of a bug. In bo2-360-nx they were the DEBUG page of its Extras menu (menus.lua);
// MW2 has no such page, and mw2mp_native_stubs.cpp answers with the defaults.

// The renderer shortcuts the page can turn off (any thread). Off falls back to the full path, the one the benchmark's
// A/B "off" arms run.
bool TextureShortcutsOff();   // texture rechecks skipped by write stamps (young ones too), and hash sampling
bool ConstantsShortcutOff();  // only the constants each shader reads are copied
bool CachesOff();             // the vertex and index caches

// The page's options (0 = the normal choice), as the DEBUG PRESET in use sets them. Changing one makes the preset
// CUSTOM. Main thread.
int TextureChecks();
void SetTextureChecks(int full);
int Constants();
void SetConstants(int full);
int Caches();
void SetCaches(int off);
int SafeRenderer();
void SetSafeRenderer(int on);
int LogDetail();
void SetLogDetail(int detailed);
int StackProfile();
void SetStackProfile(int on);
// MARK LOCATION (saved as "mark_location"): on, and DEBUG on, the pause menu in a match has MARK LOCATION at the top
// (menus.lua decides), which runs mw2mp_mark_here (the menu setting itself is mw2mp_mark_location <0|1>).
int MarkLocation();
void SetMarkLocation(int on);

// The quiet log (see mw2mp_debug_options.cpp): ConfigureStartup once the toml is read and before logging starts (the
// app's OnConfigLoaded), with the user data folder that holds mw2mp_settings.txt; AfterLoggingStarted right after.
void ConfigureStartup(const std::filesystem::path& user_data_root);
void AfterLoggingStarted();

// MAX LOG FILES (saved as "max_log_files"; mw2mp_max_log_files in the toml wins): 0 = 5, 1 = 10 (the default),
// 2 = 20, 3 = off (every log kept). A pick applies from the next start.
int MaxLogFiles();
void SetMaxLogFiles(int index);

// PURGE DEBUG INFO's page: the tier (0 LOGS, 1 SCREENSHOTS, 2 TRACES AND TEST FILES, 3 ALL; not saved) and its DELETE
// button's label: what the tier holds (counted again when the tier changes or the page opens, mw2mp_purge_scan), then
// what mw2mp_purge deleted. Main thread.
int PurgeTier();
void SetPurgeTier(int tier);
std::string PurgeLabel();

// The DEBUG switch (saved as "debug"): off, every option above works as in normal play whatever is picked; on, the
// picks apply. Sync applies the switch and the picks once the saved settings are read.
int Enabled();
void SetEnabled(int on);
void Sync();
// DEBUG PRESET (saved as "debug_preset"): 0 CUSTOM, 1 BASIC (the default), 2 VERBOSE, 3 GRAPHICS, 4 PERFORMANCE,
// 5 CRASH, 6 FULL. A preset sets the options; CUSTOM gives back the player's own picks (saved as debug_<option>,
// LoadCustom reads them before the saved preset is applied); changing an option starts CUSTOM from the preset in use.
int Preset();
void SetPreset(int preset);
void LoadCustom();

// "mw2mp_mark" (a line in the log and a screenshot), "mw2mp_screenshot", "mw2mp_debug_reset" (preset BASIC),
// "mw2mp_purge_scan" and "mw2mp_purge" (PURGE DEBUG INFO).
// false: not one of these.
bool RunCommand(std::string_view name, std::string_view argument);

// For the page's info line: the build and this run's log file.
std::string BuildText();
std::string LogFileName();

}  // namespace mw2mp::debug
