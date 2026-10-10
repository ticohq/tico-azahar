// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

// Lists the 3DS games found in tico's ROM bases (<base>/3ds/) and the module's
// own folders (tico_rom_folders in azahar.jsonc, edited from Settings >
// Library), as tico-dolphin's library does.

#include "tico/library.h"
#include "tico/session_bridge.h"

#include <sstream>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <vector>

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include "TicoUtils.h"
#include "UsbStorage.h"
#include "json.hpp"
#include "overlay/overlay_ui.h"
#include "overlay/tico_config.h"

namespace SwitchFrontend::Library {
namespace {

constexpr const char* kSlug = "3ds";
constexpr const char* kTitle = "Nintendo 3DS";
// as tico/module/module.json lists them
constexpr std::array<const char*, 10> kExtensions = {
    ".3ds", ".3dsx", ".z3dsx", ".elf", ".axf", ".cci", ".zcci", ".cxi", ".zcxi", ".app",
};

std::function<void(const std::string&)> s_launch;

std::string LowerExtension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    std::string ext = path.substr(dot);
    std::ranges::transform(ext, ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string WithSlash(std::string path) {
    std::ranges::replace(path, '\\', '/');
    if (!path.empty() && path.back() != '/') {
        path += '/';
    }
    return path;
}

// tico's ROM bases (general.jsonc): the ROMs path, then the extra bases.
std::vector<std::string> TicoRomBases() {
    std::vector<std::string> bases;
    // tico's settings, from its sealed session
    std::istringstream file(TicoBridge::SettingsText("general"));
    const nlohmann::json j =
        file.good() ? nlohmann::json::parse(file, nullptr, false, true) : nlohmann::json();
    const std::string roms = j.is_object() ? j.value("roms_path", std::string()) : std::string();
    bases.push_back(WithSlash(roms.empty() ? "sdmc:/tico/roms/" : roms));
    if (j.is_object() && j.contains("rom_base_paths") && j["rom_base_paths"].is_array()) {
        for (const auto& base : j["rom_base_paths"]) {
            if (base.is_string() && !base.get<std::string>().empty()) {
                bases.push_back(WithSlash(base.get<std::string>()));
            }
        }
    }
    return bases;
}

nlohmann::json ModuleRomFolders() {
    const std::string text = TicoConfig::GetConfigJson("tico_rom_folders");
    nlohmann::json j =
        text.empty() ? nlohmann::json::object() : nlohmann::json::parse(text, nullptr, false);
    return j.is_object() ? j : nlohmann::json::object();
}

std::vector<std::string> ModuleRomFolderList() {
    std::vector<std::string> folders;
    const nlohmann::json all = ModuleRomFolders();
    const auto it = all.find(kSlug);
    if (it != all.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_string() && !entry.get<std::string>().empty()) {
                folders.push_back(WithSlash(entry.get<std::string>()));
            }
        }
    }
    return folders;
}

void SetModuleRomFolders(const std::vector<std::string>& folders) {
    nlohmann::json all = ModuleRomFolders();
    if (folders.empty()) {
        all.erase(kSlug);
    } else {
        all[kSlug] = folders;
    }
    TicoConfig::SetConfigJson("tico_rom_folders", all.dump());
    TicoConfig::SaveConfig();
}

// Every folder the games are read from; a USB folder only while its drive is
// connected.
std::vector<std::string> RomFolders() {
    std::vector<std::string> folders;
    const auto add = [&folders](const std::string& folder) {
        const std::string mounted = UsbStorage::Resolve(folder);
        if (!mounted.empty() && std::ranges::find(folders, mounted) == folders.end()) {
            folders.push_back(mounted);
        }
    };
    for (const std::string& base : TicoRomBases()) {
        add(base + kSlug + "/");
    }
    for (const std::string& folder : ModuleRomFolderList()) {
        add(folder);
    }
    return folders;
}

void ScanFolder(const std::string& dir, int depth, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) {
        return;
    }
    while (const dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.') {
            continue;
        }
        const std::string path = (dir.back() == '/' ? dir : dir + "/") + name;
        bool is_dir = e->d_type == DT_DIR;
        if (e->d_type == DT_UNKNOWN) {
            struct stat st {};
            is_dir = stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }
        if (is_dir) {
            if (depth > 0) {
                ScanFolder(path, depth - 1, out);
            }
            continue;
        }
        const std::string ext = LowerExtension(name);
        if (std::ranges::find(kExtensions, ext) != kExtensions.end()) {
            out.push_back(path);
        }
    }
    closedir(d);
}

std::vector<OverlayUI::LibraryEntry> List() {
    std::vector<std::string> games;
    for (const std::string& folder : RomFolders()) {
        ScanFolder(folder, 2, games);
    }
    std::ranges::sort(games);
    games.erase(std::unique(games.begin(), games.end()), games.end());

    std::vector<OverlayUI::LibraryEntry> entries;
    for (const std::string& path : games) {
        const std::string filename = path.substr(path.find_last_of('/') + 1);
        std::string title = TicoUtils::GetCleanTitle(filename);
        if (title.empty()) {
            title = filename;
        }
        entries.push_back({title, "3DS", path});
    }
    std::ranges::sort(entries, [](const auto& a, const auto& b) {
        return strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    return entries;
}

} // namespace

void Register(std::function<void(const std::string& path)> launch) {
    s_launch = std::move(launch);

    OverlayUI::LibraryCallbacks library;
    library.list = [] { return List(); };
    library.launch = [](const std::string& path) {
        if (s_launch) {
            s_launch(path);
        }
    };
    OverlayUI::SetLibraryCallbacks(std::move(library));

    OverlayUI::LibraryFolderCallbacks folders;
    folders.groups = [] {
        OverlayUI::LibraryFolderGroup group;
        group.label = kTitle;
        for (const std::string& base : TicoRomBases()) {
            group.bases.push_back(base + kSlug + "/");
        }
        group.folders = ModuleRomFolderList();
        return std::vector<OverlayUI::LibraryFolderGroup>{std::move(group)};
    };
    folders.set = [](int group, const std::vector<std::string>& paths) {
        if (group != 0) {
            return;
        }
        std::vector<std::string> normalized;
        for (const std::string& path : paths) {
            normalized.push_back(WithSlash(path));
        }
        SetModuleRomFolders(normalized);
    };
    OverlayUI::SetLibraryFolderCallbacks(std::move(folders));
}

void Unregister() {
    OverlayUI::SetLibraryCallbacks({});
    OverlayUI::SetLibraryFolderCallbacks({});
    s_launch = nullptr;
}

} // namespace SwitchFrontend::Library
