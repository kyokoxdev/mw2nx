// The console's user nickname, for the game's gamertag (mw2mp_user_name.h). Its own file: switch.h's typedefs collide
// with rex's headers.

#include "mw2mp_user_name.h"

#if defined(__SWITCH__)
#include <switch.h>

#include <cstring>
#endif

namespace mw2mp {

std::string SystemUserName() {
#if defined(__SWITCH__)
  // acc:u1: an NRO from hbmenu has no launch user, so acc:u0's preselected user is missing there; u1 also answers the
  // last opened user and the list.
  if (R_FAILED(accountInitialize(AccountServiceType_System))) {
    return {};
  }
  AccountUid uid{};
  // The user the app was started with (an installed app), else the user who last opened a program (hbmenu's
  // title takeover), else the console's first user.
  if (R_FAILED(accountGetPreselectedUser(&uid)) || !accountUidIsValid(&uid)) {
    if (R_FAILED(accountGetLastOpenedUser(&uid)) || !accountUidIsValid(&uid)) {
      s32 count = 0;
      if (R_FAILED(accountListAllUsers(&uid, 1, &count)) || count < 1) {
        uid = {};
      }
    }
  }
  std::string name;
  AccountProfile profile;
  if (accountUidIsValid(&uid) && R_SUCCEEDED(accountGetProfile(&profile, uid))) {
    AccountProfileBase base{};
    if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base))) {
      name.assign(base.nickname, strnlen(base.nickname, sizeof(base.nickname)));
    }
    accountProfileClose(&profile);
  }
  accountExit();
  return name;
#else
  return {};
#endif
}

}  // namespace mw2mp
