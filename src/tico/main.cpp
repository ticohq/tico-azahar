// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "tico/switch_libnx.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <iterator>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "audio_core/input_details.h"
#include "audio_core/sink_details.h"
#include "tico/emu_window_switch.h"
#include "tico/overlay/overlay_ui.h"
#include "tico/overlay/tico_config.h"
#include "tico/overlay/vulkan_overlay.h"
#include "tico/switch_keyboard.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/param_package.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/applets/default_applets.h"
#include "core/frontend/image_interface.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/service.h"
#include "core/loader/loader.h"
#include "core/savestate.h"
#include "input_common/main.h"
#include "input_common/switch_hid.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"

extern "C" {
u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 0;
u32 __nx_exception_ignoredebug = 1;
alignas(16) u8 __nx_exception_stack[0x10000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
}

extern "C" {
extern const u8 __tdata_lma[];
extern const u8 __tdata_lma_end[];
extern u8 __tls_start[];
extern u8 __tls_end[];
extern size_t __tls_align;
}

namespace {

constexpr const char* SystemDir = "sdmc:/tico/system/3ds";
constexpr const char* DebugDir = "sdmc:/tico/system/3ds/debug";
constexpr const char* BootMarkerPath = "sdmc:/tico/system/3ds/azahar_boot.txt";
constexpr const char* StartupLogPath = "sdmc:/tico/system/3ds/debug/startup.txt";
constexpr const char* DebugLogPath = "sdmc:/tico/system/3ds/debug/azahar_switch.txt";
constexpr const char* StdoutLogPath = "sdmc:/tico/system/3ds/debug/stdout.txt";
constexpr const char* StderrLogPath = "sdmc:/tico/system/3ds/debug/stderr.txt";
constexpr const char* FallbackRomPath = "sdmc:/tico/system/3ds/game.zcci";
constexpr const char* MemMapLogPath = "sdmc:/tico/system/3ds/debug/memmap.txt";
constexpr const char* TicoLauncherPath = "sdmc:/switch/tico/tico.nro";
constexpr const char* LegacyTicoLauncherPath = "sdmc:/switch/tico.nro";
// Temporary debug file gates. Flip these back on when collecting detailed boot logs.
constexpr bool EnableStartupLogFile = false;
constexpr bool EnableStdStreamLogs = false;
constexpr bool EnableMemMapLogFile = false;
constexpr bool EnableSwitchDebugLogFile = false;
constexpr bool EnableCommonLogFile = false;
constexpr bool EnableBootMarkerFile = false;
FILE* startup_log{};
FILE* debug_log{};
bool raw_marker_enabled = true;
PadState pad{};

void EnsureSystemDirs() {
    mkdir("sdmc:/tico", 0777);
    mkdir("sdmc:/tico/system", 0777);
    mkdir(SystemDir, 0777);
}

void WriteRawBootMarker(const char* message) {
    if (!EnableBootMarkerFile) {
        return;
    }

    EnsureSystemDirs();
    const int fd = open(BootMarkerPath, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (fd < 0) {
        return;
    }

    write(fd, message, std::strlen(message));
    write(fd, "\n", 1);
    close(fd);
}


void LogTlsLayoutEarly(const char* tag) {
    const auto tls_size = static_cast<unsigned long long>(__tls_end - __tls_start);
    const auto tdata_size = static_cast<unsigned long long>(__tdata_lma_end - __tdata_lma);
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "%s: tls_start=%p tls_end=%p tls_size=0x%llx tdata_size=0x%llx align=%zu",
                  tag, __tls_start, __tls_end, tls_size, tdata_size, __tls_align);
    WriteRawBootMarker(buffer);
}

void WriteRawBootMarkerV(const char* prefix, const char* fmt, std::va_list args) {
    char buffer[1024];
    const int prefix_len = std::snprintf(buffer, sizeof(buffer), "%s", prefix);
    if (prefix_len < 0 || static_cast<std::size_t>(prefix_len) >= sizeof(buffer)) {
        return;
    }

    const int body_len = std::vsnprintf(buffer + prefix_len, sizeof(buffer) - prefix_len, fmt, args);
    if (body_len < 0) {
        return;
    }

    const std::size_t used = std::min(sizeof(buffer) - 1,
                                      static_cast<std::size_t>(prefix_len) +
                                          static_cast<std::size_t>(body_len));
    buffer[used] = 0;
    WriteRawBootMarker(buffer);
}

void EnsureDebugDirs() {
    EnsureSystemDirs();
    mkdir(DebugDir, 0777);
}

void WriteLogLine(FILE* file, const char* prefix, const char* fmt, std::va_list args) {
    if (!file) {
        return;
    }

    std::fprintf(file, "%s", prefix);
    std::vfprintf(file, fmt, args);
    std::fprintf(file, "\n");
    std::fflush(file);
}

void OpenStartupLogIfNeeded(const char* mode = "a") {
    if (!EnableStartupLogFile) {
        return;
    }
    if (startup_log) {
        return;
    }

    WriteRawBootMarker("OpenStartupLogIfNeeded: entry");
    EnsureDebugDirs();
    startup_log = std::fopen(StartupLogPath, mode);
    if (startup_log) {
        std::setvbuf(startup_log, nullptr, _IONBF, 0);
        std::fprintf(startup_log, "=== Azahar Switch startup log open (%s) ===\n", mode);
        std::fflush(startup_log);
        WriteRawBootMarker("OpenStartupLogIfNeeded: startup.txt opened");
    } else {
        WriteRawBootMarker("OpenStartupLogIfNeeded: startup.txt fopen failed");
    }
}

void StartupLog(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    if (startup_log) {
        std::va_list startup_args;
        va_copy(startup_args, args);
        WriteLogLine(startup_log, "[startup] ", fmt, startup_args);
        va_end(startup_args);
    }
    std::va_list raw_args;
    va_copy(raw_args, args);
    WriteRawBootMarkerV("[startup] ", fmt, raw_args);
    va_end(raw_args);
    va_end(args);
}

void CloseStartupLog() {
    if (startup_log) {
        std::fprintf(startup_log, "=== Azahar Switch startup log close ===\n");
        std::fflush(startup_log);
        std::fclose(startup_log);
        startup_log = nullptr;
    }
}

void DebugOpen() {
    OpenStartupLogIfNeeded("a");
    if (!EnableSwitchDebugLogFile) {
        StartupLog("DebugOpen: %s disabled", DebugLogPath);
        return;
    }

    StartupLog("DebugOpen: opening %s", DebugLogPath);
    EnsureDebugDirs();
    debug_log = std::fopen(DebugLogPath, "w");
    if (debug_log) {
        std::setvbuf(debug_log, nullptr, _IONBF, 0);
        std::fprintf(debug_log, "=== Azahar Switch standalone start ===\n");
    } else {
        StartupLog("DebugOpen: failed to open %s", DebugLogPath);
    }
}

void DebugClose() {
    StartupLog("DebugClose");
    if (debug_log) {
        std::fprintf(debug_log, "=== Azahar Switch standalone end ===\n");
        std::fclose(debug_log);
        debug_log = nullptr;
    }
}

void DebugLog(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    if (debug_log) {
        std::va_list debug_args;
        va_copy(debug_args, args);
        WriteLogLine(debug_log, "[azahar-switch] ", fmt, debug_args);
        va_end(debug_args);
    }
    if (startup_log) {
        std::va_list startup_args;
        va_copy(startup_args, args);
        WriteLogLine(startup_log, "[azahar-switch] ", fmt, startup_args);
        va_end(startup_args);
    }
    if (raw_marker_enabled) {
        std::va_list raw_args;
        va_copy(raw_args, args);
        WriteRawBootMarkerV("[azahar-switch] ", fmt, raw_args);
        va_end(raw_args);
    }
    va_end(args);
}

bool QueueTicoReturn() {
    const char* target = nullptr;
    struct stat st {};
    if (stat(TicoLauncherPath, &st) == 0) {
        target = TicoLauncherPath;
    } else if (stat(LegacyTicoLauncherPath, &st) == 0) {
        target = LegacyTicoLauncherPath;
    }

    if (!target) {
        StartupLog("QueueTicoReturn: no launcher found at %s or %s", TicoLauncherPath,
                   LegacyTicoLauncherPath);
        return false;
    }

    char args[512];
    std::snprintf(args, sizeof(args), "%s --resume", target);
    const LibnxResult rc = envSetNextLoad(target, args);
    StartupLog("QueueTicoReturn: envSetNextLoad target=%s args=%s rc=0x%x", target, args, rc);
    return rc == 0;
}

void DebugLogSwitchMemory(const char* tag) {
    u64 total = 0;
    u64 used = 0;
    const LibnxResult total_rc = svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    const LibnxResult used_rc = svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (total_rc == 0 && used_rc == 0) {
        const u64 free = total > used ? total - used : 0;
        DebugLog("memory %s: total=%llu used=%llu free=%llu", tag,
                 static_cast<unsigned long long>(total), static_cast<unsigned long long>(used),
                 static_cast<unsigned long long>(free));
    } else {
        DebugLog("memory %s: svcGetInfo failed total_rc=0x%x used_rc=0x%x", tag, total_rc,
                 used_rc);
    }
}

const char* AppletMessageName(u32 message) {
    switch (message) {
    case AppletMessage_ExitRequest:
        return "ExitRequest";
    case AppletMessage_FocusStateChanged:
        return "FocusStateChanged";
    case AppletMessage_Resume:
        return "Resume";
    case AppletMessage_OperationModeChanged:
        return "OperationModeChanged";
    case AppletMessage_PerformanceModeChanged:
        return "PerformanceModeChanged";
    case AppletMessage_RequestToDisplay:
        return "RequestToDisplay";
    case AppletMessage_CaptureButtonShortPressed:
        return "CaptureButtonShortPressed";
    case AppletMessage_AlbumScreenShotTaken:
        return "AlbumScreenShotTaken";
    case AppletMessage_AlbumRecordingSaved:
        return "AlbumRecordingSaved";
    default:
        return "Unknown";
    }
}

bool PumpAppletMessages() {
    u32 message = 0;
    const LibnxResult rc = appletGetMessage(&message);
    if (rc != 0) {
        return true;
    }

    DebugLog("applet message: %u (%s)", message, AppletMessageName(message));
    const bool keep_running = appletProcessMessage(message);
    if (!keep_running) {
        DebugLog("applet message requested exit: %u (%s)", message, AppletMessageName(message));
    }
    return keep_running;
}

const char* MemTypeName(u32 type) {
    switch (type & 0xFF) {
    case MemType_Unmapped:            return "Unmapped";
    case MemType_Io:                  return "Io";
    case MemType_Normal:              return "Normal";
    case MemType_CodeStatic:          return "CodeStatic";
    case MemType_CodeMutable:         return "CodeMutable";
    case MemType_Heap:                return "Heap";
    case MemType_SharedMem:           return "SharedMem";
    case MemType_WeirdMappedMem:      return "WeirdMapped";
    case MemType_ModuleCodeStatic:    return "ModCodeStatic";
    case MemType_ModuleCodeMutable:   return "ModCodeMutable";
    case MemType_IpcBuffer0:          return "IpcBuffer0";
    case MemType_MappedMemory:        return "MappedMemory";
    case MemType_ThreadLocal:         return "ThreadLocal";
    case MemType_TransferMemIsolated: return "TransferMemIso";
    case MemType_TransferMem:         return "TransferMem";
    case MemType_ProcessMem:          return "ProcessMem";
    case MemType_Reserved:            return "Reserved";
    case MemType_IpcBuffer1:          return "IpcBuffer1";
    case MemType_IpcBuffer3:          return "IpcBuffer3";
    case MemType_KernelStack:         return "KernelStack";
    case MemType_CodeReadOnly:        return "CodeReadOnly";
    case MemType_CodeWritable:        return "CodeWritable";
    default:                          return "Unknown";
    }
}

// After the NRO returns, hbl unmaps our segments and reloads hbmenu into the same
// heap. Any page still Borrowed / IPC-mapped / device-mapped / transfer-mem /
// svcMapMemory'd (leaked thread stack) at that point makes hbl's next kernel memory
// operation fail with 0xD401 (InvalidCurrentMemory) and abort. Dump the address
// space so the subsystem that leaked the mapping can be identified from SD logs.
void DumpMemoryMap(const char* phase) {
    if (!EnableMemMapLogFile) {
        return;
    }

    static bool first_dump = true;
    FILE* map_file = std::fopen(MemMapLogPath, first_dump ? "w" : "a");
    first_dump = false;

    u64 region_count = 0;
    u64 suspect_count = 0;
    u64 suspect_bytes = 0;
    MemoryInfo info{};
    u32 page_info = 0;
    u64 addr = 0;
    for (;;) {
        if (R_FAILED(svcQueryMemory(&info, &page_info, addr))) {
            break;
        }
        const u32 mem_type = info.type & 0xFF;
        if (mem_type != MemType_Unmapped && mem_type != MemType_Reserved) {
            region_count++;
            const bool suspect =
                info.attr != 0 || info.ipc_refcount != 0 || info.device_refcount != 0 ||
                mem_type == MemType_MappedMemory || mem_type == MemType_WeirdMappedMem ||
                mem_type == MemType_TransferMem || mem_type == MemType_TransferMemIsolated ||
                mem_type == MemType_IpcBuffer0 || mem_type == MemType_IpcBuffer1 ||
                mem_type == MemType_IpcBuffer3 || mem_type == MemType_CodeReadOnly ||
                mem_type == MemType_CodeWritable;
            if (suspect) {
                suspect_count++;
                suspect_bytes += info.size;
            }
            if (map_file) {
                std::fprintf(map_file,
                             "[%s] 0x%010llx-0x%010llx %-14s perm=%c%c%c attr=0x%x ipc=%u dev=%u%s\n",
                             phase, static_cast<unsigned long long>(info.addr),
                             static_cast<unsigned long long>(info.addr + info.size),
                             MemTypeName(mem_type), (info.perm & Perm_R) ? 'r' : '-',
                             (info.perm & Perm_W) ? 'w' : '-', (info.perm & Perm_X) ? 'x' : '-',
                             static_cast<unsigned>(info.attr), static_cast<unsigned>(info.ipc_refcount),
                             static_cast<unsigned>(info.device_refcount),
                             suspect ? "  <-- SUSPECT" : "");
            }
        }
        const u64 next = info.addr + info.size;
        if (next <= addr) { // wrapped past the end of the address space
            break;
        }
        addr = next;
    }
    if (map_file) {
        std::fflush(map_file);
        std::fclose(map_file);
    }

    char summary[192];
    std::snprintf(summary, sizeof(summary),
                  "memmap[%s]: regions=%llu suspects=%llu suspect_bytes=0x%llx", phase,
                  static_cast<unsigned long long>(region_count),
                  static_cast<unsigned long long>(suspect_count),
                  static_cast<unsigned long long>(suspect_bytes));
    WriteRawBootMarker(summary);
}

void PinCurrentThreadToCore(s32 core, const char* tag) {
    if (core < 0) {
        core = 0;
    }
    if (core > 2) {
        core = 2;
    }

    const u32 mask = 1u << static_cast<u32>(core);
    const LibnxResult set_rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, mask);
    s32 preferred_core = -1;
    u64 affinity_mask = 0;
    const LibnxResult get_rc = svcGetThreadCoreMask(&preferred_core, &affinity_mask,
                                                    CUR_THREAD_HANDLE);
    DebugLog("thread affinity %s: set core=%d mask=0x%x rc=0x%x get_rc=0x%x preferred=%d affinity=0x%llx",
             tag, core, mask, set_rc, get_rc, preferred_core,
             static_cast<unsigned long long>(affinity_mask));
}

void InstallFatalHandlers() {
    StartupLog("InstallFatalHandlers");
    std::set_terminate([] {
        DebugLog("std::terminate called");
        StartupLog("std::terminate called");
        std::abort();
    });

    auto signal_handler = [](int sig) {
        DebugLog("fatal signal %d", sig);
        StartupLog("fatal signal %d", sig);
        _Exit(128 + sig);
    };
    std::signal(SIGABRT, signal_handler);
    std::signal(SIGBUS, signal_handler);
    std::signal(SIGFPE, signal_handler);
    std::signal(SIGILL, signal_handler);
    std::signal(SIGSEGV, signal_handler);
}

const char* ResultStatusName(Core::System::ResultStatus status) {
    switch (status) {
    case Core::System::ResultStatus::Success:
        return "Success";
    case Core::System::ResultStatus::ErrorNotInitialized:
        return "ErrorNotInitialized";
    case Core::System::ResultStatus::ErrorGetLoader:
        return "ErrorGetLoader";
    case Core::System::ResultStatus::ErrorSystemMode:
        return "ErrorSystemMode";
    case Core::System::ResultStatus::ErrorLoader:
        return "ErrorLoader";
    case Core::System::ResultStatus::ErrorLoader_ErrorEncrypted:
        return "ErrorLoader_ErrorEncrypted";
    case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
        return "ErrorLoader_ErrorInvalidFormat";
    case Core::System::ResultStatus::ErrorLoader_ErrorGbaTitle:
        return "ErrorLoader_ErrorGbaTitle";
    case Core::System::ResultStatus::ErrorSystemFiles:
        return "ErrorSystemFiles";
    case Core::System::ResultStatus::ErrorSavestate:
        return "ErrorSavestate";
    case Core::System::ResultStatus::ErrorArticDisconnected:
        return "ErrorArticDisconnected";
    case Core::System::ResultStatus::ErrorN3DSApplication:
        return "ErrorN3DSApplication";
    case Core::System::ResultStatus::ShutdownRequested:
        return "ShutdownRequested";
    case Core::System::ResultStatus::ErrorUnknown:
        return "ErrorUnknown";
    }
    return "Unknown";
}

std::string GetRomPath(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!argv[i] || std::strcmp(argv[i], "ticoSetup") == 0) {
            continue;
        }
        StartupLog("GetRomPath: using argv[%d]=%s", i, argv[i]);
        return argv[i];
    }

    StartupLog("GetRomPath: no argv ROM, using fallback %s", FallbackRomPath);
    return FallbackRomPath;
}

