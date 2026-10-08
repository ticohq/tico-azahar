// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tico/movie_throttle.h"

#include <algorithm>
#include <atomic>

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"

namespace SwitchFrontend::MovieThrottle {
namespace {

std::atomic<bool> s_enabled{false};
std::atomic<int> s_percentage{45};
std::atomic<bool> s_playing{false};
std::atomic<bool> s_active{false};

void Lower(Core::System& system) {
    if (!s_enabled.load(std::memory_order_relaxed) || !system.IsPoweredOn()) {
        return;
    }
    const int percentage = s_percentage.load(std::memory_order_relaxed);
    system.CoreTiming().UpdateClockSpeed(static_cast<u32>(percentage));
    if (!s_active.exchange(true, std::memory_order_relaxed)) {
        LOG_INFO(Frontend, "movie playing: CPU clock lowered to {}%", percentage);
    }
}

void Restore(Core::System& system) {
    if (!s_active.exchange(false, std::memory_order_relaxed) || !system.IsPoweredOn()) {
        return;
    }
    const u32 percentage = Settings::values.cpu_clock_percentage.GetValue();
    system.CoreTiming().UpdateClockSpeed(percentage);
    LOG_INFO(Frontend, "movie ended: CPU clock back to {}%", percentage);
}

} // namespace

void Register(Core::System& system) {
    system.ResetMoviePlaybackState();
    s_playing = false;
    s_active = false;
    system.RegisterMoviePlaybackStateChanged([&system](bool playing) {
        s_playing.store(playing, std::memory_order_relaxed);
        if (playing) {
            Lower(system);
        } else {
            Restore(system);
        }
    });
}

void Configure(bool enabled, int percentage) {
    s_enabled.store(enabled, std::memory_order_relaxed);
    s_percentage.store(std::clamp(percentage, 10, 100), std::memory_order_relaxed);
}

void Reapply(Core::System& system) {
    // the settings have just put the clock back
    s_active.store(false, std::memory_order_relaxed);
    if (s_playing.load(std::memory_order_relaxed)) {
        Lower(system);
    }
}

} // namespace SwitchFrontend::MovieThrottle
