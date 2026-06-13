// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <cstring>
#include "common/assert.h"
#include "common/hash.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "video_core/shader/shader.h"
#include "video_core/shader/shader_jit.h"
#if CITRA_ARCH(arm64)
#include "video_core/shader/shader_jit_a64_compiler.h"
#endif
#if CITRA_ARCH(x86_64)
#include "video_core/shader/shader_jit_x64_compiler.h"
#endif

namespace Pica::Shader {

#if CITRA_ARCH(arm64)
JitEngine::JitEngine() : code_pool(std::make_unique<oaknut::CodeBlock>(kCodePoolSize)) {}
#else
JitEngine::JitEngine() = default;
#endif
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

#if CITRA_ARCH(arm64)
        const std::size_t code_size = shader->GetCompiledSize();
        // Align to 4 bytes (ARM64 instruction size).
        const std::size_t aligned_size = (code_size + 3u) & ~3u;

        if (pool_write_pos + aligned_size > kCodePoolSize) {
            // Pool full — evict all cached shaders and reuse from the beginning.
            // SetupBatch is always called immediately before Run for the same setup, so
            // dangling cached_shader pointers in other setups are re-resolved on their
            // next SetupBatch call before they are used.
            LOG_WARNING(Render_Software,
                        "Shader JIT pool full ({} shaders evicted), resetting",
                        cache.size());
            cache.clear();
            pool_write_pos = 0;
            iter = cache.end(); // cache is empty; use end() as emplace hint below
        }

        // Copy compiled code into the shared pool.
        auto* const wptr = reinterpret_cast<std::byte*>(code_pool->wptr()) + pool_write_pos;
        auto* const xptr = reinterpret_cast<std::byte*>(code_pool->xptr()) + pool_write_pos;
        std::memcpy(wptr, shader->GetCompiledCode().data(), code_size);

        // Set the shader's RX base and flush the I-cache for the written region.
        shader->FinalizePool(xptr);
        code_pool->invalidate(reinterpret_cast<std::uint32_t*>(xptr), code_size);

        pool_write_pos += aligned_size;
#endif

        setup.cached_shader = shader.get();
        cache.emplace_hint(iter, cache_key, std::move(shader));
    }
}

MICROPROFILE_DECLARE(GPU_Shader);

void JitEngine::Run(const ShaderSetup& setup, ShaderUnit& state) const {
    ASSERT(setup.cached_shader != nullptr);

    MICROPROFILE_SCOPE(GPU_Shader);

    const JitShader* shader = static_cast<const JitShader*>(setup.cached_shader);
    shader->Run(setup, state, setup.entry_point);
}

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