std::string TrimTitle(std::string title) {
    const std::size_t first = title.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = title.find_last_not_of(" \t\r\n");
    return title.substr(first, last - first + 1);
}

std::string FilenameFromPath(std::string path) {
    const std::size_t slash = path.find_last_of("/\\:");
    if (slash != std::string::npos && slash + 1 < path.size()) {
        path = path.substr(slash + 1);
    }
    return path;
}

std::string CleanTitleFromFilename(const std::string& filename) {
    std::string title = FilenameFromPath(filename);
    const std::size_t dot = title.find_last_of('.');
    if (dot != std::string::npos) {
        title = title.substr(0, dot);
    }

    std::string result;
    result.reserve(title.size());
    int paren_depth = 0;
    int bracket_depth = 0;
    bool last_was_space = false;
    for (char c : title) {
        if (c == '(') {
            paren_depth++;
            continue;
        }
        if (c == ')') {
            if (paren_depth > 0) {
                paren_depth--;
            }
            continue;
        }
        if (c == '[') {
            bracket_depth++;
            continue;
        }
        if (c == ']') {
            if (bracket_depth > 0) {
                bracket_depth--;
            }
            continue;
        }
        if (paren_depth != 0 || bracket_depth != 0) {
            continue;
        }

        if (c == ' ' || c == '_' || c == '-') {
            if (!result.empty() && !last_was_space) {
                result += ' ';
                last_was_space = true;
            }
            continue;
        }

        result += c;
        last_was_space = false;
    }
    return TrimTitle(result);
}

