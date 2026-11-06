// mw2mp - Modern Warfare 2 multiplayer (TU6), recompiled with ReXGlue for the Nintendo Switch.

#pragma once

#include <filesystem>
#include <system_error>

#include <rex/filesystem.h>
#include <rex/platform.h>
#include <rex/rex_app.h>

class Mw2mpApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Mw2mpApp>(new Mw2mpApp(ctx, "mw2mp", PPCImageConfig));
  }

  // The SDK loads game:\default.xex (the campaign); multiplayer is default_mp.xex.
  void OnLoadXexImage(std::string& xex_image) override { xex_image = "game:\\default_mp.xex"; }

  // Folders next to the NRO (hbmenu passes no command line):
  //   sdmc:/switch/mw2mp/mw2mp.nro
  //   sdmc:/switch/mw2mp/game/     the disc's files + default_mp.xexp (TU6)
  //   sdmc:/switch/mw2mp/update/   the rest of the TU6 package (patch_mp.ff, dlc1_ui_mp.ff, dlc2_ui_mp.ff)
  //   sdmc:/switch/mw2mp/user/     profile, saves, caches
  void OnConfigurePaths(rex::PathConfig& paths) override {
    std::error_code ec;
    auto dir = rex::filesystem::GetExecutableFolder();
    // Emulators may run the NRO from a temporary copy: fall back to the install folder.
    if (!std::filesystem::is_directory(dir / "game", ec) &&
        std::filesystem::is_directory("sdmc:/switch/mw2mp/game", ec)) {
      dir = "sdmc:/switch/mw2mp";
      paths.config_path = dir / "mw2mp.toml";
    }
    if (dir.empty()) {
      return;
    }
    if (paths.game_data_root.empty() && std::filesystem::is_directory(dir / "game", ec)) {
      paths.game_data_root = dir / "game";
    }
    if (paths.update_data_root.empty() && std::filesystem::is_directory(dir / "update", ec)) {
      paths.update_data_root = dir / "update";
    }
    paths.user_data_root = dir / "user";
    paths.cache_root = dir / "user" / "cache";
  }
};
