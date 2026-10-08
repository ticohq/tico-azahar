// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

// vk_common.h sets up vulkan-hpp (VK_NO_PROTOTYPES, the dynamic dispatcher) and
// comes before the ImGui Vulkan backend so the backend's <vulkan/vulkan.h> sees it.
#include "video_core/renderer_vulkan/vk_common.h"

#include "tico/game_overlay.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "TicoOverlayHost.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "overlay/imgui_overlay.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_present_window.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace SwitchFrontend::GameOverlay {
namespace {

using OverlayUI::Action;

// held directions repeat after this many polls, then every few
constexpr int kNavInitialDelay = 18;
constexpr int kNavRepeat = 5;

enum NavBits : unsigned int {
    NavBit_Up = 1u << 0,
    NavBit_Down = 1u << 1,
    NavBit_Left = 1u << 2,
    NavBit_Right = 1u << 3,
    NavBit_Accept = 1u << 4,
    NavBit_Cancel = 1u << 5,
};

// --- shared between the main loop and the present thread ---
std::atomic_bool s_registered = false;
std::atomic_bool s_visible = false;
std::atomic_bool s_cheat_refresh = false;
std::atomic_bool s_resume_prompt = false;
std::atomic_int s_pending_action = 0;
std::atomic_uint s_pending_nav = 0;
std::atomic_bool s_touch_down = false;
std::atomic<float> s_touch_x = 0.0f;
std::atomic<float> s_touch_y = 0.0f;
std::mutex s_notice_mutex;
std::optional<std::pair<std::string, std::vector<std::string>>> s_pending_notice;

// --- main loop only ---
bool s_was_combo_down = false;
u64 s_nav_held_prev = 0;
int s_nav_repeat = 0;
bool s_accept_prev = false;
bool s_cancel_prev = false;

// --- Vulkan objects, made on Init ---
vk::Instance s_instance;
vk::PhysicalDevice s_physical_device;
vk::Device s_device;
vk::Queue s_queue;
u32 s_queue_family = 0;
u32 s_image_count = 2;
vk::DescriptorPool s_descriptor_pool;
vk::CommandPool s_command_pool;
// Azahar's lock on the graphics queue: Vulkan wants every submit and wait on it serialized, and
// the scheduler's worker submits from another thread (the GPU thread's frames among them).
std::mutex* s_queue_mutex = nullptr;

// --- present thread only ---
bool s_ready = false;
bool s_failed = false;
bool s_shown = false;
bool s_backend_ready = false;
std::chrono::steady_clock::time_point s_last_frame;
vk::Format s_color_format = vk::Format::eUndefined;
vk::RenderPass s_render_pass;

struct Texture {
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView view;
    vk::Sampler sampler;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};
// the overlay's pictures (avatar, border, icons, state pictures), by texture id
std::unordered_map<ImTextureID, Texture> s_textures;

struct SwapImage {
    vk::ImageView view;
    vk::Framebuffer framebuffer;
};
std::unordered_map<VkImage, SwapImage> s_swap_images;
vk::Extent2D s_swap_extent{};

PFN_vkVoidFunction LoadFunction(const char* name, void* user_data) {
    if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr) {
        return nullptr;
    }
    return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(static_cast<VkInstance>(user_data),
                                                               name);
}

