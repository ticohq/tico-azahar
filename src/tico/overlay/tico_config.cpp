// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tico/overlay/tico_config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>

#include <json.hpp>

#include "audio_core/input_details.h"
#include "common/logging/log.h"
#include "common/settings.h"

namespace SwitchFrontend::TicoConfig {
namespace {

using OptionMap = std::map<std::string, std::string, std::less<>>;

constexpr std::array<const char*, 4> kConfigPaths = {{
    "sdmc:/tico/config/cores/azahar.jsonc",
    "sdmc:/tico/config/cores/azahar.json",
    "romfs:/config/azahar.jsonc",
    "sdmc:/tico/system/3ds/tico.jsonc",
}};

constexpr const char* kDefaultWritableConfigPath = "sdmc:/tico/config/cores/azahar.jsonc";

void EnsureWritableConfigDirectory() {
    mkdir("sdmc:/tico", 0777);
    mkdir("sdmc:/tico/config", 0777);
    mkdir("sdmc:/tico/config/cores", 0777);
}

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

std::string LowerCopy(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::optional<bool> ParseBool(std::string_view value) {
    const std::string lower = LowerCopy(value);
    if (lower == "true" || lower == "1" || lower == "on" || lower == "yes" ||
        lower == "enabled") {
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "off" || lower == "no" ||
        lower == "disabled") {
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

std::optional<float> ParseFloat(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const float result = std::stof(std::string(value), &consumed);
        if (consumed == value.size()) {
            return result;
        }
    } catch (...) {
    }
    return std::nullopt;
}

int ParseRegion(std::string_view value) {
    const std::string lower = LowerCopy(value);
    if (lower == "japan" || lower == "jp" || lower == "0") {
        return 0;
    }
    if (lower == "usa" || lower == "us" || lower == "1") {
        return 1;
    }
    if (lower == "europe" || lower == "eu" || lower == "2") {
        return 2;
    }
    if (lower == "australia" || lower == "au" || lower == "3") {
        return 3;
    }
    if (lower == "china" || lower == "cn" || lower == "4") {
        return 4;
    }
    if (lower == "korea" || lower == "kr" || lower == "5") {
        return 5;
    }
    if (lower == "taiwan" || lower == "tw" || lower == "6") {
        return 6;
    }
    return Settings::REGION_VALUE_AUTO_SELECT;
}

AudioCore::InputType ParseInputType(std::string_view value) {
    const std::string lower = LowerCopy(value);
    if (lower == "none" || lower == "null") {
        return AudioCore::InputType::Null;
    }
    if (lower == "static_noise" || lower == "static") {
        return AudioCore::InputType::Static;
    }
    return AudioCore::InputType::Auto;
}

Settings::TextureFilter ParseTextureFilter(std::string_view value) {
    const std::string lower = LowerCopy(value);
    if (lower == "anime4k ultrafast" || lower == "anime4k") {
        return Settings::TextureFilter::Anime4K;
    }
    if (lower == "bicubic") {
        return Settings::TextureFilter::Bicubic;
    }
    if (lower == "scaleforce") {
        return Settings::TextureFilter::ScaleForce;
    }
    if (lower == "xbrz" || lower == "xbrz freescale") {
        return Settings::TextureFilter::xBRZ;
    }
    if (lower == "mmpx") {
        return Settings::TextureFilter::MMPX;
    }
    return Settings::TextureFilter::NoFilter;
}

Settings::TextureSampling ParseTextureSampling(std::string_view value) {
    const std::string lower = LowerCopy(value);
    if (lower == "nearestneighbor" || lower == "nearest") {
        return Settings::TextureSampling::NearestNeighbor;
    }
    if (lower == "linear") {
        return Settings::TextureSampling::Linear;
    }
    return Settings::TextureSampling::GameControlled;
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
            if (IsUsernameKey(key)) {
                root[key] = value;
            } else if (const auto b = ParseBool(value)) {
                root[key] = *b;
            } else if (const auto i = ParseInt(value)) {
                root[key] = *i;
            } else {
                root[key] = value;
            }
        }
        const std::string serialized = root.dump(2);

        EnsureWritableConfigDirectory();

        const char* target = kDefaultWritableConfigPath;
        std::FILE* fp = std::fopen(target, "wb");
        if (!fp) {
            LOG_ERROR(Frontend, "failed to open tico config for write: {}", target);
            return false;
        }
        const std::size_t written = std::fwrite(serialized.data(), 1, serialized.size(), fp);
        std::fclose(fp);
        if (written != serialized.size()) {
            LOG_ERROR(Frontend, "failed to write full tico config: {}", target);
            return false;
        }
        loaded_path = target;
        return true;
    }

    void ApplyConfig() {
        // Map the subset of tico options that translate cleanly onto azahar's
        // Settings. Unknown keys are ignored so the same config file can carry
        // options this core doesn't understand.
        if (const auto v = GetFirstOptional({"upscale", "resolution_factor",
                                             "citra_resolution_factor"});
            v) {
            if (const auto factor = ParseInt(*v)) {
                Settings::values.resolution_factor.SetValue(
                    static_cast<u16>(std::clamp(*factor, 0, 10)));
            }
        }
        if (const auto v = GetFirstOptional({"new_3ds", "is_new_3ds", "citra_is_new_3ds"}); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.is_new_3ds.SetValue(*b);
            } else {
                Settings::values.is_new_3ds.SetValue(*v == "New 3DS");
            }
        }
        if (const auto v = GetFirstOptional({"cpu_clock", "cpu_clock_percentage",
                                             "citra_cpu_clock_percentage"});
            v) {
            if (const auto pct = ParseInt(*v)) {
                Settings::values.cpu_clock_percentage.SetValue(std::clamp(*pct, 5, 400));
            }
        }
        if (const auto v = GetFirstOptional({"region", "region_value", "citra_region_value"}); v) {
            Settings::values.region_value.SetValue(ParseRegion(*v));
        }
        if (const auto v = GetFirstOptional({"input_type", "mic_input", "citra_input_type"}); v) {
            Settings::values.input_type.SetValue(ParseInputType(*v));
        }
        if (const auto v = GetFirstOptional({"use_hw_shader", "hardware_shaders",
                                             "citra_use_hw_shader", "citra_use_hw_shaders"});
            v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.use_hw_shader.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"shader_jit", "use_shader_jit",
                                             "citra_use_shader_jit"});
            v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.use_shader_jit.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"accurate_mul", "shaders_accurate_mul",
                                             "citra_shaders_accurate_mul", "citra_use_acc_mul"});
            v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.shaders_accurate_mul.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"disk_shader_cache", "use_disk_shader_cache",
                                             "citra_use_disk_shader_cache",
                                             "citra_use_hw_shader_cache"});
            v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.use_disk_shader_cache.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"async_shaders", "async_shader_compilation"}); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.async_shader_compilation.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"vsync", "use_vsync"}); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.use_vsync.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"simulate_3ds_gpu_timings",
                                             "citra_simulate_3ds_gpu_timings"});
            v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.simulate_3ds_gpu_timings.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"right_eye", "render_right_eye"}); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.disable_right_eye_render.SetValue(!*b);
            }
        }
        if (const auto v = GetFirstOptional({"disable_right_eye", "disable_right_eye_render",
                                             "citra_disable_right_eye_render"});
            v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.disable_right_eye_render.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"texture_filter", "citra_texture_filter"}); v) {
            Settings::values.texture_filter.SetValue(ParseTextureFilter(*v));
        }
        if (const auto v = GetFirstOptional({"texture_sampling", "citra_texture_sampling"}); v) {
            Settings::values.texture_sampling.SetValue(ParseTextureSampling(*v));
        }
        if (const auto v = GetFirstOptional({"custom_textures", "citra_custom_textures"}); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.custom_textures.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"dump_textures", "citra_dump_textures"}); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.dump_textures.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"use_virtual_sd", "citra_use_virtual_sd"}); v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.use_virtual_sd.SetValue(*b);
            }
        }
        if (const auto v = GetFirstOptional({"layout", "layout_option", "citra_layout_option"});
            v) {
            ApplyLayout(*v);
        }
        if (const auto v = GetOptional("small_screen_position"); v) {
            ApplySmallScreenPosition(*v);
        }
        const auto orientation = GetFirstOptional({"display_orientation", "orientation",
                                                   "upright_screen"});
        if (orientation) {
            ApplyOrientation(*orientation);
        }
        if (const auto v = GetFirstOptional({"display_rotation", "rotation",
                                             "screen_rotation_180", "rotate_180"});
            v && !orientation) {
            ApplyRotation(*v);
        }
        if (const auto v = GetOptional("display_size"); v) {
            ApplyDisplaySize(*v);
        } else if (const auto legacy = GetOptional("display_mode"); legacy) {
            ApplyDisplaySize(*legacy);
        }
        if (const auto v = GetFirstOptional({"swap_screens", "swap_screen", "citra_swap_screen"});
            v) {
            if (const auto b = ParseBool(*v)) {
                Settings::values.swap_screen.SetValue(*b);
            } else {
                Settings::values.swap_screen.SetValue(*v == "Bottom");
            }
        }
        if (const auto v = GetFirstOptional({"large_screen_proportion",
                                             "citra_large_screen_proportion"});
            v) {
            if (const auto f = ParseFloat(*v)) {
                Settings::values.large_screen_proportion.SetValue(std::clamp(*f, 1.0f, 16.0f));
            }
        }
    }

    std::string GetLoadedConfigPath() const {
        return loaded_path;
    }

    std::size_t GetLoadedOptionCount() const {
        return options.size();
    }

    std::string GetConfiguredUsername() const {
        if (const auto v = GetFirstOptional({"username", "profile_name", "user_name",
                                             "system_username", "citra_username"});
            v) {
            return *v;
        }
        return {};
    }

    std::string GetConfiguredSystemLanguage() const {
        if (const auto v = GetFirstOptional({"language", "system_language", "citra_language"});
            v) {
            return *v;
        }
        return {};
    }

