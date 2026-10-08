// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <memory>

#include "common/common_types.h"

namespace Frontend {
class EmuWindow;
}

namespace Core {
class System;
}

namespace Pica {
class PicaCore;
}

namespace VideoCore {

class RendererBase;

std::unique_ptr<RendererBase> CreateRenderer(Frontend::EmuWindow& emu_window,
                                             Frontend::EmuWindow* secondary_window,
                                             Pica::PicaCore& pica, Core::System& system);

// Shader and pipeline builds handed to the compile workers and not yet done, for the
// frontend's "Compiling shaders" notice.
void NotifyShaderCompileBegin();
void NotifyShaderCompileEnd();
u32 GetPendingShaderCompiles();

} // namespace VideoCore
