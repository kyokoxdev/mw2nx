// Script mods for multiplayer: a GSC loader and the script functions bot mods need, compatible with CoD Xe's MW2 TU6
// mods (github.com/michaeloliverx/codxe, src/game/iw4/mp_tu6; same default_mp.xex, so its addresses are ours).
//
//   sdmc:/switch/mw2mp/mods/<name>/...   one folder per mod, the game's script paths inside it
//   mw2mp_mod = "<name>" in mw2mp.toml   the active mod ("" = none)
//
// - Scripts: MW2 compiles GSC source when a map loads. Scr_AddSourceBuffer (8229F2C8) reads each script by its game path
//   ("maps/mp/gametypes/_callbacksetup.gsc"); a file at that path in the mod replaces the game's or adds a new one.
// - Script functions: the compiler asks Scr_GetFunction / Scr_GetMethod (82254C38 / 82254D88) for every name it does
//   not know. Ours answer first, each a host function behind a guest address of its own (FunctionDispatcher thunks).
//   printconsole, fileread/filewrite/fileexists/openfile/readstream/closefile (paths inside the mod folder),
//   addtestclient([name]) and the bot methods botaction, botstop, botmovement, botmeleeparams, botremoteangles,
//   botangles, as CoD Xe's.
// - Bots: SV_BotUserMove (822C3208) builds a test client's input each server frame; ours builds it from what the bot
//   methods set. Test clients may hold Use (planting, defusing) and get the weapon scripts switch them to.
//
// Everything here runs on the Server thread (script VM and server frame), so the bot table needs no lock.

