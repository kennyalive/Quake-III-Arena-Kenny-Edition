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

struct Uniform_Buffer_Object {
    float mvp[16];
};

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
    VkResult result = vkCreateFence(vk_instance.device, &fence_desc, nullptr, &rendering_finished_fence);
    check_vk_result(result, "vkCreateFence");

    create_descriptor_pool();

    create_uniform_buffer();

    create_texture_sampler();

    create_descriptor_set_layout();
    create_pipeline_layout();

    upload_geometry();
}

void Vulkan_Demo::create_descriptor_pool() {
    std::array<VkDescriptorPoolSize, 2> pool_sizes;
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    pool_sizes[0].descriptorCount = 1024;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = 1024;

    VkDescriptorPoolCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.maxSets = 1024;
    desc.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    desc.pPoolSizes = pool_sizes.data();

    descriptor_pool = get_resource_manager()->create_descriptor_pool(desc);
}

void Vulkan_Demo::create_uniform_buffer() {
    auto size = static_cast<VkDeviceSize>(sizeof(Uniform_Buffer_Object)) * 1024;
    uniform_staging_buffer = create_permanent_staging_buffer(size, uniform_staging_buffer_memory);
    uniform_buffer = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk_instance.physical_device, &props);
    VkDeviceSize offset_align = props.limits.minUniformBufferOffsetAlignment;
    tess_ubo_offset_step = (uint32_t)((sizeof(Uniform_Buffer_Object) + offset_align - 1) / offset_align * offset_align);
}

VkImage Vulkan_Demo::create_texture(const uint8_t* pixels, int bytes_per_pixel, int image_width, int image_height, VkImageView& image_view) {
    VkImage staging_image = create_staging_texture(image_width, image_height,
        bytes_per_pixel == 3 ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8A8_UNORM, pixels, bytes_per_pixel);

    Defer_Action destroy_staging_image([this, &staging_image]() {
        vkDestroyImage(vk_instance.device, staging_image, nullptr);
    });

    VkImage texture_image = ::create_texture(image_width, image_height,
        bytes_per_pixel == 3 ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8A8_UNORM);

    record_and_run_commands(vk_instance.command_pool, vk_instance.queue,
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
    desc.maxAnisotropy = 16;
    desc.compareEnable = VK_FALSE;
    desc.compareOp = VK_COMPARE_OP_ALWAYS;
    desc.minLod = 0.0f;
    desc.maxLod = 0.0f;
    desc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    desc.unnormalizedCoordinates = VK_FALSE;

    texture_image_sampler = get_resource_manager()->create_sampler(desc);
}

void Vulkan_Demo::create_descriptor_set_layout() {
    std::array<VkDescriptorSetLayoutBinding, 3> descriptor_bindings;
    descriptor_bindings[0].binding = 0;
    descriptor_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descriptor_bindings[0].descriptorCount = 1;
    descriptor_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    descriptor_bindings[0].pImmutableSamplers = nullptr;

    descriptor_bindings[1].binding = 1;
    descriptor_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_bindings[1].descriptorCount = 1;
    descriptor_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptor_bindings[1].pImmutableSamplers = nullptr;

    descriptor_bindings[2].binding = 2;
    descriptor_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_bindings[2].descriptorCount = 1;
    descriptor_bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptor_bindings[2].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.bindingCount = static_cast<uint32_t>(descriptor_bindings.size());
    desc.pBindings = descriptor_bindings.data();

    descriptor_set_layout = get_resource_manager()->create_descriptor_set_layout(desc);
}

void Vulkan_Demo::create_image_descriptor_set(const image_t* image) {
    VkDescriptorSetAllocateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    desc.pNext = nullptr;
    desc.descriptorPool = descriptor_pool;
    desc.descriptorSetCount = 1;
    desc.pSetLayouts = &descriptor_set_layout;

    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(vk_instance.device, &desc, &set);
    check_vk_result(result, "vkAllocateDescriptorSets");

    VkDescriptorImageInfo image_info;
    image_info.sampler = texture_image_sampler;
    image_info.imageView = image->vk_image_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 1> descriptor_writes;
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = set;
    descriptor_writes[0].dstBinding = 1;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].pNext = nullptr;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[0].pImageInfo = &image_info;
    descriptor_writes[0].pBufferInfo = nullptr;
    descriptor_writes[0].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(vk_instance.device, (uint32_t)descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);

    update_ubo_descriptor(set);

    image_descriptor_sets[image] = set;
}

