// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "tico/emu_window_switch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "core/3ds.h"

namespace SwitchFrontend {
namespace {
constexpr u32 DefaultWidth = 1280;
constexpr u32 DefaultHeight = 720;
constexpr u32 TouchWidth = 1280;
constexpr u32 TouchHeight = 720;
constexpr float CursorMaxSpeed = 220.0f;
constexpr float CursorDeadzone = 0.24f;
constexpr float CursorDefaultFrameTime = 1.0f / 60.0f;
constexpr float CursorMaxFrameTime = 1.0f / 30.0f;
constexpr float StickScale = 1.0f / 32767.0f;

std::pair<float, float> ApplyCursorResponse(float x_axis, float y_axis) {
    const float magnitude = std::min(std::sqrt(x_axis * x_axis + y_axis * y_axis), 1.0f);
    if (magnitude < CursorDeadzone) {
        return {0.0f, 0.0f};
    }

    const float normalized = (magnitude - CursorDeadzone) / (1.0f - CursorDeadzone);
    const float curved = normalized * normalized * normalized;
    const float scale = curved / magnitude;
    return {x_axis * scale, y_axis * scale};
}
} // namespace

EmuWindowSwitch::EmuWindowSwitch(NWindow* window_)
    : window{window_ ? window_ : nwindowGetDefault()},
      cursor_x{static_cast<float>(Core::kScreenBottomWidth) * 0.5f},
      cursor_y{static_cast<float>(Core::kScreenBottomHeight) * 0.5f} {
    window_info.type = Frontend::WindowSystemType::Switch;
    window_info.render_surface = window;
    window_info.render_surface_scale = 1.0f;
    padInitializeDefault(&cursor_pad);
    hidInitializeTouchScreen();
    RefreshDimensions();
}

void EmuWindowSwitch::PollEvents() {
    RefreshDimensions();
    const bool physical_touch_active = PollTouch();
    if (!physical_touch_active) {
        PollControllerCursor();
    }
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

bool EmuWindowSwitch::PollTouch() {
    if (!GetFramebufferLayout().bottom_screen_enabled) {
        if (physical_touch_pressed || cursor_touch_pressed) {
            TouchReleased();
            physical_touch_pressed = false;
            cursor_touch_pressed = false;
        }
        return false;
    }

    HidTouchScreenState state{};
    if (!hidGetTouchScreenStates(&state, 1) || state.count <= 0) {
        if (physical_touch_pressed) {
            TouchReleased();
            physical_touch_pressed = false;
        }
        return false;
    }

    const HidTouchState& touch = state.touches[0];
    const unsigned x = ScaleTouchX(touch.x);
    const unsigned y = ScaleTouchY(touch.y);

    if (cursor_touch_pressed) {
        TouchReleased();
        cursor_touch_pressed = false;
    }

    if (physical_touch_pressed) {
        TouchMoved(x, y);
    } else {
        physical_touch_pressed = TouchPressed(x, y);
    }
    return true;
}

void EmuWindowSwitch::PollControllerCursor() {
    padUpdate(&cursor_pad);
    const u64 buttons = padGetButtons(&cursor_pad);
    const u64 buttons_down = padGetButtonsDown(&cursor_pad);
    const auto now = std::chrono::steady_clock::now();
    float delta_time = CursorDefaultFrameTime;
    if (last_cursor_update.time_since_epoch().count() != 0) {
        delta_time = std::chrono::duration<float>(now - last_cursor_update).count();
        delta_time = std::clamp(delta_time, 0.0f, CursorMaxFrameTime);
    }
    last_cursor_update = now;

    const bool trigger_combo_down =
        (buttons & HidNpadButton_ZL) && (buttons & HidNpadButton_ZR);
    const bool toggle_cursor =
        (buttons_down & HidNpadButton_StickR) ||
        (trigger_combo_down && (buttons_down & (HidNpadButton_ZL | HidNpadButton_ZR)));

    if (toggle_cursor) {
        cursor_visible = !cursor_visible;
        if (!cursor_visible && cursor_touch_pressed) {
            TouchReleased();
            cursor_touch_pressed = false;
        }
    }

    if (!cursor_visible || !GetFramebufferLayout().bottom_screen_enabled) {
        return;
    }

    const HidAnalogStickState stick = padGetStickPos(&cursor_pad, 1);
    float x_axis = std::clamp(static_cast<float>(stick.x) * StickScale, -1.0f, 1.0f);
    float y_axis = std::clamp(static_cast<float>(stick.y) * StickScale, -1.0f, 1.0f);

    std::tie(x_axis, y_axis) = ApplyCursorResponse(x_axis, y_axis);

    cursor_x = std::clamp(cursor_x + x_axis * CursorMaxSpeed * delta_time, 0.0f,
                          static_cast<float>(Core::kScreenBottomWidth - 1));
    cursor_y = std::clamp(cursor_y - y_axis * CursorMaxSpeed * delta_time, 0.0f,
                          static_cast<float>(Core::kScreenBottomHeight - 1));

    const auto [framebuffer_x, framebuffer_y] = CursorFramebufferPosition();
    if ((buttons & HidNpadButton_ZR) && !trigger_combo_down) {
        if (cursor_touch_pressed) {
            TouchMoved(framebuffer_x, framebuffer_y);
        } else {
            cursor_touch_pressed = TouchPressed(framebuffer_x, framebuffer_y);
        }
    } else if (cursor_touch_pressed) {
        TouchReleased();
        cursor_touch_pressed = false;
    }
}

std::pair<unsigned, unsigned> EmuWindowSwitch::CursorFramebufferPosition() const {
    const auto& bottom = GetFramebufferLayout().bottom_screen;
    const float projected_x =
        cursor_x * static_cast<float>(bottom.GetWidth()) / Core::kScreenBottomWidth;
    const float projected_y =
        cursor_y * static_cast<float>(bottom.GetHeight()) / Core::kScreenBottomHeight;
    const auto x = static_cast<unsigned>(bottom.left + static_cast<u32>(projected_x));
    const auto y = static_cast<unsigned>(bottom.top + static_cast<u32>(projected_y));
    return {std::min<unsigned>(x, bottom.right - 1), std::min<unsigned>(y, bottom.bottom - 1)};
}

Frontend::EmuWindow::CursorInfo EmuWindowSwitch::GetCursorInfo() const {
    if (!cursor_visible || !GetFramebufferLayout().bottom_screen_enabled) {
        return {};
    }

    const auto& bottom = GetFramebufferLayout().bottom_screen;
    return {
        .visible = true,
        .projected_x = cursor_x * static_cast<float>(bottom.GetWidth()) / Core::kScreenBottomWidth,
        .projected_y =
            cursor_y * static_cast<float>(bottom.GetHeight()) / Core::kScreenBottomHeight,
    };
}

} // namespace SwitchFrontend
