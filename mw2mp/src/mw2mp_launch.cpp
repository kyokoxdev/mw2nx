// The installed program (mw2mp_launch.h): switching between the Campaign and Multiplayer, and the saves in the
// console's save data. Its own file: switch.h's typedefs collide with rex's headers.
//
// The NSP is one application with two programs, as retail collections are: program 0 is the Campaign (mw2sp), program
// 1 Multiplayer (mw2mp), each its own ExeFS main. On the 360, default.xex and default_mp.xex start each other with
// XamLoaderLaunchTitle: the running title ends and the other executable starts from nothing, with only the launch data
// carried over. Here the game's XamLoaderLaunchTitle (sdk/src/kernel/xam/xam_info.cpp) calls RexSwitchLaunchTitle,
// which does the same with appletExecuteProgram: Horizon ends this program and starts the other one, handing it the
// launch data in its user channel.

#include "mw2mp_launch.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#if defined(__SWITCH__)
#include <switch.h>
#include <fcntl.h>
#include <sys/iosupport.h>
#endif

namespace {
// The programs of the application (tools/make_nsp.py packs them in this order).
constexpr s32 kProgramCampaign = 0;
constexpr s32 kProgramMultiplayer = 1;
// What the other program finds in its user channel: magic, launch data size, the Switch user (16 bytes), launch data.
// The user: Horizon does not carry the preselected user over appletExecuteProgram (run mw2mp_001: Multiplayer
// started from the Campaign had none, 2345-0009, and saved to the SD card instead of the Campaign's save).
constexpr uint32_t kLaunchMagic = 0x5532574Du;  // "MW2U"
constexpr size_t kHeaderSize = 24;
constexpr size_t kRequestMax = 0x1000;  // appletExecuteProgram's limit

// The request this program was started with, read once (the user channel pops once): MountUserSave needs its user
// before TakeLaunchData hands over its launch data.
struct LaunchRequest {
  bool present = false;
  uint64_t uid[2] = {};
  std::vector<uint8_t> data;
};
LaunchRequest g_request;
uint64_t g_save_uid[2] = {};  // the user whose save is mounted, passed on to the other program

bool EndsWithNoCase(const char* text, const char* suffix) {
  const size_t n = strlen(text);
  const size_t m = strlen(suffix);
  if (m > n) {
    return false;
  }
  for (size_t i = 0; i < m; ++i) {
    char c = text[n - m + i];
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
    if (c != suffix[i]) {
      return false;
    }
  }
  return true;
}

#if defined(__SWITCH__)
// The save's size: MW2's profile and saves are well under 1 MB on the 360; the journal bounds what one commit holds.
constexpr s64 kSaveDataSize = 32ll << 20;
constexpr s64 kSaveJournalSize = 16ll << 20;
constexpr char kSaveDevice[] = "save";

// Bumped by every change on "save:" (the wrapped device calls below); the commit thread commits when it moved.
std::atomic<uint64_t> g_save_changes{0};
std::atomic<bool> g_save_mounted{false};
devoptab_t g_save_original;  // libnx's calls, which the wrappers forward to

int SaveOpen(struct _reent* r, void* file, const char* path, int flags, int mode) {
  if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) g_save_changes++;
  return g_save_original.open_r(r, file, path, flags, mode);
}
ssize_t SaveWrite(struct _reent* r, void* fd, const char* ptr, size_t len) {
  g_save_changes++;
  return g_save_original.write_r(r, fd, ptr, len);
}
int SaveClose(struct _reent* r, void* fd) {
  const int result = g_save_original.close_r(r, fd);
  g_save_changes++;  // a closed writer is what lets the commit go through
  return result;
}
int SaveUnlink(struct _reent* r, const char* path) {
  g_save_changes++;
  return g_save_original.unlink_r(r, path);
}
int SaveRename(struct _reent* r, const char* old_name, const char* new_name) {
  g_save_changes++;
  return g_save_original.rename_r(r, old_name, new_name);
}
int SaveMkdir(struct _reent* r, const char* path, int mode) {
  g_save_changes++;
  return g_save_original.mkdir_r(r, path, mode);
}
int SaveRmdir(struct _reent* r, const char* path) {
  g_save_changes++;
  return g_save_original.rmdir_r(r, path);
}
int SaveFtruncate(struct _reent* r, void* fd, off_t len) {
  g_save_changes++;
  return g_save_original.ftruncate_r(r, fd, len);
}

