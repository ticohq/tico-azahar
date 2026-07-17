// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono>
#include <utility>

#include "tico/switch_libnx.h"
#include "core/frontend/emu_window.h"

namespace SwitchFrontend {

class EmuWindowSwitch final : public Frontend::EmuWindow {
public:
    explicit EmuWindowSwitch(NWindow* window = nwindowGetDefault());

    void PollEvents() override;
    CursorInfo GetCursorInfo() const override;

private:
    void RefreshDimensions();
    bool PollTouch();
    void PollControllerCursor();
    std::pair<unsigned, unsigned> CursorFramebufferPosition() const;
    unsigned ScaleTouchX(u32 touch_x) const;
    unsigned ScaleTouchY(u32 touch_y) const;

    NWindow* window{};
    PadState cursor_pad{};
    bool physical_touch_pressed{};
    bool cursor_visible{};
    bool cursor_touch_pressed{};
    float cursor_x{};
    float cursor_y{};
    std::chrono::steady_clock::time_point last_cursor_update{};
};

} // namespace SwitchFrontend
