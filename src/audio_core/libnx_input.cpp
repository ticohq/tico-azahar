// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifdef __SWITCH__

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "audio_core/libnx_input.h"
#include "common/logging/log.h"

namespace AudioCore {
namespace {

constexpr u32 NativeSampleRate = 48000;
constexpr u32 NativeChannels = 2;
constexpr u32 NativeBytesPerSample = sizeof(s16);
constexpr u32 FramesPerBuffer = 240; // 5 ms at audin's fixed 48 kHz rate.
constexpr u32 DataSize = FramesPerBuffer * NativeChannels * NativeBytesPerSample;
constexpr u32 BufferAlignment = 0x1000;
constexpr u32 BufferSize = (DataSize + BufferAlignment - 1) & ~(BufferAlignment - 1);
constexpr u64 PollTimeoutNs = 0;
constexpr std::size_t TargetReadSamples = 16;

u8 LowByte(u16 value) {
    return static_cast<u8>(value & 0xFF);
}

u8 HighByte(u16 value) {
    return static_cast<u8>((value >> 8) & 0xFF);
}

} // namespace

LibnxInput::~LibnxInput() {
    StopSampling();
}

bool LibnxInput::InitializeAudioIn() {
    const LibnxResult init_rc = audinInitialize();
    if (init_rc != 0) {
        LOG_WARNING(Audio, "audinInitialize failed: 0x{:08x}", static_cast<unsigned>(init_rc));
        return false;
    }

    if (audinGetSampleRate() != NativeSampleRate || audinGetChannelCount() != NativeChannels ||
        audinGetPcmFormat() != PcmFormat_Int16) {
        LOG_WARNING(Audio, "audin opened unsupported format: {} Hz, {} channels, pcm={}",
                    audinGetSampleRate(), audinGetChannelCount(),
                    static_cast<unsigned>(audinGetPcmFormat()));
        audinExit();
        return false;
    }

    for (auto& buffer : buffers) {
        buffer.data = aligned_alloc(BufferAlignment, BufferSize);
        if (!buffer.data) {
            LOG_ERROR(Audio, "Failed to allocate audin capture buffer");
            audinExit();
            return false;
        }
        std::memset(buffer.data, 0, BufferSize);
        buffer.handle.next = nullptr;
        buffer.handle.buffer = buffer.data;
        buffer.handle.buffer_size = BufferSize;
        buffer.handle.data_size = DataSize;
        buffer.handle.data_offset = 0;
    }

    const LibnxResult start_rc = audinStartAudioIn();
    if (start_rc != 0) {
        LOG_WARNING(Audio, "audinStartAudioIn failed: 0x{:08x}", static_cast<unsigned>(start_rc));
        audinExit();
        return false;
    }

    initialized = true;
    QueueBuffers();
    return true;
}

void LibnxInput::QueueBuffers() {
    for (auto& buffer : buffers) {
        const LibnxResult rc = audinAppendAudioInBuffer(&buffer.handle);
        if (rc != 0) {
            LOG_WARNING(Audio, "audinAppendAudioInBuffer failed: 0x{:08x}",
                        static_cast<unsigned>(rc));
        }
    }
}

void LibnxInput::StartSampling(const InputParameters& params) {
    if (IsSampling()) {
        return;
    }

    parameters = params;
    source_position = 0.0;
    mono_samples.clear();

    if (!InitializeAudioIn()) {
        StopSampling();
        return;
    }

    is_sampling = true;
    LOG_INFO(Audio, "Switch audio input started: {} Hz mono {}-bit", params.sample_rate,
             params.sample_size);
}

void LibnxInput::StopSampling() {
    if (initialized) {
        audinStopAudioIn();
        audinExit();
        initialized = false;
    }

    for (auto& buffer : buffers) {
        std::free(buffer.data);
        buffer.data = nullptr;
        buffer.handle = {};
    }

    mono_samples.clear();
    source_position = 0.0;
    is_sampling = false;
}

bool LibnxInput::IsSampling() {
    return is_sampling;
}

void LibnxInput::AdjustSampleRate(u32 sample_rate) {
    parameters.sample_rate = sample_rate;
}

void LibnxInput::PollReleasedBuffers() {
    while (initialized) {
        AudioInBuffer* released = nullptr;
        u32 released_count = 0;
        const LibnxResult rc = audinWaitCaptureFinish(&released, &released_count, PollTimeoutNs);
        if (rc != 0 || released_count == 0 || released == nullptr) {
            return;
        }

        const auto* found = std::find_if(buffers.begin(), buffers.end(), [released](const auto& b) {
            return &b.handle == released;
        });
        if (found != buffers.end()) {
            ProcessCapturedBuffer(*found);
            released->data_size = DataSize;
            released->data_offset = 0;
            audinAppendAudioInBuffer(released);
        }
    }
}

void LibnxInput::ProcessCapturedBuffer(const Buffer& buffer) {
    const auto* samples = static_cast<const s16*>(buffer.data);
    const std::size_t frames =
        static_cast<std::size_t>(buffer.handle.data_size) / (NativeChannels * NativeBytesPerSample);

    mono_samples.reserve(mono_samples.size() + frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const int left = samples[frame * 2];
        const int right = samples[frame * 2 + 1];
        mono_samples.push_back(static_cast<s16>((left + right) / 2));
    }
}

Samples LibnxInput::ConvertQueuedSamples() {
    if (parameters.sample_rate == 0 || mono_samples.size() < 2) {
        return {};
    }

    const double step = static_cast<double>(NativeSampleRate) / parameters.sample_rate;
    const std::size_t bytes_per_sample = parameters.sample_size / 8;
    Samples output;
    output.reserve(TargetReadSamples * bytes_per_sample);

    for (std::size_t i = 0; i < TargetReadSamples; ++i) {
        const std::size_t base = static_cast<std::size_t>(source_position);
        if (base + 1 >= mono_samples.size()) {
            break;
        }

        const double fraction = source_position - base;
        const double mixed = static_cast<double>(mono_samples[base]) * (1.0 - fraction) +
                             static_cast<double>(mono_samples[base + 1]) * fraction;
        const auto sample = static_cast<s16>(
            std::clamp(std::lround(mixed), static_cast<long>(std::numeric_limits<s16>::min()),
                       static_cast<long>(std::numeric_limits<s16>::max())));

        if (parameters.sample_size == 8) {
            const s8 sample8 = static_cast<s8>(sample >> 8);
            output.push_back(parameters.sign == Signedness::Unsigned
                                 ? static_cast<u8>(static_cast<int>(sample8) + 128)
                                 : static_cast<u8>(sample8));
        } else {
            const u16 out = parameters.sign == Signedness::Unsigned
                                ? static_cast<u16>(static_cast<int>(sample) + 32768)
                                : static_cast<u16>(sample);
            output.push_back(LowByte(out));
            output.push_back(HighByte(out));
        }

        source_position += step;
    }

    const std::size_t consumed = static_cast<std::size_t>(source_position);
    if (consumed > 0) {
        mono_samples.erase(mono_samples.begin(),
                           mono_samples.begin() +
                               std::min(consumed, mono_samples.size()));
        source_position -= consumed;
    }

    return output;
}

Samples LibnxInput::Read() {
    if (!IsSampling()) {
        return {};
    }

    PollReleasedBuffers();
    return ConvertQueuedSamples();
}

std::vector<std::string> ListLibnxInputDevices() {
    return {"Switch Audio Input"};
}

} // namespace AudioCore

#endif // __SWITCH__