std::string GetDisplayTitle(int argc, char** argv, const std::string& rom_path) {
    if (argc > 2 && argv[2]) {
        std::string title = TrimTitle(argv[2]);
        if (!title.empty()) {
            StartupLog("GetDisplayTitle: using argv[2]=%s", title.c_str());
            return title;
        }
    }

    std::string title = CleanTitleFromFilename(rom_path);
    if (title.empty()) {
        title = "Azahar";
    }
    StartupLog("GetDisplayTitle: using fallback title=%s", title.c_str());
    return title;
}

void ConfigureSettings() {
    Settings::RestoreGlobalState(false);
    Settings::values.use_cpu_jit.SetValue(true);
    Settings::values.cpu_clock_percentage.SetValue(100);
    Settings::values.is_new_3ds.SetValue(true);
    Settings::values.enable_required_online_lle_modules.SetValue(false);
    Settings::values.delay_start_for_lle_modules.SetValue(false);
    Settings::values.graphics_api.SetValue(Settings::GraphicsAPI::Vulkan);
    Settings::values.physical_device.SetValue(0);
    Settings::values.use_gles.SetValue(false);
    Settings::values.renderer_debug.SetValue(false);
    Settings::values.dump_command_buffers.SetValue(false);
    Settings::values.async_shader_compilation.SetValue(true);
    Settings::values.async_presentation.SetValue(true);
    Settings::values.spirv_shader_gen.SetValue(true);
    Settings::values.disable_spirv_optimizer.SetValue(true);
    Settings::values.use_hw_shader.SetValue(true);
    Settings::values.disable_right_eye_render.SetValue(true);
    Settings::values.use_disk_shader_cache.SetValue(true);
    Settings::values.use_shader_jit.SetValue(true);
    Settings::values.resolution_factor.SetValue(1);
    Settings::values.use_vsync.SetValue(true);
    Settings::values.frame_limit.SetValue(0.0); // 0 = unlimited; vsync handles pacing
    Settings::values.layout_option.SetValue(Settings::LayoutOption::Default);
    Settings::values.audio_emulation.SetValue(Settings::AudioEmulation::HLE);
    Settings::values.output_type.SetValue(AudioCore::SinkType::Auto);
    Settings::values.input_type.SetValue(AudioCore::InputType::Null);

    auto& profile = Settings::values.current_input_profile;
    const auto MakeButton = [](u64 mask) {
        Common::ParamPackage pkg;
        pkg.Set("engine", "switch_hid");
        pkg.Set("button", static_cast<int>(mask));
        return pkg.Serialize();
    };
    profile.buttons[Settings::NativeButton::A]      = MakeButton(HidNpadButton_A);
    profile.buttons[Settings::NativeButton::B]      = MakeButton(HidNpadButton_B);
    profile.buttons[Settings::NativeButton::X]      = MakeButton(HidNpadButton_X);
    profile.buttons[Settings::NativeButton::Y]      = MakeButton(HidNpadButton_Y);
    profile.buttons[Settings::NativeButton::Up]     = MakeButton(HidNpadButton_Up);
    profile.buttons[Settings::NativeButton::Down]   = MakeButton(HidNpadButton_Down);
    profile.buttons[Settings::NativeButton::Left]   = MakeButton(HidNpadButton_Left);
    profile.buttons[Settings::NativeButton::Right]  = MakeButton(HidNpadButton_Right);
    profile.buttons[Settings::NativeButton::L]      = MakeButton(HidNpadButton_L);
    profile.buttons[Settings::NativeButton::R]      = MakeButton(HidNpadButton_R);
    profile.buttons[Settings::NativeButton::Start]  = MakeButton(HidNpadButton_Plus);
    profile.buttons[Settings::NativeButton::Select] = MakeButton(HidNpadButton_Minus);
    profile.buttons[Settings::NativeButton::ZL]     = MakeButton(HidNpadButton_ZL);
    profile.buttons[Settings::NativeButton::ZR]     = MakeButton(HidNpadButton_ZR);

    const auto MakeAnalog = [](int axis) {
        Common::ParamPackage pkg;
        pkg.Set("engine", "switch_hid_analog");
        pkg.Set("axis", axis);
        return pkg.Serialize();
    };
    profile.analogs[Settings::NativeAnalog::CirclePad] = MakeAnalog(0);
    profile.analogs[Settings::NativeAnalog::CStick]    = MakeAnalog(1);
    profile.touch_device = "engine:emu_window";
    profile.controller_touch_device.clear();
    profile.use_touchpad = false;
    profile.use_touch_from_button = false;
    profile.touch_from_button_map_index = 0;

    DebugLog("switch settings: cpu_jit=%d cpu_clock=%d new3ds=%d vulkan=%d hw_shader=%d shader_jit=%d async_shader=%d async_present=%d disk_cache=%d audio_hle=%d res=%u",
             Settings::values.use_cpu_jit.GetValue() ? 1 : 0,
             Settings::values.cpu_clock_percentage.GetValue(),
             Settings::values.is_new_3ds.GetValue() ? 1 : 0,
             Settings::values.graphics_api.GetValue() == Settings::GraphicsAPI::Vulkan ? 1 : 0,
             Settings::values.use_hw_shader.GetValue() ? 1 : 0,
             Settings::values.use_shader_jit.GetValue() ? 1 : 0,
             Settings::values.async_shader_compilation.GetValue() ? 1 : 0,
             Settings::values.async_presentation.GetValue() ? 1 : 0,
             Settings::values.use_disk_shader_cache.GetValue() ? 1 : 0,
             Settings::values.audio_emulation.GetValue() == Settings::AudioEmulation::HLE ? 1 : 0,
             Settings::values.resolution_factor.GetValue());

    for (const auto& service_module : Service::service_module_map) {
        Settings::values.lle_modules.emplace(service_module.name, false);
    }
}

