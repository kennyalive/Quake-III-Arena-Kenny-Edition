#pragma once

#include <memory>
#include <vector>
#include "vk_definitions.h"

struct SDL_SysWMinfo;

class Vulkan_Demo {
public:
    Vulkan_Demo(int window_width, int window_height, const SDL_SysWMinfo& window_sys_info);
    ~Vulkan_Demo();

    void run_frame();

private:
    void create_command_pool();
    void create_descriptor_pool();

    void create_uniform_buffer();
    void create_texture();
    void create_texture_sampler();
    void create_depth_buffer_resources();

    void create_descriptor_set_layout();
    void create_descriptor_set();
    void create_render_pass();
    void create_framebuffers();
    void create_pipeline_layout();
    void create_pipeline();

    void upload_geometry();
    void record_render_scene();
    void record_render_frame();
    void update_uniform_buffer();

private:
    const int window_width = 0;
    const int window_height = 0;

    VkSemaphore image_acquired = VK_NULL_HANDLE;
    VkSemaphore rendering_finished = VK_NULL_HANDLE;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

    VkBuffer uniform_staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory uniform_staging_buffer_memory = VK_NULL_HANDLE;
    VkBuffer uniform_buffer = VK_NULL_HANDLE;
    VkImage texture_image = VK_NULL_HANDLE;
    VkImageView texture_image_view = VK_NULL_HANDLE;
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

    std::vector<VkCommandBuffer> render_frame_command_buffers; // command buffer per swapchain image
    VkCommandBuffer render_scene_command_buffer;
};