void Vulkan_Demo::create_multitexture_descriptor_set(const image_t* image, const image_t* image2) {
    VkDescriptorSetAllocateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    desc.pNext = nullptr;
    desc.descriptorPool = descriptor_pool;
    desc.descriptorSetCount = 1;
    desc.pSetLayouts = &descriptor_set_layout;

    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(vk_instance.device, &desc, &set);
    check_vk_result(result, "vkAllocateDescriptorSets");

    VkDescriptorImageInfo image_info[2];
    image_info[0].sampler = texture_image_sampler;
    image_info[0].imageView = image->vk_image_view;
    image_info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    image_info[1].sampler = texture_image_sampler;
    image_info[1].imageView = image2->vk_image_view;
    image_info[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> descriptor_writes;
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = set;
    descriptor_writes[0].dstBinding = 1;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].pNext = nullptr;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[0].pImageInfo = &image_info[0];
    descriptor_writes[0].pBufferInfo = nullptr;
    descriptor_writes[0].pTexelBufferView = nullptr;

    descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[1].dstSet = set;
    descriptor_writes[1].dstBinding = 2;
    descriptor_writes[1].dstArrayElement = 0;
    descriptor_writes[1].descriptorCount = 1;
    descriptor_writes[1].pNext = nullptr;
    descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[1].pImageInfo = &image_info[1];
    descriptor_writes[1].pBufferInfo = nullptr;
    descriptor_writes[1].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(vk_instance.device, (uint32_t)descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);

    update_ubo_descriptor(set);

    auto images = std::make_pair(image, image2);
    multitexture_descriptor_sets[images] = set;
}

void Vulkan_Demo::create_pipeline_layout() {
    VkPipelineLayoutCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.setLayoutCount = 1;
    desc.pSetLayouts = &descriptor_set_layout;
    desc.pushConstantRangeCount = 0;
    desc.pPushConstantRanges = nullptr;

    pipeline_layout = get_resource_manager()->create_pipeline_layout(desc);
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
        VkResult result = vkBindBufferMemory(vk_instance.device, tess_vertex_buffer, tess_vertex_buffer_memory, 0);
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
        VkResult result = vkBindBufferMemory(vk_instance.device, tess_index_buffer, tess_index_buffer_memory, 0);
        check_vk_result(result, "vkBindBufferMemory");
    }
}

void Vulkan_Demo::update_ubo_descriptor(VkDescriptorSet set) {
    VkDescriptorBufferInfo buffer_info;
    buffer_info.buffer = uniform_buffer;
    buffer_info.offset = 0;
    buffer_info.range = sizeof(Uniform_Buffer_Object);

    std::array<VkWriteDescriptorSet, 1> descriptor_writes;
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].pNext = nullptr;
    descriptor_writes[0].dstSet = set;
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descriptor_writes[0].pImageInfo = nullptr;
    descriptor_writes[0].pBufferInfo = &buffer_info;
    descriptor_writes[0].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(vk_instance.device, (uint32_t)descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);
}

void Vulkan_Demo::update_uniform_buffer() {
    Uniform_Buffer_Object ubo;

    if (backEnd.projection2D) {
        float mvp0 = 2.0f / glConfig.vidWidth;
        float mvp5 = 2.0f / glConfig.vidHeight;

        ubo.mvp[0]  =  mvp0; ubo.mvp[1]  =  0.0f; ubo.mvp[2]  = 0.0f; ubo.mvp[3]  = 0.0f;
        ubo.mvp[4]  =  0.0f; ubo.mvp[5]  =  mvp5; ubo.mvp[6]  = 0.0f; ubo.mvp[7]  = 0.0f;
        ubo.mvp[8]  =  0.0f; ubo.mvp[9]  =  0.0f; ubo.mvp[10] = 1.0f; ubo.mvp[11] = 0.0f;
        ubo.mvp[12] = -1.0f; ubo.mvp[13] = -1.0f; ubo.mvp[14] = 0.0f; ubo.mvp[15] = 1.0f;

    } else {
        const float* p = backEnd.viewParms.projectionMatrix;

        // update q3's proj matrix (opengl) to vulkan conventions: z - [0, 1] instead of [-1, 1] and invert y direction
        float zNear	= r_znear->value;
        float zFar = tr.viewParms.zFar;
        float p10 = -zFar / (zFar - zNear);
        float p14 = -zFar*zNear / (zFar - zNear);
        float p5 = -p[5];

        float proj[16] = {
            p[0], p[1], p[2], p[3],
            p[4], p5, p[6], p[7],
            p[8], p[9], p10, p[11],
            p[12], p[13], p14, p[15]
        };

        extern void myGlMultMatrix( const float *a, const float *b, float *out );
        myGlMultMatrix(backEnd.or.modelMatrix, proj, ubo.mvp);
    }

    void* data;
    VkResult result = vkMapMemory(vk_instance.device, uniform_staging_buffer_memory, tess_ubo_offset, sizeof(ubo), 0, &data);
    check_vk_result(result, "vkMapMemory");
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(vk_instance.device, uniform_staging_buffer_memory);
}