// Screen layouts cycled by the ZL+Minus hotkey. SeparateWindows and CustomLayout
// are intentionally excluded: the Switch frontend is single-window and has no
// custom-layout configuration UI.
constexpr Settings::LayoutOption kCycleLayouts[] = {
    Settings::LayoutOption::Default,
    Settings::LayoutOption::SingleScreen,
    Settings::LayoutOption::LargeScreen,
    Settings::LayoutOption::SideScreen,
    Settings::LayoutOption::HybridScreen,
};

const char* LayoutOptionName(Settings::LayoutOption option) {
    switch (option) {
    case Settings::LayoutOption::Default:
        return "Default";
    case Settings::LayoutOption::SingleScreen:
        return "SingleScreen";
    case Settings::LayoutOption::LargeScreen:
        return "LargeScreen";
    case Settings::LayoutOption::SideScreen:
        return "SideScreen";
    case Settings::LayoutOption::HybridScreen:
        return "HybridScreen";
    default:
        return "Other";
    }
}

void CycleScreenLayout() {
    const Settings::LayoutOption current = Settings::values.layout_option.GetValue();
    const auto* found = std::find(std::begin(kCycleLayouts), std::end(kCycleLayouts), current);
    // If the current layout is not in the cycle list, start from the first entry;
    // otherwise advance to the next one, wrapping around.
    std::size_t next_index = 0;
    if (found != std::end(kCycleLayouts)) {
        next_index = (static_cast<std::size_t>(found - std::begin(kCycleLayouts)) + 1) %
                     std::size(kCycleLayouts);
    }
    const Settings::LayoutOption next = kCycleLayouts[next_index];
    // The Switch emu window re-reads layout_option every frame in RefreshDimensions(),
    // so simply updating the value applies the new layout on the next present.
    Settings::values.layout_option.SetValue(next);
    DebugLog("screen layout cycled: %s -> %s", LayoutOptionName(current), LayoutOptionName(next));
}

