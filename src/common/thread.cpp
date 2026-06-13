// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>

#include "common/error.h"
#include "common/logging/log.h"
#include "common/thread.h"
#ifdef __APPLE__
#include <mach/mach.h>
#elif defined(_WIN32)
#include <windows.h>
#include "common/string_util.h"
#elif defined(__SWITCH__)
#else
#if defined(__Bitrig__) || defined(__DragonFly__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <pthread_np.h>
#else
#include <pthread.h>
#endif
#include <sched.h>
#endif
#if !defined(_WIN32) && !defined(__SWITCH__)
#include <unistd.h>
#endif

#ifdef __FreeBSD__
#define cpu_set_t cpuset_t
#endif

namespace Common {

#ifdef _WIN32

void SetCurrentThreadPriority(ThreadPriority new_priority) {
    auto handle = GetCurrentThread();
    int windows_priority = 0;
    switch (new_priority) {
    case ThreadPriority::Low:
        windows_priority = THREAD_PRIORITY_BELOW_NORMAL;
        break;
    case ThreadPriority::Normal:
        windows_priority = THREAD_PRIORITY_NORMAL;
        break;
    case ThreadPriority::High:
        windows_priority = THREAD_PRIORITY_ABOVE_NORMAL;
        break;
    case ThreadPriority::VeryHigh:
        windows_priority = THREAD_PRIORITY_HIGHEST;
        break;
    case ThreadPriority::Critical:
        windows_priority = THREAD_PRIORITY_TIME_CRITICAL;
        break;
    default:
        windows_priority = THREAD_PRIORITY_NORMAL;
        break;
    }
    SetThreadPriority(handle, windows_priority);
}

#elif defined(__SWITCH__)

// Switch priorities: 0 = highest, 63 = lowest; user apps default to 44.
// 0xFFFF8000 is CUR_THREAD_HANDLE (same value as INVALID_HANDLE in libnx).
extern "C" unsigned svcSetThreadPriority(unsigned handle, unsigned prio);
extern "C" unsigned svcSetThreadCoreMask(unsigned handle, int preferred_core, unsigned long long affinity_mask);

void SetCurrentThreadPriority(ThreadPriority new_priority) {
    unsigned prio;
    switch (new_priority) {
    case ThreadPriority::Low:      prio = 51; break;
    case ThreadPriority::Normal:   prio = 44; break;
    case ThreadPriority::High:     prio = 40; break;
    case ThreadPriority::VeryHigh: prio = 36; break;
    case ThreadPriority::Critical: prio = 32; break;
    default:                       prio = 44; break;
    }
    svcSetThreadPriority(0xFFFF8000U, prio);
}

void SetCurrentThreadAffinityMask(s32 preferred_core, u64 affinity_mask) {
    svcSetThreadCoreMask(0xFFFF8000U, preferred_core, affinity_mask);
}

#else

void SetCurrentThreadAffinityMask(s32 preferred_core, u64 affinity_mask) {
    (void)preferred_core;
    (void)affinity_mask;
}

void SetCurrentThreadPriority(ThreadPriority new_priority) {
    pthread_t this_thread = pthread_self();

    const auto scheduling_type = SCHED_OTHER;
    s32 max_prio = sched_get_priority_max(scheduling_type);
    s32 min_prio = sched_get_priority_min(scheduling_type);
    u32 level = std::max(static_cast<u32>(new_priority) + 1, 4U);

    struct sched_param params;
    if (max_prio > min_prio) {
        params.sched_priority = min_prio + ((max_prio - min_prio) * level) / 4;
    } else {
        params.sched_priority = min_prio - ((min_prio - max_prio) * level) / 4;
    }

    pthread_setschedparam(this_thread, scheduling_type, &params);
}

#endif

#ifdef _MSC_VER

// Sets the debugger-visible name of the current thread.
void SetCurrentThreadName(const char* name) {
    SetThreadDescription(GetCurrentThread(), UTF8ToUTF16W(name).data());
}

#else // !MSVC_VER, so must be POSIX threads

// MinGW with the POSIX threading model does not support pthread_setname_np
#if !defined(_WIN32) || defined(_MSC_VER)
void SetCurrentThreadName(const char* name) {
#ifdef __APPLE__
    pthread_setname_np(name);
#elif defined(__Bitrig__) || defined(__DragonFly__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    pthread_set_name_np(pthread_self(), name);
#elif defined(__NetBSD__)
    pthread_setname_np(pthread_self(), "%s", (void*)name);
#elif defined(__linux__)
    // Linux limits thread names to 15 characters and will outright reject any
    // attempt to set a longer name with ERANGE.
    std::string truncated(name, std::min(strlen(name), static_cast<std::size_t>(15)));
    if (int e = pthread_setname_np(pthread_self(), truncated.c_str())) {
        errno = e;
        LOG_ERROR(Common, "Failed to set thread name to '{}': {}", truncated, GetLastErrorMsg());
    }
#elif defined(__SWITCH__)
    (void)name;
#else
    pthread_setname_np(pthread_self(), name);
#endif
}
#endif

#if defined(_WIN32)
void SetCurrentThreadName(const char*) {
    // Do Nothing on MingW
}
#endif

#endif

} // namespace Common
