// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <cstring>
#include "common/assert.h"
#include "common/hash.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "common/thread.h"
#include "video_core/shader/shader.h"
#include "video_core/shader/shader_interpreter.h"
#include "video_core/shader/shader_jit.h"
#if CITRA_ARCH(arm64)
#include "video_core/shader/shader_jit_a64_compiler.h"
#endif
#if CITRA_ARCH(x86_64)
#include "video_core/shader/shader_jit_x64_compiler.h"
#endif

namespace Pica::Shader {

#if CITRA_ARCH(arm64)

// The hybrid of a shared pool and asynchronous compiles: Horizon maps executable memory in
// few, costly regions, so every shader shares one pool; a new shader compiles into plain memory
// on the compile worker, runs on the interpreter meanwhile, and only this thread (the one
// setting shaders up) copies code into the pool.

JitEngine::JitEngine()
    : code_pool(std::make_unique<oaknut::CodeBlock>(kCodePoolSize)),
      interpreter(std::make_unique<InterpreterEngine>()) {
    if (Settings::values.async_shader_compilation.GetValue()) {
        compile_worker = std::make_unique<Common::ThreadWorker>(1, "Shader JIT");
    }
}

JitEngine::~JitEngine() = default;

void JitEngine::CompileEntry(CacheEntry& entry, const ProgramCode& program_code,
                             const SwizzleData& swizzle_data) {
    try {
        auto shader = std::make_unique<JitShader>();
        shader->Compile(&program_code, &swizzle_data);
        entry.shader = std::move(shader);
    } catch (const std::exception& e) {
        LOG_ERROR(HW_GPU, "Failed to compile shader, falling back to the interpreter: {}",
                  e.what());
        entry.failed = true;
    }
    entry.ready.store(true, std::memory_order_release);
}

void JitEngine::EmptyPool() {
    LOG_WARNING(Render_Software, "Shader JIT pool full ({} shaders evicted), resetting",
                cache.size());
    retired.clear();
    for (auto& [key, entry] : cache) {
        retired.push_back(std::move(entry));
    }
    cache.clear();
    pool_write_pos = 0;
    pool_generation++;
}

void JitEngine::Install(u64 key, const Entry& entry) {
    JitShader& shader = *entry->shader;
    const std::size_t code_size = shader.GetCompiledSize();
    // Align to 4 bytes (ARM64 instruction size).
    const std::size_t aligned_size = (code_size + 3u) & ~3u;
    if (pool_write_pos + aligned_size > kCodePoolSize) {
        EmptyPool();
        cache.emplace(key, entry);
    }

    // Copy compiled code into the shared pool.
    auto* const wptr = reinterpret_cast<std::byte*>(code_pool->wptr()) + pool_write_pos;
    auto* const xptr = reinterpret_cast<std::byte*>(code_pool->xptr()) + pool_write_pos;
    std::memcpy(wptr, shader.GetCompiledCode().data(), code_size);

    // Set the shader's RX base and flush the I-cache for the written region.
    shader.FinalizePool(xptr, pool_generation);
    code_pool->invalidate(reinterpret_cast<std::uint32_t*>(xptr), code_size);

    pool_write_pos += aligned_size;
    entry->installed = true;
}

void JitEngine::SetupBatch(ShaderSetup& setup, u32 entry_point) {
    ASSERT(entry_point < MAX_PROGRAM_CODE_LENGTH);
    setup.entry_point = entry_point;

    setup.DoProgramCodeFixup();
    const u64 code_hash = setup.GetProgramCodeHash();
    const u64 swizzle_hash = setup.GetSwizzleDataHash();
    const u64 cache_key = Common::HashCombine(code_hash, swizzle_hash);

    auto [iter, inserted] = cache.try_emplace(cache_key);
    if (inserted) {
        iter->second = std::make_shared<CacheEntry>();
        if (compile_worker) {
            // a copy of the bytecode: the setup keeps changing while the worker compiles
            compile_worker->QueueWork(
                [entry = iter->second, program_code = setup.GetProgramCode(),
                 swizzle_data = setup.GetSwizzleData()] {
                    static thread_local const bool lowered = [] {
                        Common::SetCurrentThreadPriority(Common::ThreadPriority::Low);
                        return true;
                    }();
                    (void)lowered;
                    CompileEntry(*entry, program_code, swizzle_data);
                });
        } else {
            CompileEntry(*iter->second, setup.GetProgramCode(), setup.GetSwizzleData());
        }
    }

    const Entry entry = iter->second;
    if (!entry->ready.load(std::memory_order_acquire) || entry->failed) {
        // still compiling, or could not be: the interpreter runs it
        setup.cached_shader = nullptr;
        return;
    }
    if (!entry->installed) {
        Install(cache_key, entry);
    }
    setup.cached_shader = entry->shader.get();
}

#else

JitEngine::JitEngine() = default;
JitEngine::~JitEngine() = default;

void JitEngine::SetupBatch(ShaderSetup& setup, u32 entry_point) {
    ASSERT(entry_point < MAX_PROGRAM_CODE_LENGTH);
    setup.entry_point = entry_point;

    setup.DoProgramCodeFixup();
    const u64 code_hash = setup.GetProgramCodeHash();
    const u64 swizzle_hash = setup.GetSwizzleDataHash();

    const u64 cache_key = Common::HashCombine(code_hash, swizzle_hash);
    auto iter = cache.find(cache_key);
    if (iter != cache.end()) {
        setup.cached_shader = iter->second.get();
    } else {
        auto shader = std::make_unique<JitShader>();
        shader->Compile(&setup.GetProgramCode(), &setup.GetSwizzleData());
        setup.cached_shader = shader.get();
        cache.emplace_hint(iter, cache_key, std::move(shader));
    }
}

#endif

MICROPROFILE_DECLARE(GPU_Shader);

void JitEngine::Run(const ShaderSetup& setup, ShaderUnit& state) const {
#if CITRA_ARCH(arm64)
    const JitShader* shader = static_cast<const JitShader*>(setup.cached_shader);
    if (!shader || shader->PoolGeneration() != pool_generation) {
        interpreter->Run(setup, state);
        return;
    }
#else
    ASSERT(setup.cached_shader != nullptr);
    const JitShader* shader = static_cast<const JitShader*>(setup.cached_shader);
#endif

    MICROPROFILE_SCOPE(GPU_Shader);
    shader->Run(setup, state, setup.entry_point);
}

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