void ToggleUprightScreen() {
    const bool current = Settings::values.upright_screen.GetValue();
    const bool next = !current;
    Settings::values.upright_screen.SetValue(next);
    DebugLog("upright screen toggled: %d -> %d", current ? 1 : 0, next ? 1 : 0);
    SwitchFrontend::OverlayUI::ShowToast(next ? "Upright screens on" : "Upright screens off",
                                         SwitchFrontend::OverlayUI::ToastCorner::TopRight);
}

// Rising-edge detection so each press advances a single layout instead of cycling
// every frame the combo is held. Shares the frontend pad state updated in Run().
bool LayoutComboPressed() {
    static bool was_down = false;
    const u64 buttons = padGetButtons(&pad);
    const bool down = (buttons & HidNpadButton_ZL) && (buttons & HidNpadButton_Minus);
    const bool triggered = down && !was_down;
    was_down = down;
    return triggered;
}

bool UprightComboPressed() {
    static bool was_down = false;
    const u64 buttons = padGetButtons(&pad);
    const bool down = (buttons & HidNpadButton_L) && (buttons & HidNpadButton_Minus);
    const bool triggered = down && !was_down;
    was_down = down;
    return triggered;
}

bool DrainAsyncOperationsForSavestate(Core::System& system) {
    if (!system.KernelRunning() || !system.Kernel().AreAsyncOperationsPending()) {
        return true;
    }

    DebugLog("savestate waiting for pending async operations");
    const auto start = std::chrono::steady_clock::now();
    while (system.Kernel().AreAsyncOperationsPending()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
            DebugLog("savestate async drain timed out");
            SwitchFrontend::OverlayUI::ShowToast("State failed: busy",
                                                 SwitchFrontend::OverlayUI::ToastCorner::TopRight);
            return false;
        }

        const auto result = system.RunLoop();
        if (result != Core::System::ResultStatus::Success) {
            DebugLog("savestate async drain failed: %s (%u)", ResultStatusName(result),
                     static_cast<unsigned>(result));
            SwitchFrontend::OverlayUI::ShowToast("State failed",
                                                 SwitchFrontend::OverlayUI::ToastCorner::TopRight);
            return false;
        }
    }

    DebugLog("savestate async drain completed");
    return true;
}

bool SaveStateFromOverlay(Core::System& system, u32 slot) {
    if (!DrainAsyncOperationsForSavestate(system)) {
        return false;
    }

    DebugLog("overlay begin direct save state slot %u", slot);
    DebugLogSwitchMemory("before-save");
    try {
        system.SaveState(slot);
        DebugLogSwitchMemory("after-save");
        DebugLog("overlay direct save state completed");
        SwitchFrontend::OverlayUI::ShowToast("State saved",
                                             SwitchFrontend::OverlayUI::ToastCorner::TopRight);
        system.frame_limiter.AdvanceFrame();
        return true;
    } catch (const std::exception& e) {
        DebugLogSwitchMemory("save-failed");
        DebugLog("overlay direct save state failed: %s", e.what());
        SwitchFrontend::OverlayUI::ShowToast(std::string{"Save failed: "} + e.what(),
                                             SwitchFrontend::OverlayUI::ToastCorner::TopRight);
        return false;
    }
}

