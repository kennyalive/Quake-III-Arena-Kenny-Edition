#pragma once

#include <array>
#include <map>
#include <memory>
#include <vector>

#include "vk.h"
#include "tr_local.h"

class Vulkan_Demo {
public:
    Vulkan_Demo(int window_width, int window_height);

    void begin_frame();
    void end_frame();
    void render_tess(const shaderStage_t* stage);
    void render_tess_multi(const shaderStage_t* stage);

public:
    void create_descriptor_pool();

    void create_uniform_buffer();
    VkImage create_texture(const uint8_t* pixels, int bytes_per_pixel, int width, int height, VkImageView& image_view);
    void create_texture_sampler();

    void create_descriptor_set_layout();
    void create_image_descriptor_set(const image_t* image);
    void create_multitexture_descriptor_set(const image_t* image, const image_t* image2);
    void create_pipeline_layout();

    void upload_geometry();
    void update_ubo_descriptor(VkDescriptorSet set);
    void update_uniform_buffer();

public:
    const int window_width = 0;
    const int window_height = 0;

    VkSemaphore image_acquired = VK_NULL_HANDLE;
    VkSemaphore rendering_finished = VK_NULL_HANDLE;
    VkFence rendering_finished_fence = VK_NULL_HANDLE;

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

    VkBuffer uniform_staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory uniform_staging_buffer_memory = VK_NULL_HANDLE;
    VkBuffer uniform_buffer = VK_NULL_HANDLE;

    VkSampler texture_image_sampler = VK_NULL_HANDLE;
    
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

    VkBuffer tess_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory tess_vertex_buffer_memory = VK_NULL_HANDLE;
    VkDeviceSize tess_vertex_buffer_offset = 0;
    VkBuffer tess_index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory tess_index_buffer_memory = VK_NULL_HANDLE;
    VkDeviceSize tess_index_buffer_offset = 0;

    uint32_t tess_ubo_offset = 0;
    uint32_t tess_ubo_offset_step = -1;

    std::map<const image_t*, VkDescriptorSet> image_descriptor_sets; // quick UI prototyping
    std::map<std::pair<const image_t*, const image_t*>, VkDescriptorSet> multitexture_descriptor_sets;

    uint32_t swapchain_image_index = -1;
};
