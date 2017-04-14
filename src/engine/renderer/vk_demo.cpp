#include "vk_allocator.h"
#include "vk_resource_manager.h"
#include "vk_demo.h"
#include "vk.h"
#include "vk_utils.h"

#include "stb_image.h"

#include <array>
#include <chrono>
#include <iostream>
#include <functional>
#include <unordered_map>

#include "tr_local.h"

FILE* vk_log_file;

Vulkan_Demo::Vulkan_Demo(int window_width, int window_height)
: window_width(window_width) 
, window_height(window_height)
{
    vk_log_file = fopen("vk_dev.log", "w");

    image_acquired = get_resource_manager()->create_semaphore();
    rendering_finished = get_resource_manager()->create_semaphore();

    VkFenceCreateInfo fence_desc;
    fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_desc.pNext = nullptr;
    fence_desc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkResult result = vkCreateFence(vk.device, &fence_desc, nullptr, &rendering_finished_fence);
    check_vk_result(result, "vkCreateFence");

    create_texture_sampler();
}

VkImage Vulkan_Demo::create_texture(const uint8_t* pixels, int bytes_per_pixel, int image_width, int image_height, VkImageView& image_view) {
    VkImage staging_image = create_staging_texture(image_width, image_height,
        bytes_per_pixel == 3 ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8A8_UNORM, pixels, bytes_per_pixel);

    Defer_Action destroy_staging_image([this, &staging_image]() {
        vkDestroyImage(vk.device, staging_image, nullptr);
    });

    VkImage texture_image = ::create_texture(image_width, image_height,
        bytes_per_pixel == 3 ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8A8_UNORM);

    record_and_run_commands(vk.command_pool, vk.queue,
        [&texture_image, &staging_image, &image_width, &image_height, this](VkCommandBuffer command_buffer) {

        record_image_layout_transition(command_buffer, staging_image, VK_FORMAT_R8G8B8A8_UNORM,
            VK_ACCESS_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        record_image_layout_transition(command_buffer, texture_image, VK_FORMAT_R8G8B8A8_UNORM,
            0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // copy staging image's data to device local image
        VkImageSubresourceLayers subresource_layers;
        subresource_layers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource_layers.mipLevel = 0;
        subresource_layers.baseArrayLayer = 0;
        subresource_layers.layerCount = 1;

        VkImageCopy region;
        region.srcSubresource = subresource_layers;
        region.srcOffset = {0, 0, 0};
        region.dstSubresource = subresource_layers;
        region.dstOffset = {0, 0, 0};
        region.extent.width = image_width;
        region.extent.height = image_height;
        region.extent.depth = 1;

        vkCmdCopyImage(command_buffer,
            staging_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);

        record_image_layout_transition(command_buffer, texture_image, VK_FORMAT_R8G8B8A8_UNORM,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    image_view = create_image_view(texture_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    return texture_image;
}

void Vulkan_Demo::create_texture_sampler() {
    VkSamplerCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.magFilter = VK_FILTER_LINEAR;
    desc.minFilter = VK_FILTER_LINEAR;
    desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.mipLodBias = 0.0f;
    desc.anisotropyEnable = VK_TRUE;
    desc.maxAnisotropy = 1;
    desc.compareEnable = VK_FALSE;
    desc.compareOp = VK_COMPARE_OP_ALWAYS;
    desc.minLod = 0.0f;
    desc.maxLod = 0.0f;
    desc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    desc.unnormalizedCoordinates = VK_FALSE;

    texture_image_sampler = get_resource_manager()->create_sampler(desc);
}

void Vulkan_Demo::begin_frame() {
    if (r_logFile->integer)
        fprintf(vk_log_file, "begin_frame\n");

    std::array<VkClearValue, 2> clear_values;
    clear_values[0].color = {1.0f, 0.3f, 0.3f, 0.0f};
    clear_values[1].depthStencil = {1.0, 0};

    VkRenderPassBeginInfo render_pass_begin_info;
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.pNext = nullptr;
    render_pass_begin_info.renderPass = vk.render_pass;
    render_pass_begin_info.framebuffer = vk.framebuffers[swapchain_image_index];
    render_pass_begin_info.renderArea.offset = {0, 0};
    render_pass_begin_info.renderArea.extent = {(uint32_t)window_width, (uint32_t)window_height};
    render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_begin_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(vk.command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    vk.vertex_buffer_offset = 0;
    vk.index_buffer_offset = 0;

    glState.vk_dirty_attachments = false;
}

void Vulkan_Demo::end_frame() {
    if (r_logFile->integer)
        fprintf(vk_log_file, "end_frame (vb_size %d, ib_size %d, copy_time %d)\n", (int)vk.vertex_buffer_offset, (int)vk.index_buffer_offset, int(vertex_copy_time * 1000000000));
    vertex_copy_time = 0.0f;
    vkCmdEndRenderPass(vk.command_buffer);
}
