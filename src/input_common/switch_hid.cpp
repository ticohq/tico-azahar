// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifdef __SWITCH__

#include <algorithm>
#include <array>
#include "tico/switch_libnx.h"
#include "common/math_util.h"
#include "common/param_package.h"
#include "common/vector_math.h"
#include "core/frontend/input.h"
#include "input_common/switch_hid.h"

namespace InputCommon {
namespace SwitchHID {

static PadState g_pad{};
static std::array<HidSixAxisSensorHandle, 6> g_sixaxis_handles{};
static std::array<bool, 6> g_sixaxis_started{};

constexpr std::size_t kSixAxisHandheld = 0;
constexpr std::size_t kSixAxisFullKey = 1;
constexpr std::size_t kSixAxisJoyDualLeft = 2;
constexpr std::size_t kSixAxisJoyDualRight = 3;
constexpr std::size_t kSixAxisJoyLeft = 4;
constexpr std::size_t kSixAxisJoyRight = 5;
constexpr float kDefaultMotionSensitivity = 1.25f;

bool StartSixAxisSensor(std::size_t index) {
    const LibnxResult rc = hidStartSixAxisSensor(g_sixaxis_handles[index]);
    g_sixaxis_started[index] = rc == 0;
    return g_sixaxis_started[index];
}

bool EnsureSingleSixAxisSensor(std::size_t index, HidNpadIdType id, HidNpadStyleTag style) {
    if (g_sixaxis_started[index]) {
        return true;
    }
    if (hidGetSixAxisSensorHandles(&g_sixaxis_handles[index], 1, id, style) != 0) {
        return false;
    }
    return StartSixAxisSensor(index);
}

bool EnsureJoyDualSixAxisSensors() {
    if (g_sixaxis_started[kSixAxisJoyDualLeft] || g_sixaxis_started[kSixAxisJoyDualRight]) {
        return true;
    }
    if (hidGetSixAxisSensorHandles(&g_sixaxis_handles[kSixAxisJoyDualLeft], 2, HidNpadIdType_No1,
                                   HidNpadStyleTag_NpadJoyDual) != 0) {
        return false;
    }
    const bool left_started = StartSixAxisSensor(kSixAxisJoyDualLeft);
    const bool right_started = StartSixAxisSensor(kSixAxisJoyDualRight);
    return left_started || right_started;
}

void Init() {
    padInitializeDefault(&g_pad);
}

void Update() {
    padUpdate(&g_pad);
}

void Shutdown() {
    for (std::size_t i = 0; i < g_sixaxis_handles.size(); ++i) {
        if (g_sixaxis_started[i]) {
            hidStopSixAxisSensor(g_sixaxis_handles[i]);
            g_sixaxis_started[i] = false;
        }
    }
}

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

// ---- MotionDevice ----

class SwitchHIDMotion final : public Input::MotionDevice {
public:
    explicit SwitchHIDMotion(float sensitivity_) : sensitivity(sensitivity_) {}

    std::tuple<Common::Vec3<float>, Common::Vec3<float>> GetStatus() const override {
        HidSixAxisSensorState state{};
        if (!ReadSixAxisState(state)) {
            return {Common::Vec3<float>{0.0f, 0.0f, -1.0f},
                    Common::Vec3<float>{0.0f, 0.0f, 0.0f}};
        }

        Common::Vec3<float> accel{
            -state.acceleration.x,
            state.acceleration.z,
            state.acceleration.y,
        };
        Common::Vec3<float> gyro{
            -state.angular_velocity.x,
            state.angular_velocity.z,
            state.angular_velocity.y,
        };

        constexpr float kRadToDeg = 180.0f / Common::PI;
        return {accel * sensitivity, gyro * kRadToDeg * sensitivity};
    }

private:
    float sensitivity;

    static bool ReadSixAxisState(HidSixAxisSensorState& state) {
        padUpdate(&g_pad);
        const u64 style_set = padGetStyleSet(&g_pad);
        if ((style_set & HidNpadStyleTag_NpadHandheld) &&
            EnsureSingleSixAxisSensor(kSixAxisHandheld, HidNpadIdType_Handheld,
                                      HidNpadStyleTag_NpadHandheld)) {
            if (hidGetSixAxisSensorStates(g_sixaxis_handles[kSixAxisHandheld], &state, 1) > 0) {
                return true;
            }
            g_sixaxis_started[kSixAxisHandheld] = false;
            return false;
        }
        if ((style_set & HidNpadStyleTag_NpadFullKey) &&
            EnsureSingleSixAxisSensor(kSixAxisFullKey, HidNpadIdType_No1,
                                      HidNpadStyleTag_NpadFullKey)) {
            if (hidGetSixAxisSensorStates(g_sixaxis_handles[kSixAxisFullKey], &state, 1) > 0) {
                return true;
            }
            g_sixaxis_started[kSixAxisFullKey] = false;
            return false;
        }
        if ((style_set & HidNpadStyleTag_NpadJoyDual) && EnsureJoyDualSixAxisSensors()) {
            const u64 attributes = padGetAttributes(&g_pad);
            if ((attributes & HidNpadAttribute_IsLeftConnected) &&
                g_sixaxis_started[kSixAxisJoyDualLeft]) {
                if (hidGetSixAxisSensorStates(g_sixaxis_handles[kSixAxisJoyDualLeft], &state, 1) >
                    0) {
                    return true;
                }
                g_sixaxis_started[kSixAxisJoyDualLeft] = false;
                return false;
            }
            if ((attributes & HidNpadAttribute_IsRightConnected) &&
                g_sixaxis_started[kSixAxisJoyDualRight]) {
                if (hidGetSixAxisSensorStates(g_sixaxis_handles[kSixAxisJoyDualRight], &state, 1) >
                    0) {
                    return true;
                }
                g_sixaxis_started[kSixAxisJoyDualRight] = false;
                return false;
            }
        }
        if ((style_set & HidNpadStyleTag_NpadJoyLeft) &&
            EnsureSingleSixAxisSensor(kSixAxisJoyLeft, HidNpadIdType_No1,
                                      HidNpadStyleTag_NpadJoyLeft)) {
            if (hidGetSixAxisSensorStates(g_sixaxis_handles[kSixAxisJoyLeft], &state, 1) > 0) {
                return true;
            }
            g_sixaxis_started[kSixAxisJoyLeft] = false;
            return false;
        }
        if ((style_set & HidNpadStyleTag_NpadJoyRight) &&
            EnsureSingleSixAxisSensor(kSixAxisJoyRight, HidNpadIdType_No1,
                                      HidNpadStyleTag_NpadJoyRight)) {
            if (hidGetSixAxisSensorStates(g_sixaxis_handles[kSixAxisJoyRight], &state, 1) > 0) {
                return true;
            }
            g_sixaxis_started[kSixAxisJoyRight] = false;
            return false;
        }
        return false;
    }
};

std::unique_ptr<Input::MotionDevice> SwitchHIDMotionFactory::Create(
    const Common::ParamPackage& params) {
    const float sensitivity = params.Get("sensitivity", kDefaultMotionSensitivity);
    return std::make_unique<SwitchHIDMotion>(sensitivity);
}

} // namespace SwitchHID
} // namespace InputCommon

#endif // __SWITCH__
