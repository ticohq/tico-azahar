// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include "common/polyfill_thread.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#ifdef ENABLE_LSFG
#include "video_core/frame_generation.h"
#include "video_core/renderer_vulkan/vk_lsfg.h"
#endif

VK_DEFINE_HANDLE(VmaAllocation)

namespace Frontend {
class EmuWindow;
}

namespace Vulkan {

#ifdef __SWITCH__
// Hook used by the tico overlay to composite ImGui over the final swapchain image.
// Registered from the Switch frontend; called on the present thread after the
// emulator frame has been blitted into the swapchain image, with the image already
// transitioned to eColorAttachmentOptimal. The callee records its own load-op=LOAD
// render pass into `cmd` and must leave the image in eColorAttachmentOptimal.
using OverlayDrawCallback =
    std::function<void(vk::CommandBuffer cmd, vk::Image image, vk::Extent2D extent,
                       vk::Format format)>;

// Called just before the swapchain's images are destroyed (on recreate/shutdown) so
// the overlay can drop the image views and framebuffers it derived from them.
using OverlayResetCallback = std::function<void()>;

void SetOverlayDrawCallback(OverlayDrawCallback callback);
void SetOverlayResetCallback(OverlayResetCallback callback);
bool HasOverlayDrawCallback();
#endif

class Instance;
class Swapchain;
class Scheduler;
class RenderManager;

struct Frame {
    u32 width;
    u32 height;
    VmaAllocation allocation;
    vk::Framebuffer framebuffer;
    vk::Image image;
    vk::ImageView image_view;
    vk::Semaphore render_ready;
    vk::Fence present_done;
    vk::CommandBuffer cmdbuf;
#ifdef ENABLE_LSFG
    // one per generated frame, presented ahead of this one
    std::array<vk::CommandBuffer, kMaxGeneratedFrames> generated_cmdbufs;
    VideoCore::FrameGenerationDecision frame_gen;
#endif
};

class PresentWindow final {
public:
    explicit PresentWindow(Frontend::EmuWindow& emu_window, const Instance& instance,
                           Scheduler& scheduler, bool low_refresh_rate);
    ~PresentWindow();

    /// Waits for all queued frames to finish presenting.
    void WaitPresent();

    /// Returns the last used render frame.
    Frame* GetRenderFrame();

    /// Recreates the render frame to match provided parameters.
    void RecreateFrame(Frame* frame, u32 width, u32 height);

    /// Queues the provided frame for presentation.
    void Present(Frame* frame);

    /// This is called to notify the rendering backend of a surface change
    void NotifySurfaceChanged();

    [[nodiscard]] vk::RenderPass Renderpass() const noexcept {
        return present_renderpass;
    }

    u32 ImageCount() const noexcept {
        return swapchain.GetImageCount();
    }

    vk::Format GetSurfaceFormat() const noexcept {
        return swapchain.GetSurfaceFormat().format;
    }

private:
    void PresentThread(std::stop_token token);

    void RecreateSwapchain(u32 width, u32 height);

    void AcquireSwapchainImage(u32 width, u32 height);

    struct BlitSource {
        vk::Image image;
        u32 width;
        u32 height;
        vk::ImageLayout layout;
        vk::AccessFlags access;
        vk::PipelineStageFlags stage;
    };

    /// Blits `source` into the acquired swapchain image, draws the overlay over it and leaves
    /// it ready to present.
    void RecordBlitToSwapchain(vk::CommandBuffer cmdbuf, const BlitSource& source);

    void SubmitAndPresent(vk::CommandBuffer cmdbuf, vk::Semaphore render_ready, vk::Fence fence);

    void CopyToSwapchain(Frame* frame);

#ifdef ENABLE_LSFG
    void ResetFrameGeneration();

    void UpdateFrameGeneration(Frame* frame);

    /// Presents the generated frames, then the frame itself; false when generation failed.
    bool CopyToSwapchainGenerated(Frame* frame);
#endif

    vk::RenderPass CreateRenderpass();

private:
    Frontend::EmuWindow& emu_window;
    const Instance& instance;
    Scheduler& scheduler;
    bool low_refresh_rate;
    vk::SurfaceKHR surface;
    vk::SurfaceKHR next_surface{};
    Swapchain swapchain;
    vk::CommandPool command_pool;
    vk::Queue graphics_queue;
    vk::RenderPass present_renderpass;
    std::vector<Frame> swap_chain;
    std::queue<Frame*> free_queue;
    std::queue<Frame*> present_queue;
    std::condition_variable free_cv;
    std::condition_variable recreate_surface_cv;
    std::condition_variable_any frame_cv;
    std::mutex swapchain_mutex;
    std::mutex recreate_surface_mutex;
    std::mutex queue_mutex;
    std::mutex free_mutex;
    std::jthread present_thread;
    bool vsync_enabled{};
    bool blit_supported;
    bool async_presentation{true};
    std::atomic<bool> use_present_thread{true};
    void* last_render_surface{};
#ifdef ENABLE_LSFG
    LsfgBridgePtr lsfg_bridge;
    struct LsfgConfig {
        u32 width;
        u32 height;
        u32 multiplier;
        u32 flow_scale;
        bool performance_mode;

        bool operator==(const LsfgConfig&) const = default;
    };
    LsfgConfig lsfg_config{};
    bool lsfg_attempted{};
    std::atomic<bool> lsfg_unavailable{};
#endif
};

} // namespace Vulkan
