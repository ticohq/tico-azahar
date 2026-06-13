// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "tico/switch_libnx.h"

namespace Vulkan {
class RendererVulkan;
}

namespace SwitchFrontend::VulkanOverlay {

// Initializes the ImGui Vulkan backend against azahar's Vulkan renderer and
// registers a present-time draw callback. Must be called after at least one guest
// frame has been presented (so the swapchain exists). Returns false if the renderer
// is not ready yet; safe to retry.
bool Init(Vulkan::RendererVulkan& renderer);

// Polls the pad: toggles the overlay on Plus+Minus and feeds D-pad/stick navigation.
void Update(PadState* pad);

bool IsVisible();
bool ShouldExit();

// Returns and clears the last queued OverlayUI::Action (0 when none).
int ConsumeAction();

void Shutdown();

} // namespace SwitchFrontend::VulkanOverlay