bool LoadStateFromOverlay(Core::System& system, u32 slot) {
    if (!DrainAsyncOperationsForSavestate(system)) {
        return false;
    }

    DebugLog("overlay begin direct load state slot %u", slot);
    DebugLogSwitchMemory("before-load");
    try {
        system.LoadState(slot);
        DebugLogSwitchMemory("after-load");
        DebugLog("overlay direct load state completed");
        SwitchFrontend::OverlayUI::ShowToast("State loaded",
                                             SwitchFrontend::OverlayUI::ToastCorner::TopRight);
        system.frame_limiter.AdvanceFrame();
        return true;
    } catch (const std::exception& e) {
        DebugLogSwitchMemory("load-failed");
        DebugLog("overlay direct load state failed: %s", e.what());
        SwitchFrontend::OverlayUI::ShowToast(std::string{"Load failed: "} + e.what(),
                                             SwitchFrontend::OverlayUI::ToastCorner::TopRight);
        return false;
    }
}

void ConfigureOverlay(Core::System& system, const std::string& display_title) {
    SwitchFrontend::OverlayUI::SetGameTitle(display_title.empty() ? std::string{"Azahar"}
                                                                  : display_title);

    u64 title_id = 0;
    if (system.GetAppLoader().ReadProgramId(title_id) != Loader::ResultStatus::Success) {
        title_id = 0;
    }

    SwitchFrontend::OverlayUI::SetSlotOccupiedCallback([&system, title_id](int slot) {
        if (title_id == 0 || slot < 0) {
            return false;
        }
        const auto savestates = Core::ListSaveStates(title_id, system.Movie().GetCurrentMovieID());
        return std::any_of(savestates.begin(), savestates.end(), [slot](const auto& info) {
            return info.slot == static_cast<u32>(slot) &&
                   info.status == Core::SaveStateInfo::ValidationStatus::OK;
        });
    });
}

bool HandleOverlayAction(Core::System& system, SwitchFrontend::OverlayUI::Action action) {
    using SwitchFrontend::OverlayUI::Action;

    if (action == Action::None) {
        return false;
    }
    if (action == Action::Exit || SwitchFrontend::VulkanOverlay::ShouldExit()) {
        DebugLog("overlay requested exit");
        system.RequestShutdown();
        return false;
    }

    const int slot = SwitchFrontend::OverlayUI::GetStateSlotForAction(action);
    if (slot <= 0) {
        return false;
    }

    if (SwitchFrontend::OverlayUI::IsSaveStateAction(action)) {
        DebugLog("overlay requested save state slot %d", slot);
        SaveStateFromOverlay(system, static_cast<u32>(slot));
    } else if (SwitchFrontend::OverlayUI::IsLoadStateAction(action)) {
        DebugLog("overlay requested load state slot %d", slot);
        SwitchFrontend::VulkanOverlay::Shutdown();
        LoadStateFromOverlay(system, static_cast<u32>(slot));
        return true;
    }
    return false;
}