#include <array>
#include <cstdint>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <strings.h>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_STRING(mw2mp_mod, "bot_warfare", "MW2",
                      "Script mod: the folder name under mods/ beside the NRO (sdmc:/switch/mw2mp/mods/<name>), whose "
                      "GSC scripts replace or add to the game's. Bot Warfare ships in mods/ (mw2mp/mods). \"\" = none")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Game functions (TU6 default_mp addresses, from CoD Xe's mp_tu6/symbols.h): the hooked ones, then the ones only called
// ("call only" keeps tools/direct_calls.py from counting them as hooks).
#define GAME_FN(name, address) \
  REX_EXTERN(__imp__sub_##address); \
  [[maybe_unused]] static PPCFunc* const name = __imp__sub_##address
REX_EXTERN(__imp__sub_8229F2C8);  // Scr_AddSourceBuffer(filename, extFilename) -> char*
REX_EXTERN(__imp__sub_82254C38);  // Scr_GetFunction(const char** name, int* type)
REX_EXTERN(__imp__sub_82254D88);  // Scr_GetMethod(const char** name, int* type)
REX_EXTERN(__imp__sub_822C3208);  // SV_BotUserMove(client_t*)
REX_EXTERN(__imp__sub_822C3B10);  // SV_IsClientBot(clientNum)
REX_EXTERN(__imp__sub_82261410);  // G_SelectWeaponIndex(clientNum, weaponIndex)
REX_EXTERN(__imp__sub_822A36E8);  // Scr_ShutdownSystem(sys)
GAME_FN(Hunk_AllocateTempMemoryHighInternal, 822FDF08);  // (size)  call only
GAME_FN(Scr_RegisterFunction, 822963C0);  // (func, name)  call only
GAME_FN(Scr_GetNumParam, 822ADC88);  // ()  call only
GAME_FN(Scr_GetString, 822B33A8);  // (i)  call only
GAME_FN(Scr_GetInt, 822B2D70);  // (i)  call only
GAME_FN(Scr_GetFloat, 822B30D0);  // (i) -> f1  call only
GAME_FN(Scr_AddInt, 822ADD18);  // (v)  call only
GAME_FN(Scr_AddUndefined, 822ADE98);  // ()  call only
GAME_FN(Scr_AddString, 822ADFF0);  // (s)  call only
GAME_FN(Scr_AddEntity, 82259A60);  // (ent)  call only
GAME_FN(Scr_Error, 822AE470);  // (msg)  call only
GAME_FN(Scr_ParamError, 822AE600);  // (i, msg)  call only
GAME_FN(GetPlayerEntity, 8223F540);  // (entref) -> gentity*  call only
GAME_FN(Com_Printf, 8227F448);  // (channel, fmt, ...)  call only
GAME_FN(SV_AddTestClient, 822BDCB8);  // () -> gentity*  call only
GAME_FN(Info_SetValueForKey, 823167D8);  // (s, key, value)  call only
GAME_FN(SV_UserinfoChanged, 822BBED8);  // (client_t*)  call only
GAME_FN(SV_ClientThink, 822BC778);  // (client_t*, usercmd_s*)  call only

namespace {

// Guest data (CoD Xe mp_tu6/structs.h, checked against the stock SV_BotUserMove's code).
constexpr uint32_t kGEntities = 0x82E2A580, kGEntitySize = 640;  // g_entities  call only
constexpr uint32_t kLevel = 0x82FF2F08;                            // level_locals_t: gclient_s* clients at +0  call only
constexpr uint32_t kGClientSize = 0x3700;                           // gclient_s, playerState_s at +0
constexpr uint32_t kSvs = 0x8367A010;  // serverStaticHeader_t: time +0, clientCount +8, clients +0xC  call only
constexpr uint32_t kClientSize = 0x97F80;  // client_t
constexpr uint32_t kClientDeltaMessage = 0x8, kClientOutgoingSequence = 0x18, kClientUserinfo = 0x650,
                   kClientGentity = 0x21294;
constexpr uint32_t kPsDeltaAngles = 0x60, kPsViewAngles = 0x10C, kPsOffHand = 0x2A8, kPsWeapon = 0x2B4,
                   kPsPrimaryForAlt = 0x2B8;
constexpr uint32_t kSvBotUserMoveActivateCheck = 0x82263650;  // Player_ActivateHoldCmd's return from SV_IsClientBot
constexpr int kMaxClients = 18;

uint32_t Load32(uint8_t* base, uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); }
float LoadF(uint8_t* base, uint32_t a) { return rex::memory::load_and_swap<float>(base + a); }
template <typename T>
void Store(uint8_t* base, uint32_t a, T v) { rex::memory::store_and_swap<T>(base + a, v); }
const char* GuestStr(uint8_t* base, uint32_t a) { return a ? reinterpret_cast<const char*>(base + a) : ""; }

// Guest calls from a hook: scratch memory below the caller's stack pointer (ours as the callee), the callee's frame
// below that. Locals stay trivially destructible: Scr_Error longjmps over this frame.
struct Guest {
  PPCContext& parent;
  uint8_t* base;
  uint32_t top;
  Guest(PPCContext& ctx, uint8_t* b) : parent(ctx), base(b), top(ctx.r1.u32 - 0x100) {}
  uint32_t Alloc(uint32_t n) {
    top = (top - n) & ~15u;
    std::memset(base + top, 0, n);
    return top;
  }
  uint32_t Str(const char* s) {
    const uint32_t n = uint32_t(std::strlen(s)) + 1, a = Alloc(n);
    std::memcpy(base + a, s, n);
    return a;
  }
  struct Result {
    uint64_t r3;
    double f1;
  };
  Result Call(PPCFunc* fn, uint64_t r3 = 0, uint64_t r4 = 0, uint64_t r5 = 0) {
    rex::CallFrame frame(parent);
    frame.ctx.r1.u32 = top - 0x80;
    frame.ctx.r3.u64 = r3;
    frame.ctx.r4.u64 = r4;
    frame.ctx.r5.u64 = r5;
    fn(frame.ctx, base);
    return {frame.ctx.r3.u64, frame.ctx.f1.f64};
  }
  uint32_t Call32(PPCFunc* fn, uint64_t r3 = 0, uint64_t r4 = 0, uint64_t r5 = 0) {
    return uint32_t(Call(fn, r3, r4, r5).r3);
  }
  [[noreturn]] void Error(const char* msg) {
    Call(Scr_Error, Str(msg));
    __builtin_unreachable();
  }
  [[noreturn]] void ParamError(uint32_t i, const char* msg) {
    Call(Scr_ParamError, i, Str(msg));
    __builtin_unreachable();
  }
  uint32_t NumParam() { return Call32(Scr_GetNumParam); }
  const char* String(uint32_t i) { return GuestStr(base, Call32(Scr_GetString, i)); }
  int32_t Int(uint32_t i) { return int32_t(Call32(Scr_GetInt, i)); }
  float Float(uint32_t i) { return float(Call(Scr_GetFloat, i).f1); }
  void AddInt(int32_t v) { Call(Scr_AddInt, uint32_t(v)); }
  void AddUndefined() { Call(Scr_AddUndefined); }
  void AddString(const char* s) { Call(Scr_AddString, Str(s)); }
};

// --- Mod files -----------------------------------------------------------------------------------------------------

// sdmc:/switch/mw2mp/mods/<mw2mp_mod>/, empty when no mod is set. Beside the NRO, as mw2mp_app.h's other folders.
const std::filesystem::path& ModDir() {
  static const std::filesystem::path dir = [] {
    const std::string& name = REXCVAR_GET(mw2mp_mod);
    if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name == "." || name == "..") {
      if (!name.empty()) REXLOG_ERROR("[gsc] mw2mp_mod \"{}\": not a folder name", name);
      return std::filesystem::path();
    }
    std::error_code ec;
    auto root = rex::filesystem::GetExecutableFolder();
    if (!std::filesystem::is_directory(root / "game", ec)) root = "/switch/mw2mp";
    auto path = root / "mods" / name;
    if (!std::filesystem::is_directory(path, ec)) {
      REXLOG_ERROR("[gsc] mod \"{}\": no folder {}", name, path.string());
      return std::filesystem::path();
    }
    REXLOG_INFO("[gsc] mod \"{}\" ({})", name, path.string());
    return path;
  }();
  return dir;
}

// A path inside the mod for a script-supplied relative path; false for none, absolute or escaping ones.
bool ModPath(const char* relative, char (&out)[512]) {
  const auto& dir = ModDir();
  if (dir.empty() || !*relative || *relative == '/' || *relative == '\\' || std::strchr(relative, ':') ||
      std::strstr(relative, "..")) {
    return false;
  }
  const int n = std::snprintf(out, sizeof(out), "%s/%s", dir.c_str(), relative);
  return n > 0 && n < int(sizeof(out));
}

// --- Script functions ----------------------------------------------------------------------------------------------

FILE* g_stream = nullptr;  // openfile/readstream/closefile: one file at a time, as CoD Xe
constexpr uint32_t kTextSize = 65536;  // fileread's contents, readstream's line (as CoD Xe, 64 KiB at most)

// A guest buffer for file text handed to Scr_AddString: too big for the guest stack.
char* Text(uint8_t* base, uint32_t& guest) {
  static const uint32_t text = rex::system::kernel_state()->memory()->SystemHeapAlloc(kTextSize);
  guest = text;
  return text ? reinterpret_cast<char*>(base + text) : nullptr;
}

void CloseStream() {
  if (g_stream) std::fclose(g_stream);
  g_stream = nullptr;
}

void GScr_PrintConsole(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  if (g.NumParam() != 1) g.Error("Usage: printconsole(<string>)");
  const char* text = g.String(0);
  REXLOG_INFO("[gsc] {}", text);
  g.Call(Com_Printf, 0, g.Str("%s"), g.Str(text));
}

void GScr_FileWrite(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  if (g.NumParam() != 3) g.Error("Usage: filewrite(<file>, <contents>, <mode>)");
  const char* mode = g.String(2);
  const char* fmode = !strcasecmp(mode, "write") ? "wb" : !strcasecmp(mode, "append") ? "ab" : nullptr;
  if (!fmode) g.ParamError(2, "filewrite: mode must be \"write\" or \"append\"");
  char path[512];
  bool ok = false;
  if (ModPath(g.String(0), path)) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (FILE* f = std::fopen(path, fmode)) {
      const char* contents = g.String(1);
      const size_t n = std::strlen(contents);
      ok = std::fwrite(contents, 1, n, f) == n;
      std::fclose(f);
    }
  }
  g.AddInt(ok);
}

