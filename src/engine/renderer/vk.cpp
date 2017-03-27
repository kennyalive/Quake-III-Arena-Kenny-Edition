#include "vk.h"
#include "vk_utils.h"
#include "vk_allocator.h"
#include "tr_local.h"
#include "vk_demo.h"

#define SDL_MAIN_HANDLED
#include "sdl/SDL_syswm.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

struct Vulkan_Globals {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    uint32_t queue_family_index = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format = {};
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
};

static Vulkan_Globals vulkan_globals;

static const std::vector<const char*> instance_extensions = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME
};

static const std::vector<const char*> device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static bool is_extension_available(const std::vector<VkExtensionProperties>& properties, const char* extension_name) {
    for (const auto& property : properties) {
        if (strcmp(property.extensionName, extension_name) == 0)
            return true;
    }
    return false;
}

static uint32_t select_queue_family(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

    // select queue family with presentation and graphics support
    for (uint32_t i = 0; i < queue_family_count; i++) {
        VkBool32 presentation_supported;
        auto result = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &presentation_supported);
        check_vk_result(result, "vkGetPhysicalDeviceSurfaceSupportKHR");

        if (presentation_supported && (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            return i;
    }
    error("failed to find queue family");
    return -1;
}

static VkInstance create_instance() {
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    check_vk_result(result, "vkEnumerateInstanceExtensionProperties");

    std::vector<VkExtensionProperties> extension_properties(count);
    result = vkEnumerateInstanceExtensionProperties(nullptr, &count, extension_properties.data());
    check_vk_result(result, "vkEnumerateInstanceExtensionProperties");

    for (auto name : instance_extensions) {
        if (!is_extension_available(extension_properties, name))
            error(std::string("required instance extension is not available: ") + name);
    }

    VkInstanceCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.pApplicationInfo = nullptr;
    desc.enabledLayerCount = 0;
    desc.ppEnabledLayerNames = nullptr;
    desc.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size());
    desc.ppEnabledExtensionNames = instance_extensions.data();

    VkInstance instance;
    result = vkCreateInstance(&desc, nullptr, &instance);
    check_vk_result(result, "vkCreateInstance");
    return instance;
}

static VkPhysicalDevice select_physical_device(VkInstance instance) {
    uint32_t count;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    check_vk_result(result, "vkEnumeratePhysicalDevices");

    if (count == 0)
        error("no physical device found");

    std::vector<VkPhysicalDevice> physical_devices(count);
    result = vkEnumeratePhysicalDevices(instance, &count, physical_devices.data());
    check_vk_result(result, "vkEnumeratePhysicalDevices");
    return physical_devices[0]; // just get the first one
}

static VkSurfaceKHR create_surface(VkInstance instance, const SDL_SysWMinfo& window_sys_info) {
    VkWin32SurfaceCreateInfoKHR desc;
    desc.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.hinstance = ::GetModuleHandle(nullptr);
    desc.hwnd = window_sys_info.info.win.window;

    VkSurfaceKHR surface;
    VkResult result = vkCreateWin32SurfaceKHR(instance, &desc, nullptr, &surface);
    check_vk_result(result, "vkCreateWin32SurfaceKHR");
    return surface;
}

static VkDevice create_device(VkPhysicalDevice physical_device, uint32_t queue_family_index) {
    uint32_t count = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    check_vk_result(result, "vkEnumerateDeviceExtensionProperties");

    std::vector<VkExtensionProperties> extension_properties(count);
    result = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extension_properties.data());
    check_vk_result(result, "vkEnumerateDeviceExtensionProperties");

    for (auto name : device_extensions) {
        if (!is_extension_available(extension_properties, name))
            error(std::string("required device extension is not available: ") + name);
    }

    const float priority = 1.0;
    VkDeviceQueueCreateInfo queue_desc;
    queue_desc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_desc.pNext = nullptr;
    queue_desc.flags = 0;
    queue_desc.queueFamilyIndex = queue_family_index;
    queue_desc.queueCount = 1;
    queue_desc.pQueuePriorities = &priority;

    VkDeviceCreateInfo device_desc;
    device_desc.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_desc.pNext = nullptr;
    device_desc.flags = 0;
    device_desc.queueCreateInfoCount = 1;
    device_desc.pQueueCreateInfos = &queue_desc;
    device_desc.enabledLayerCount = 0;
    device_desc.ppEnabledLayerNames = nullptr;
    device_desc.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
    device_desc.ppEnabledExtensionNames = device_extensions.data();
    device_desc.pEnabledFeatures = nullptr;

    VkDevice device;
    result = vkCreateDevice(physical_device, &device_desc, nullptr, &device);
    check_vk_result(result, "vkCreateDevice");
    return device;
}

