// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The renderer the overlay draws with, provided by the frontend: Azahar's
// Vulkan presentation (src/tico/game_overlay.cpp).
namespace SwitchFrontend::OverlayRenderer {

// Sets up the renderer for the current ImGui context.
bool Init();
void Shutdown();
// Before ImGui::NewFrame.
void BeginFrame();

} // namespace SwitchFrontend::OverlayRenderer
