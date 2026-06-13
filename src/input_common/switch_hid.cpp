// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifdef __SWITCH__

#include <algorithm>
#include <cmath>
#include "tico/switch_libnx.h"
#include "common/param_package.h"
#include "core/frontend/input.h"
#include "input_common/switch_hid.h"

namespace InputCommon {
namespace SwitchHID {

static PadState g_pad{};

void Init() {
    padInitializeDefault(&g_pad);
}

void Update() {
    padUpdate(&g_pad);
}

void Shutdown() {}

// ---- ButtonDevice ----

class SwitchHIDButton final : public Input::ButtonDevice {
public:
    explicit SwitchHIDButton(u64 mask) : m_mask(mask) {}

    bool GetStatus() const override {
        return (padGetButtons(&g_pad) & m_mask) != 0;
    }

private:
    u64 m_mask;
};

std::unique_ptr<Input::ButtonDevice> SwitchHIDButtonFactory::Create(
    const Common::ParamPackage& params) {
    const u64 mask = static_cast<u64>(params.Get("button", 0));
    return std::make_unique<SwitchHIDButton>(mask);
}

// ---- AnalogDevice ----
// axis 0 = left stick (circle pad), axis 1 = right stick (C-stick)

class SwitchHIDAnalog final : public Input::AnalogDevice {
public:
    explicit SwitchHIDAnalog(int stick_idx) : m_stick(stick_idx) {}

    std::tuple<float, float> GetStatus() const override {
        const HidAnalogStickState pos = padGetStickPos(&g_pad, m_stick);
        // Normalize to [-1, 1]. Range is -32768..32767; use 32767.0f to avoid clamping +1.
        constexpr float kScale = 1.0f / 32767.0f;
        const float x = std::clamp(pos.x * kScale, -1.0f, 1.0f);
        const float y = std::clamp(pos.y * kScale, -1.0f, 1.0f);
        return {x, y};
    }

private:
    int m_stick;
};

std::unique_ptr<Input::AnalogDevice> SwitchHIDAnalogFactory::Create(
    const Common::ParamPackage& params) {
    const int axis = params.Get("axis", 0);
    return std::make_unique<SwitchHIDAnalog>(axis);
}

} // namespace SwitchHID
} // namespace InputCommon

#endif // __SWITCH__
