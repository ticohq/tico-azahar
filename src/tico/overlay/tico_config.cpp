// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tico/overlay/tico_config.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <json.hpp>

#include "common/logging/log.h"
#include "common/settings.h"

namespace SwitchFrontend::TicoConfig {
namespace {

using OptionMap = std::map<std::string, std::string, std::less<>>;

constexpr std::array<const char*, 5> kConfigPaths = {{
    "sdmc:/tiicu/config/cores/azahar.jsonc",
    "sdmc:/tico/config/cores/azahar.jsonc",
    "sdmc:/tico/config/cores/azahar.json",
    "romfs:/config/azahar.jsonc",
    "sdmc:/tico/system/3ds/tico.jsonc",
}};

constexpr const char* kDefaultWritableConfigPath = "sdmc:/tico/config/cores/azahar.jsonc";

// Strips // line and /* */ block comments so a .jsonc file parses as plain JSON.
std::string StripJsonComments(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (in_string) {
            output.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            output.push_back(c);
            continue;
        }
        if (c == '/' && i + 1 < input.size()) {
            if (input[i + 1] == '/') {
                i += 2;
                while (i < input.size() && input[i] != '\n') {
                    ++i;
                }
                if (i < input.size()) {
                    output.push_back('\n');
                }
                continue;
            }
            if (input[i + 1] == '*') {
                i += 2;
                while (i + 1 < input.size() && !(input[i] == '*' && input[i + 1] == '/')) {
                    ++i;
                }
                if (i + 1 < input.size()) {
                    ++i;
                }
                continue;
            }
        }
        output.push_back(c);
    }
    return output;
}

bool ReadWholeFile(const char* path, std::string& out) {
    std::FILE* fp = std::fopen(path, "rb");
    if (!fp) {
        return false;
    }
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(fp);
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    out.resize(read);
    return true;
}

// Converts a JSON scalar into the canonical string we store internally.
std::string JsonScalarToString(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float()) {
        return std::to_string(value.get<double>());
    }
    return {};
}

std::optional<bool> ParseBool(std::string_view value) {
    if (value == "true" || value == "1" || value == "on" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "off" || value == "no") {
        return false;
    }
    return std::nullopt;
}

std::optional<int> ParseInt(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const int result = std::stoi(std::string(value), &consumed);
        if (consumed == value.size()) {
            return result;
        }
    } catch (...) {
    }
    return std::nullopt;
}

class Manager {
public:
    void ReloadConfig() {
        options.clear();
        loaded_path.clear();

        for (const char* path : kConfigPaths) {
            std::string content;
            if (!ReadWholeFile(path, content)) {
                continue;
            }
            const std::string stripped = StripJsonComments(content);
            nlohmann::json root = nlohmann::json::parse(stripped, nullptr, false);
            if (root.is_discarded() || !root.is_object()) {
                LOG_WARNING(Frontend, "tico config at {} is not a JSON object", path);
                continue;
            }
            for (auto it = root.begin(); it != root.end(); ++it) {
                if (it.value().is_object() || it.value().is_array()) {
                    continue;
                }
                options[it.key()] = JsonScalarToString(it.value());
            }
            loaded_path = path;
            LOG_INFO(Frontend, "tico config loaded from {} ({} options)", path, options.size());
            return;
        }
        LOG_INFO(Frontend, "no tico config found; using defaults");
    }

    std::string GetConfigValue(std::string_view key, std::string_view default_value) const {
        const auto it = options.find(key);
        if (it != options.end()) {
            return it->second;
        }
        return std::string(default_value);
    }

    void SetConfigValue(const std::string& key, const std::string& value) {
        options[key] = value;
    }

    bool SaveConfig() {
        nlohmann::json root = nlohmann::json::object();
        for (const auto& [key, value] : options) {
            if (const auto b = ParseBool(value)) {
                root[key] = *b;
            } else if (const auto i = ParseInt(value)) {
                root[key] = *i;
            } else {
                root[key] = value;
            }
        }
        const std::string serialized = root.dump(2);

        const char* target =
            loaded_path.empty() || loaded_path.rfind("sdmc:/", 0) != 0 ? kDefaultWritableConfigPath
                                                                       : loaded_path.c_str();
        std::FILE* fp = std::fopen(target, "wb");
        if (!fp) {
            LOG_ERROR(Frontend, "failed to open tico config for write: {}", target);
            return false;
        }
        std::fwrite(serialized.data(), 1, serialized.size(), fp);
        std::fclose(fp);
        loaded_path = target;
        return true;
    }