int Run(int argc, char** argv) {
    OpenStartupLogIfNeeded("a");
    StartupLog("Run: entry argc=%d", argc);
    StartupLog("Run: appletLockExit");
    const LibnxResult lock_exit_rc = appletLockExit();
    StartupLog("Run: appletLockExit rc=0x%x", lock_exit_rc);
    DebugOpen();
    InstallFatalHandlers();

    DebugLog("argc=%d", argc);
    for (int i = 0; i < argc; i++) {
        DebugLog("argv[%d]=%s", i, argv[i] ? argv[i] : "(null)");
    }

    StartupLog("Run: parsing ROM path");
    const std::string rom_path = GetRomPath(argc, argv);
    const std::string display_title = GetDisplayTitle(argc, argv, rom_path);
    if (rom_path.empty()) {
        DebugLog("no ROM path supplied");
        DebugClose();
        QueueTicoReturn();
        appletUnlockExit();
        return EXIT_FAILURE;
    }

    StartupLog("Run: romfsInit");
    const LibnxResult romfs_result = romfsInit();
    const bool romfs_initialized = romfs_result == 0;
    DebugLog("romfsInit=0x%x", romfs_result);

    StartupLog("Run: apm performance config");
    apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, 0x92220007);
    apmSetPerformanceConfiguration(ApmPerformanceMode_Boost, 0x92220008);

    StartupLog("Run: pin main thread affinity");
    PinCurrentThreadToCore(2, "main");

    StartupLog("Run: pad init");
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    StartupLog("Run: InputCommon::Init");
    InputCommon::Init();

    StartupLog("Run: ConfigureSettings");
    ConfigureSettings();
    SwitchFrontend::TicoConfig::ReloadConfig();
    SwitchFrontend::TicoConfig::ApplyConfig();
    DebugLog("tico config applied: path=%s options=%zu upscale=%s effective_res=%u",
             SwitchFrontend::TicoConfig::GetLoadedConfigPath().c_str(),
             SwitchFrontend::TicoConfig::GetLoadedOptionCount(),
             SwitchFrontend::TicoConfig::GetConfigValue("upscale", "default").c_str(),
             Settings::values.resolution_factor.GetValue());
    StartupLog("Run: FileUtil::SetUserPath %s/", SystemDir);
    FileUtil::SetUserPath(std::string{SystemDir} + "/");
    StartupLog("Run: Common::Log init");
    bool common_log_started = false;
    if (EnableCommonLogFile) {
        Common::Log::Initialize("azahar_common.txt");
        Common::Log::Start();
        common_log_started = true;
    } else {
        StartupLog("Run: Common::Log disabled");
    }

    StartupLog("Run: Core::System::GetInstance");
    auto& system = Core::System::GetInstance();
    StartupLog("Run: frontend applets/image interface");
    system.RegisterImageInterface(std::make_shared<Frontend::ImageInterface>());
    Frontend::RegisterDefaultApplets(system);
    system.RegisterSoftwareKeyboard(std::make_shared<SwitchFrontend::SwitchKeyboard>());

    StartupLog("Run: creating NWindow frontend");
    SwitchFrontend::EmuWindowSwitch window{nwindowGetDefault()};
    DebugLog("loading ROM: %s", rom_path.c_str());
    const Core::System::ResultStatus load_result = system.Load(window, rom_path);
    if (load_result != Core::System::ResultStatus::Success) {
        DebugLog("load failed: %s (%u)", ResultStatusName(load_result),
                 static_cast<unsigned>(load_result));
        if (common_log_started) {
            Common::Log::Stop();
            common_log_started = false;
        }
        if (romfs_initialized) {
            romfsExit();
        }
        DebugClose();
        QueueTicoReturn();
        appletUnlockExit();
        return EXIT_FAILURE;
    }
    DebugLog("renderer resolution scale factor=%u",
             system.GPU().Renderer().GetResolutionScaleFactor());

    StartupLog("Run: ApplySettings");
    system.ApplySettings();

    ConfigureOverlay(system, display_title);
    DebugLog("startup keepalive armed");

    DebugLog("entering main loop");
    raw_marker_enabled = false;

    using Clock = std::chrono::steady_clock;
    auto last_keepalive = Clock::now();
    u64 loop_count = 0;
    u64 keepalive_count = 0;
    bool applet_loop_active = true;
    const s32 initial_renderer_frame = system.GPU().Renderer().GetCurrentFrame();
    bool saw_guest_frame = initial_renderer_frame > 0;
    bool overlay_init_attempted = false;
    bool overlay_initialized = false;
    s32 last_logged_frame = initial_renderer_frame;
    auto last_heartbeat = Clock::now();
    u64 last_heartbeat_loop_count = loop_count;
    s32 last_heartbeat_frame = last_logged_frame;
    const char* exit_reason = "loop condition ended";
    while (true) {
        const auto now = Clock::now();
        applet_loop_active = PumpAppletMessages();
        if (!applet_loop_active) {
            exit_reason = "applet message requested exit";
            DebugLog("main loop exit: applet message requested exit after %llu iterations",
                     static_cast<unsigned long long>(loop_count));
            break;
        }
        if (!system.IsPoweredOn()) {
            exit_reason = "system powered off before RunLoop";
            DebugLog("main loop exit: system powered off before RunLoop after %llu iterations",
                     static_cast<unsigned long long>(loop_count));
            break;
        }

        auto& renderer = system.GPU().Renderer();
        auto* vulkan_renderer =
            Settings::values.graphics_api.GetValue() == Settings::GraphicsAPI::Vulkan
                ? static_cast<Vulkan::RendererVulkan*>(&renderer)
                : nullptr;

        if (!saw_guest_frame && now - last_keepalive >= std::chrono::seconds(2)) {
            keepalive_count++;
            DebugLog("startup keepalive present #%llu: renderer_frame=%d",
                     static_cast<unsigned long long>(keepalive_count), renderer.GetCurrentFrame());
            renderer.TryPresent(0);
            DebugLog("startup keepalive present #%llu complete",
                     static_cast<unsigned long long>(keepalive_count));
            last_keepalive = now;
        }

        window.PollEvents();
        InputCommon::SwitchHID::Update();
        padUpdate(&pad);

        if (vulkan_renderer && !overlay_init_attempted && renderer.GetCurrentFrame() > 0) {
            overlay_init_attempted = true;
            overlay_initialized = SwitchFrontend::VulkanOverlay::Init(*vulkan_renderer);
            DebugLog("tico overlay init %s", overlay_initialized ? "succeeded" : "failed");
        }

        if (overlay_initialized) {
            SwitchFrontend::VulkanOverlay::Update(&pad);
            const bool renderer_reset_requested = HandleOverlayAction(
                system, static_cast<SwitchFrontend::OverlayUI::Action>(
                            SwitchFrontend::VulkanOverlay::ConsumeAction()));
            if (renderer_reset_requested) {
                overlay_initialized = false;
                overlay_init_attempted = false;
                last_keepalive = Clock::now();
                DebugLog("tico overlay shut down for renderer reset");
            }
        }
        if ((!overlay_initialized || !SwitchFrontend::VulkanOverlay::IsVisible()) &&
            LayoutComboPressed()) {
            CycleScreenLayout();
        }
        if ((!overlay_initialized || !SwitchFrontend::VulkanOverlay::IsVisible()) &&
            UprightComboPressed()) {
            ToggleUprightScreen();
        }

        const bool overlay_visible =
            overlay_initialized && SwitchFrontend::VulkanOverlay::IsVisible();
        Core::System::ResultStatus run_result = Core::System::ResultStatus::Success;
        if (overlay_visible && vulkan_renderer) {
            vulkan_renderer->RedrawCurrentFrame();
        } else {
            run_result = system.RunLoop();
        }
        loop_count++;
        const s32 renderer_frame =
            system.IsPoweredOn() ? system.GPU().Renderer().GetCurrentFrame() : last_logged_frame;
        if (!saw_guest_frame && renderer_frame > 0) {
            saw_guest_frame = true;
            DebugLog("first guest frame reached: renderer_frame=%d keepalives=%llu", renderer_frame,
                     static_cast<unsigned long long>(keepalive_count));
        }
        if (renderer_frame != last_logged_frame) {
            last_logged_frame = renderer_frame;
        }
        if (now - last_heartbeat >= std::chrono::seconds(1)) {
            const auto heartbeat_elapsed = std::chrono::duration<double>(now - last_heartbeat).count();
            const u64 loop_delta = loop_count - last_heartbeat_loop_count;
            const s32 frame_delta = renderer_frame - last_heartbeat_frame;
            const auto stats = system.GetAndResetPerfStats();
            DebugLog("main loop heartbeat: iterations=%llu loops_per_sec=%.1f renderer_frame=%d frame_delta=%d frontend_fps=%.1f system_fps=%.1f game_fps=%.1f emu_speed=%.2f powered=%d applet=%d keepalives=%llu",
                     static_cast<unsigned long long>(loop_count),
                     heartbeat_elapsed > 0.0 ? static_cast<double>(loop_delta) / heartbeat_elapsed : 0.0,
                     renderer_frame, frame_delta,
                     heartbeat_elapsed > 0.0 ? static_cast<double>(frame_delta) / heartbeat_elapsed : 0.0,
                     stats.system_fps, stats.game_fps, stats.emulation_speed,
                     system.IsPoweredOn() ? 1 : 0, applet_loop_active ? 1 : 0,
                     static_cast<unsigned long long>(keepalive_count));
            last_heartbeat = now;
            last_heartbeat_loop_count = loop_count;
            last_heartbeat_frame = renderer_frame;
        }
        if (run_result == Core::System::ResultStatus::ShutdownRequested) {
            exit_reason = "RunLoop returned ShutdownRequested";
            DebugLog("core requested shutdown after %llu iterations",
                     static_cast<unsigned long long>(loop_count));
            break;
        }
        if (run_result == Core::System::ResultStatus::ErrorSavestate) {
            const std::string details = system.GetStatusDetails();
            DebugLog("savestate operation failed after %llu iterations: %s",
                     static_cast<unsigned long long>(loop_count),
                     details.empty() ? "unknown error" : details.c_str());
            SwitchFrontend::OverlayUI::ShowToast(
                details.empty() ? std::string{"Save state failed"}
                                : std::string{"Save state failed: "} + details,
                SwitchFrontend::OverlayUI::ToastCorner::TopRight);
            continue;
        }
        if (run_result != Core::System::ResultStatus::Success) {
            exit_reason = "RunLoop returned error";
            DebugLog("run loop failed after %llu iterations: %s (%u)",
                     static_cast<unsigned long long>(loop_count), ResultStatusName(run_result),
                     static_cast<unsigned>(run_result));
            break;
        }
    }

    DebugLog("shutting down: reason=%s powered=%d applet=%d iterations=%llu", exit_reason,
             system.IsPoweredOn() ? 1 : 0, applet_loop_active ? 1 : 0,
             static_cast<unsigned long long>(loop_count));
    if (overlay_initialized) {
        DebugLog("shutdown step: overlay.Shutdown begin");
        SwitchFrontend::VulkanOverlay::Shutdown();
        DebugLog("shutdown step: overlay.Shutdown done");
    }
    if (system.IsPoweredOn()) {
        DebugLog("shutdown step: system.Shutdown begin");
        system.Shutdown();
        DebugLog("shutdown step: system.Shutdown done");
    }
    DebugLog("shutdown step: InputCommon::Shutdown begin");
    InputCommon::Shutdown();
    DebugLog("shutdown step: InputCommon::Shutdown done");
    DebugLog("shutdown step: Common::Log::Stop begin");
    if (common_log_started) {
        Common::Log::Stop();
        common_log_started = false;
    }
    DebugLog("shutdown step: Common::Log::Stop done");
    if (romfs_initialized) {
        DebugLog("shutdown step: romfsExit begin");
        romfsExit();
        DebugLog("shutdown step: romfsExit done");
    }
    DebugLog("shutdown step: DebugClose begin");
    DebugClose();
    DumpMemoryMap("after-shutdown");
    QueueTicoReturn();
    StartupLog("Run: appletUnlockExit begin");
    const LibnxResult unlock_exit_rc = appletUnlockExit();
    StartupLog("Run: appletUnlockExit rc=0x%x", unlock_exit_rc);
    return EXIT_SUCCESS;
}

} // namespace

