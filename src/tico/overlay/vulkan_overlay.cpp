// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

// vk_common.h establishes the vulkan-hpp configuration (VK_NO_PROTOTYPES, dynamic
// dispatcher) and must be included before the ImGui Vulkan backend so the backend's
// <vulkan/vulkan.h> sees the same prototype-less configuration.
#include "video_core/renderer_vulkan/vk_common.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "tico/overlay/overlay_ui.h"
#include "tico/overlay/vulkan_overlay.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_present_window.h"

namespace SwitchFrontend::VulkanOverlay {
namespace {

constexpr const char* TAG = "[tico-overlay]";
constexpr std::array<const char*, 3> kFontPaths = {{
    "romfs:/fonts/font.ttf",
    "sdmc:/tico/fonts/font.ttf",
    "sdmc:/tico/system/3ds/fonts/font.ttf",
}};
constexpr std::array<const char*, 9> kAvatarPaths = {{
    "sdmc:/tico/assets/avatar.jpg",
    "sdmc:/tico/assets/avatar.jpeg",
    "sdmc:/tico/assets/avatar.png",
    "sdmc:/tiicu/assets/avatar.jpg",
    "sdmc:/tiicu/assets/avatar.jpeg",
    "sdmc:/tiicu/assets/avatar.png",
    "romfs:/assets/avatar.jpg",
    "romfs:/assets/avatar.jpeg",
    "romfs:/assets/avatar.png",
}};

std::atomic_bool s_initialized{false};
std::atomic_bool s_visible{false};
std::atomic_bool s_exit_requested{false};
std::atomic_int s_pending_action{0};
std::atomic_uint s_pending_nav_mask{0};

bool s_was_combo_down = false;
bool s_psm_initialized = false;

// Present-thread-only state (touched exclusively inside the draw/reset callbacks).
bool s_backend_ready = false;
vk::Instance s_instance;
vk::PhysicalDevice s_physical_device;
vk::Device s_device;
vk::Queue s_graphics_queue;
u32 s_graphics_queue_family = 0;
u32 s_image_count = 2;

vk::DescriptorPool s_descriptor_pool;
vk::CommandPool s_command_pool;
vk::RenderPass s_render_pass;
vk::Format s_color_format = vk::Format::eUndefined;

struct OverlayTextureResource {
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView view;
    vk::Sampler sampler;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};
std::vector<OverlayTextureResource> s_overlay_textures;
bool s_avatar_load_attempted = false;
unsigned long long s_avatar_texture_id = 0;

struct SwapImageResources {
    vk::ImageView view;
    vk::Framebuffer framebuffer;
};
std::unordered_map<VkImage, SwapImageResources> s_swap_resources;
vk::Extent2D s_fb_extent{};

struct NavPrev {
    bool up;
    bool down;
    bool left;
    bool right;
    bool a;
    bool b;
};
NavPrev s_nav_prev{};

enum NavBits : unsigned int {
    NavBit_Up = 1u << 0,
    NavBit_Down = 1u << 1,
    NavBit_Left = 1u << 2,
    NavBit_Right = 1u << 3,
    NavBit_Accept = 1u << 4,
    NavBit_Cancel = 1u << 5,
};

PFN_vkVoidFunction OverlayLoader(const char* name, void* user_data) {
    auto instance = static_cast<VkInstance>(user_data);
    if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr) {
        return nullptr;
    }
    return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(instance, name);
}

bool CreateDescriptorPool() {
    const vk::DescriptorPoolSize pool_size{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 64,
    };
    const vk::DescriptorPoolCreateInfo info{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 64,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    s_descriptor_pool = s_device.createDescriptorPool(info);
    return true;
}

bool CreateCommandPool() {
    const vk::CommandPoolCreateInfo info{
        .flags = vk::CommandPoolCreateFlagBits::eTransient |
                 vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = s_graphics_queue_family,
    };
    s_command_pool = s_device.createCommandPool(info);
    return true;
}

bool FindMemoryType(u32 type_filter, vk::MemoryPropertyFlags properties, u32& out_index) {
    const vk::PhysicalDeviceMemoryProperties memory_properties =
        s_physical_device.getMemoryProperties();
    for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            out_index = i;
            return true;
        }
    }
    return false;
}

