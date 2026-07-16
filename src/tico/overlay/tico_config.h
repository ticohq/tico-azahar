// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace SwitchFrontend::TicoConfig {

// Loads the tico config for the azahar core from the first existing path in the
// search list (sdmc:/tico/config/cores/azahar.jsonc and fallbacks). Safe to call
// repeatedly; a missing file simply yields an empty option set.
void ReloadConfig();

// Returns the string value for `key`, or `default_value` if the key is absent.
std::string GetConfigValue(std::string_view key, std::string_view default_value = {});

// Sets an option in memory (call SaveConfig to persist it to the writable path).
void SetConfigValue(const std::string& key, const std::string& value);

// Writes the current option set back to the writable config path as JSON.
bool SaveConfig();

// Applies the loaded options to azahar's Settings. `is_new_3ds` selects the
// system model. Call before Core::System::Load or via ApplySettings afterward.
void ApplyConfig();

std::string GetLoadedConfigPath();
std::size_t GetLoadedOptionCount();

// Optional emulated 3DS profile name from tico config. Empty means leave the
// current CFG savedata username untouched.
std::string GetConfiguredUsername();

// Optional emulated 3DS system language from tico config. Empty means leave the
// current CFG savedata language untouched.
std::string GetConfiguredSystemLanguage();

} // namespace SwitchFrontend::TicoConfig
