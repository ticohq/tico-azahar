// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

// tico's sealed session (TicoSession.h) for Azahar's own code. That header
// brings in libnx's switch.h, whose types clash with Azahar's (u128,
// Service), so only session_bridge.cpp includes it.
#pragma once

#include <string>

namespace TicoBridge {

/// tico::UserContentRoot: the current user's saves or states folder.
std::string UserContentRoot(const std::string& folder, bool saves);
/// tico::Session::inheritsShared: old data in tico/system/3ds is this user's.
bool InheritsShared();
/// tico::SettingsText: tico's "general", "display" or "audio" settings.
std::string SettingsText(const char* name);

} // namespace TicoBridge