std::optional<u32> FindMemoryType(u32 type_filter, vk::MemoryPropertyFlags properties) {
    const vk::PhysicalDeviceMemoryProperties memory = s_physical_device.getMemoryProperties();
    for (u32 i = 0; i < memory.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return std::nullopt;
}

void Barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
             vk::AccessFlags src_access, vk::AccessFlags dst_access,
             vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage) {
    const vk::ImageMemoryBarrier barrier{
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmd.pipelineBarrier(src_stage, dst_stage, vk::DependencyFlagBits::eByRegion, {}, {}, barrier);
}

void DestroyTexture(Texture& texture) {
    if (texture.descriptor && s_backend_ready) {
        ImGui_ImplVulkan_RemoveTexture(texture.descriptor);
    }
    if (texture.sampler) {
        s_device.destroySampler(texture.sampler);
    }
    if (texture.view) {
        s_device.destroyImageView(texture.view);
    }
    if (texture.image) {
        s_device.destroyImage(texture.image);
    }
    if (texture.memory) {
        s_device.freeMemory(texture.memory);
    }
    texture = {};
}

// An RGBA8 image as a sampled texture ImGui can draw, uploaded on the spot.
ImTextureID CreateTexture(const unsigned char* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0 || !s_backend_ready) {
        return 0;
    }
    const vk::DeviceSize size = static_cast<vk::DeviceSize>(width) * height * 4;
    Texture texture;
    vk::Buffer staging;
    vk::DeviceMemory staging_memory;
    vk::CommandBuffer cmd;
    const auto fail = [&](const char* why) -> ImTextureID {
        LOG_ERROR(Frontend, "tico overlay: no texture ({})", why);
        if (cmd) {
            s_device.freeCommandBuffers(s_command_pool, cmd);
        }
        if (staging) {
            s_device.destroyBuffer(staging);
        }
        if (staging_memory) {
            s_device.freeMemory(staging_memory);
        }
        DestroyTexture(texture);
        return 0;
    };

    staging = s_device.createBuffer({.size = size,
                                     .usage = vk::BufferUsageFlagBits::eTransferSrc,
                                     .sharingMode = vk::SharingMode::eExclusive});
    const vk::MemoryRequirements staging_req = s_device.getBufferMemoryRequirements(staging);
    const auto staging_type =
        FindMemoryType(staging_req.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible |
                                                       vk::MemoryPropertyFlagBits::eHostCoherent);
    if (!staging_type) {
        return fail("no host-visible memory");
    }
    staging_memory = s_device.allocateMemory(
        {.allocationSize = staging_req.size, .memoryTypeIndex = *staging_type});
    s_device.bindBufferMemory(staging, staging_memory, 0);
    std::memcpy(s_device.mapMemory(staging_memory, 0, size), rgba, static_cast<std::size_t>(size));
    s_device.unmapMemory(staging_memory);

    texture.image = s_device.createImage({
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .extent = vk::Extent3D(static_cast<u32>(width), static_cast<u32>(height), 1),
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    const vk::MemoryRequirements image_req = s_device.getImageMemoryRequirements(texture.image);
    const auto image_type =
        FindMemoryType(image_req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    if (!image_type) {
        return fail("no device-local memory");
    }
    texture.memory =
        s_device.allocateMemory({.allocationSize = image_req.size, .memoryTypeIndex = *image_type});
    s_device.bindImageMemory(texture.image, texture.memory, 0);
    texture.view = s_device.createImageView({
        .image = texture.image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .subresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    });
    texture.sampler = s_device.createSampler({
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
    });

    cmd = s_device
              .allocateCommandBuffers({.commandPool = s_command_pool,
                                       .level = vk::CommandBufferLevel::ePrimary,
                                       .commandBufferCount = 1})
              .front();
    cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    Barrier(cmd, texture.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            {}, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer);
    cmd.copyBufferToImage(staging, texture.image, vk::ImageLayout::eTransferDstOptimal,
                          vk::BufferImageCopy{
                              .imageSubresource{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                              .imageExtent = vk::Extent3D(static_cast<u32>(width),
                                                          static_cast<u32>(height), 1),
                          });
    Barrier(cmd, texture.image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader);
    cmd.end();
    // only this upload is waited for, under Azahar's queue lock for the submit alone
    const vk::Fence uploaded = s_device.createFence({});
    {
        std::scoped_lock lock{*s_queue_mutex};
        s_queue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &cmd}, uploaded);
    }
    static_cast<void>(s_device.waitForFences(uploaded, VK_TRUE, UINT64_MAX));
    s_device.destroyFence(uploaded);
    s_device.freeCommandBuffers(s_command_pool, cmd);
    s_device.destroyBuffer(staging);
    s_device.freeMemory(staging_memory);

    texture.descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(texture.sampler),
                                                     static_cast<VkImageView>(texture.view),
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.descriptor));
    s_textures.emplace(id, texture);
    return id;
}

class Host final : public IOverlayHost {
public:
    std::string GetGamePath() override {
        return {};
    }
    bool IsGameLoaded() override {
        return true;
    }
    bool StateSlotExists(int) override {
        return false;
    }
    void SaveStateSlot(int) override {}
    void LoadStateSlot(int) override {}
    void SwapDisc(const std::string&) override {}

    ImTextureID CreateTextureRGBA(const unsigned char* rgba, int width, int height) override {
        return CreateTexture(rgba, width, height);
    }

    void DestroyTexture(ImTextureID id) override {
        const auto it = s_textures.find(id);
        if (it == s_textures.end()) {
            return;
        }
        GameOverlay::DestroyTexture(it->second);
        s_textures.erase(it);
    }
};
Host s_host;