// Committed and uncommitted change counts; a commit fails while any file is open for writing, and is tried again.
uint64_t g_save_committed = 0;

void CommitIfChanged() {
  static std::atomic_flag busy = ATOMIC_FLAG_INIT;
  if (busy.test_and_set()) return;
  const uint64_t changes = g_save_changes.load();
  if (changes != g_save_committed && R_SUCCEEDED(fsdevCommitDevice(kSaveDevice))) {
    g_save_committed = changes;
  }
  busy.clear();
}

#endif
}  // namespace

namespace mw2mp {

#if defined(__SWITCH__)
static const LaunchRequest& Request();
#endif

std::string MountUserSave(std::string* error) {
#if defined(__SWITCH__)
  auto fail = [error](const char* what, Result rc) {
    if (error) {
      char text[96];
      snprintf(text, sizeof(text), "%s (2%03u-%04u)", what, R_MODULE(rc), R_DESCRIPTION(rc));
      *error = text;
    }
    return std::string();
  };
  u64 program_id = 0;
  Result rc = svcGetInfo(&program_id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
  if (R_FAILED(rc)) return fail("no program id", rc);
  // One save for both programs: the application's (program 1's id is the application's + 1).
  const u64 application_id = program_id & ~u64(0xF);

  // The user the other program passed (a switch between Campaign and Multiplayer carries no preselection), else the
  // one this program was started with (the NACP asks for one at start), else the last one who opened a program.
  AccountUid uid{};
  memcpy(uid.uid, Request().uid, sizeof(uid.uid));
  rc = accountInitialize(AccountServiceType_Application);
  if (R_FAILED(rc)) return fail("account service", rc);
  if (accountUidIsValid(&uid)) {
    accountExit();
  } else if (R_FAILED(accountGetPreselectedUser(&uid)) || !accountUidIsValid(&uid)) {
    accountExit();
    if (R_SUCCEEDED(accountInitialize(AccountServiceType_System))) {
      if (R_FAILED(accountGetLastOpenedUser(&uid))) uid = {};
      accountExit();
    }
  } else {
    accountExit();
  }
  if (!accountUidIsValid(&uid)) return fail("no Switch user", MAKERESULT(Module_Libnx, LibnxError_NotFound));

  rc = fsdevMountSaveData(kSaveDevice, application_id, uid);
  if (R_FAILED(rc)) {
    // First start: create it as a game's own account save (what save managers list).
    FsSaveDataAttribute attr{};
    attr.application_id = application_id;
    attr.uid = uid;
    attr.save_data_type = FsSaveDataType_Account;
    FsSaveDataCreationInfo info{};
    info.save_data_size = kSaveDataSize;
    info.journal_size = kSaveJournalSize;
    info.available_size = 0x4000;
    info.owner_id = application_id;
    info.save_data_space_id = FsSaveDataSpaceId_User;
    FsSaveDataMetaInfo meta{};
    meta.size = 0x40060;
    meta.type = FsSaveDataMetaType_Thumbnail;
    const Result created = fsCreateSaveDataFileSystem(&attr, &info, &meta);
    if (R_FAILED(created)) return fail("save data not created", created);
    rc = fsdevMountSaveData(kSaveDevice, application_id, uid);
    if (R_FAILED(rc)) return fail("save data not mounted", rc);
  }

  memcpy(g_save_uid, uid.uid, sizeof(g_save_uid));

  // Every write on save: marks a change; libnx's calls stay underneath.
  auto* device = const_cast<devoptab_t*>(GetDeviceOpTab(kSaveDevice));
  if (!device) return fail("save device missing", MAKERESULT(Module_Libnx, LibnxError_NotFound));
  g_save_original = *device;
  device->open_r = SaveOpen;
  device->write_r = SaveWrite;
  device->close_r = SaveClose;
  device->unlink_r = SaveUnlink;
  device->rename_r = SaveRename;
  device->mkdir_r = SaveMkdir;
  device->rmdir_r = SaveRmdir;
  device->ftruncate_r = SaveFtruncate;
  g_save_mounted = true;

  // Leaked, never detached: std::thread::detach throws on libnx.
  new std::thread([] {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      CommitIfChanged();
    }
  });
  return "save:/";
#else
  if (error) *error = "not a Switch";
  return {};
#endif
}

void CommitUserSave() {
#if defined(__SWITCH__)
  if (g_save_mounted) CommitIfChanged();
#endif
}

bool IsInstalledProgram() {
#if defined(__SWITCH__)
  return envIsNso();
#else
  return false;
#endif
}

#if defined(__SWITCH__)
// Pops and checks the user channel's request on the first call; later calls return the same.
static const LaunchRequest& Request() {
  static bool popped = false;
  if (popped || !IsInstalledProgram()) {
    return g_request;
  }
  popped = true;
  AppletStorage storage;
  if (R_FAILED(appletPopLaunchParameter(&storage, AppletLaunchParameterKind_UserChannel))) {
    return g_request;  // started from the HOME menu
  }
  static uint8_t request[kRequestMax];
  s64 size = 0;
  const bool read = R_SUCCEEDED(appletStorageGetSize(&storage, &size)) && size >= s64(kHeaderSize) &&
                    size <= s64(sizeof(request)) &&
                    R_SUCCEEDED(appletStorageRead(&storage, 0, request, size_t(size)));
  appletStorageClose(&storage);
  uint32_t header[2] = {};
  if (read) {
    memcpy(header, request, sizeof(header));
  }
  if (!read || header[0] != kLaunchMagic || header[1] > size - s64(kHeaderSize)) {
    return g_request;
  }
  g_request.present = true;
  memcpy(g_request.uid, request + 8, sizeof(g_request.uid));
  g_request.data.assign(request + kHeaderSize, request + kHeaderSize + header[1]);
  return g_request;
}
#endif

bool TakeLaunchData(std::vector<uint8_t>* out) {
#if defined(__SWITCH__)
  const LaunchRequest& request = Request();
  if (!request.present) {
    return false;
  }
  *out = request.data;
  return true;
#else
  (void)out;
  return false;
#endif
}

}  // namespace mw2mp