    void ApplyConfig() {
        // Map the subset of tico options that translate cleanly onto azahar's
        // Settings. Unknown keys are ignored so the same config file can carry
        // options this core doesn't understand.
        if (const auto v = GetOptional("upscale"); v) {
            if (const auto factor = ParseInt(*v)) {
                Settings::values.resolution_factor.SetValue(
                    static_cast<u16>(std::clamp(*factor, 0, 10)));
            }
        }
        if (const auto v = GetOptional("new_3ds"); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.is_new_3ds.SetValue(*b);
            }
        }
        if (const auto v = GetOptional("cpu_clock"); v) {
            if (const auto pct = ParseInt(*v)) {
                Settings::values.cpu_clock_percentage.SetValue(std::clamp(*pct, 5, 400));
            }
        }
        if (const auto v = GetOptional("shader_jit"); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.use_shader_jit.SetValue(*b);
            }
        }
        if (const auto v = GetOptional("async_shaders"); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.async_shader_compilation.SetValue(*b);
            }
        }
        if (const auto v = GetOptional("vsync"); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.use_vsync.SetValue(*b);
            }
        }
        if (const auto v = GetOptional("layout"); v) {
            ApplyLayout(*v);
        }
        if (const auto v = GetOptional("display_orientation"); v) {
            ApplyOrientation(*v);
        } else if (const auto legacy = GetOptional("orientation"); legacy) {
            ApplyOrientation(*legacy);
        }
        if (const auto v = GetOptional("display_size"); v) {
            ApplyDisplaySize(*v);
        } else if (const auto legacy = GetOptional("display_mode"); legacy) {
            ApplyDisplaySize(*legacy);
        }
        if (const auto v = GetOptional("swap_screens"); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.swap_screen.SetValue(*b);
            }
        }
    }

    std::string GetLoadedConfigPath() const {
        return loaded_path;
    }

    std::size_t GetLoadedOptionCount() const {
        return options.size();
    }

private:
    std::optional<std::string> GetOptional(std::string_view key) const {
        const auto it = options.find(key);
        if (it == options.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    static void ApplyLayout(std::string_view value) {
        using L = Settings::LayoutOption;
        L layout = L::Default;
        if (value == "single" || value == "SingleScreen") {
            layout = L::SingleScreen;
        } else if (value == "large" || value == "LargeScreen") {
            layout = L::LargeScreen;
        } else if (value == "side" || value == "SideScreen") {
            layout = L::SideScreen;
        } else if (value == "hybrid" || value == "HybridScreen") {
            layout = L::HybridScreen;
        }
        Settings::values.layout_option.SetValue(layout);
    }

    static void ApplyOrientation(std::string_view value) {
        if (value == "vertical" || value == "Vertical" || value == "portrait" ||
            value == "Portrait") {
            Settings::values.upright_screen.SetValue(true);
        } else if (value == "horizontal" || value == "Horizontal" || value == "landscape" ||
                   value == "Landscape") {
            Settings::values.upright_screen.SetValue(false);
        } else if (const auto b = ParseBool(value)) {
            Settings::values.upright_screen.SetValue(*b);
        }
    }

    static void ApplyDisplaySize(std::string_view value) {
        const bool stretch = value == "Stretch" || value == "stretch";
        const bool original = value == "Original" || value == "original" || value == "Integer" ||
                              value == "integer" || value == "1x" || value == "2x" ||
                              value == "Auto";

        Settings::values.aspect_ratio.SetValue(stretch ? Settings::AspectRatio::Stretch
                                                       : Settings::AspectRatio::Default);
        Settings::values.use_integer_scaling.SetValue(original);
        Settings::values.screen_top_stretch.SetValue(stretch);
        Settings::values.screen_bottom_stretch.SetValue(stretch);
        if (stretch) {
            Settings::values.screen_top_leftright_padding.SetValue(0);
            Settings::values.screen_top_topbottom_padding.SetValue(0);
            Settings::values.screen_bottom_leftright_padding.SetValue(0);
            Settings::values.screen_bottom_topbottom_padding.SetValue(0);
        }
    }

    OptionMap options;
    std::string loaded_path;
};

Manager& GetManager() {
    static Manager manager;
    return manager;
}

} // namespace

void ReloadConfig() {
    GetManager().ReloadConfig();
}

std::string GetConfigValue(std::string_view key, std::string_view default_value) {
    return GetManager().GetConfigValue(key, default_value);
}

void SetConfigValue(const std::string& key, const std::string& value) {
    GetManager().SetConfigValue(key, value);
}

bool SaveConfig() {
    return GetManager().SaveConfig();
}

void ApplyConfig() {
    GetManager().ApplyConfig();
}

std::string GetLoadedConfigPath() {
    return GetManager().GetLoadedConfigPath();
}

std::size_t GetLoadedOptionCount() {
    return GetManager().GetLoadedOptionCount();
}

} // namespace SwitchFrontend::TicoConfig
