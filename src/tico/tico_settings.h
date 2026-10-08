// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace Core {
class System;
}

// tico's settings for Azahar (tico/module/settings.json, the azahar_* keys)
// applied to Azahar's own Settings::values.
namespace SwitchFrontend::TicoSettings {

// Configs written before the module have the old key names; they become the
// azahar_* ones once, so nothing set before is lost.
void MigrateOldKeys();

// Every option, before the game boots.
void Apply();

// After a change in the quick menu: the options that take effect in game.
void ApplyLive(Core::System& system);

// The console's profile: name and language, written to its config save.
void ApplyProfile(Core::System& system);

// The Switch buttons whose macro swaps the screens (Controls > Macros), as an
// HidNpadButton mask; 0 when none does.
unsigned long long SwapScreensMacroButtons();

} // namespace SwitchFrontend::TicoSettings
