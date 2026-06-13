// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "tico/emu_window_switch.h"

#include <algorithm>

namespace SwitchFrontend {
namespace {
constexpr u32 DefaultWidth = 1280;
constexpr u32 DefaultHeight = 720;
constexpr u32 TouchWidth = 1280;
constexpr u32 TouchHeight = 720;
} // namespace

EmuWindowSwitch::EmuWindowSwitch(NWindow* window_)
    : window{window_ ? window_ : nwindowGetDefault()} {
    window_info.type = Frontend::WindowSystemType::Switch;
    window_info.render_surface = window;
    window_info.render_surface_scale = 1.0f;
    hidInitializeTouchScreen();
    RefreshDimensions();
}

void EmuWindowSwitch::PollEvents() {
    RefreshDimensions();
    PollTouch();
}

void EmuWindowSwitch::RefreshDimensions() {
    u32 width = DefaultWidth;
    u32 height = DefaultHeight;
    if (window) {
        static_cast<void>(nwindowGetDimensions(window, &width, &height));
        if (width == 0 || height == 0) {
            width = DefaultWidth;
            height = DefaultHeight;
        }
    }
    UpdateCurrentFramebufferLayout(width, height, false);
}

unsigned EmuWindowSwitch::ScaleTouchX(u32 touch_x) const {
    const auto& layout = GetFramebufferLayout();
    if (layout.width <= 1) {
        return 0;
    }

    const u64 scaled = static_cast<u64>(touch_x) * layout.width / TouchWidth;
    return static_cast<unsigned>(std::min<u64>(scaled, layout.width - 1));
}

unsigned EmuWindowSwitch::ScaleTouchY(u32 touch_y) const {
    const auto& layout = GetFramebufferLayout();
    if (layout.height <= 1) {
        return 0;
    }

    const u64 scaled = static_cast<u64>(touch_y) * layout.height / TouchHeight;
    return static_cast<unsigned>(std::min<u64>(scaled, layout.height - 1));
}

void EmuWindowSwitch::PollTouch() {
    if (!GetFramebufferLayout().bottom_screen_enabled) {
        if (touch_pressed) {
            TouchReleased();
            touch_pressed = false;
        }
        return;
    }

    HidTouchScreenState state{};
    if (!hidGetTouchScreenStates(&state, 1) || state.count <= 0) {
        if (touch_pressed) {
            TouchReleased();
            touch_pressed = false;
        }
        return;
    }

    const HidTouchState& touch = state.touches[0];
    const unsigned x = ScaleTouchX(touch.x);
    const unsigned y = ScaleTouchY(touch.y);

    if (touch_pressed) {
        TouchMoved(x, y);
    } else {
        touch_pressed = TouchPressed(x, y);
    }
}

} // namespace SwitchFrontend
