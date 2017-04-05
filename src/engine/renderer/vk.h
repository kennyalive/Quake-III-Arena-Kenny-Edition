#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#include "vulkan/vulkan.h"

#include <vector>

bool initialize_vulkan(HWND hwnd);
void deinitialize_vulkan();

VkPhysicalDevice get_physical_device();
VkDevice get_device();
uint32_t get_queue_family_index();
VkQueue get_queue();
VkSwapchainKHR get_swapchain();
VkFormat get_swapchain_image_format();
const std::vector<VkImageView>& get_swapchain_image_views();

struct Vk_Staging_Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE; // memory associated with a buffer
    VkDeviceSize offset = -1;
    VkDeviceSize size = 0;
};

VkImage vk_create_cinematic_image(int width, int height, Vk_Staging_Buffer& staging_buffer);
void vk_update_cinematic_image(VkImage image, const Vk_Staging_Buffer& staging_buffer, int width, int height, const uint8_t* rgba_pixels);

// Shaders.
extern unsigned char single_texture_vert_spv[];
extern long long single_texture_vert_spv_size;

extern unsigned char single_texture_frag_spv[];
extern long long single_texture_frag_spv_size;

extern unsigned char multi_texture_vert_spv[];
extern long long multi_texture_vert_spv_size;

extern unsigned char multi_texture_add_frag_spv[];
extern long long multi_texture_add_frag_spv_size;

extern unsigned char multi_texture_mul_frag_spv[];
extern long long multi_texture_mul_frag_spv_size;
