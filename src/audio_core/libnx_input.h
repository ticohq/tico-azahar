// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#ifdef __SWITCH__

#include <array>
#include <string>
#include <vector>
#include "audio_core/input.h"
#include "common/common_types.h"
#include "tico/switch_libnx.h"

namespace AudioCore {

class LibnxInput final : public Input {
public:
    LibnxInput() = default;
    ~LibnxInput() override;

    void StartSampling(const InputParameters& params) override;
    void StopSampling() override;
    bool IsSampling() override;
    void AdjustSampleRate(u32 sample_rate) override;
    Samples Read() override;

private:
    struct Buffer {
        AudioInBuffer handle{};
        void* data = nullptr;
    };

    bool InitializeAudioIn();
    void QueueBuffers();
    void PollReleasedBuffers();
    void ProcessCapturedBuffer(const Buffer& buffer);
    Samples ConvertQueuedSamples();

    std::array<Buffer, 3> buffers{};
    std::vector<s16> mono_samples;
    double source_position = 0.0;
    bool initialized = false;
    bool is_sampling = false;
};

std::vector<std::string> ListLibnxInputDevices();

} // namespace AudioCore

#endif // __SWITCH__
