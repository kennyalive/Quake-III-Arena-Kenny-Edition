#pragma once

#include <memory>
#include <vector>
#include "vk.h"

struct SDL_SysWMinfo;

class Vulkan_Demo {
public:
    Vulkan_Demo(int window_width, int window_height, const SDL_SysWMinfo& window_sys_info);
    ~Vulkan_Demo();

    void render_view();
    void render_cinematic_frame();

public:
    void create_command_pool();
    void create_descriptor_pool();

    void create_uniform_buffer();
    VkImage create_texture(const uint8_t* pixels, int bytes_per_pixel, int width, int height, VkImageView& image_view);
    void create_texture_sampler();
    void create_depth_buffer_resources();

    void create_descriptor_set_layout();
    void create_descriptor_set();
    void create_render_pass();
    void create_framebuffers();
    void create_pipeline_layout();
    void create_pipeline();

    void upload_geometry();
    void record_render_frame();
    void update_ubo_descriptor();
    void update_image_descriptor(bool cinematic);
    void update_uniform_buffer(bool cinematic);

public:
    const int window_width = 0;
    const int window_height = 0;

    VkSemaphore image_acquired = VK_NULL_HANDLE;
    VkSemaphore rendering_finished = VK_NULL_HANDLE;
    VkFence rendering_finished_fence = VK_NULL_HANDLE;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

    VkBuffer uniform_staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory uniform_staging_buffer_memory = VK_NULL_HANDLE;
    VkBuffer uniform_buffer = VK_NULL_HANDLE;

    VkSampler texture_image_sampler = VK_NULL_HANDLE;
    VkImage depth_image = VK_NULL_HANDLE;
    VkImageView depth_image_view = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    uint32_t model_indices_count = 0;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    uint32_t swapchain_image_index = -1;
    VkImageView cinematic_image_view = VK_NULL_HANDLE;
};
