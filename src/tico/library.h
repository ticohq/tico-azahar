// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <string>

// The game list shown when Azahar starts without a game.
namespace SwitchFrontend::Library {

// Registers the overlay's library (the 3DS games in tico's ROM folders and the
// module's own) and its Settings > Library folder editor. `launch` gets the
// chosen game.
void Register(std::function<void(const std::string& path)> launch);
void Unregister();

} // namespace SwitchFrontend::Library
