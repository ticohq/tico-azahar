// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>
#include <queue>

#include "common/logging/log.h"
#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "common/unique_function.h"

namespace Common {

#ifdef __SWITCH__
namespace SwitchThreadWorker {
extern "C" std::uint32_t svcSetThreadCoreMask(std::uint32_t handle,
                                               std::int32_t preferred_core,
                                               std::uint32_t affinity_mask);
extern "C" std::uint32_t svcGetThreadCoreMask(std::int32_t* out_preferred_core,
                                               std::uint64_t* out_affinity_mask,
                                               std::uint32_t handle);

constexpr std::uint32_t CurrentThreadHandle = 0xFFFF8000;

inline std::int32_t SelectWorkerCore(std::string_view name, std::size_t index) {
    std::size_t offset = 0;
    if (name.find("Shader") != std::string_view::npos) {
        offset = 0;
    } else if (name.find("Pipeline") != std::string_view::npos) {
        offset = 1;
    }
    return static_cast<std::int32_t>((index + offset) % 2);
}

inline void PinWorkerThread(std::string_view name, std::size_t index) {
    const std::int32_t core = SelectWorkerCore(name, index);
    const std::uint32_t mask = 1u << static_cast<std::uint32_t>(core);
    const std::uint32_t set_rc = svcSetThreadCoreMask(CurrentThreadHandle, core, mask);
    std::int32_t preferred = -1;
    std::uint64_t affinity = 0;
    const std::uint32_t get_rc = svcGetThreadCoreMask(&preferred, &affinity, CurrentThreadHandle);
    LOG_INFO(Common,
             "Switch worker affinity {}[{}]: set core={} mask=0x{:x} rc=0x{:x} get_rc=0x{:x} preferred={} affinity=0x{:x}",
             name, index, core, mask, set_rc, get_rc, preferred, affinity);
}
} // namespace SwitchThreadWorker
#endif

template <class StateType = void>
class StatefulThreadWorker {
    static constexpr bool with_state = !std::is_same_v<StateType, void>;

    struct DummyCallable {
        int operator()(std::size_t) const noexcept {
            return 0;
        }
    };

    using Task =
        std::conditional_t<with_state, UniqueFunction<void, StateType*>, UniqueFunction<void>>;
    using StateMaker =
        std::conditional_t<with_state, std::function<StateType(std::size_t)>, DummyCallable>;

public:
    explicit StatefulThreadWorker(std::size_t num_workers, std::string_view name,
                                  StateMaker func = {})
        : workers_queued{num_workers}, thread_name{name} {
        const auto lambda = [this, func](std::stop_token stop_token, std::size_t index) {
            Common::SetCurrentThreadName(thread_name.data());
#ifdef __SWITCH__
            SwitchThreadWorker::PinWorkerThread(thread_name, index);
#endif
            {
                [[maybe_unused]] std::conditional_t<with_state, StateType, int> state{func(index)};
                while (!stop_token.stop_requested()) {
                    Task task;
                    {
                        std::unique_lock lock{queue_mutex};
                        if (requests.empty()) {
                            wait_condition.notify_all();
                        }
                        Common::CondvarWait(condition, lock, stop_token,
                                            [this] { return !requests.empty(); });
                        if (stop_token.stop_requested()) {
                            break;
                        }
                        task = std::move(requests.front());
                        requests.pop();
                    }
                    if constexpr (with_state) {
                        task(&state);
                    } else {
                        task();
                    }
                    ++work_done;
                }
            }
            ++workers_stopped;
            wait_condition.notify_all();
        };
        threads.reserve(num_workers);
        for (std::size_t i = 0; i < num_workers; ++i) {
            threads.emplace_back(lambda, i);
        }
    }

    StatefulThreadWorker& operator=(const StatefulThreadWorker&) = delete;
    StatefulThreadWorker(const StatefulThreadWorker&) = delete;

    StatefulThreadWorker& operator=(StatefulThreadWorker&&) = delete;
    StatefulThreadWorker(StatefulThreadWorker&&) = delete;

    void QueueWork(Task work) {
        {
            std::unique_lock lock{queue_mutex};
            requests.emplace(std::move(work));
            ++work_scheduled;
        }
        condition.notify_one();
    }

    void WaitForRequests(std::stop_token stop_token = {}) {
        std::stop_callback callback(stop_token, [this] {
            for (auto& thread : threads) {
                thread.request_stop();
            }
        });
        std::unique_lock lock{queue_mutex};
        wait_condition.wait(lock, [this] {
            return workers_stopped >= workers_queued || work_done >= work_scheduled;
        });
    }

    const std::size_t NumWorkers() const noexcept {
        return threads.size();
    }

private:
    std::queue<Task> requests;
    std::mutex queue_mutex;
    std::condition_variable_any condition;
    std::condition_variable wait_condition;
    std::atomic<std::size_t> work_scheduled{};
    std::atomic<std::size_t> work_done{};
    std::atomic<std::size_t> workers_stopped{};
    std::atomic<std::size_t> workers_queued{};
    std::string_view thread_name;
    std::vector<std::jthread> threads;
};

using ThreadWorker = StatefulThreadWorker<>;

} // namespace Common
