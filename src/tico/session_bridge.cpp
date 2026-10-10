// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tico/session_bridge.h"
#include "TicoSession.h"

namespace TicoBridge {

std::string UserContentRoot(const std::string& folder, bool saves) {
    return tico::UserContentRoot(folder, saves);
}

bool InheritsShared() {
    return tico::CurrentSession().inheritsShared;
}

std::string SettingsText(const char* name) {
    return tico::SettingsText(name);
}

} // namespace TicoBridge
