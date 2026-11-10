// mw2mp - Modern Warfare 2 multiplayer (TU6), recompiled with ReXGlue for the Nintendo Switch.

#pragma once

#include <filesystem>
#include <system_error>

#include <rex/filesystem.h>
#include <rex/platform.h>
#include <rex/logging.h>
#include <rex/rex_app.h>

#include "mw2mp_switch.h"
#include "native/mw2mp_native_system.h"
#include "native/render/mw2mp_native_precompile.h"
#include "native/render/mw2mp_graphics_settings.h"

// mw2sp builds this same app for the campaign (mw2sp/CMakeLists.txt): MW2_CAMPAIGN 1, MW2_APP_NAME "mw2sp". Both
// programs share sdmc:/switch/mw2mp (the disc's files hold default.xex and default_mp.xex).
#ifndef MW2_CAMPAIGN
#define MW2_CAMPAIGN 0
#endif
#ifndef MW2_APP_NAME
#define MW2_APP_NAME "mw2mp"
#endif

#if REX_PLATFORM_SWITCH
// libnx's (switch.h's typedefs collide with rex): starts the bsd service that BSD sockets need on Horizon.
struct SocketInitConfig;
extern "C" uint32_t socketInitialize(const SocketInitConfig* config);
#endif

class Mw2mpApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Mw2mpApp>(new Mw2mpApp(ctx, MW2_APP_NAME, PPCImageConfig));
  }

  // The SDK loads game:\default.xex (the campaign, mw2sp: MW2_CAMPAIGN keeps it); multiplayer is default_mp.xex.
#if !MW2_CAMPAIGN
  void OnLoadXexImage(std::string& xex_image) override { xex_image = "game:\\default_mp.xex"; }
#endif

  // The emulated Xenos GPU unless the toml or command line names another (the cvar defaults to none: black screen).
  // mw2mp_renderer = native/skeleton: the port's own graphics system (native/), and ReXApp loads no plugin.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (mw2mp::native::Active()) {
      config.graphics = mw2mp::native::CreateGraphicsSystem();
    } else if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
#if REX_PLATFORM_SWITCH
    // The runtime's NetDll_socket uses BSD sockets and nothing in it starts the service: without this every socket()
    // fails with ENOENT and MW2 stops at init ("WARNING: Couldn't allocate IP port"), even offline.
    if (const uint32_t rc = socketInitialize(nullptr); rc != 0) {
      REXLOG_ERROR("[switch] socketInitialize failed: {:#x}", rc);
    }
#endif
  }

  // mw2nx: the handheld GPU profile (mw2mp_switch_gpu_mhz) and the overlay FPS block, once the toml is read. Nothing
  // called it: the GPU stayed at homebrew's 307.2 MHz.
  void OnPostInitLogging() override {
    mw2mp::settings::RequestBootClocks();
    mw2mp::ConfigureCores();
  }

  // The native renderer compiles the pipelines earlier runs used before the game starts (mw2mp_native_precompile.h).
  bool OnHoldModuleLaunch(std::function<void()> start) override {
    return mw2mp::native::precompile::Hold(std::move(start), imgui_drawer());
  }

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
      paths.config_path = dir / MW2_APP_NAME ".toml";
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