// LOAD and STORE on the swapchain image, which the present path leaves in
// eColorAttachmentOptimal before calling us and takes back to present after.
vk::RenderPass CreateRenderPass(vk::Format format) {
    const vk::AttachmentDescription attachment{
        .format = format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::AttachmentReference color{
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    const vk::SubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        .dstAccessMask =
            vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead,
    };
    return s_device.createRenderPass({
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    });
}

void DestroySwapImages() {
    for (auto& [image, swap] : s_swap_images) {
        s_device.destroyFramebuffer(swap.framebuffer);
        s_device.destroyImageView(swap.view);
    }
    s_swap_images.clear();
    s_swap_extent = vk::Extent2D{};
}

// A framebuffer for each swapchain image, made the first time it is drawn to.
vk::Framebuffer FramebufferFor(vk::Image image, vk::Extent2D extent) {
    if (extent != s_swap_extent) {
        DestroySwapImages();
        s_swap_extent = extent;
    }
    const auto key = static_cast<VkImage>(image);
    if (const auto it = s_swap_images.find(key); it != s_swap_images.end()) {
        return it->second.framebuffer;
    }
    SwapImage swap;
    swap.view = s_device.createImageView({
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = s_color_format,
        .subresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    });
    swap.framebuffer = s_device.createFramebuffer({
        .renderPass = s_render_pass,
        .attachmentCount = 1,
        .pAttachments = &swap.view,
        .width = extent.width,
        .height = extent.height,
        .layers = 1,
    });
    s_swap_images.emplace(key, swap);
    return swap.framebuffer;
}

void PublishHudStats() {
    auto& system = Core::System::GetInstance();
    OverlayUI::HudStats stats;
    stats.fps = static_cast<float>(system.GetLastPerfStats().game_fps);
    // from the settings, not the renderer: this runs on the present thread, which the GPU
    // thread waits on, and asking the GPU for its renderer waits for the GPU thread
    if (const u32 scale = Settings::values.resolution_factor.GetValue(); scale != 0) {
        stats.rendered_width = static_cast<int>(400 * scale);
        stats.rendered_height = static_cast<int>(240 * scale);
    }
    OverlayUI::SetHudStats(stats);
}

void Hide() {
    s_visible.store(false);
    s_pending_nav.store(0);
    ImGuiOverlay::SetVisible(false);
    s_shown = false;
}

// Runs on the present thread, after the game's frame is in `image`.
void DrawCallback(vk::CommandBuffer cmd, vk::Image image, vk::Extent2D extent, vk::Format format) {
    if (!s_registered.load() || s_failed) {
        return;
    }
    if (!s_ready) {
        s_color_format = format;
        if (!ImGuiOverlay::Init(&s_host)) {
            LOG_ERROR(Frontend, "tico overlay could not start; the game runs without a menu");
            s_failed = true;
            return;
        }
        OverlayUI::ReloadSettings();
        s_last_frame = std::chrono::steady_clock::now();
        s_ready = true;
    }

    {
        std::lock_guard lock(s_notice_mutex);
        if (s_pending_notice) {
            s_visible.store(true);
            ImGuiOverlay::SetVisible(true);
            s_shown = true;
            OverlayUI::ShowNotice(std::move(s_pending_notice->first),
                                  std::move(s_pending_notice->second));
            s_pending_notice.reset();
        }
    }
    if (s_cheat_refresh.exchange(false)) {
        OverlayUI::RefreshCheatList();
    }
    if (s_resume_prompt.exchange(false)) {
        s_visible.store(true);
        ImGuiOverlay::SetVisible(true);
        s_shown = true;
        OverlayUI::ShowResumePrompt();
    }

    const bool visible = s_visible.load();
    if (visible != s_shown) {
        ImGuiOverlay::SetVisible(visible);
        s_shown = visible;
    }

    PublishHudStats();
    const auto now = std::chrono::steady_clock::now();
    const float delta = std::chrono::duration<float>(now - s_last_frame).count();
    s_last_frame = now;
    if (!visible && !OverlayUI::HasTransientContent()) {
        return;
    }

    if (visible) {
        const unsigned int nav = s_pending_nav.exchange(0);
        ImGuiOverlay::FeedNav({
            .up = (nav & NavBit_Up) != 0,
            .down = (nav & NavBit_Down) != 0,
            .left = (nav & NavBit_Left) != 0,
            .right = (nav & NavBit_Right) != 0,
            .accept = (nav & NavBit_Accept) != 0,
            .cancel = (nav & NavBit_Cancel) != 0,
        });
        ImGuiOverlay::FeedTouch({s_touch_down.load(), s_touch_x.load(), s_touch_y.load()});
    }

    ImDrawData* draw_data =
        ImGuiOverlay::BuildFrame(static_cast<float>(extent.width),
                                 static_cast<float>(extent.height),
                                 delta > 0.0f && delta < 0.25f ? delta : 1.0f / 60.0f);
    const Action action = ImGuiOverlay::ConsumeAction();
    if (action != Action::None) {
        s_pending_action.store(static_cast<int>(action));
        if (action == Action::Resume) {
            Hide();
        }
    }
    if (!draw_data) {
        return;
    }

    cmd.beginRenderPass(
        {
            .renderPass = s_render_pass,
            .framebuffer = FramebufferFor(image, extent),
            .renderArea{.offset = {0, 0}, .extent = extent},
        },
        vk::SubpassContents::eInline);
    {
        // ImGui's backend uploads textures (the font) with a submit and a queue wait of its own
        std::scoped_lock lock{*s_queue_mutex};
        ImGui_ImplVulkan_RenderDrawData(draw_data, static_cast<VkCommandBuffer>(cmd));
    }
    cmd.endRenderPass();
}

// Called on the present thread with the queue idle, before the swapchain images go.
void ResetCallback() {
    DestroySwapImages();
}

} // namespace

