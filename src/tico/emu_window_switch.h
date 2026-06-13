// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "tico/switch_libnx.h"
#include "core/frontend/emu_window.h"

namespace SwitchFrontend {

class EmuWindowSwitch final : public Frontend::EmuWindow {
public:
    explicit EmuWindowSwitch(NWindow* window = nwindowGetDefault());

    void PollEvents() override;

private:
    void RefreshDimensions();
    void PollTouch();
    unsigned ScaleTouchX(u32 touch_x) const;
    unsigned ScaleTouchY(u32 touch_y) const;

    NWindow* window{};
    bool touch_pressed{};
};

} // namespace SwitchFrontend
