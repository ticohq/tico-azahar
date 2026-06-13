// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifdef __SWITCH__

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "audio_core/audio_types.h"
#include "audio_core/libnx_sink.h"
#include "tico/switch_libnx.h"
#include "common/logging/log.h"

namespace AudioCore {

// Audout is fixed at 48000 Hz, stereo PCM16.
static constexpr unsigned int kAudoutRate     = 48000;
static constexpr std::size_t  kOutSamples     = 1024;               // stereo frames per audout buffer
static constexpr std::size_t  kOutBytes       = kOutSamples * sizeof(s16) * 2; // 4096 bytes
static constexpr std::size_t  kAlignedSize    = 0x1000;
static_assert(kOutBytes == kAlignedSize, "audout buffer must be page-aligned");
static constexpr int kNumBuffers = 2;
static constexpr u64 kWaitTimeoutNs = 33'000'000ULL; // 33ms

// s_input_buf must cover all resampling source positions:
// max index = floor((kOutSamples-1) * native/audout) + 1 = floor(697.71) + 1 = 698 → 699 slots.
// The per-callback request count is computed via Bresenham and averages kOutSamples *
// native/audout = 698.19, eliminating the steady FIFO drain that causes pitch-low.
static constexpr std::size_t kInBufSize = 699; // buffer slots (must cover max resampling index)

static AudioOutBuffer s_audio_buffers[kNumBuffers];
static void*          s_audio_data[kNumBuffers];
static s16            s_input_buf[kInBufSize * 2]; // stereo input at native_sample_rate

LibnxSink::LibnxSink(std::string_view) {
    const LibnxResult rc = audoutInitialize();
    if (rc != 0) {
        LOG_ERROR(Audio_Sink, "audoutInitialize failed: 0x{:08x}", static_cast<unsigned>(rc));
        return;
    }

    const LibnxResult start_rc = audoutStartAudioOut();
    if (start_rc != 0) {
        LOG_ERROR(Audio_Sink, "audoutStartAudioOut failed: 0x{:08x}", static_cast<unsigned>(start_rc));
        audoutExit();
        return;
    }

    for (int i = 0; i < kNumBuffers; i++) {
        s_audio_data[i] = aligned_alloc(kAlignedSize, kAlignedSize);
        if (!s_audio_data[i]) {
            LOG_ERROR(Audio_Sink, "Failed to allocate audio buffer {}", i);
            audoutStopAudioOut();
            audoutExit();
            return;
        }
        std::memset(s_audio_data[i], 0, kAlignedSize);

        s_audio_buffers[i].next        = nullptr;
        s_audio_buffers[i].buffer      = s_audio_data[i];
        s_audio_buffers[i].buffer_size = kAlignedSize;
        s_audio_buffers[i].data_size   = kOutBytes;
        s_audio_buffers[i].data_offset = 0;

        audoutAppendAudioOutBuffer(&s_audio_buffers[i]);
    }

    initialized = true;
    running.store(true, std::memory_order_release);
    audio_thread = std::thread(&LibnxSink::AudioThread, this);

    LOG_INFO(Audio_Sink, "LibnxSink: {}Hz→{}Hz, {}×{}-sample audout buffers",
             native_sample_rate, kAudoutRate, kNumBuffers, kOutSamples);
}

LibnxSink::~LibnxSink() {
    running.store(false, std::memory_order_release);
    if (audio_thread.joinable())
        audio_thread.join();

    if (initialized) {
        audoutStopAudioOut();
        audoutExit();
        for (int i = 0; i < kNumBuffers; i++) {
            free(s_audio_data[i]);
            s_audio_data[i] = nullptr;
        }
        initialized = false;
    }
}

void LibnxSink::SetCallback(std::function<void(s16*, std::size_t)> cb) {
    callback = std::move(cb);
}

void LibnxSink::AudioThread() {
    // Boost priority so the audio thread isn't preempted before appending its buffer.
    svcSetThreadPriority(CUR_THREAD_HANDLE, 38);

    // Bresenham accumulator: n_in alternates between 698 and 699 so the average is exactly
    // kOutSamples * native_sample_rate / kAudoutRate = 698.19, matching DSP production rate.
    std::size_t input_acc = 0;

    while (running.load(std::memory_order_acquire)) {
        AudioOutBuffer* released = nullptr;
        u32 count = 0;

        const LibnxResult rc = audoutWaitPlayFinish(&released, &count, kWaitTimeoutNs);
        if (rc != 0 || count == 0 || released == nullptr)
            continue;

        input_acc += kOutSamples * static_cast<std::size_t>(native_sample_rate);
        const std::size_t n_in = input_acc / kAudoutRate;
        input_acc %= kAudoutRate;

        if (callback) {
            callback(s_input_buf, n_in);
        } else {
            std::memset(s_input_buf, 0, n_in * sizeof(s16) * 2);
        }

        // Linear interpolation: native_sample_rate Hz → kAudoutRate Hz
        auto* out = reinterpret_cast<s16*>(released->buffer);
        constexpr float kRatio = static_cast<float>(native_sample_rate) / static_cast<float>(kAudoutRate);
        for (std::size_t i = 0; i < kOutSamples; i++) {
            const float src  = static_cast<float>(i) * kRatio;
            const std::size_t idx0 = std::min(static_cast<std::size_t>(src), n_in - 1);
            const float frac = src - static_cast<float>(idx0);
            const std::size_t idx1 = std::min(idx0 + 1, n_in - 1);
            for (int ch = 0; ch < 2; ch++) {
                const float a = static_cast<float>(s_input_buf[idx0 * 2 + ch]);
                const float b = static_cast<float>(s_input_buf[idx1 * 2 + ch]);
                out[i * 2 + ch] = static_cast<s16>(a + frac * (b - a));
            }
        }

        released->data_size = kOutBytes;
        audoutAppendAudioOutBuffer(released);
    }
}

std::vector<std::string> ListLibnxSinkDevices() {
    return {"Default"};
}

} // namespace AudioCore

#endif // __SWITCH__
