// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tico/saves.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include <fmt/format.h>

#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/3ds.h"
#include "core/core.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/frontend/image_interface.h"
#include "overlay/tico_config.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

namespace SwitchFrontend::Saves {
namespace {

// a picture of both screens, stacked, at the 3DS's own size
constexpr u32 kPictureWidth = Core::kScreenTopWidth;
constexpr u32 kPictureHeight = Core::kScreenTopHeight + Core::kScreenBottomHeight;

enum class Picture { None, Pending, Ready };
std::atomic<Picture> s_picture = Picture::None;
std::vector<u8> s_picture_bgra;

// Undo Load State: the game as it was before the last load
std::vector<u8> s_undo_load;
// Undo Save State: the slot the last save overwrote, and that state's time
int s_undo_slot = 0;
std::string s_undo_saved_at;

// tico's root for a kind of content (saves, states), with the 3DS's folder in it
std::string ContentDir(const char* key, const char* default_root) {
    std::string root = TicoConfig::GetConfigValue(key, default_root);
    if (root.empty()) {
        root = default_root;
    }
    if (root.back() != '/') {
        root += '/';
    }
    return root + "3ds/";
}

bool Exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

// Moves what @p from holds into @p to, entry by entry, never over one already
// there. False when an entry could not move (e.g. @p to is on another drive).
bool MoveEntries(const std::string& from, const std::string& to) {
    DIR* dir = opendir(from.c_str());
    if (!dir) {
        return true;
    }
    std::vector<std::string> names;
    while (const dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            names.push_back(name);
        }
    }
    closedir(dir);

    bool moved = true;
    for (const std::string& name : names) {
        const std::string source = from + name;
        const std::string target = to + name;
        if (Exists(target)) {
            LOG_WARNING(Frontend, "saves: {} is already there, {} stays", target, source);
            continue;
        }
        if (std::rename(source.c_str(), target.c_str()) == 0) {
            LOG_INFO(Frontend, "saves: moved {} to {}", source, target);
        } else {
            LOG_WARNING(Frontend, "saves: could not move {} to {}", source, target);
            moved = false;
        }
    }
    return moved;
}

// Points one of Azahar's folders at @p dir, moving what the old one held. When
// something could not move, the old folder stays in use, so no save goes missing.
void UseFolder(FileUtil::UserPath path, const std::string& dir) {
    const std::string old_dir = FileUtil::GetUserPath(path);
    if (!FileUtil::CreateFullPath(dir)) {
        LOG_ERROR(Frontend, "saves: could not make {}, keeping {}", dir, old_dir);
        return;
    }
    if (old_dir != dir && FileUtil::IsDirectory(old_dir) && !MoveEntries(old_dir, dir)) {
        LOG_ERROR(Frontend, "saves: {} could not move entirely, keeping it", old_dir);
        return;
    }
    FileUtil::UpdateUserPath(path, dir);
}

bool CopyFile(const std::string& from, const std::string& to) {
    return Exists(from) && FileUtil::Copy(from, to);
}

std::string ModifiedAt(const std::string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
        return {};
    }
    char when[32];
    if (!std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", std::localtime(&st.st_mtime))) {
        return {};
    }
    return when;
}

std::string UndoPath(u64 title_id) {
    return fmt::format("{}{:016X}.undo.cst", FileUtil::GetUserPath(FileUtil::UserPath::StatesDir),
                       title_id);
}

} // namespace

void UseTicoFolders() {
    UseFolder(FileUtil::UserPath::SDMCDir, ContentDir("tico_saves_path", "sdmc:/tico/saves/"));
    UseFolder(FileUtil::UserPath::StatesDir, ContentDir("tico_states_path", "sdmc:/tico/states/"));
    LOG_INFO(Frontend, "saves: SD card in {}, states in {}",
             FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir),
             FileUtil::GetUserPath(FileUtil::UserPath::StatesDir));
}

