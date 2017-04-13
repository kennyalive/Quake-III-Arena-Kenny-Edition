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

FILE* logfile;

Vulkan_Demo::Vulkan_Demo(int window_width, int window_height)
: window_width(window_width) 
, window_height(window_height)
{
    logfile = fopen("vk_dev.log", "w");

    image_acquired = get_resource_manager()->create_semaphore();
    rendering_finished = get_resource_manager()->create_semaphore();

    VkFenceCreateInfo fence_desc;
    fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_desc.pNext = nullptr;
    fence_desc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkResult result = vkCreateFence(vk.device, &fence_desc, nullptr, &rendering_finished_fence);
    check_vk_result(result, "vkCreateFence");

    create_texture_sampler();

    upload_geometry();
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

void Vulkan_Demo::upload_geometry() {
 
    {
        VkBufferCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.size = 16 * 1024 * 1024;
        desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        desc.queueFamilyIndexCount = 0;
        desc.pQueueFamilyIndices = nullptr;

        tess_vertex_buffer = get_resource_manager()->create_buffer(desc);
        tess_vertex_buffer_memory = get_allocator()->allocate_staging_memory(tess_vertex_buffer);
        VkResult result = vkBindBufferMemory(vk.device, tess_vertex_buffer, tess_vertex_buffer_memory, 0);
        check_vk_result(result, "vkBindBufferMemory");
    }

    {
        VkBufferCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.size = 16 * 1024 * 1024;
        desc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        desc.queueFamilyIndexCount = 0;
        desc.pQueueFamilyIndices = nullptr;

        tess_index_buffer = get_resource_manager()->create_buffer(desc);
        tess_index_buffer_memory = get_allocator()->allocate_staging_memory(tess_index_buffer);
        VkResult result = vkBindBufferMemory(vk.device, tess_index_buffer, tess_index_buffer_memory, 0);
        check_vk_result(result, "vkBindBufferMemory");
    }
}

void Vulkan_Demo::begin_frame() {
    fprintf(logfile, "begin_frame\n");
    fflush(logfile);

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

    tess_vertex_buffer_offset = 0;
    tess_index_buffer_offset = 0;

    glState.vk_dirty_attachments = false;
}

void Vulkan_Demo::end_frame() {
    fprintf(logfile, "end_frame (vb_size %d, ib_size %d)\n", (int)tess_vertex_buffer_offset, (int)tess_index_buffer_offset);
    fflush(logfile);
    vkCmdEndRenderPass(vk.command_buffer);
}

void Vulkan_Demo::render_tess(const shaderStage_t* stage) {
    fprintf(logfile, "render_tess (vert %d, inds %d)\n", tess.numVertexes, tess.numIndexes);
    fflush(logfile);

    void* data;
    VkResult result = vkMapMemory(vk.device, tess_vertex_buffer_memory, tess_vertex_buffer_offset, tess.numVertexes * sizeof(Vk_Vertex), 0, &data);
    check_vk_result(result, "vkMapMemory");
    Vk_Vertex* v = (Vk_Vertex*)data;
    for (int i = 0; i < tess.numVertexes; i++, v++) {
        v->pos[0] = tess.xyz[i][0];
        v->pos[1] = tess.xyz[i][1];
        v->pos[2] = tess.xyz[i][2];
        v->color[0] = tess.svars.colors[i][0] / 255.0f;
        v->color[1] = tess.svars.colors[i][1] / 255.0f;
        v->color[2] = tess.svars.colors[i][2] / 255.0f;
        v->color[3] = tess.svars.colors[i][3] / 255.0f;
        v->st[0] = tess.svars.texcoords[0][i][0];
        v->st[1] = tess.svars.texcoords[0][i][1];
    }
    vkUnmapMemory(vk.device, tess_vertex_buffer_memory);

    result = vkMapMemory(vk.device, tess_index_buffer_memory, tess_index_buffer_offset, tess.numIndexes * sizeof(uint32_t), 0, &data);
    check_vk_result(result, "vkMapMemory");
    uint32_t* ind = (uint32_t*)data;
    for (int i = 0; i < tess.numIndexes; i++, ind++) {
        *ind = tess.indexes[i];
    }
    vkUnmapMemory(vk.device, tess_index_buffer_memory);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(vk.command_buffer, 0, 1, &tess_vertex_buffer, &tess_vertex_buffer_offset);
    vkCmdBindIndexBuffer(vk.command_buffer, tess_index_buffer, tess_index_buffer_offset, VK_INDEX_TYPE_UINT32);

    VkDescriptorSet set = glState.vk_current_images[0]->vk_descriptor_set;

    float mvp[16];
    vk_get_mvp_transform(mvp);
    vkCmdPushConstants(vk.command_buffer, vk.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp);

    vkCmdBindDescriptorSets(vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0, 1, &set, 0, nullptr);
    
    vkCmdBindPipeline(vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, stage->vk_pipeline);

    VkRect2D r = vk_get_viewport_rect();
    vkCmdSetScissor(vk.command_buffer, 0, 1, &r);

    VkViewport viewport;
    viewport.x = (float)r.offset.x;
    viewport.y = (float)r.offset.y;
    viewport.width = (float)r.extent.width;
    viewport.height = (float)r.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(vk.command_buffer, 0, 1, &viewport);
    
    if (tess.shader->polygonOffset) {
        vkCmdSetDepthBias(vk.command_buffer, r_offsetUnits->value, 0.0f, r_offsetFactor->value);
    }

    vkCmdDrawIndexed(vk.command_buffer, tess.numIndexes, 1, 0, 0, 0);
    tess_vertex_buffer_offset += tess.numVertexes * sizeof(Vk_Vertex);
    tess_index_buffer_offset += tess.numIndexes * sizeof(uint32_t);

    glState.vk_dirty_attachments = true;
}

void Vulkan_Demo::render_tess_multi(const shaderStage_t* stage) {
    fprintf(logfile, "render_tess_multi (vert %d, inds %d)\n", tess.numVertexes, tess.numIndexes);
    fflush(logfile);

    void* data;
    VkResult result = vkMapMemory(vk.device, tess_vertex_buffer_memory, tess_vertex_buffer_offset, tess.numVertexes * sizeof(Vk_Vertex2), 0, &data);
    check_vk_result(result, "vkMapMemory");
    Vk_Vertex2* v = (Vk_Vertex2*)data;
    for (int i = 0; i < tess.numVertexes; i++, v++) {
        v->pos[0] = tess.xyz[i][0];
        v->pos[1] = tess.xyz[i][1];
        v->pos[2] = tess.xyz[i][2];
        v->color[0] = tess.svars.colors[i][0] / 255.0f;
        v->color[1] = tess.svars.colors[i][1] / 255.0f;
        v->color[2] = tess.svars.colors[i][2] / 255.0f;
        v->color[3] = tess.svars.colors[i][3] / 255.0f;
        v->st[0] = tess.svars.texcoords[0][i][0];
        v->st[1] = tess.svars.texcoords[0][i][1];
        v->st2[0] = tess.svars.texcoords[1][i][0];
        v->st2[1] = tess.svars.texcoords[1][i][1];
    }
    vkUnmapMemory(vk.device, tess_vertex_buffer_memory);

    result = vkMapMemory(vk.device, tess_index_buffer_memory, tess_index_buffer_offset, tess.numIndexes * sizeof(uint32_t), 0, &data);
    check_vk_result(result, "vkMapMemory");
    uint32_t* ind = (uint32_t*)data;
    for (int i = 0; i < tess.numIndexes; i++, ind++) {
        *ind = tess.indexes[i];
    }
    vkUnmapMemory(vk.device, tess_index_buffer_memory);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(vk.command_buffer, 0, 1, &tess_vertex_buffer, &tess_vertex_buffer_offset);
    vkCmdBindIndexBuffer(vk.command_buffer, tess_index_buffer, tess_index_buffer_offset, VK_INDEX_TYPE_UINT32);

    image_t* image = glState.vk_current_images[0];
    image_t* image2 = glState.vk_current_images[1];

    VkDescriptorSet sets[2] = { image->vk_descriptor_set, image2->vk_descriptor_set };

    float mvp[16];
    vk_get_mvp_transform(mvp);
    vkCmdPushConstants(vk.command_buffer, vk.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp);
    
    vkCmdBindDescriptorSets(vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0, 2, sets, 0, nullptr);

    vkCmdBindPipeline(vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, stage->vk_pipeline);

    VkRect2D r = vk_get_viewport_rect();
    vkCmdSetScissor(vk.command_buffer, 0, 1, &r);

    VkViewport viewport;
    viewport.x = (float)r.offset.x;
    viewport.y = (float)r.offset.y;
    viewport.width = (float)r.extent.width;
    viewport.height = (float)r.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(vk.command_buffer, 0, 1, &viewport);

    if (tess.shader->polygonOffset) {
        vkCmdSetDepthBias(vk.command_buffer, r_offsetUnits->value, 0.0f, r_offsetFactor->value);
    }

    vkCmdDrawIndexed(vk.command_buffer, tess.numIndexes, 1, 0, 0, 0);
    tess_vertex_buffer_offset += tess.numVertexes * sizeof(Vk_Vertex2);
    tess_index_buffer_offset += tess.numIndexes * sizeof(uint32_t);

    glState.vk_dirty_attachments = true;
}
