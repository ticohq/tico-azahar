// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <string>

namespace SwitchFrontend::OverlayUI {

enum class Action {
    None,
    Resume,
    Exit,
    SaveStateSlot1,
    SaveStateSlot2,
    SaveStateSlot3,
    SaveStateSlot4,
    LoadStateSlot1,
    LoadStateSlot2,
    LoadStateSlot3,
    LoadStateSlot4,
};

enum class ToastCorner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

bool IsSaveStateAction(Action action);
bool IsLoadStateAction(Action action);
int GetStateSlotForAction(Action action);

void SetVisible(bool visible);

Action Render(int display_w, int display_h);

void SetGameTitle(std::string title);
void SetNickname(std::string nickname);
void SetAvatarTextureId(unsigned long long texture_id);
void ShowToast(std::string message, ToastCorner corner = ToastCorner::TopLeft);
bool HasTransientContent();

// Predicate the overlay uses to label save/load slots as "In Use" or "Empty".
// The Switch frontend registers one that queries azahar's savestate listing.
using SlotOccupiedFn = std::function<bool(int slot)>;
void SetSlotOccupiedCallback(SlotOccupiedFn callback);

struct NavInput {
    bool up;
    bool down;
    bool left;
    bool right;
    bool accept;
    bool cancel;
};
void FeedNav(const NavInput& nav);

} // namespace SwitchFrontend::OverlayUI
