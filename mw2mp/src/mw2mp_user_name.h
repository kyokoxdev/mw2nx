#pragma once

#include <string>

namespace mw2mp {

// The Switch user's nickname (UTF-8, as the console shows it): the user the app was started with, else the last user
// who opened a program, else the console's first user. "" off the Switch or when the account service refuses.
std::string SystemUserName();

}  // namespace mw2mp
