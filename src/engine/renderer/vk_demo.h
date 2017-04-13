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
    VkImage create_texture(const uint8_t* pixels, int bytes_per_pixel, int width, int height, VkImageView& image_view);
    void create_texture_sampler();

    void upload_geometry();

public:
    const int window_width = 0;
    const int window_height = 0;

    VkSemaphore image_acquired = VK_NULL_HANDLE;
    VkSemaphore rendering_finished = VK_NULL_HANDLE;
    VkFence rendering_finished_fence = VK_NULL_HANDLE;

    VkSampler texture_image_sampler = VK_NULL_HANDLE;
    
    VkBuffer tess_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory tess_vertex_buffer_memory = VK_NULL_HANDLE;
    VkDeviceSize tess_vertex_buffer_offset = 0;
    VkBuffer tess_index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory tess_index_buffer_memory = VK_NULL_HANDLE;
    VkDeviceSize tess_index_buffer_offset = 0;
    uint32_t swapchain_image_index = -1;
};
