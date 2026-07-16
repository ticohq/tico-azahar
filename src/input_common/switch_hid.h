// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#ifdef __SWITCH__

#include <memory>
#include "common/param_package.h"
#include "core/frontend/input.h"

namespace InputCommon {
namespace SwitchHID {

/// Call once at startup after padConfigureInput/padInitialize
void Init();
/// Call each frame before RunLoop to refresh button state
void Update();
void Shutdown();

class SwitchHIDButtonFactory final : public Input::Factory<Input::ButtonDevice> {
public:
    std::unique_ptr<Input::ButtonDevice> Create(const Common::ParamPackage& params) override;
};

class SwitchHIDAnalogFactory final : public Input::Factory<Input::AnalogDevice> {
public:
    std::unique_ptr<Input::AnalogDevice> Create(const Common::ParamPackage& params) override;
};

class SwitchHIDMotionFactory final : public Input::Factory<Input::MotionDevice> {
public:
    std::unique_ptr<Input::MotionDevice> Create(const Common::ParamPackage& params) override;
};

} // namespace SwitchHID
} // namespace InputCommon

#endif // __SWITCH__