void TransitionImage(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout old_layout,
                     vk::ImageLayout new_layout, vk::AccessFlags src_access,
                     vk::AccessFlags dst_access, vk::PipelineStageFlags src_stage,
                     vk::PipelineStageFlags dst_stage) {
    const vk::ImageMemoryBarrier barrier{
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    cmd.pipelineBarrier(src_stage, dst_stage, vk::DependencyFlagBits::eByRegion, {}, {}, barrier);
}

void DestroyOverlayTexture(OverlayTextureResource& texture) {
    if (texture.descriptor && s_backend_ready) {
        ImGui_ImplVulkan_RemoveTexture(texture.descriptor);
        texture.descriptor = VK_NULL_HANDLE;
    }
    if (texture.sampler) {
        s_device.destroySampler(texture.sampler);
        texture.sampler = VK_NULL_HANDLE;
    }
    if (texture.view) {
        s_device.destroyImageView(texture.view);
        texture.view = VK_NULL_HANDLE;
    }
    if (texture.image) {
        s_device.destroyImage(texture.image);
        texture.image = VK_NULL_HANDLE;
    }
    if (texture.memory) {
        s_device.freeMemory(texture.memory);
        texture.memory = VK_NULL_HANDLE;
    }
}

void DestroyOverlayTextures() {
    for (auto& texture : s_overlay_textures) {
        DestroyOverlayTexture(texture);
    }
    s_overlay_textures.clear();
    s_avatar_texture_id = 0;
    OverlayUI::SetAvatarTextureId(0);
}

unsigned long long CreateOverlayTextureRGBA(const unsigned char* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0 || !s_backend_ready || !s_command_pool) {
        return 0;
    }

    const vk::DeviceSize upload_size =
        static_cast<vk::DeviceSize>(width) * static_cast<vk::DeviceSize>(height) * 4;

    vk::Buffer staging_buffer;
    vk::DeviceMemory staging_memory;
    vk::CommandBuffer command_buffer;
    OverlayTextureResource texture{};

    auto cleanup = [&] {
        if (command_buffer)
            s_device.freeCommandBuffers(s_command_pool, command_buffer);
        if (staging_buffer)
            s_device.destroyBuffer(staging_buffer);
        if (staging_memory)
            s_device.freeMemory(staging_memory);
        DestroyOverlayTexture(texture);
    };

    const vk::BufferCreateInfo buffer_info{
        .size = upload_size,
        .usage = vk::BufferUsageFlagBits::eTransferSrc,
        .sharingMode = vk::SharingMode::eExclusive,
    };
    staging_buffer = s_device.createBuffer(buffer_info);

    const vk::MemoryRequirements buffer_requirements =
        s_device.getBufferMemoryRequirements(staging_buffer);
    u32 staging_memory_type = 0;
    if (!FindMemoryType(buffer_requirements.memoryTypeBits,
                        vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent,
                        staging_memory_type)) {
        LOG_ERROR(Render_Vulkan, "{} no host-visible coherent memory for avatar upload", TAG);
        cleanup();
        return 0;
    }

    const vk::MemoryAllocateInfo buffer_alloc_info{
        .allocationSize = buffer_requirements.size,
        .memoryTypeIndex = staging_memory_type,
    };
    staging_memory = s_device.allocateMemory(buffer_alloc_info);
    s_device.bindBufferMemory(staging_buffer, staging_memory, 0);

    void* mapped = s_device.mapMemory(staging_memory, 0, upload_size);
    std::memcpy(mapped, rgba, static_cast<std::size_t>(upload_size));
    s_device.unmapMemory(staging_memory);

    const vk::ImageCreateInfo image_info{
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
    };
    texture.image = s_device.createImage(image_info);

    const vk::MemoryRequirements image_requirements =
        s_device.getImageMemoryRequirements(texture.image);
    u32 image_memory_type = 0;
    if (!FindMemoryType(image_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal,
                        image_memory_type)) {
        LOG_ERROR(Render_Vulkan, "{} no device-local memory for avatar texture", TAG);
        cleanup();
        return 0;
    }

    const vk::MemoryAllocateInfo image_alloc_info{
        .allocationSize = image_requirements.size,
        .memoryTypeIndex = image_memory_type,
    };
    texture.memory = s_device.allocateMemory(image_alloc_info);
    s_device.bindImageMemory(texture.image, texture.memory, 0);

    const vk::ImageViewCreateInfo view_info{
        .image = texture.image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    texture.view = s_device.createImageView(view_info);

    const vk::SamplerCreateInfo sampler_info{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    };
    texture.sampler = s_device.createSampler(sampler_info);

    const vk::CommandBufferAllocateInfo command_alloc_info{
        .commandPool = s_command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    command_buffer = s_device.allocateCommandBuffers(command_alloc_info)[0];
    command_buffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    TransitionImage(command_buffer, texture.image, vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eTransferDstOptimal, {},
                    vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eTransfer);

    const vk::BufferImageCopy copy_region{
        .imageSubresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageExtent = vk::Extent3D(static_cast<u32>(width), static_cast<u32>(height), 1),
    };
    command_buffer.copyBufferToImage(staging_buffer, texture.image,
                                     vk::ImageLayout::eTransferDstOptimal, copy_region);

    TransitionImage(command_buffer, texture.image, vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eTransferWrite,
                    vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eTransfer,
                    vk::PipelineStageFlagBits::eFragmentShader);

    command_buffer.end();

    const vk::SubmitInfo submit_info{
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };
    s_graphics_queue.submit(submit_info);
    s_graphics_queue.waitIdle();

    s_device.freeCommandBuffers(s_command_pool, command_buffer);
    command_buffer = VK_NULL_HANDLE;
    s_device.destroyBuffer(staging_buffer);
    staging_buffer = VK_NULL_HANDLE;
    s_device.freeMemory(staging_memory);
    staging_memory = VK_NULL_HANDLE;

    texture.descriptor =
        ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(texture.sampler),
                                    static_cast<VkImageView>(texture.view),
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const unsigned long long texture_id =
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(texture.descriptor));
    s_overlay_textures.push_back(texture);
    return texture_id;
}

bool LoadAvatarFromRGBA(const unsigned char* rgba, int width, int height, const char* source) {
    const unsigned long long texture_id = CreateOverlayTextureRGBA(rgba, width, height);
    if (texture_id == 0) {
        return false;
    }
    s_avatar_texture_id = texture_id;
    OverlayUI::SetAvatarTextureId(texture_id);
    LOG_INFO(Render_Vulkan, "{} loaded avatar: {} ({}x{})", TAG, source, width, height);
    return true;
}

bool LoadAvatarFromFile(const char* path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* rgba = stbi_load(path, &width, &height, &channels, 4);
    if (!rgba) {
        return false;
    }
    const bool loaded = LoadAvatarFromRGBA(rgba, width, height, path);
    stbi_image_free(rgba);
    return loaded;
}

bool LoadAvatarFromMemory(const unsigned char* data, std::size_t size, const char* source) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* rgba =
        stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!rgba) {
        return false;
    }
    const bool loaded = LoadAvatarFromRGBA(rgba, width, height, source);
    stbi_image_free(rgba);
    return loaded;
}