bool Init(Vulkan::RendererVulkan& renderer) {
    if (s_registered.load()) {
        return true;
    }
    const Vulkan::Instance& instance = renderer.GetVulkanInstance();
    s_instance = instance.GetInstance();
    s_physical_device = instance.GetPhysicalDevice();
    s_device = instance.GetDevice();
    s_queue = instance.GetGraphicsQueue();
    s_queue_family = instance.GetGraphicsQueueFamilyIndex();
    s_image_count = std::max(renderer.GetMainPresentWindow().ImageCount(), 2u);
    s_queue_mutex = &renderer.GetScheduler().submit_mutex;
    if (!s_instance || !s_device) {
        return false;
    }

    s_command_pool = s_device.createCommandPool({
        .flags = vk::CommandPoolCreateFlagBits::eTransient |
                 vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = s_queue_family,
    });
    // ImGui's font atlas and every picture the menu shows
    const vk::DescriptorPoolSize pool_size{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 128,
    };
    s_descriptor_pool = s_device.createDescriptorPool({
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 128,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    });

    s_visible.store(false);
    s_pending_action.store(0);
    s_pending_nav.store(0);
    hidInitializeTouchScreen();

    // last, so no callback runs half set up
    Vulkan::SetOverlayResetCallback(&ResetCallback);
    Vulkan::SetOverlayDrawCallback(&DrawCallback);
    s_registered.store(true);
    return true;
}

void Shutdown() {
    if (!s_registered.load()) {
        return;
    }
    Vulkan::SetOverlayDrawCallback(nullptr);
    Vulkan::SetOverlayResetCallback(nullptr);
    s_registered.store(false);
    {
        std::scoped_lock lock{*s_queue_mutex};
        s_device.waitIdle();
    }

    if (s_ready) {
        ImGuiOverlay::Shutdown();
    }
    DestroySwapImages();
    s_device.destroyDescriptorPool(s_descriptor_pool);
    s_device.destroyCommandPool(s_command_pool);
    s_descriptor_pool = VK_NULL_HANDLE;
    s_command_pool = VK_NULL_HANDLE;
    s_ready = false;
    s_failed = false;
    s_shown = false;
    s_visible.store(false);
    s_pending_action.store(0);
    s_pending_nav.store(0);
}