void GScr_FileRead(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  if (g.NumParam() != 1) g.Error("Usage: fileread(<file>)");
  char path[512];
  FILE* f = ModPath(g.String(0), path) ? std::fopen(path, "rb") : nullptr;
  uint32_t guest;
  char* text = f ? Text(base, guest) : nullptr;
  if (!text) {
    if (f) std::fclose(f);
    return g.AddUndefined();
  }
  const size_t n = std::fread(text, 1, kTextSize - 1, f);
  std::fclose(f);
  text[n] = '\0';
  g.Call(Scr_AddString, guest);
}

void GScr_FileExists(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  if (g.NumParam() != 1) g.Error("Usage: fileexists(<file>)");
  char path[512];
  std::error_code ec;
  g.AddInt(ModPath(g.String(0), path) && std::filesystem::is_regular_file(path, ec));
}

void GScr_OpenFile(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  if (g.NumParam() != 2) g.Error("Usage: openfile(<file>, <mode>)");
  char path[512];
  if (strcasecmp(g.String(1), "read") || g_stream || !ModPath(g.String(0), path) ||
      !(g_stream = std::fopen(path, "r"))) {
    return g.AddInt(-1);
  }
  g.AddInt(1);
}

void GScr_ReadStream(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  if (g.NumParam() != 0) g.Error("Usage: readstream()");
  uint32_t guest;
  char* text = g_stream ? Text(base, guest) : nullptr;
  if (!text || !std::fgets(text, 4096, g_stream)) return g.AddUndefined();
  g.Call(Scr_AddString, guest);
}