bool LoadAvatarFromAccount() {
    if (accountInitialize(AccountServiceType_Application) != 0) {
        return false;
    }

    AccountUid uid{};
    bool found = false;
    if (accountGetPreselectedUser(&uid) == 0 && accountUidIsValid(&uid)) {
        found = true;
    }
    if (!found && accountGetLastOpenedUser(&uid) == 0 && accountUidIsValid(&uid)) {
        found = true;
    }
    if (!found) {
        s32 user_count = 0;
        if (accountGetUserCount(&user_count) == 0 && user_count > 0) {
            AccountUid uids[ACC_USER_LIST_SIZE]{};
            s32 actual_total = 0;
            if (accountListAllUsers(uids, ACC_USER_LIST_SIZE, &actual_total) == 0 &&
                actual_total > 0) {
                uid = uids[0];
                found = accountUidIsValid(&uid);
            }
        }
    }

    bool loaded = false;
    if (found) {
        AccountProfile profile{};
        AccountProfileBase profile_base{};
        if (accountGetProfile(&profile, uid) == 0) {
            if (accountProfileGet(&profile, nullptr, &profile_base) == 0 &&
                profile_base.nickname[0] != '\0') {
                OverlayUI::SetNickname(profile_base.nickname);
            }

            u32 image_size = 0;
            if (accountProfileGetImageSize(&profile, &image_size) == 0 && image_size > 0) {
                std::vector<unsigned char> jpeg_data(image_size);
                u32 actual_size = 0;
                if (accountProfileLoadImage(&profile, jpeg_data.data(), image_size, &actual_size) ==
                        0 &&
                    actual_size > 0) {
                    loaded = LoadAvatarFromMemory(jpeg_data.data(), actual_size,
                                                  "switch-account-avatar");
                }
            }
            accountProfileClose(&profile);
        }
    }

    accountExit();
    return loaded;
}