std::string StatePath(u64 title_id, int slot) {
    // as Core::GetSaveStatePath names it (no movie playing)
    return fmt::format("{}{:016X}.{:02d}.cst",
                       FileUtil::GetUserPath(FileUtil::UserPath::StatesDir), title_id, slot);
}

std::string PicturePath(u64 title_id, int slot) {
    return StatePath(title_id, slot) + ".png";
}

void RequestPicture(Core::System& system) {
    if (!system.IsPoweredOn() || s_picture.load() == Picture::Pending) {
        return;
    }
    auto& renderer = system.GPU().Renderer();
    if (renderer.IsScreenshotPending()) {
        return;
    }
    s_picture_bgra.assign(static_cast<std::size_t>(kPictureWidth) * kPictureHeight * 4, 0);
    s_picture.store(Picture::Pending);
    renderer.RequestScreenshot(
        s_picture_bgra.data(), [](bool) { s_picture.store(Picture::Ready); },
        Layout::DefaultFrameLayout(kPictureWidth, kPictureHeight,
                                   Settings::values.swap_screen.GetValue(), false));
}

bool PictureDone() {
    return s_picture.load() == Picture::Ready;
}

void KeepPictureFor(Core::System& system, u64 title_id, int slot) {
    if (s_picture.load() != Picture::Ready) {
        return;
    }
    // the screenshot is BGRA; the PNG is RGBA, opaque
    std::vector<u8> rgba(s_picture_bgra.size());
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        rgba[i + 0] = s_picture_bgra[i + 2];
        rgba[i + 1] = s_picture_bgra[i + 1];
        rgba[i + 2] = s_picture_bgra[i + 0];
        rgba[i + 3] = 0xFF;
    }
    if (!system.GetImageInterface()->EncodePNG(PicturePath(title_id, slot), kPictureWidth,
                                               kPictureHeight, rgba)) {
        LOG_WARNING(Frontend, "saves: no picture for slot {}", slot);
    }
}

void SaveSlot(Core::System& system, u64 title_id, int slot) {
    const std::string path = StatePath(title_id, slot);
    const bool overwrites = Exists(path);
    if (overwrites && CopyFile(path, UndoPath(title_id))) {
        CopyFile(PicturePath(title_id, slot), UndoPath(title_id) + ".png");
        s_undo_slot = slot;
        s_undo_saved_at = ModifiedAt(path);
    }
    system.SaveState(static_cast<u32>(slot));
    KeepPictureFor(system, title_id, slot);
}

void LoadSlot(Core::System& system, int slot) {
    try {
        s_undo_load = system.SaveStateBuffer();
    } catch (const std::exception& e) {
        LOG_WARNING(Frontend, "saves: nothing to undo this load ({})", e.what());
        s_undo_load.clear();
    }
    system.LoadState(static_cast<u32>(slot));
}

bool CanUndoLoad() {
    return !s_undo_load.empty();
}

void UndoLoad(Core::System& system) {
    if (s_undo_load.empty()) {
        return;
    }
    system.LoadStateBuffer(std::exchange(s_undo_load, {}));
}

std::string OverwrittenSavedAt() {
    return s_undo_slot != 0 ? s_undo_saved_at : std::string{};
}

void UndoSave(Core::System& system, u64 title_id) {
    if (s_undo_slot == 0) {
        return;
    }
    const int slot = std::exchange(s_undo_slot, 0);
    s_undo_saved_at.clear();
    const std::string undo = UndoPath(title_id);
    const bool restored = CopyFile(undo, StatePath(title_id, slot));
    if (restored && !CopyFile(undo + ".png", PicturePath(title_id, slot))) {
        std::remove(PicturePath(title_id, slot).c_str());
    }
    std::remove(undo.c_str());
    std::remove((undo + ".png").c_str());
    if (restored) {
        LoadSlot(system, slot);
    }
}

} // namespace SwitchFrontend::Saves
