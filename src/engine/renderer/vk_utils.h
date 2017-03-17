#pragma once

#include "vk_definitions.h"
#include <functional>
#include <string>

struct Defer_Action {
    Defer_Action(std::function<void()> action)
        : action(action) {}
    ~Defer_Action() {
        action();
    }
    std::function<void()> action;
};

struct Shader_Module {
    Shader_Module(const std::string& spriv_file_name);
    ~Shader_Module();
    VkShaderModule handle;
};

// Errors
void check_vk_result(VkResult result, const std::string& function_name);
void error(const std::string& message);

// Command buffers
void record_and_run_commands(VkCommandPool command_pool, VkQueue queue, std::function<void(VkCommandBuffer)> recorder);

void record_image_layout_transition(
    VkCommandBuffer command_buffer, VkImage image, VkFormat format,
    VkAccessFlags src_access_flags, VkImageLayout old_layout,
    VkAccessFlags dst_access_flags, VkImageLayout new_layout);

// Images
VkImage create_texture(int image_width, int image_height, VkFormat format);
VkImage create_staging_texture(int image_width, int image_height, VkFormat format, const uint8_t* pixels, int bytes_per_pixel);
VkImage create_depth_attachment_image(int image_width, int image_height, VkFormat format);
VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags);

// Buffers
VkBuffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage);
VkBuffer create_staging_buffer(VkDeviceSize size, const void* data);
VkBuffer create_permanent_staging_buffer(VkDeviceSize size, VkDeviceMemory& memory);
