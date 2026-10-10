// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tico/clocks.h"

#include <atomic>

#include "common/logging/log.h"
#include "tico/switch_libnx.h"

namespace SwitchFrontend::Clocks {
namespace {

constexpr u32 kCpuClockHz = 1785000000;
constexpr u32 kGpuClockHz = 768000000;

bool s_boosted = false;
// Boost mode raises a clock, never lowers it: each is boosted only when it
// runs at or below the target. One already faster (an overclock, a clock
// manager's profile) is left as it is.
bool s_boost_cpu = false;
bool s_boost_gpu = false;
bool s_service_initialized = false;
bool s_uses_clkrst = false;
bool s_restore_cpu = false;
bool s_restore_gpu = false;
u32 s_original_cpu_hz = 0;
u32 s_original_gpu_hz = 0;

constexpr PcvModule Module(bool cpu) {
    return cpu ? PcvModule_CpuBus : PcvModule_GPU;
}

constexpr PcvModuleId ModuleId(bool cpu) {
    return cpu ? PcvModuleId_CpuBus : PcvModuleId_GPU;
}

bool InitializeService() {
    if (s_service_initialized) {
        return true;
    }
    const bool use_clkrst = hosversionAtLeast(8, 0, 0);
    const LibnxResult rc = use_clkrst ? clkrstInitialize() : pcvInitialize();
    if (rc != 0) {
        LOG_WARNING(Frontend, "clocks: {} unavailable (0x{:x})", use_clkrst ? "clkrst" : "pcv",
                    rc);
        return false;
    }
    s_service_initialized = true;
    s_uses_clkrst = use_clkrst;
    return true;
}

void ShutdownService() {
    if (!s_service_initialized) {
        return;
    }
    if (s_uses_clkrst) {
        clkrstExit();
    } else {
        pcvExit();
    }
    s_service_initialized = false;
}

bool GetRate(bool cpu, u32* hz) {
    if (!InitializeService()) {
        return false;
    }
    LibnxResult rc = 0;
    if (s_uses_clkrst) {
        ClkrstSession session{};
        rc = clkrstOpenSession(&session, ModuleId(cpu), 3);
        if (rc == 0) {
            rc = clkrstGetClockRate(&session, hz);
            clkrstCloseSession(&session);
        }
    } else {
        rc = pcvGetClockRate(Module(cpu), hz);
    }
    return rc == 0;
}

bool SetRate(bool cpu, u32 hz) {
    if (!InitializeService()) {
        return false;
    }
    LibnxResult rc = 0;
    if (s_uses_clkrst) {
        ClkrstSession session{};
        rc = clkrstOpenSession(&session, ModuleId(cpu), 3);
        if (rc == 0) {
            rc = clkrstSetClockRate(&session, hz);
            clkrstCloseSession(&session);
        }
    } else {
        rc = pcvSetClockRate(Module(cpu), hz);
    }
    if (rc != 0) {
        LOG_WARNING(Frontend, "clocks: {} at {} refused (0x{:x})", cpu ? "CPU" : "GPU", hz, rc);
    }
    return rc == 0;
}

// A clock manager (sys-clk and its forks) puts its own clocks back when it
// sees others: hoc-clk resets any clock it didn't set to stock on its next
// tick. Both take an override (command 8: module, Hz) that they keep applying,
// docked or not, until it is cleared with 0.
libnx_Service s_clock_manager;
bool s_clock_manager_open = false;

bool SetOverride(u32 module, u32 hz) {
    const struct {
        u32 module;
        u32 hz;
    } args = {module, hz};
    return serviceDispatchIn(&s_clock_manager, 8, args) == 0;
}

// Atmosphère's sm answers whether a service is registered (AtmosphereHasService)
bool HasService(const char* name) {
    const SmServiceName service_name = smEncodeName(name);
    u8 has = 0;
    // sm speaks TIPC from 12.0.0
    const LibnxResult rc =
        hosversionAtLeast(12, 0, 0)
            ? tipcDispatchInOut(smGetServiceSessionTipc(), 65100, service_name, has)
            : serviceDispatchInOut(smGetServiceSession(), 65100, service_name, has);
    return rc == 0 && has != 0;
}

bool OverrideWithClockManager() {
    // Horizon-OC's hoc-clk, kefir/4IFIR's sys-clk-OC, sys-clk
    for (const char* name : {"hoc:clk", "sysclkOC", "sysclk"}) {
        // GetService waits for a service that isn't there, so ask first
        if (!HasService(name) || smGetService(&s_clock_manager, name) != 0) {
            continue;
        }
        s_clock_manager_open = true;
        // modules: 0 CPU, 1 GPU; only the ones being boosted
        if ((!s_boost_cpu || SetOverride(0, kCpuClockHz)) &&
            (!s_boost_gpu || SetOverride(1, kGpuClockHz))) {
            LOG_INFO(Frontend, "clocks: boosted through {}", name);
            return true;
        }
        LOG_WARNING(Frontend, "clocks: {} refused the override", name);
        serviceClose(&s_clock_manager);
        s_clock_manager_open = false;
    }
    return false;
}

} // namespace

void Boost() {
    // A clock that can't be read is boosted, as before
    u32 cpu_now = 0;
    u32 gpu_now = 0;
    s_boost_cpu = !GetRate(true, &cpu_now) || cpu_now <= kCpuClockHz;
    s_boost_gpu = !GetRate(false, &gpu_now) || gpu_now <= kGpuClockHz;
    if (!s_boost_cpu) {
        LOG_INFO(Frontend, "clocks: the CPU already runs faster ({} Hz), left as it is", cpu_now);
    }
    if (!s_boost_gpu) {
        LOG_INFO(Frontend, "clocks: the GPU already runs faster ({} Hz), left as it is", gpu_now);
    }
    if (!s_boost_cpu && !s_boost_gpu) {
        return;
    }
    s_boosted = true;
    if (OverrideWithClockManager()) {
        return;
    }
    s_restore_cpu = s_boost_cpu && GetRate(true, &s_original_cpu_hz);
    s_restore_gpu = s_boost_gpu && GetRate(false, &s_original_gpu_hz);
    const bool cpu = s_boost_cpu && SetRate(true, kCpuClockHz);
    const bool gpu = s_boost_gpu && SetRate(false, kGpuClockHz);
    LOG_INFO(Frontend, "clocks: boosted (cpu {} gpu {}, were {} and {} Hz)", cpu, gpu,
             s_original_cpu_hz, s_original_gpu_hz);
}

// Without a clock manager both are checked once a second and set again. A
// clock manager is asked again instead (both overrides cleared and set, which
// it treats as a change): sys-clk and sys-clk-OC only apply clocks when
// something changes, waking up changes nothing they watch, and hoc-clk only
// compares the GPU clock. Clocks are never set behind a manager's back.
void Keep() {
    if (!s_boosted) {
        return;
    }
    const u64 now = armGetSystemTick();
    const u64 one_second = armNsToTicks(1000000000ULL);

    // the loop runs every frame: a long gap means the console slept or the game
    // was suspended
    static u64 last_pass = 0;
    const bool resumed = last_pass != 0 && now - last_pass > one_second;
    last_pass = now;
    static u8 last_mode = 0xFF;
    const u8 mode = appletGetOperationMode();
    const bool mode_changed = last_mode != 0xFF && mode != last_mode;
    last_mode = mode;

    if (s_clock_manager_open) {
        if (resumed || mode_changed) {
            SetOverride(0, 0);
            SetOverride(1, 0);
            if (s_boost_cpu) {
                SetOverride(0, kCpuClockHz);
            }
            if (s_boost_gpu) {
                SetOverride(1, kGpuClockHz);
            }
        }
        return;
    }
    if (!s_service_initialized) {
        return;
    }
    static u64 last_check = 0;
    if (!resumed && !mode_changed && now - last_check < one_second) {
        return;
    }
    last_check = now;

    u32 cpu_hz = 0;
    u32 gpu_hz = 0;
    // Only what Boost mode raised, only when it fell below: a clock something
    // else raised higher stays
    const bool cpu_reset = s_boost_cpu && GetRate(true, &cpu_hz) && cpu_hz < kCpuClockHz;
    const bool gpu_reset = s_boost_gpu && GetRate(false, &gpu_hz) && gpu_hz < kGpuClockHz;
    if (cpu_reset || gpu_reset) {
        LOG_INFO(Frontend, "clocks: were reset (cpu {} gpu {} Hz), setting them again", cpu_hz,
                 gpu_hz);
        if (cpu_reset) {
            SetRate(true, kCpuClockHz);
        }
        if (gpu_reset) {
            SetRate(false, kGpuClockHz);
        }
    }
}

namespace {
std::atomic<bool> s_load_boost_allowed{false};
std::atomic<int> s_load_boost_holds{0};
} // namespace

void AllowLoadBoost(bool allowed) {
    s_load_boost_allowed = allowed;
}

bool HoldLoadBoost(const char* why) {
    if (!s_load_boost_allowed.load(std::memory_order_acquire)) {
        return false;
    }
    u32 cpu_hz = 0;
    if (s_load_boost_holds.load(std::memory_order_acquire) == 0 && GetRate(true, &cpu_hz) &&
        cpu_hz >= kCpuClockHz) {
        LOG_INFO(Frontend, "clocks: load boost skipped ({}), the CPU already runs at {} Hz", why,
                 cpu_hz);
        return false;
    }
    if (s_load_boost_holds.fetch_add(1, std::memory_order_acq_rel) == 0) {
        if (appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad) != 0) {
            s_load_boost_holds.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }
        LOG_INFO(Frontend, "clocks: load boost on ({})", why);
    }
    return true;
}

void ReleaseLoadBoost() {
    if (s_load_boost_holds.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        LOG_INFO(Frontend, "clocks: load boost off");
    }
}

void Restore() {
    if (s_load_boost_holds.exchange(0, std::memory_order_acq_rel) > 0) {
        appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    }
    if (!s_boosted) {
        return;
    }
    s_boosted = false;
    if (s_clock_manager_open) {
        SetOverride(0, 0);
        SetOverride(1, 0);
        serviceClose(&s_clock_manager);
        s_clock_manager_open = false;
        return;
    }
    if (s_restore_gpu) {
        SetRate(false, s_original_gpu_hz);
    }
    if (s_restore_cpu) {
        SetRate(true, s_original_cpu_hz);
    }
    ShutdownService();
}

} // namespace SwitchFrontend::Clocks
