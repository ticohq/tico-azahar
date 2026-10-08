// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>
#include "common/arch.h"
#if CITRA_ARCH(arm64)
#include <atomic>
#include <oaknut/code_block.hpp>
#include "common/thread_worker.h"
#include "video_core/pica/shader_setup.h"
#endif
#include "common/common_types.h"
#include "video_core/shader/shader.h"

namespace Pica::Shader {

class JitShader;
class InterpreterEngine;

class JitEngine final : public ShaderEngine {
public:
    JitEngine();
    ~JitEngine() override;

    void SetupBatch(ShaderSetup& setup, u32 entry_point) override;
    void Run(const ShaderSetup& setup, ShaderUnit& state) const override;

private:
#if CITRA_ARCH(arm64)
    // A shader compiled into plain memory, by the compile worker or inline, and then copied
    // into the pool by SetupBatch. Shared so a compile still running when the pool is emptied
    // finishes into an entry nobody uses any more rather than into freed memory.
    struct CacheEntry {
        std::unique_ptr<JitShader> shader;
        std::atomic<bool> ready{false};
        bool failed = false; // set before ready: the interpreter runs this one
        bool installed = false;
    };
    using Entry = std::shared_ptr<CacheEntry>;

    static void CompileEntry(CacheEntry& entry, const ProgramCode& program_code,
                             const SwizzleData& swizzle_data);
    // Copies the entry's code into the pool, emptying the pool first when it is full.
    void Install(u64 key, const Entry& entry);
    void EmptyPool();

    // One shared code pool for all compiled shaders — avoids per-shader kernel JIT handles.
    static constexpr std::size_t kCodePoolSize = 32 * 1024 * 1024; // 32 MiB
    std::unique_ptr<oaknut::CodeBlock> code_pool;
    std::size_t pool_write_pos = 0;
    // Counts the times the pool was emptied; a shader from an older one runs on the
    // interpreter (the geometry shader's setup can empty the pool between the vertex
    // shader's setup and its run).
    u64 pool_generation = 1;

    std::unordered_map<u64, Entry> cache;
    // The last generation's entries, kept so a shader set up before the pool was emptied is
    // still there to be found stale.
    std::vector<Entry> retired;
    std::unique_ptr<InterpreterEngine> interpreter;
    // Compiles new shaders off the emulation thread when shaders compile asynchronously.
    std::unique_ptr<Common::ThreadWorker> compile_worker;
#else
    std::unordered_map<u64, std::unique_ptr<JitShader>> cache;
#endif
};

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