// sdk/src/kernel/xam/xam_info.cpp's hook. Only the installed program: an NRO from the Homebrew Menu runs inside
// another title, whose programs these are not.
extern "C" int RexSwitchLaunchTitle(const char* guest_path, const uint8_t* launch_data, size_t launch_data_size) {
#if defined(__SWITCH__)
  if (!mw2mp::IsInstalledProgram() || !guest_path) {
    return 0;
  }
  s32 program;
  if (EndsWithNoCase(guest_path, "default_mp.xex")) {
    program = kProgramMultiplayer;
  } else if (EndsWithNoCase(guest_path, "default.xex")) {
    program = kProgramCampaign;
  } else {
    return 0;
  }
  if (launch_data_size > kRequestMax - kHeaderSize) {
    launch_data_size = 0;  // the 360 caps launch data well below this; never seen
  }
  // MW2's Sys_Error relaunches the title with "e<message>" for the next start to show. An error the relaunched program
  // hits again before it gets there relaunches it forever behind a black screen (issue #2: a disc TU6 does not patch,
  // ~3 s per start). The same error twice in a row: show it and let the title end instead.
  const LaunchRequest& started = mw2mp::Request();
  if (launch_data && launch_data_size && launch_data[0] == 'e' && started.data.size() == launch_data_size &&
      memcmp(started.data.data(), launch_data, launch_data_size) == 0) {
    char message[512];
    snprintf(message, sizeof(message), "%.*s", int(strnlen(reinterpret_cast<const char*>(launch_data) + 1,
                                                           launch_data_size - 1)),
             reinterpret_cast<const char*>(launch_data) + 1);
    ErrorApplicationConfig error;
    if (R_SUCCEEDED(errorApplicationCreate(&error, "Modern Warfare 2 stopped on an error at every start.", message))) {
      errorApplicationShow(&error);
    }
    return 0;
  }
  static uint8_t request[kRequestMax];
  const uint32_t header[2] = {kLaunchMagic, uint32_t(launch_data_size)};
  memcpy(request, header, sizeof(header));
  memcpy(request + 8, g_save_uid, sizeof(g_save_uid));  // zero without a mounted save: the other program looks itself
  if (launch_data_size) {
    memcpy(request + kHeaderSize, launch_data, launch_data_size);
  }
  mw2mp::CommitUserSave();
  const Result rc = appletExecuteProgram(program, request, kHeaderSize + launch_data_size);
  return R_SUCCEEDED(rc) ? 1 : -int(rc & 0x3FFFFF);
#else
  (void)guest_path;
  (void)launch_data;
  (void)launch_data_size;
  return 0;
#endif
}
