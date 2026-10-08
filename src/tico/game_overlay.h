// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include "overlay/overlay_ui.h"
#include "tico/switch_libnx.h"

namespace Vulkan {
class RendererVulkan;
}

// tico's overlay (tico/overlay) drawn over Azahar's Vulkan presentation.
//
// The menu is built and drawn on the present thread, after the game's frame is
// in the swapchain image; the main loop only queues input and takes actions.
namespace SwitchFrontend::GameOverlay {

// Once the renderer presents: draws the overlay from the next present on.
bool Init(Vulkan::RendererVulkan& renderer);
void Shutdown();

// Plus + Minus opens and closes the menu; while it is open the pad drives it.
void Update(PadState* pad);
bool IsVisible();
void SetVisible(bool visible);

// Opens the menu on a message with one row per choice; the choice comes back
// as Action::NoticeChoice.
void ShowNotice(std::string message, std::vector<std::string> choices);

// Opens the menu on "Continue where you left off?"; Continue comes back as the
// auto slot's load action, Start Over as Action::Resume.
void ShowResumePrompt();

// A picture (PNG) as a texture for the menu, and its width / height; 0 when there
// is none. Only while the menu is being drawn (e.g. from a slot preview callback).
unsigned long long LoadPicture(const std::string& path, float* aspect);
void FreePicture(unsigned long long texture);

// Reads the Cheats menu's list again on the next frame.
void RequestCheatRefresh();

// The action the menu returned since the last call, once.
OverlayUI::Action ConsumeAction();

} // namespace SwitchFrontend::GameOverlay