void LoadAvatarTexture() {
    if (s_avatar_load_attempted) {
        return;
    }
    s_avatar_load_attempted = true;

    for (const char* path : kAvatarPaths) {
        if (LoadAvatarFromFile(path)) {
            return;
        }
    }
    if (LoadAvatarFromAccount()) {
        return;
    }
    LOG_INFO(Render_Vulkan, "{} no avatar image found", TAG);
}

// Load-op=LOAD render pass compatible with the swapchain color format. The present
// path leaves the image in eColorAttachmentOptimal before calling us and restores
// the present layout afterwards, so both initial and final layout stay color-attachment.
bool CreateRenderPass(vk::Format format) {
    const vk::AttachmentDescription color_attachment{
        .format = format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::AttachmentReference color_ref{
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };
    const vk::SubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                         vk::AccessFlagBits::eColorAttachmentRead,
    };
    const vk::RenderPassCreateInfo info{
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    s_render_pass = s_device.createRenderPass(info);
    return true;
}

// Lazily creates (and caches) an image view + framebuffer for a swapchain image.
vk::Framebuffer GetFramebufferFor(vk::Image image, vk::Extent2D extent) {
    const VkImage key = static_cast<VkImage>(image);
    if (extent != s_fb_extent) {
        // Extent changed without a swapchain reset notification; drop stale entries.
        for (auto& [img, res] : s_swap_resources) {
            s_device.destroyFramebuffer(res.framebuffer);
            s_device.destroyImageView(res.view);
        }
        s_swap_resources.clear();
        s_fb_extent = extent;
    }

    const auto it = s_swap_resources.find(key);
    if (it != s_swap_resources.end()) {
        return it->second.framebuffer;
    }

    const vk::ImageViewCreateInfo view_info{
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = s_color_format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    const vk::ImageView view = s_device.createImageView(view_info);

    const vk::FramebufferCreateInfo fb_info{
        .renderPass = s_render_pass,
        .attachmentCount = 1,
        .pAttachments = &view,
        .width = extent.width,
        .height = extent.height,
        .layers = 1,
    };
    const vk::Framebuffer framebuffer = s_device.createFramebuffer(fb_info);

    s_swap_resources.emplace(key, SwapImageResources{view, framebuffer});
    return framebuffer;
}

void DestroySwapResources() {
    for (auto& [img, res] : s_swap_resources) {
        s_device.destroyFramebuffer(res.framebuffer);
        s_device.destroyImageView(res.view);
    }
    s_swap_resources.clear();
    s_fb_extent = vk::Extent2D{};
}

bool EnsureBackend(vk::Format format) {
    if (s_backend_ready) {
        return true;
    }
    s_color_format = format;
    if (!CreateRenderPass(format)) {
        return false;
    }

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_1;
    init_info.Instance = static_cast<VkInstance>(s_instance);
    init_info.PhysicalDevice = static_cast<VkPhysicalDevice>(s_physical_device);
    init_info.Device = static_cast<VkDevice>(s_device);
    init_info.QueueFamily = s_graphics_queue_family;
    init_info.Queue = static_cast<VkQueue>(s_graphics_queue);
    init_info.DescriptorPool = static_cast<VkDescriptorPool>(s_descriptor_pool);
    init_info.RenderPass = static_cast<VkRenderPass>(s_render_pass);
    init_info.MinImageCount = s_image_count >= 2 ? s_image_count : 2;
    init_info.ImageCount = s_image_count;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.Subpass = 0;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        LOG_ERROR(Render_Vulkan, "{} ImGui_ImplVulkan_Init failed", TAG);
        return false;
    }
    s_backend_ready = true;
    LOG_INFO(Render_Vulkan, "{} ImGui backend ready (format={}, images={})", TAG,
             vk::to_string(format), s_image_count);
    LoadAvatarTexture();
    return true;
}

// Runs on the present thread, after the emulator frame is blitted into `image`.
void DrawCallback(vk::CommandBuffer cmd, vk::Image image, vk::Extent2D extent, vk::Format format) {
    if (!s_initialized.load()) {
        return;
    }
    if (!EnsureBackend(format)) {
        return;
    }

    const bool visible = s_visible.load();
    const bool has_transient = OverlayUI::HasTransientContent();
    if (!visible && !has_transient) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize =
        ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
    io.DeltaTime = 1.0f / 60.0f;

    const unsigned int nav_mask = s_pending_nav_mask.exchange(0);
    OverlayUI::FeedNav({
        .up = (nav_mask & NavBit_Up) != 0,
        .down = (nav_mask & NavBit_Down) != 0,
        .left = (nav_mask & NavBit_Left) != 0,
        .right = (nav_mask & NavBit_Right) != 0,
        .accept = (nav_mask & NavBit_Accept) != 0,
        .cancel = (nav_mask & NavBit_Cancel) != 0,
    });

    ImGui::NewFrame();
    const OverlayUI::Action action =
        OverlayUI::Render(static_cast<int>(extent.width), static_cast<int>(extent.height));
    ImGui::Render();

    if (action != OverlayUI::Action::None) {
        s_pending_action.store(static_cast<int>(action));
        if (action == OverlayUI::Action::Exit) {
            s_exit_requested.store(true);
        }
        if (action == OverlayUI::Action::Resume || action == OverlayUI::Action::Exit ||
            OverlayUI::IsSaveStateAction(action) || OverlayUI::IsLoadStateAction(action)) {
            s_visible.store(false);
            s_pending_nav_mask.store(0);
            OverlayUI::SetVisible(false);
        }
    }

    const vk::Framebuffer framebuffer = GetFramebufferFor(image, extent);
    if (!framebuffer) {
        return;
    }

    const vk::RenderPassBeginInfo rp_begin{
        .renderPass = s_render_pass,
        .framebuffer = framebuffer,
        .renderArea{.offset = {0, 0}, .extent = extent},
        .clearValueCount = 0,
        .pClearValues = nullptr,
    };
    cmd.beginRenderPass(rp_begin, vk::SubpassContents::eInline);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmd));
    cmd.endRenderPass();
}