void Vulkan_Demo::begin_frame() {
    fprintf(logfile, "begin_frame\n");
    fflush(logfile);

    VkBufferCopy region;
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = sizeof(Uniform_Buffer_Object) * 1024;
    vkCmdCopyBuffer(vk_instance.command_buffer, uniform_staging_buffer, uniform_buffer, 1, &region);

    VkBufferMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = uniform_buffer;
    barrier.offset = 0;
    barrier.size = sizeof(Uniform_Buffer_Object) * 1024;

    vkCmdPipelineBarrier(vk_instance.command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
        0, nullptr, 1, &barrier, 0, nullptr);

    std::array<VkClearValue, 2> clear_values;
    clear_values[0].color = {1.0f, 0.3f, 0.3f, 0.0f};
    clear_values[1].depthStencil = {1.0, 0};

    VkRenderPassBeginInfo render_pass_begin_info;
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.pNext = nullptr;
    render_pass_begin_info.renderPass = vk_instance.render_pass;
    render_pass_begin_info.framebuffer = vk_instance.framebuffers[swapchain_image_index];
    render_pass_begin_info.renderArea.offset = {0, 0};
    render_pass_begin_info.renderArea.extent = {(uint32_t)window_width, (uint32_t)window_height};
    render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_begin_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(vk_instance.command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    tess_vertex_buffer_offset = 0;
    tess_index_buffer_offset = 0;
    tess_ubo_offset = 0;
}

void Vulkan_Demo::end_frame() {
    fprintf(logfile, "end_frame (vb_size %d, ib_size %d, ubo_size %d)\n", (int)tess_vertex_buffer_offset, (int)tess_index_buffer_offset, (int)tess_ubo_offset);
    fflush(logfile);
    vkCmdEndRenderPass(vk_instance.command_buffer);
}

void Vulkan_Demo::render_tess(const shaderStage_t* stage) {
    fprintf(logfile, "render_tess (vert %d, inds %d)\n", tess.numVertexes, tess.numIndexes);
    fflush(logfile);

    void* data;
    VkResult result = vkMapMemory(vk_instance.device, tess_vertex_buffer_memory, tess_vertex_buffer_offset, tess.numVertexes * sizeof(Vk_Vertex), 0, &data);
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
    vkUnmapMemory(vk_instance.device, tess_vertex_buffer_memory);

    result = vkMapMemory(vk_instance.device, tess_index_buffer_memory, tess_index_buffer_offset, tess.numIndexes * sizeof(uint32_t), 0, &data);
    check_vk_result(result, "vkMapMemory");
    uint32_t* ind = (uint32_t*)data;
    for (int i = 0; i < tess.numIndexes; i++, ind++) {
        *ind = tess.indexes[i];
    }
    vkUnmapMemory(vk_instance.device, tess_index_buffer_memory);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(vk_instance.command_buffer, 0, 1, &tess_vertex_buffer, &tess_vertex_buffer_offset);
    vkCmdBindIndexBuffer(vk_instance.command_buffer, tess_index_buffer, tess_index_buffer_offset, VK_INDEX_TYPE_UINT32);

    image_t* image = glState.vk_current_images[0];
    VkDescriptorSet set = image_descriptor_sets[image];

    update_uniform_buffer();
    vkCmdBindDescriptorSets(vk_instance.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &set, 1, &tess_ubo_offset);
    tess_ubo_offset += tess_ubo_offset_step;

    VkViewport viewport;
    VkRect2D scissor;

    if (backEnd.projection2D) {
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float) glConfig.vidWidth;
        viewport.height = (float)glConfig.vidHeight;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        scissor.offset = {0, 0};
        scissor.extent = {(uint32_t)glConfig.vidWidth, (uint32_t)glConfig.vidHeight};
    } else {
        viewport.x = backEnd.viewParms.viewportX;
        viewport.y = (float)(glConfig.vidHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight));
        viewport.width = (float) backEnd.viewParms.viewportWidth;
        viewport.height = (float)backEnd.viewParms.viewportHeight;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
   
        scissor.offset = {backEnd.viewParms.viewportX, (glConfig.vidHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight))};
        if (scissor.offset.y < 0) scissor.offset.y = 0; // receive such data from backEnd, so just adjust to valid value to prevent vulkan warnings
        scissor.extent = {(uint32_t)backEnd.viewParms.viewportWidth, (uint32_t)backEnd.viewParms.viewportHeight};
    }

    vkCmdBindPipeline(vk_instance.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, stage->vk_pipeline);

    vkCmdSetViewport(vk_instance.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(vk_instance.command_buffer, 0, 1, &scissor);
    if (tess.shader->polygonOffset) {
        vkCmdSetDepthBias(vk_instance.command_buffer, r_offsetUnits->value, 0.0f, r_offsetFactor->value);
    }

    vkCmdDrawIndexed(vk_instance.command_buffer, tess.numIndexes, 1, 0, 0, 0);
    tess_vertex_buffer_offset += tess.numVertexes * sizeof(Vk_Vertex);
    tess_index_buffer_offset += tess.numIndexes * sizeof(uint32_t);
}

