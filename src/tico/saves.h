// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "common/common_types.h"

namespace Core {
class System;
}

// Game saves and save states in tico's folders, as the other cores keep them.
//
// The emulated SD card, where 3DS games keep their saves and extra data, is
// sdmc:/tico/saves/3ds, and the save states are in sdmc:/tico/states/3ds (or
// under the roots set in tico). The console's NAND stays in tico/system/3ds.
namespace SwitchFrontend::Saves {

// Points Azahar at tico's folders, moving the SD card and states from where
// earlier versions kept them, once. Never overwrites. Call before the game loads.
void UseTicoFolders();

// Slot @p slot's state file and its picture, for the running game.
std::string StatePath(u64 title_id, int slot);
std::string PicturePath(u64 title_id, int slot);

// A picture of the next frame the game draws (both screens), kept in memory.
void RequestPicture(Core::System& system);
// True once the requested picture is taken.
bool PictureDone();
// Writes the last picture as @p slot's, once the state is saved.
void KeepPictureFor(Core::System& system, u64 title_id, int slot);

// Saves @p slot, keeping the state it overwrites for Undo Save State.
void SaveSlot(Core::System& system, u64 title_id, int slot);
// Loads @p slot, keeping the game as it was for Undo Load State.
void LoadSlot(Core::System& system, int slot);

// Undo Load State: back to the game as it was before the last load.
bool CanUndoLoad();
void UndoLoad(Core::System& system);
// Undo Save State: the state the last save overwrote, back in its slot and
// loaded. Its time ("2026-10-04 03:21"), empty when there is nothing to undo.
std::string OverwrittenSavedAt();
void UndoSave(Core::System& system, u64 title_id);

} // namespace SwitchFrontend::Saves
