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

public:
    VkImage create_texture(const uint8_t* pixels, int bytes_per_pixel, int width, int height, VkImageView& image_view);
    void create_texture_sampler();

public:
    const int window_width = 0;
    const int window_height = 0;

    VkSemaphore image_acquired = VK_NULL_HANDLE;
    VkSemaphore rendering_finished = VK_NULL_HANDLE;
    VkFence rendering_finished_fence = VK_NULL_HANDLE;

    VkSampler texture_image_sampler = VK_NULL_HANDLE;

    uint32_t swapchain_image_index = -1;
};
