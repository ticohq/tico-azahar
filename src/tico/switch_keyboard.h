// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>

#include "core/frontend/applets/swkbd.h"

namespace SwitchFrontend {

class SwitchKeyboard final : public Frontend::SoftwareKeyboard {
public:
    void Execute(const Frontend::KeyboardConfig& config) override;
    void ShowError(const std::string& error) override;

private:
    std::string validation_error;
};

} // namespace SwitchFrontend
