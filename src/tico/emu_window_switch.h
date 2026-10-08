// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
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
    std::pair<u32, u32> GetTargetFramebufferSize() const override;
    // Vulkan has no context to share with the GPU thread: a do-nothing one.
    std::unique_ptr<Frontend::GraphicsContext> CreateSharedContext() const override;

    // True once after the screen's size changed (docked, undocked), for the GPU to lay the
    // screens out again.
    bool TakeSizeChange();

private:
    void RefreshDimensions();
    bool PollTouch();
    void PollControllerCursor();
    std::pair<unsigned, unsigned> CursorFramebufferPosition() const;
    unsigned ScaleTouchX(u32 touch_x) const;
    unsigned ScaleTouchY(u32 touch_y) const;

    NWindow* window{};
    // width << 32 | height, read by the thread that renders
    std::atomic<u64> target_size{};
    std::atomic<bool> size_changed{};
    // the window was sized 1080p (the game started docked)
    bool window_is_1080p = false;
    PadState cursor_pad{};
    bool physical_touch_pressed{};
    bool cursor_visible{};
    bool cursor_touch_pressed{};
    float cursor_x{};
    float cursor_y{};
    std::chrono::steady_clock::time_point last_cursor_update{};
};

} // namespace SwitchFrontend