extern "C" void userAppInit() {
    WriteRawBootMarker("userAppInit: entry");
    LogTlsLayoutEarly("userAppInit TLS");
    OpenStartupLogIfNeeded("w");
    StartupLog("userAppInit: begin");
    if (EnableStdStreamLogs) {
        std::freopen(StdoutLogPath, "w", stdout);
        std::freopen(StderrLogPath, "w", stderr);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
    setenv("HOME", SystemDir, 1);
    StartupLog("userAppInit: stdout/stderr logs %s HOME=%s",
               EnableStdStreamLogs ? "enabled" : "disabled", SystemDir);
}

extern "C" void userAppExit() {
    WriteRawBootMarker("userAppExit: entry");
    // Runs after C++ static destructors — the closest observable state to what hbl
    // inherits. Anything still flagged SUSPECT here is what kills hbl with 0xD401.
    DumpMemoryMap("static-dtors-done");
    StartupLog("userAppExit");
    CloseStartupLog();
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    WriteRawBootMarker("__libnx_exception_handler: entry");
    OpenStartupLogIfNeeded("a");
    if (ctx) {
        StartupLog("libnx exception: desc=0x%08x pc=0x%016llx lr=0x%016llx sp=0x%016llx far=0x%016llx esr=0x%08x",
                   static_cast<unsigned>(ctx->error_desc),
                   static_cast<unsigned long long>(ctx->pc.x),
                   static_cast<unsigned long long>(ctx->lr.x),
                   static_cast<unsigned long long>(ctx->sp.x),
                   static_cast<unsigned long long>(ctx->far.x), static_cast<unsigned>(ctx->esr));
        StartupLog("libnx exception: x0=0x%016llx x1=0x%016llx x2=0x%016llx x3=0x%016llx",
                   static_cast<unsigned long long>(ctx->cpu_gprs[0].x),
                   static_cast<unsigned long long>(ctx->cpu_gprs[1].x),
                   static_cast<unsigned long long>(ctx->cpu_gprs[2].x),
                   static_cast<unsigned long long>(ctx->cpu_gprs[3].x));
    } else {
        StartupLog("libnx exception: null context");
    }
}

int main(int argc, char** argv) {
    WriteRawBootMarker("main: entry");
    OpenStartupLogIfNeeded("a");
    StartupLog("main: entry argc=%d", argc);
    const int result = Run(argc, argv);
    StartupLog("main: exit result=%d", result);
    return result;
}