void GScr_CloseFile(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  if (g.NumParam() != 0) g.Error("Usage: closefile()");
  if (!g_stream) return g.AddInt(-1);
  const int rc = std::fclose(g_stream);
  g_stream = nullptr;
  g.AddInt(rc);
}

// --- Bots ----------------------------------------------------------------------------------------------------------

struct BotInput {
  bool active, has_move, has_angles, has_remote_angles;
  uint32_t buttons;
  uint16_t weapon;
  int8_t forward, right;
  int8_t remote_angles[2];
  float angles[3];
  float melee_yaw;
  uint8_t melee_dist;
};
BotInput g_bots[kMaxClients];

int8_t ClampMove(int v) { return int8_t(v < -127 ? -127 : v > 127 ? 127 : v); }
uint32_t PlayerState(uint8_t* base, int num) { return Load32(base, kLevel) + uint32_t(num) * kGClientSize; }

// GetPlayerEntity checks the entity is a player (or raises the script error); the slot is its entity number.
int BotSlot(Guest& g, const char* usage, uint32_t params) {
  const uint32_t ent = g.Call32(GetPlayerEntity, g.parent.r3.u32);
  if (g.NumParam() != params) g.Error(usage);
  const int num = int((ent - kGEntities) / kGEntitySize);
  if (num < 0 || num >= kMaxClients) g.Error("bot method: not a client");
  return num;
}

struct BotAction {
  const char* name;
  uint32_t buttons;
};
constexpr BotAction kBotActions[] = {
    {"gostand", 1u << 10},  {"gocrouch", 1u << 9},          {"goprone", 1u << 8},   {"fire", 1u << 0},
    {"attack", 1u << 0},    {"melee", 1u << 2},             {"frag", 1u << 14},     {"smoke", 1u << 15},
    {"reload", 1u << 4},    {"sprint", 1u << 1},            {"leanleft", 1u << 6},  {"leanright", 1u << 7},
    {"ads", 0x80800},       {"speed_throw", 0x80800},       {"holdbreath", 1u << 13}, {"usereload", 1u << 5},
    {"activate", 1u << 3},  {"use", 1u << 3},               {"remote", 1u << 20},   {"crouch", 1u << 9},
    {"prone", 1u << 8},
};