void Vulkan_Demo::render_tess_multi(const shaderStage_t* stage) {
    fprintf(logfile, "render_tess_multi (vert %d, inds %d)\n", tess.numVertexes, tess.numIndexes);
    fflush(logfile);

    void* data;
    VkResult result = vkMapMemory(vk_instance.device, tess_vertex_buffer_memory, tess_vertex_buffer_offset, tess.numVertexes * sizeof(Vk_Vertex2), 0, &data);
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
    vkUnmapMemory(vk_instance.device, tess_vertex_buffer_memory);

    result = vkMapMemory(vk_instance.device, tess_index_buffer_memory, tess_index_buffer_offset, tess.numIndexes * sizeof(uint32_t), 0, &data);
    check_vk_result(result, "vkMapMemory");
    uint32_t* ind = (uint32_t*)data;
    for (int i = 0; i < tess.numIndexes; i++, ind++) {
        *ind = tess.indexes[i];
    }
    vkUnmapMemory(vk_instance.device, tess_index_buffer_memory);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(vk_instance.command_buffer, 0, 1, &tess_vertex_buffer, &tess_vertex_buffer_offset);
    vkCmdBindIndexBuffer(vk_instance.command_buffer, tess_index_buffer, tess_index_buffer_offset, VK_INDEX_TYPE_UINT32);

    image_t* image = glState.vk_current_images[0];
    image_t* image2 = glState.vk_current_images[1];
    auto images = std::make_pair(image, image2);
    auto it = multitexture_descriptor_sets.find(images);
    if (it == multitexture_descriptor_sets.cend()) {
        create_multitexture_descriptor_set(image, image2);
        it = multitexture_descriptor_sets.find(images);
    }
    auto set = it->second;

    update_uniform_buffer();
    vkCmdBindDescriptorSets(vk_instance.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &set, 1, &tess_ubo_offset);
    tess_ubo_offset += tess_ubo_offset_step;

    VkViewport viewport;
    VkRect2D scissor;

    if (backEnd.projection2D) {
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float) glConfig.vidWidth;
        viewport.height = (float)glConfig.vidHeight;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        scissor.offset = {0, 0};
        scissor.extent = {(uint32_t)glConfig.vidWidth, (uint32_t)glConfig.vidHeight};
    } else {
        viewport.x = backEnd.viewParms.viewportX;
        viewport.y = (float)(glConfig.vidHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight));
        viewport.width = (float) backEnd.viewParms.viewportWidth;
        viewport.height = (float)backEnd.viewParms.viewportHeight;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        scissor.offset = {backEnd.viewParms.viewportX, (glConfig.vidHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight))};
        if (scissor.offset.y < 0) scissor.offset.y = 0; // receive such data from backEnd, so just adjust to valid value to prevent vulkan warnings
        scissor.extent = {(uint32_t)backEnd.viewParms.viewportWidth, (uint32_t)backEnd.viewParms.viewportHeight};
    }

    vkCmdBindPipeline(vk_instance.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, stage->vk_pipeline);

    vkCmdSetViewport(vk_instance.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(vk_instance.command_buffer, 0, 1, &scissor);
    if (tess.shader->polygonOffset) {
        vkCmdSetDepthBias(vk_instance.command_buffer, r_offsetUnits->value, 0.0f, r_offsetFactor->value);
    }

    vkCmdDrawIndexed(vk_instance.command_buffer, tess.numIndexes, 1, 0, 0, 0);
    tess_vertex_buffer_offset += tess.numVertexes * sizeof(Vk_Vertex2);
    tess_index_buffer_offset += tess.numIndexes * sizeof(uint32_t);
}
