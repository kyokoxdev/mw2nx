// The installed program's first start (mw2mp_first_start.h): its folder on the SD card, and the screen that lists
// what is missing until the player has copied it.

#include "mw2mp_first_start.h"

#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <system_error>
#include <utility>

// mw2sp builds this file with MW2_CAMPAIGN 1 and its own shader library name (mw2sp/src/mw2sp_game_remap.h).
#ifndef MW2_CAMPAIGN
#define MW2_CAMPAIGN 0
#endif
#ifndef MW2MP_SHADER_LIBRARY_FILE
#define MW2MP_SHADER_LIBRARY_FILE "mw2mp_shaders.spvpack"  // as in native/render/mw2mp_native_render.cpp
#endif

namespace fs = std::filesystem;

namespace {

constexpr char kGameNote[] =
    "Put the files of your own Modern Warfare 2 disc here (everything on the disc except $SystemUpdate),\n"
    "plus default.xexp and default_mp.xexp from Title Update 6.\n"
    "The PC setup script lays all of this out from your ISO; no game files come with mw2nx.\n";
constexpr char kUpdateNote[] =
    "Put the rest of Title Update 6 here: patch_mp.ff, dlc1_ui_mp.ff and dlc2_ui_mp.ff.\n"
    "The PC setup script downloads it after it has checked your disc.\n";

void WriteNote(const fs::path& path, const char* text) {
  std::error_code ec;
  if (fs::exists(path, ec)) {
    return;
  }
  if (FILE* file = std::fopen(path.string().c_str(), "wb")) {
    std::fputs(text, file);
    std::fclose(file);
  }
}

// "/atmosphere/..." (newlib's default device is the SD card) as the player sees it.
std::string ForPlayer(const fs::path& path) {
  return "sdmc:" + path.generic_string();
}

class MissingFilesScreen final : public rex::ui::ImGuiDialog {
 public:
  MissingFilesScreen(rex::ui::ImGuiDrawer* drawer, fs::path folder, std::vector<std::string> missing,
                     std::function<void()> ready)
      : ImGuiDialog(drawer), folder_(std::move(folder)), missing_(std::move(missing)), ready_(std::move(ready)),
        checked_at_(std::chrono::steady_clock::now()) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const auto now = std::chrono::steady_clock::now();
    if (now - checked_at_ >= std::chrono::seconds(2)) {
      checked_at_ = now;
      missing_ = mw2mp::MissingFiles(folder_);
      if (missing_.empty()) {
        REXLOG_INFO("[setup] all files found: starting");
        auto ready = std::move(ready_);
        Close();  // deletes this
        ready();
        return;
      }
    }

    const ImVec2 size = io.DisplaySize;
    // The SDK's one font is ProggyTiny at 10 px: whole-number scales keep it sharp. Laid out for 720 lines.
    const float unit = size.y / 720.0f;
    const float scale_title = std::max(1.0f, std::round(3.0f * unit));
    const float scale_text = std::max(1.0f, std::round(2.0f * unit));
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##mw2mp_missing_files", nullptr, flags)) {
      const float left = std::floor(size.x * 0.06f);
      float y = std::floor(size.y * 0.10f);
      const auto line = [&](const std::string& text, float scale, ImU32 color, float gap) {
        ImGui::SetWindowFontScale(scale);
        ImGui::SetCursorPos(ImVec2(left, y));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
        y += std::floor(gap * unit);
      };
      const ImU32 white = IM_COL32(255, 255, 255, 255);
      const ImU32 grey = IM_COL32(170, 170, 170, 255);
      const ImU32 orange = IM_COL32(230, 140, 20, 255);
      line("GAME FILES MISSING", scale_title, white, 56.0f);
      line(MW2_CAMPAIGN ? "The Campaign needs these files from your own disc and Title Update 6:"
                        : "Multiplayer needs these files from your own disc and Title Update 6:",
           scale_text, grey, 40.0f);
      const size_t shown = std::min<size_t>(missing_.size(), 10);
      for (size_t i = 0; i < shown; ++i) {
        line(missing_[i], scale_text, orange, 28.0f);
      }
      if (missing_.size() > shown) {
        line("... and " + std::to_string(missing_.size() - shown) + " more", scale_text, orange, 28.0f);
      }
      y += std::floor(24.0f * unit);
      line("Run the mw2nx setup script on your PC with your ISO, then copy what it makes to", scale_text, grey, 28.0f);
      line(ForPlayer(folder_), scale_text, white, 40.0f);
      line("The game checks again every few seconds and starts once everything is there.", scale_text, grey, 28.0f);
      line("You can also close it with HOME, copy the files and start it again.", scale_text, grey, 28.0f);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
  }

 private:
  fs::path folder_;
  std::vector<std::string> missing_;
  std::function<void()> ready_;
  std::chrono::steady_clock::time_point checked_at_;
};

}  // namespace

namespace mw2mp {

void PrepareInstallFolder(const fs::path& folder) {
  std::error_code ec;
  fs::create_directories(folder / "game", ec);
  fs::create_directories(folder / "update", ec);
  WriteNote(folder / "game" / "PUT_YOUR_DISC_FILES_HERE.txt", kGameNote);
  WriteNote(folder / "update" / "PUT_TITLE_UPDATE_6_HERE.txt", kUpdateNote);
}

std::vector<std::string> MissingFiles(const fs::path& folder) {
  // What the game cannot start without; the rest of the disc comes with these when the setup script copied it.
  // Optional files (the shader library's index, pipelines/, mods/) are made or skipped by the game.
  std::vector<fs::path> needed;
  if (MW2_CAMPAIGN) {
    needed.push_back(folder / "game" / "code_post_gfx.ff");
    needed.push_back(folder / "game" / "default.xex");
    needed.push_back(folder / "game" / "default.xexp");
  } else {
    needed.push_back(folder / "game" / "code_post_gfx_mp.ff");
    needed.push_back(folder / "game" / "default_mp.xex");
    needed.push_back(folder / "game" / "default_mp.xexp");
    for (const char* name : {"patch_mp.ff", "dlc1_ui_mp.ff", "dlc2_ui_mp.ff"}) {
      needed.push_back(folder / "update" / name);
    }
  }
  needed.push_back(folder / MW2MP_SHADER_LIBRARY_FILE);
  std::vector<std::string> missing;
  for (const fs::path& path : needed) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
      const bool disc = path.extension() == ".ff" && path.parent_path().filename() == "game";
      missing.push_back(ForPlayer(path) + (disc ? "  (and the rest of the disc)" : ""));
    }
  }
  return missing;
}

void ShowMissingFiles(rex::ui::ImGuiDrawer* drawer, const fs::path& folder, std::function<void()> ready) {
  new MissingFilesScreen(drawer, folder, MissingFiles(folder), std::move(ready));  // deletes itself (Close)
}

}  // namespace mw2mp