void PlayerCmd_BotAction(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  const int num = BotSlot(g, "Usage: <bot> botAction(<action>);", 1);
  const char* action = g.String(0);
  if (action[0] != '+' && action[0] != '-') g.ParamError(0, "Sign for bot action must be '+' or '-'.");
  for (const auto& a : kBotActions) {
    if (!strcasecmp(action + 1, a.name)) {
      g_bots[num].buttons = action[0] == '+' ? g_bots[num].buttons | a.buttons : g_bots[num].buttons & ~a.buttons;
      g_bots[num].active = true;
      return;
    }
  }
  g.ParamError(0, "Unknown bot action.");
}

void PlayerCmd_BotStop(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  const int num = BotSlot(g, "Usage: <bot> botStop();", 0);
  const uint32_t ps = PlayerState(base, num);
  BotInput& b = g_bots[num];
  b = BotInput{};
  b.active = true;
  for (int i = 0; i < 3; ++i) b.angles[i] = LoadF(base, ps + kPsViewAngles + 4 * i);
  b.weapon = uint16_t(Load32(base, ps + kPsWeapon));
}

void PlayerCmd_BotMovement(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  const int num = BotSlot(g, "Usage: <bot> botMovement(<forward>, <right>);", 2);
  g_bots[num].forward = ClampMove(g.Int(0));
  g_bots[num].right = ClampMove(g.Int(1));
  g_bots[num].active = g_bots[num].has_move = true;
}

void PlayerCmd_BotMeleeParams(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  const int num = BotSlot(g, "Usage: <bot> botMeleeParams(<yaw>, <dist>);", 2);
  g_bots[num].melee_yaw = g.Float(0);
  const int dist = int(g.Float(1));
  g_bots[num].melee_dist = uint8_t(dist < 0 ? 0 : dist > 255 ? 255 : dist);
  g_bots[num].active = true;
}

void PlayerCmd_BotRemoteAngles(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  const int num = BotSlot(g, "Usage: <bot> botRemoteAngles(<pitch>, <yaw>);", 2);
  g_bots[num].remote_angles[0] = ClampMove(int(g.Float(0)));
  g_bots[num].remote_angles[1] = ClampMove(int(g.Float(1)));
  g_bots[num].active = g_bots[num].has_remote_angles = true;
}

void PlayerCmd_BotAngles(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  const int num = BotSlot(g, "Usage: <bot> botAngles(<pitch>, <yaw>, <roll>);", 3);
  for (int i = 0; i < 3; ++i) g_bots[num].angles[i] = g.Float(uint32_t(i));
  g_bots[num].active = g_bots[num].has_angles = true;
}

// addtestclient([name]): the stock test client, renamed when a name is given.
void GScr_AddTestClient(PPCContext& ctx, uint8_t* base) {
  Guest g(ctx, base);
  const uint32_t params = g.NumParam();
  if (params > 1) g.Error("Usage: addtestclient(<name>);");
  char name[32] = {};
  if (params == 1) {
    int j = 0;
    for (const char* s = g.String(0); *s && j < int(sizeof(name)) - 1; ++s) {
      if (uint8_t(*s) >= 0x20) name[j++] = *s;
    }
    if (!j) g.Error("addtestclient: name must be at least 1 character long");
  }
  const uint32_t ent = g.Call32(SV_AddTestClient);
  if (!ent) return;
  if (name[0]) {
    const uint32_t cl = Load32(base, kSvs + 0xC) + (ent - kGEntities) / kGEntitySize * kClientSize;
    g.Call(Info_SetValueForKey, cl + kClientUserinfo, g.Str("name"), g.Str(name));
    g.Call(SV_UserinfoChanged, cl);
  }
  g.Call(Scr_AddEntity, ent);
}