void ResetCallback() {
    // Called on the present thread with the queue idle, before swapchain images die.
    DestroySwapResources();
}

} // namespace

bool Init(Vulkan::RendererVulkan& renderer) {
    if (s_initialized.load()) {
        return true;
    }

    const Vulkan::Instance& instance = renderer.GetVulkanInstance();
    s_instance = instance.GetInstance();
    s_physical_device = instance.GetPhysicalDevice();
    s_device = instance.GetDevice();
    s_graphics_queue = instance.GetGraphicsQueue();
    s_graphics_queue_family = instance.GetGraphicsQueueFamilyIndex();
    s_image_count = renderer.GetMainPresentWindow().ImageCount();
    if (s_image_count < 2) {
        s_image_count = 2;
    }
    if (!s_device || !s_instance) {
        LOG_WARNING(Render_Vulkan, "{} renderer not ready yet", TAG);
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    for (const char* font_path : kFontPaths) {
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(font_path, 32.0f)) {
            io.FontDefault = font;
            LOG_INFO(Render_Vulkan, "{} loaded font: {}", TAG, font_path);
            break;
        }
    }
    if (!io.FontDefault) {
        LOG_WARNING(Render_Vulkan, "{} could not load overlay font, using ImGui default", TAG);
    }
    ImGui::StyleColorsDark();

    if (!CreateCommandPool() || !CreateDescriptorPool()) {
        if (s_command_pool) {
            s_device.destroyCommandPool(s_command_pool);
            s_command_pool = VK_NULL_HANDLE;
        }
        ImGui::DestroyContext();
        return false;
    }

    if (!s_psm_initialized && psmInitialize() == 0) {
        s_psm_initialized = true;
    }

    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_1, OverlayLoader,
                                        static_cast<VkInstance>(s_instance))) {
        LOG_ERROR(Render_Vulkan, "{} ImGui_ImplVulkan_LoadFunctions failed", TAG);
        s_device.destroyCommandPool(s_command_pool);
        s_command_pool = VK_NULL_HANDLE;
        s_device.destroyDescriptorPool(s_descriptor_pool);
        s_descriptor_pool = VK_NULL_HANDLE;
        ImGui::DestroyContext();
        return false;
    }

    s_visible.store(false);
    s_exit_requested.store(false);
    s_pending_action.store(0);
    s_pending_nav_mask.store(0);
    s_backend_ready = false;
    s_avatar_load_attempted = false;

    // Register present-time hooks last so no callback fires mid-initialization.
    Vulkan::SetOverlayResetCallback(&ResetCallback);
    Vulkan::SetOverlayDrawCallback(&DrawCallback);

    s_initialized.store(true);
    LOG_INFO(Render_Vulkan, "{} initialized (images={})", TAG, s_image_count);
    return true;
}

