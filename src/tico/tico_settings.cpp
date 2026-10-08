// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tico/tico_settings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "audio_core/input_details.h"
#include "common/logging/log.h"
#include "common/param_package.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "video_core/gpu.h"
#include "core/hle/service/cfg/cfg.h"
#include "overlay/tico_config.h"
#include "tico/movie_throttle.h"
#include "tico/switch_libnx.h"

namespace SwitchFrontend::TicoSettings {
namespace {

namespace TicoConfig = SwitchFrontend::TicoConfig;

// the key each option had before the module, and the azahar_* key it is now
constexpr std::array<std::pair<const char*, const char*>, 25> kOldKeys = {{
    {"new_3ds", "azahar_new_3ds"},
    {"region", "azahar_region"},
    {"language", "azahar_language"},
    {"username", "azahar_username"},
    {"use_virtual_sd", "azahar_use_virtual_sd"},
    {"cpu_clock", "azahar_cpu_clock"},
    {"upscale", "azahar_resolution"},
    {"use_hw_shader", "azahar_use_hw_shader"},
    {"shader_jit", "azahar_shader_jit"},
    {"accurate_mul", "azahar_accurate_mul"},
    {"disk_shader_cache", "azahar_disk_shader_cache"},
    {"async_shaders", "azahar_async_shaders"},
    {"vsync", "azahar_vsync"},
    {"simulate_3ds_gpu_timings", "azahar_simulate_gpu_timings"},
    {"right_eye", "azahar_right_eye"},
    {"texture_filter", "azahar_texture_filter"},
    {"texture_sampling", "azahar_texture_sampling"},
    {"custom_textures", "azahar_custom_textures"},
    {"dump_textures", "azahar_dump_textures"},
    {"layout", "azahar_layout"},
    {"large_screen_proportion", "azahar_large_screen_proportion"},
    {"display_orientation", "azahar_orientation"},
    {"display_size", "azahar_display_size"},
    {"swap_screens", "azahar_swap_screens"},
    {"input_type", "azahar_mic_input"},
}};

using Values = std::map<std::string, std::string, std::less<>>;

// Every azahar_* option as Azahar reads it: toggles as "true"/"false", choices
// as one of their values, settings.json's default where nothing is set.
Values CurrentValues() {
    Values values;
    TicoConfig::ApplyToCore([&values](const std::string& key, const std::string& value) {
        values[key] = value;
    });
    return values;
}

std::string Get(const Values& values, std::string_view key) {
    const auto it = values.find(key);
    return it != values.end() ? it->second : std::string{};
}

bool GetBool(const Values& values, std::string_view key) {
    return Get(values, key) == "true";
}

std::optional<int> GetInt(const Values& values, std::string_view key) {
    const std::string value = Get(values, key);
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<float> GetFloat(const Values& values, std::string_view key) {
    const std::string value = Get(values, key);
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        return std::stof(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::string Lower(std::string_view value) {
    std::string out(value);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

int Region(std::string_view value) {
    constexpr std::array<std::string_view, 7> kRegions = {
        "japan", "usa", "europe", "australia", "china", "korea", "taiwan"};
    const std::string lower = Lower(value);
    for (std::size_t i = 0; i < kRegions.size(); ++i) {
        if (lower == kRegions[i]) {
            return static_cast<int>(i);
        }
    }
    return Settings::REGION_VALUE_AUTO_SELECT;
}

std::optional<Service::CFG::SystemLanguage> Language(std::string_view value) {
    using namespace Service::CFG;
    static const std::map<std::string, SystemLanguage, std::less<>> kLanguages = {
        {"japanese", LANGUAGE_JP},           {"english", LANGUAGE_EN},
        {"french", LANGUAGE_FR},             {"german", LANGUAGE_DE},
        {"italian", LANGUAGE_IT},            {"spanish", LANGUAGE_ES},
        {"simplified chinese", LANGUAGE_ZH}, {"korean", LANGUAGE_KO},
        {"dutch", LANGUAGE_NL},              {"portuguese", LANGUAGE_PT},
        {"russian", LANGUAGE_RU},            {"traditional chinese", LANGUAGE_TW},
    };
    const auto it = kLanguages.find(Lower(value));
    if (it == kLanguages.end()) {
        return std::nullopt;
    }
    return it->second;
}

AudioCore::InputType MicInput(std::string_view value) {
    if (value == "auto") {
        return AudioCore::InputType::Auto;
    }
    if (value == "static_noise") {
        return AudioCore::InputType::Static;
    }
    return AudioCore::InputType::Null;
}

Settings::TextureFilter TextureFilter(std::string_view value) {
    const std::string lower = Lower(value);
    if (lower == "anime4k ultrafast") {
        return Settings::TextureFilter::Anime4K;
    }
    if (lower == "bicubic") {
        return Settings::TextureFilter::Bicubic;
    }
    if (lower == "scaleforce") {
        return Settings::TextureFilter::ScaleForce;
    }
    if (lower == "xbrz") {
        return Settings::TextureFilter::xBRZ;
    }
    if (lower == "mmpx") {
        return Settings::TextureFilter::MMPX;
    }
    return Settings::TextureFilter::NoFilter;
}

Settings::TextureSampling TextureSampling(std::string_view value) {
    if (value == "NearestNeighbor") {
        return Settings::TextureSampling::NearestNeighbor;
    }
    if (value == "Linear") {
        return Settings::TextureSampling::Linear;
    }
    return Settings::TextureSampling::GameControlled;
}

Settings::AnisotropicFiltering Anisotropy(std::string_view value) {
    using A = Settings::AnisotropicFiltering;
    if (value == "1x") {
        return A::Off;
    }
    if (value == "2x") {
        return A::X2;
    }
    if (value == "4x") {
        return A::X4;
    }
    if (value == "8x") {
        return A::X8;
    }
    return A::X16;
}

// The layout, and which side the small screen sits on for the inverted ones.
void ApplyLayout(std::string_view value) {
    using L = Settings::LayoutOption;
    using P = Settings::SmallScreenPosition;
    L layout = L::Default;
    P position = P::BottomRight;
    if (value == "single") {
        layout = L::SingleScreen;
    } else if (value == "large" || value == "large_inverted") {
        layout = L::LargeScreen;
        position = value == "large" ? P::BottomRight : P::BottomLeft;
    } else if (value == "side") {
        layout = L::SideScreen;
    } else if (value == "hybrid" || value == "hybrid_inverted") {
        layout = L::HybridScreen;
        position = value == "hybrid" ? P::BottomRight : P::BottomLeft;
    }
    Settings::values.layout_option.SetValue(layout);
    Settings::values.small_screen_position.SetValue(position);
}

void ApplyOrientation(std::string_view value) {
    Settings::values.upright_screen.SetValue(value.starts_with("vertical"));
    Settings::values.screen_rotation_180.SetValue(value.ends_with("_inverted"));
}

void ApplyDisplaySize(std::string_view value) {
    const bool stretch = value == "Stretch";
    Settings::values.aspect_ratio.SetValue(stretch ? Settings::AspectRatio::Stretch
                                                   : Settings::AspectRatio::Default);
    Settings::values.use_integer_scaling.SetValue(value == "Original");
    Settings::values.screen_top_stretch.SetValue(stretch);
    Settings::values.screen_bottom_stretch.SetValue(stretch);
    if (stretch) {
        Settings::values.screen_top_leftright_padding.SetValue(0);
        Settings::values.screen_top_topbottom_padding.SetValue(0);
        Settings::values.screen_bottom_leftright_padding.SetValue(0);
        Settings::values.screen_bottom_topbottom_padding.SetValue(0);
    }
}

// The Switch button a mapping option names, 0 for none.
u64 SwitchButton(std::string_view name) {
    static const std::map<std::string, u64, std::less<>> kButtons = {
        {"A", HidNpadButton_A},          {"B", HidNpadButton_B},
        {"X", HidNpadButton_X},          {"Y", HidNpadButton_Y},
        {"L", HidNpadButton_L},          {"R", HidNpadButton_R},
        {"ZL", HidNpadButton_ZL},        {"ZR", HidNpadButton_ZR},
        {"Plus", HidNpadButton_Plus},    {"Minus", HidNpadButton_Minus},
        {"StickL", HidNpadButton_StickL}, {"StickR", HidNpadButton_StickR},
        {"Up", HidNpadButton_Up},        {"Down", HidNpadButton_Down},
        {"Left", HidNpadButton_Left},    {"Right", HidNpadButton_Right},
    };
    const auto it = kButtons.find(name);
    return it != kButtons.end() ? it->second : 0;
}

// Controls > Button Mapping and Sticks: each 3DS button on its Switch button,
// the Circle Pad and the C-Stick on a stick, through the switch_hid engine.
void ApplyControls(const Values& values) {
    static constexpr std::array<std::pair<Settings::NativeButton::Values, const char*>, 15>
        kButtons = {{
        {Settings::NativeButton::A, "azahar_map_a"},
        {Settings::NativeButton::B, "azahar_map_b"},
        {Settings::NativeButton::X, "azahar_map_x"},
        {Settings::NativeButton::Y, "azahar_map_y"},
        {Settings::NativeButton::L, "azahar_map_l"},
        {Settings::NativeButton::R, "azahar_map_r"},
        {Settings::NativeButton::ZL, "azahar_map_zl"},
        {Settings::NativeButton::ZR, "azahar_map_zr"},
        {Settings::NativeButton::Start, "azahar_map_start"},
        {Settings::NativeButton::Select, "azahar_map_select"},
        {Settings::NativeButton::Up, "azahar_map_up"},
        {Settings::NativeButton::Down, "azahar_map_down"},
        {Settings::NativeButton::Left, "azahar_map_left"},
        {Settings::NativeButton::Right, "azahar_map_right"},
        {Settings::NativeButton::Home, "azahar_map_home"},
    }};
    auto& profile = Settings::values.current_input_profile;
    for (const auto& [native, key] : kButtons) {
        const u64 mask = SwitchButton(Get(values, key));
        if (mask == 0) {
            profile.buttons[native].clear();
            continue;
        }
        Common::ParamPackage params;
        params.Set("engine", "switch_hid");
        params.Set("button", static_cast<int>(mask));
        profile.buttons[native] = params.Serialize();
    }

    // axis 0 is the Switch's left stick, 1 its right stick
    const auto stick = [&values](const char* key) -> std::string {
        const std::string value = Get(values, key);
        if (value != "Left" && value != "Right") {
            return {};
        }
        Common::ParamPackage params;
        params.Set("engine", "switch_hid_analog");
        params.Set("axis", value == "Left" ? 0 : 1);
        return params.Serialize();
    };
    profile.analogs[Settings::NativeAnalog::CirclePad] = stick("azahar_map_circle_pad");
    profile.analogs[Settings::NativeAnalog::CStick] = stick("azahar_map_c_stick");
}

// What can change while a game runs.
void ApplyLiveValues(const Values& values) {
    if (const auto clock = GetInt(values, "azahar_cpu_clock")) {
        Settings::values.cpu_clock_percentage.SetValue(std::clamp(*clock, 5, 400));
    }
    if (const auto factor = GetInt(values, "azahar_resolution")) {
        Settings::values.resolution_factor.SetValue(static_cast<u32>(std::clamp(*factor, 1, 10)));
    }
    Settings::values.use_vsync.SetValue(GetBool(values, "azahar_vsync"));
    Settings::values.show_shader_compile_notice.SetValue(
        GetBool(values, "azahar_shader_notice"));
    Settings::values.use_frame_generation.SetValue(GetBool(values, "azahar_frame_gen"));
    if (const auto multiplier = GetInt(values, "azahar_frame_gen_multiplier")) {
        Settings::values.frame_generation_multiplier.SetValue(
            static_cast<u32>(std::clamp(*multiplier, 2, 4)));
    }
    if (const auto flow_scale = GetInt(values, "azahar_frame_gen_flow_scale")) {
        Settings::values.frame_generation_flow_scale.SetValue(
            static_cast<u32>(std::clamp(*flow_scale, 12, 100)));
    }
    Settings::values.frame_generation_performance_mode.SetValue(
        GetBool(values, "azahar_frame_gen_performance"));
    Settings::values.skip_slow_draw.SetValue(GetBool(values, "azahar_skip_slow_draw"));
    Settings::values.skip_texture_copy.SetValue(GetBool(values, "azahar_skip_texture_copy"));
    Settings::values.skip_cpu_write.SetValue(GetBool(values, "azahar_skip_cpu_write"));
    MovieThrottle::Configure(GetBool(values, "azahar_movie_throttle"),
                             GetInt(values, "azahar_movie_clock").value_or(45));
    Settings::values.simulate_3ds_gpu_timings.SetValue(
        GetBool(values, "azahar_simulate_gpu_timings"));
    Settings::values.disable_right_eye_render.SetValue(!GetBool(values, "azahar_right_eye"));
    Settings::values.texture_filter.SetValue(TextureFilter(Get(values, "azahar_texture_filter")));
    Settings::values.texture_sampling.SetValue(
        TextureSampling(Get(values, "azahar_texture_sampling")));
    Settings::values.anisotropic_filtering.SetValue(
        Anisotropy(Get(values, "azahar_anisotropic_filtering")));

    ApplyLayout(Get(values, "azahar_layout"));
    if (const auto proportion = GetFloat(values, "azahar_large_screen_proportion")) {
        Settings::values.large_screen_proportion.SetValue(std::clamp(*proportion, 1.0f, 16.0f));
    }
    ApplyOrientation(Get(values, "azahar_orientation"));
    ApplyDisplaySize(Get(values, "azahar_display_size"));
    Settings::values.swap_screen.SetValue(GetBool(values, "azahar_swap_screens"));

    if (const auto volume = GetInt(values, "azahar_volume")) {
        Settings::values.volume.SetValue(static_cast<float>(std::clamp(*volume, 0, 100)) / 100.0f);
    }
    Settings::values.enable_audio_stretching.SetValue(GetBool(values, "azahar_audio_stretching"));
    ApplyControls(values);
}

} // namespace

void MigrateOldKeys() {
    bool migrated = false;
    for (const auto& [old_key, new_key] : kOldKeys) {
        const std::string old_value = TicoConfig::GetConfigValue(old_key);
        if (old_value.empty() || !TicoConfig::GetConfigValue(new_key).empty()) {
            continue;
        }
        TicoConfig::SetConfigValue(new_key, old_value);
        migrated = true;
    }
    if (migrated) {
        TicoConfig::SaveConfig();
        LOG_INFO(Frontend, "tico settings: moved the old option names to azahar_*");
    }
}

void Apply() {
    const Values values = CurrentValues();
    Settings::values.is_new_3ds.SetValue(GetBool(values, "azahar_new_3ds"));
    Settings::values.fastmem.SetValue(GetBool(values, "azahar_fastmem"));
    Settings::values.async_gpu_emulation.SetValue(GetBool(values, "azahar_async_gpu"));
    Settings::values.region_value.SetValue(Region(Get(values, "azahar_region")));
    Settings::values.use_virtual_sd.SetValue(GetBool(values, "azahar_use_virtual_sd"));
    Settings::values.use_hw_shader.SetValue(GetBool(values, "azahar_use_hw_shader"));
    Settings::values.use_shader_jit.SetValue(GetBool(values, "azahar_shader_jit"));
    Settings::values.shaders_accurate_mul.SetValue(GetBool(values, "azahar_accurate_mul"));
    Settings::values.use_disk_shader_cache.SetValue(GetBool(values, "azahar_disk_shader_cache"));
    Settings::values.async_shader_compilation.SetValue(GetBool(values, "azahar_async_shaders"));
    Settings::values.custom_textures.SetValue(GetBool(values, "azahar_custom_textures"));
    Settings::values.dump_textures.SetValue(GetBool(values, "azahar_dump_textures"));
    Settings::values.input_type.SetValue(MicInput(Get(values, "azahar_mic_input")));
    ApplyLiveValues(values);
}

void ApplyLive(Core::System& system) {
    // the renderer reads these settings: changed only while the GPU thread is idle
    system.GPU().WaitIdle();
    ApplyLiveValues(CurrentValues());
    system.ApplySettings();
    MovieThrottle::Reapply(system);
}

void ApplyProfile(Core::System& system) {
    const Values values = CurrentValues();
    auto cfg = Service::CFG::GetModule(system);
    if (!cfg) {
        return;
    }

    if (const auto language = Language(Get(values, "azahar_language"))) {
        cfg->SetSystemLanguage(*language);
    }
    std::u16string username = Common::UTF8ToUTF16(Get(values, "azahar_username"));
    if (!username.empty()) {
        // the console's name holds 10 characters
        username.resize(std::min<std::size_t>(username.size(), 10));
        cfg->SetUsername(username);
    }
    const Result rc = cfg->UpdateConfigNANDSavegame();
    LOG_INFO(Frontend, "tico settings: console language and name written (rc=0x{:x})", rc.raw);
}

} // namespace SwitchFrontend::TicoSettings