void Update(PadState* pad) {
    if (!s_registered.load() || !pad) {
        return;
    }

    const u64 held = padGetButtons(pad);
    const bool combo_down = (held & HidNpadButton_Plus) != 0 && (held & HidNpadButton_Minus) != 0;
    if (combo_down && !s_was_combo_down) {
        s_visible.store(!s_visible.load());
        s_pending_nav.store(0);
    }
    s_was_combo_down = combo_down;

    if (!s_visible.load()) {
        s_nav_held_prev = 0;
        s_accept_prev = (held & HidNpadButton_A) != 0;
        s_cancel_prev = (held & HidNpadButton_B) != 0;
        s_touch_down.store(false);
        return;
    }

    // directions fire on press, then repeat while held
    const u64 directions = held & (HidNpadButton_AnyUp | HidNpadButton_AnyDown |
                                   HidNpadButton_AnyLeft | HidNpadButton_AnyRight);
    u64 fire = directions & ~s_nav_held_prev;
    if (directions != 0 && directions == s_nav_held_prev) {
        if (--s_nav_repeat <= 0) {
            fire |= directions;
            s_nav_repeat = kNavRepeat;
        }
    } else if (fire != 0) {
        s_nav_repeat = kNavInitialDelay;
    }
    s_nav_held_prev = directions;

    const bool accept = (held & HidNpadButton_A) != 0;
    const bool cancel = (held & HidNpadButton_B) != 0;
    unsigned int nav = 0;
    if (fire & HidNpadButton_AnyUp) {
        nav |= NavBit_Up;
    }
    if (fire & HidNpadButton_AnyDown) {
        nav |= NavBit_Down;
    }
    if (fire & HidNpadButton_AnyLeft) {
        nav |= NavBit_Left;
    }
    if (fire & HidNpadButton_AnyRight) {
        nav |= NavBit_Right;
    }
    if (accept && !s_accept_prev) {
        nav |= NavBit_Accept;
    }
    if (cancel && !s_cancel_prev) {
        nav |= NavBit_Cancel;
    }
    s_accept_prev = accept;
    s_cancel_prev = cancel;
    if (nav != 0) {
        s_pending_nav.fetch_or(nav);
    }

    HidTouchScreenState touch{};
    if (hidGetTouchScreenStates(&touch, 1) && touch.count > 0) {
        s_touch_x.store(static_cast<float>(touch.touches[0].x));
        s_touch_y.store(static_cast<float>(touch.touches[0].y));
        s_touch_down.store(true);
    } else {
        s_touch_down.store(false);
    }
}

bool IsVisible() {
    return s_visible.load();
}

void SetVisible(bool visible) {
    s_visible.store(visible);
    s_pending_nav.store(0);
}

void ShowNotice(std::string message, std::vector<std::string> choices) {
    std::lock_guard lock(s_notice_mutex);
    s_pending_notice.emplace(std::move(message), std::move(choices));
    s_visible.store(true);
}

void ShowResumePrompt() {
    s_resume_prompt.store(true);
    s_visible.store(true);
}

unsigned long long LoadPicture(const std::string& path, float* aspect) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* rgba = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!rgba) {
        return 0;
    }
    const ImTextureID texture = s_host.CreateTextureRGBA(rgba, width, height);
    stbi_image_free(rgba);
    if (aspect && height > 0) {
        *aspect = static_cast<float>(width) / static_cast<float>(height);
    }
    return static_cast<unsigned long long>(texture);
}

void FreePicture(unsigned long long texture) {
    if (texture) {
        s_host.DestroyTexture(static_cast<ImTextureID>(texture));
    }
}

void RequestCheatRefresh() {
    s_cheat_refresh.store(true);
}

Action ConsumeAction() {
    return static_cast<Action>(s_pending_action.exchange(0));
}

} // namespace SwitchFrontend::GameOverlay

// The renderer tico's overlay draws with during a game: ImGui's Vulkan backend,
// recording into Azahar's present command buffer (see DrawCallback).
namespace SwitchFrontend::GameOverlay {

bool RendererInit() {
    if (s_color_format == vk::Format::eUndefined) {
        return false;
    }
    s_render_pass = CreateRenderPass(s_color_format);
    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_1, LoadFunction,
                                        static_cast<VkInstance>(s_instance))) {
        LOG_ERROR(Frontend, "tico overlay: ImGui_ImplVulkan_LoadFunctions failed");
        return false;
    }
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_1;
    info.Instance = static_cast<VkInstance>(s_instance);
    info.PhysicalDevice = static_cast<VkPhysicalDevice>(s_physical_device);
    info.Device = static_cast<VkDevice>(s_device);
    info.QueueFamily = s_queue_family;
    info.Queue = static_cast<VkQueue>(s_queue);
    info.DescriptorPool = static_cast<VkDescriptorPool>(s_descriptor_pool);
    info.RenderPass = static_cast<VkRenderPass>(s_render_pass);
    info.MinImageCount = s_image_count;
    info.ImageCount = s_image_count;
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&info)) {
        LOG_ERROR(Frontend, "tico overlay: ImGui_ImplVulkan_Init failed");
        return false;
    }
    s_backend_ready = true;
    return true;
}

void RendererShutdown() {
    for (auto& [id, texture] : s_textures) {
        DestroyTexture(texture);
    }
    s_textures.clear();
    if (s_backend_ready) {
        ImGui_ImplVulkan_Shutdown();
        s_backend_ready = false;
    }
    if (s_render_pass) {
        s_device.destroyRenderPass(s_render_pass);
        s_render_pass = VK_NULL_HANDLE;
    }
    s_color_format = vk::Format::eUndefined;
}

void RendererBeginFrame() {
    ImGui_ImplVulkan_NewFrame();
}

} // namespace SwitchFrontend::GameOverlay