// --- Registration --------------------------------------------------------------------------------------------------

struct Builtin {
  const char* name;
  PPCFunc* fn;
};
constexpr Builtin kFunctions[] = {
    {"printconsole", GScr_PrintConsole}, {"filewrite", GScr_FileWrite},   {"fileread", GScr_FileRead},
    {"fileexists", GScr_FileExists},     {"openfile", GScr_OpenFile},     {"readstream", GScr_ReadStream},
    {"closefile", GScr_CloseFile},       {"addtestclient", GScr_AddTestClient},
};
constexpr Builtin kMethods[] = {
    {"botaction", PlayerCmd_BotAction},         {"botstop", PlayerCmd_BotStop},
    {"botmovement", PlayerCmd_BotMovement},     {"botmeleeparams", PlayerCmd_BotMeleeParams},
    {"botremoteangles", PlayerCmd_BotRemoteAngles}, {"botangles", PlayerCmd_BotAngles},
};
constexpr size_t kBuiltins = std::size(kFunctions) + std::size(kMethods);

// Guest addresses for the builtins, kFunctions then kMethods: the VM calls them through the dispatch table.
const uint32_t* Thunks() {
  static const auto thunks = [] {
    std::array<uint32_t, kBuiltins> t{};
    auto* dispatcher = rex::system::kernel_state()->function_dispatcher();
    size_t i = 0;
    for (const auto& b : kFunctions) t[i++] = dispatcher->AllocateThunk(b.fn, 0);
    for (const auto& b : kMethods) t[i++] = dispatcher->AllocateThunk(b.fn, 0);
    return t;
  }();
  return thunks.data();
}

// Scr_GetFunction/Scr_GetMethod: name null = register every builtin (the game's debugger table); else our builtin of
// that name, if any, into r3 (and its type, any, into *type).
template <size_t N>
bool AnswerBuiltin(PPCContext& ctx, uint8_t* base, const Builtin (&table)[N], const uint32_t* thunks) {
  const uint32_t pname = ctx.r3.u32, ptype = ctx.r4.u32;
  if (!pname) {
    Guest g(ctx, base);
    for (size_t i = 0; i < N; ++i) {
      if (thunks[i]) g.Call(Scr_RegisterFunction, thunks[i], g.Str(table[i].name));
    }
    return false;
  }
  const char* name = GuestStr(base, Load32(base, pname));
  for (size_t i = 0; i < N; ++i) {
    if (thunks[i] && !strcasecmp(name, table[i].name)) {
      if (ptype) Store<uint32_t>(base, ptype, 0);
      ctx.r3.u64 = thunks[i];
      return true;
    }
  }
  return false;
}

}  // namespace

// Scripts from the active mod first.
REX_HOOK_RAW(sub_8229F2C8) {
  char path[512];
  const char* script = GuestStr(base, ctx.r4.u32);
  if (FILE* f = ModPath(script, path) ? std::fopen(path, "rb") : nullptr) {
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    Guest g(ctx, base);
    const uint32_t buffer = size > 0 ? g.Call32(Hunk_AllocateTempMemoryHighInternal, uint32_t(size) + 1) : 0;
    const bool ok = buffer && std::fread(base + buffer, 1, size_t(size), f) == size_t(size);
    std::fclose(f);
    if (ok) {
      base[buffer + uint32_t(size)] = 0;
      REXLOG_INFO("[gsc] {} from the mod", script);
      ctx.r3.u64 = buffer;
      return;
    }
    REXLOG_ERROR("[gsc] {}: could not read {}", script, path);
  }
  __imp__sub_8229F2C8(ctx, base);
}

REX_HOOK_RAW(sub_82254C38) {
  if (!AnswerBuiltin(ctx, base, kFunctions, Thunks())) __imp__sub_82254C38(ctx, base);
}

