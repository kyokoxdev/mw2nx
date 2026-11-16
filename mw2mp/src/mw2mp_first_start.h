#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rex::ui {
class ImGuiDrawer;
}

namespace mw2mp {

// The installed program's first start (mw2mp_launch.h): its folder (sdmc:/atmosphere/contents/<program id>/) with
// game/ and update/, each holding a note on what goes in it. Existing files are left alone.
void PrepareInstallFolder(const std::filesystem::path& folder);

// The files this mode needs that are not in the folder yet, as the player should copy them (sdmc:/... paths).
std::vector<std::string> MissingFiles(const std::filesystem::path& folder);

// A full-screen list of the missing files, checked again every two seconds; when they are all there it closes and
// calls ready on the UI thread (the game then starts as usual).
void ShowMissingFiles(rex::ui::ImGuiDrawer* drawer, const std::filesystem::path& folder, std::function<void()> ready);

}  // namespace mw2mp
