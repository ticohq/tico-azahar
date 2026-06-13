// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#ifdef __SWITCH__

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "audio_core/sink.h"
#include "common/common_types.h"

namespace AudioCore {

class LibnxSink final : public Sink {
public:
    explicit LibnxSink(std::string_view device_id);
    ~LibnxSink() override;

    unsigned int GetNativeSampleRate() const override {
        return static_cast<unsigned int>(native_sample_rate); // 32728 Hz — LibnxSink upsamples to 48000
    }

    void SetCallback(std::function<void(s16*, std::size_t)> cb) override;

private:
    void AudioThread();

    std::function<void(s16*, std::size_t)> callback;
    std::thread audio_thread;
    std::atomic<bool> running{false};
    bool initialized{false};
};

std::vector<std::string> ListLibnxSinkDevices();

} // namespace AudioCore

#endif // __SWITCH__