static VkSurfaceFormatKHR select_surface_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
    uint32_t format_count;
    VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
    check_vk_result(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    assert(format_count > 0);

    std::vector<VkSurfaceFormatKHR> candidates(format_count);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, candidates.data());
    check_vk_result(result, "vkGetPhysicalDeviceSurfaceFormatsKHR");

    // special case that means we can choose any format
    if (candidates.size() == 1 && candidates[0].format == VK_FORMAT_UNDEFINED) {
        VkSurfaceFormatKHR surface_format;
        surface_format.format = VK_FORMAT_R8G8B8A8_UNORM;
        surface_format.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        return surface_format;
    }
    return candidates[0];
}

static VkSwapchainKHR create_swapchain(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format) {
    VkSurfaceCapabilitiesKHR surface_caps;
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps);
    check_vk_result(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    VkExtent2D image_extent = surface_caps.currentExtent;
    if (image_extent.width == 0xffffffff && image_extent.height == 0xffffffff) {
        image_extent.width = std::min(surface_caps.maxImageExtent.width, std::max(surface_caps.minImageExtent.width, 640u));
        image_extent.height = std::min(surface_caps.maxImageExtent.height, std::max(surface_caps.minImageExtent.height, 480u));
    }

    // transfer destination usage is required by image clear operations
    if ((surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
        error("VK_IMAGE_USAGE_TRANSFER_DST_BIT is not supported by the swapchain");
    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // determine present mode and swapchain image count
    uint32_t present_mode_count;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, nullptr);
    check_vk_result(result, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes.data());
    check_vk_result(result, "vkGetPhysicalDeviceSurfacePresentModesKHR");

    VkPresentModeKHR present_mode;
    uint32_t image_count;

    auto it = std::find(present_modes.cbegin(), present_modes.cend(), VK_PRESENT_MODE_MAILBOX_KHR);
    if (it != present_modes.cend()) {
        present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        image_count = std::max(3u, surface_caps.minImageCount);
        if (surface_caps.maxImageCount > 0) {
            image_count = std::min(image_count, surface_caps.maxImageCount);
        }
    }
    else {
        present_mode = VK_PRESENT_MODE_FIFO_KHR;
        image_count = surface_caps.minImageCount;
    }

    // create swap chain
    VkSwapchainCreateInfoKHR desc;
    desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.surface = surface;
    desc.minImageCount = image_count;
    desc.imageFormat = surface_format.format;
    desc.imageColorSpace = surface_format.colorSpace;
    desc.imageExtent = image_extent;
    desc.imageArrayLayers = 1;
    desc.imageUsage = image_usage;
    desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = nullptr;
    desc.preTransform = surface_caps.currentTransform;
    desc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    desc.presentMode = present_mode;
    desc.clipped = VK_TRUE;
    desc.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain;
    result = vkCreateSwapchainKHR(device, &desc, nullptr, &swapchain);
    check_vk_result(result, "vkCreateSwapchainKHR");
    return swapchain;
}

void initialize_vulkan(const SDL_SysWMinfo& window_sys_info) {
    auto& g = vulkan_globals;

    g.instance = create_instance();
    g.physical_device = select_physical_device(g.instance);
    g.surface = create_surface(g.instance, window_sys_info);
    g.queue_family_index = select_queue_family(g.physical_device, g.surface);
    g.device = create_device(g.physical_device, g.queue_family_index);

    vkGetDeviceQueue(g.device, g.queue_family_index, 0, &g.queue);

    g.surface_format = select_surface_format(g.physical_device, g.surface);
    g.swapchain = create_swapchain(g.physical_device, g.device, g.surface, g.surface_format);

    uint32_t image_count;
    VkResult result = vkGetSwapchainImagesKHR(g.device, g.swapchain, &image_count, nullptr);
    check_vk_result(result, "vkGetSwapchainImagesKHR");
    g.swapchain_images.resize(image_count);
    result = vkGetSwapchainImagesKHR(g.device, g.swapchain, &image_count, g.swapchain_images.data());
    check_vk_result(result, "vkGetSwapchainImagesKHR");

    g.swapchain_image_views.resize(image_count);
    for (std::size_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.image = g.swapchain_images[i];
        desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
        desc.format = g.surface_format.format;
        desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        desc.subresourceRange.baseMipLevel = 0;
        desc.subresourceRange.levelCount = 1;
        desc.subresourceRange.baseArrayLayer = 0;
        desc.subresourceRange.layerCount = 1;
        result = vkCreateImageView(g.device, &desc, nullptr, &g.swapchain_image_views[i]);
        check_vk_result(result, "vkCreateImageView");
    }
}

void deinitialize_vulkan() {
    auto& g = vulkan_globals;
    for (auto image_view : g.swapchain_image_views) {
        vkDestroyImageView(g.device, image_view, nullptr);
    }
    vkDestroySwapchainKHR(g.device, g.swapchain, nullptr);
    vkDestroyDevice(g.device, nullptr);
    vkDestroySurfaceKHR(g.instance, g.surface, nullptr);
    vkDestroyInstance(g.instance, nullptr);
    g = Vulkan_Globals();
}

VkPhysicalDevice get_physical_device() {
    return vulkan_globals.physical_device;
}

VkDevice get_device() {
    return vulkan_globals.device;
}

uint32_t get_queue_family_index() {
    return vulkan_globals.queue_family_index;
}

VkQueue get_queue() {
    return vulkan_globals.queue;
}

VkSwapchainKHR get_swapchain() {
    return vulkan_globals.swapchain;
}

VkFormat get_swapchain_image_format() {
    return vulkan_globals.surface_format.format;
}

const std::vector<VkImageView>& get_swapchain_image_views() {
    return vulkan_globals.swapchain_image_views;
}

VkImage vk_create_cinematic_image(int width, int height, Vk_Staging_Buffer& staging_buffer) {
    VkBufferCreateInfo buffer_desc;
    buffer_desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_desc.pNext = nullptr;
    buffer_desc.flags = 0;
    buffer_desc.size = width * height * 4;
    buffer_desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_desc.queueFamilyIndexCount = 0;
    buffer_desc.pQueueFamilyIndices = nullptr;

    VkBuffer buffer;
    VkResult result = vkCreateBuffer(get_device(), &buffer_desc, nullptr, &buffer);
    check_vk_result(result, "vkCreateBuffer");

    VkDeviceMemory buffer_memory = get_allocator()->allocate_staging_memory(buffer);
    result = vkBindBufferMemory(get_device(), buffer, buffer_memory, 0);
    check_vk_result(result, "vkBindBufferMemory");

    VkImageCreateInfo image_desc;
    image_desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_desc.pNext = nullptr;
    image_desc.flags = 0;
    image_desc.imageType = VK_IMAGE_TYPE_2D;
    image_desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_desc.extent.width = width;
    image_desc.extent.height = height;
    image_desc.extent.depth = 1;
    image_desc.mipLevels = 1;
    image_desc.arrayLayers = 1;
    image_desc.samples = VK_SAMPLE_COUNT_1_BIT;
    image_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_desc.queueFamilyIndexCount = 0;
    image_desc.pQueueFamilyIndices = nullptr;
    image_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
    result = vkCreateImage(get_device(), &image_desc, nullptr, &image);
    check_vk_result(result, "vkCreateImage");

    VkDeviceMemory image_memory = get_allocator()->allocate_memory(image);
    result = vkBindImageMemory(get_device(), image, image_memory, 0);
    check_vk_result(result, "vkBindImageMemory");

    staging_buffer.handle = buffer;
    staging_buffer.memory = buffer_memory;
    staging_buffer.offset = 0;
    staging_buffer.size = width * height * 4;
    return image;
}

void vk_update_cinematic_image(VkImage image, const Vk_Staging_Buffer& staging_buffer, int width, int height, const uint8_t* rgba_pixels) {
    void* buffer_data;
    VkResult result = vkMapMemory(get_device(), staging_buffer.memory, staging_buffer.offset, staging_buffer.size, 0, &buffer_data);
    check_vk_result(result, "vkMapMemory");
    memcpy(buffer_data, rgba_pixels, staging_buffer.size);
    vkUnmapMemory(get_device(), staging_buffer.memory);

    record_and_run_commands(vulkan_demo->command_pool, get_queue(),
        [&image, &staging_buffer, &width, &height](VkCommandBuffer command_buffer) {

        record_image_layout_transition(command_buffer, image, VK_FORMAT_R8G8B8A8_UNORM,
            0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = VkOffset3D{ 0, 0, 0 };
        region.imageExtent = VkExtent3D{ (uint32_t)width, (uint32_t)height, 1 };

        vkCmdCopyBufferToImage(command_buffer, staging_buffer.handle, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        record_image_layout_transition(command_buffer, image, VK_FORMAT_R8G8B8A8_UNORM,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
}