private:
    static bool IsUsernameKey(std::string_view key) {
        return key == "username" || key == "profile_name" || key == "user_name" ||
               key == "system_username" || key == "citra_username";
    }

    std::optional<std::string> GetOptional(std::string_view key) const {
        const auto it = options.find(key);
        if (it == options.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::string> GetFirstOptional(
        std::initializer_list<std::string_view> keys) const {
        for (const std::string_view key : keys) {
            if (const auto value = GetOptional(key)) {
                return value;
            }
        }
        return std::nullopt;
    }

    static void ApplyLayout(std::string_view value) {
        using L = Settings::LayoutOption;
        L layout = L::Default;
        const std::string lower = LowerCopy(value);
        if (lower == "single" || lower == "single_screen" || value == "SingleScreen") {
            layout = L::SingleScreen;
        } else if (lower == "large" || lower == "large_screen" || value == "LargeScreen") {
            layout = L::LargeScreen;
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::BottomRight);
        } else if (lower == "large_inverted" || lower == "large_screen_inverted") {
            layout = L::LargeScreen;
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::BottomLeft);
        } else if (lower == "side" || lower == "side_by_side" || value == "SideScreen") {
            layout = L::SideScreen;
        } else if (lower == "hybrid" || lower == "hybrid_screen" || value == "HybridScreen") {
            layout = L::HybridScreen;
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::BottomRight);
        } else if (lower == "hybrid_inverted" || lower == "hybrid_screen_inverted") {
            layout = L::HybridScreen;
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::BottomLeft);
        }
        Settings::values.layout_option.SetValue(layout);
    }

    static void ApplySmallScreenPosition(std::string_view value) {
        const std::string lower = LowerCopy(value);
        if (lower == "top_right" || lower == "topright") {
            Settings::values.small_screen_position.SetValue(Settings::SmallScreenPosition::TopRight);
        } else if (lower == "middle_right" || lower == "middleright" || lower == "right") {
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::MiddleRight);
        } else if (lower == "bottom_right" || lower == "bottomright") {
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::BottomRight);
        } else if (lower == "top_left" || lower == "topleft") {
            Settings::values.small_screen_position.SetValue(Settings::SmallScreenPosition::TopLeft);
        } else if (lower == "middle_left" || lower == "middleleft" || lower == "left") {
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::MiddleLeft);
        } else if (lower == "bottom_left" || lower == "bottomleft") {
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::BottomLeft);
        } else if (lower == "above_large" || lower == "above") {
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::AboveLarge);
        } else if (lower == "below_large" || lower == "below") {
            Settings::values.small_screen_position.SetValue(
                Settings::SmallScreenPosition::BelowLarge);
        }
    }

    static void ApplyOrientation(std::string_view value) {
        const std::string lower = LowerCopy(value);
        if (lower == "vertical" || lower == "portrait") {
            Settings::values.upright_screen.SetValue(true);
            Settings::values.screen_rotation_180.SetValue(false);
        } else if (lower == "horizontal" || lower == "landscape") {
            Settings::values.upright_screen.SetValue(false);
            Settings::values.screen_rotation_180.SetValue(false);
        } else if (lower == "vertical_inverted" || lower == "vertical-inverted" ||
                   lower == "portrait_inverted" || lower == "portrait-inverted") {
            Settings::values.upright_screen.SetValue(true);
            Settings::values.screen_rotation_180.SetValue(true);
        } else if (lower == "horizontal_inverted" || lower == "horizontal-inverted" ||
                   lower == "landscape_inverted" || lower == "landscape-inverted") {
            Settings::values.upright_screen.SetValue(false);
            Settings::values.screen_rotation_180.SetValue(true);
        } else if (const auto b = ParseBool(value)) {
            Settings::values.upright_screen.SetValue(*b);
            Settings::values.screen_rotation_180.SetValue(false);
        }
    }

    static void ApplyRotation(std::string_view value) {
        const std::string lower = LowerCopy(value);
        if (lower == "180" || lower == "180deg" || lower == "flipped" || lower == "upside_down") {
            Settings::values.screen_rotation_180.SetValue(true);
        } else if (lower == "0" || lower == "0deg" || lower == "normal") {
            Settings::values.screen_rotation_180.SetValue(false);
        } else if (const auto b = ParseBool(value)) {
            Settings::values.screen_rotation_180.SetValue(*b);
        }
    }

    static void ApplyDisplaySize(std::string_view value) {
        const std::string lower = LowerCopy(value);
        const bool stretch = lower == "stretch";
        const bool original = lower == "original" || lower == "integer" || lower == "1x" ||
                              lower == "2x" || lower == "auto";

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

std::string GetConfiguredUsername() {
    return GetManager().GetConfiguredUsername();
}

std::string GetConfiguredSystemLanguage() {
    return GetManager().GetConfiguredSystemLanguage();
}

} // namespace SwitchFrontend::TicoConfig