REX_HOOK_RAW(sub_82254D88) {
  if (!AnswerBuiltin(ctx, base, kMethods, Thunks() + std::size(kFunctions))) __imp__sub_82254D88(ctx, base);
}

// A test client's input: what the bot methods set (CoD Xe's SV_BotUserMove_Stub). Humans the scripts never drove keep
// the stock path.
REX_HOOK_RAW(sub_822C3208) {
  const uint32_t cl = ctx.r3.u32;
  const uint32_t clients = Load32(base, kSvs + 0xC);
  const int num = cl >= clients ? int((cl - clients) / kClientSize) : -1;
  if (!cl || !Load32(base, cl + kClientGentity) || num < 0 || num >= kMaxClients ||
      num >= int(Load32(base, kSvs + 8))) {
    return;
  }
  Guest g(ctx, base);
  const BotInput& b = g_bots[num];
  if (!b.active && !(g.Call32(__imp__sub_822C3B10, uint32_t(num)) & 0xFF)) {
    return __imp__sub_822C3208(ctx, base);
  }
  const uint32_t ps = PlayerState(base, num);
  const uint32_t cmd = g.Alloc(0x28);  // usercmd_s
  Store<uint32_t>(base, cmd + 0x00, Load32(base, kSvs));
  Store<uint32_t>(base, cmd + 0x04, b.buttons);
  if (b.has_angles) {
    for (int i = 0; i < 3; ++i) {
      const float delta = b.angles[i] - LoadF(base, ps + kPsDeltaAngles + 4 * i);
      Store<int32_t>(base, cmd + 0x08 + 4 * i, int32_t(delta * (65536.0f / 360.0f)) & 65535);
    }
  }
  Store<uint16_t>(base, cmd + 0x14, b.weapon ? b.weapon : uint16_t(Load32(base, ps + kPsWeapon)));
  // ponytail: alt-mode weapons (grenade launcher attachments) keep the playerState's primary; CoD Xe looks up the
  // primary owning the alt weapon (WeaponCompleteDef::altWeaponIndex) if bots need to switch into one.
  Store<uint16_t>(base, cmd + 0x16, uint16_t(Load32(base, ps + kPsPrimaryForAlt)));
  Store<uint16_t>(base, cmd + 0x18, uint16_t(Load32(base, ps + kPsOffHand)));
  if (b.has_move) {
    base[cmd + 0x1A] = uint8_t(b.forward);
    base[cmd + 0x1B] = uint8_t(b.right);
  }
  Store<float>(base, cmd + 0x1C, b.melee_yaw);
  base[cmd + 0x20] = b.melee_dist;
  if (b.has_remote_angles) {
    base[cmd + 0x24] = uint8_t(b.remote_angles[0]);
    base[cmd + 0x25] = uint8_t(b.remote_angles[1]);
  }
  Store<uint32_t>(base, cl + kClientDeltaMessage, Load32(base, cl + kClientOutgoingSequence) - 1);
  g.Call(SV_ClientThink, cl, cmd);
}

// Player_ActivateHoldCmd skips Use for bots; CoD Xe nops that check (0x82263658), here its SV_IsClientBot says no.
REX_HOOK_RAW(sub_822C3B10) {
  if (ctx.lr == kSvBotUserMoveActivateCheck) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_822C3B10(ctx, base);
}

// The weapon a script switches a client to (switchToWeapon, giveWeapon...): test clients ask for it in their input.
REX_HOOK_RAW(sub_82261410) {
  const int num = ctx.r3.s32;
  if (num >= 0 && num < kMaxClients) g_bots[num].weapon = uint16_t(ctx.r4.u32);
  __imp__sub_82261410(ctx, base);
}

// Script VM shutdown (map end): bot input and the open script file go with it.
REX_HOOK_RAW(sub_822A36E8) {
  std::memset(g_bots, 0, sizeof(g_bots));
  CloseStream();
  __imp__sub_822A36E8(ctx, base);
}
