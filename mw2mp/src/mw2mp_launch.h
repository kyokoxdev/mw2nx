#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mw2mp {

// True for the installed program (this executable is the NSP's ExeFS main, an NSO), false for an NRO from the Homebrew
// Menu.
bool IsInstalledProgram();

// The installed program's saves: the console's save data for this program and the Switch user it runs as, mounted
// as "save:" (so save managers such as JKSV see and back it up, and deleting the program's SD card folder cannot take
// it). Creates the save the first time. Writes there are committed within about two seconds of the last file closing.
// Returns "save:/" when mounted, else "" with the reason in *error.
std::string MountUserSave(std::string* error);

// Commits the save now (before the program restarts or ends). Nothing to do when nothing changed.
void CommitUserSave();

// The launch data the other program passed when it started this one (its XamLoaderLaunchTitle); false when the
// program was started from the HOME menu. Read once.
bool TakeLaunchData(std::vector<uint8_t>* out);

}  // namespace mw2mp
