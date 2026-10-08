// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

// The renderer tico's overlay draws with: SDL's while the game list runs before a
// game (LibraryScreen), Azahar's Vulkan presentation during a game (GameOverlay).

#include "overlay/overlay_renderer.h"

#include "tico/game_overlay.h"
#include "tico/library_screen.h"

namespace SwitchFrontend::OverlayRenderer {

bool Init() {
    return LibraryScreen::IsActive() ? LibraryScreen::RendererInit()
                                     : GameOverlay::RendererInit();
}

void Shutdown() {
    if (LibraryScreen::IsActive()) {
        LibraryScreen::RendererShutdown();
    } else {
        GameOverlay::RendererShutdown();
    }
}

void BeginFrame() {
    if (LibraryScreen::IsActive()) {
        LibraryScreen::RendererBeginFrame();
    } else {
        GameOverlay::RendererBeginFrame();
    }
}

} // namespace SwitchFrontend::OverlayRenderer