void Update(PadState* pad) {
    if (!s_initialized.load() || !pad) {
        return;
    }

    const u64 held = padGetButtons(pad);
    const bool plus = (held & HidNpadButton_Plus) != 0;
    const bool minus = (held & HidNpadButton_Minus) != 0;
    const bool combo_down = plus && minus;

    if (combo_down && !s_was_combo_down) {
        const bool new_visible = !s_visible.load();
        s_visible.store(new_visible);
        if (!new_visible) {
            s_pending_nav_mask.store(0);
        }
    }
    s_was_combo_down = combo_down;

    const bool visible = s_visible.load();
    OverlayUI::SetVisible(visible);

    const bool up = (held & (HidNpadButton_Up | HidNpadButton_StickLUp)) != 0;
    const bool down = (held & (HidNpadButton_Down | HidNpadButton_StickLDown)) != 0;
    const bool left = (held & (HidNpadButton_Left | HidNpadButton_StickLLeft)) != 0;
    const bool right = (held & (HidNpadButton_Right | HidNpadButton_StickLRight)) != 0;
    const bool a = (held & HidNpadButton_A) != 0;
    const bool b = (held & HidNpadButton_B) != 0;

    unsigned int nav_mask = 0;
    if (up && !s_nav_prev.up)
        nav_mask |= NavBit_Up;
    if (down && !s_nav_prev.down)
        nav_mask |= NavBit_Down;
    if (left && !s_nav_prev.left)
        nav_mask |= NavBit_Left;
    if (right && !s_nav_prev.right)
        nav_mask |= NavBit_Right;
    if (a && !s_nav_prev.a)
        nav_mask |= NavBit_Accept;
    if (b && !s_nav_prev.b)
        nav_mask |= NavBit_Cancel;

    if (visible && nav_mask != 0) {
        s_pending_nav_mask.fetch_or(nav_mask);
    }

    s_nav_prev = {up, down, left, right, a, b};
}

bool IsVisible() {
    return s_visible.load();
}

bool ShouldExit() {
    return s_exit_requested.load();
}

int ConsumeAction() {
    return s_pending_action.exchange(0);
}

void Shutdown() {
    if (!s_initialized.load()) {
        return;
    }

    // Stop the present thread from calling our draw callback, then tear down.
    Vulkan::SetOverlayDrawCallback(nullptr);
    Vulkan::SetOverlayResetCallback(nullptr);

    if (s_device) {
        s_device.waitIdle();
    }

    DestroySwapResources();
    DestroyOverlayTextures();

    if (s_backend_ready) {
        ImGui_ImplVulkan_Shutdown();
        s_backend_ready = false;
    }
    ImGui::DestroyContext();

    if (s_render_pass) {
        s_device.destroyRenderPass(s_render_pass);
        s_render_pass = VK_NULL_HANDLE;
    }
    if (s_descriptor_pool) {
        s_device.destroyDescriptorPool(s_descriptor_pool);
        s_descriptor_pool = VK_NULL_HANDLE;
    }
    if (s_command_pool) {
        s_device.destroyCommandPool(s_command_pool);
        s_command_pool = VK_NULL_HANDLE;
    }

    OverlayUI::SetVisible(false);
    OverlayUI::ShowToast(std::string{});

    s_visible.store(false);
    s_exit_requested.store(false);
    s_pending_action.store(0);
    s_pending_nav_mask.store(0);
    s_color_format = vk::Format::eUndefined;
    s_avatar_load_attempted = false;
    s_initialized.store(false);

    if (s_psm_initialized) {
        psmExit();
        s_psm_initialized = false;
    }
}

} // namespace SwitchFrontend::VulkanOverlay
