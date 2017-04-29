#include "tr_local.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <vector>

FILE* vk_log_file;

const int VERTEX_CHUNK_SIZE = 512 * 1024;

const int XYZ_SIZE      = 4 * VERTEX_CHUNK_SIZE;
const int COLOR_SIZE    = 1 * VERTEX_CHUNK_SIZE;
const int ST0_SIZE      = 2 * VERTEX_CHUNK_SIZE;
const int ST1_SIZE      = 2 * VERTEX_CHUNK_SIZE;

const int XYZ_OFFSET    = 0;
const int COLOR_OFFSET  = XYZ_OFFSET + XYZ_SIZE;
const int ST0_OFFSET    = COLOR_OFFSET + COLOR_SIZE;
const int ST1_OFFSET    = ST0_OFFSET + ST0_SIZE;

static const int VERTEX_BUFFER_SIZE = XYZ_SIZE + COLOR_SIZE + ST0_SIZE + ST1_SIZE;
static const int INDEX_BUFFER_SIZE = 2 * 1024 * 1024;

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

static uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t memory_type_bits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((memory_type_bits & (1 << i)) != 0 &&
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    ri.Error(ERR_FATAL, "Vulkan error: failed to find matching memory type with requested properties");
    return -1;
}

static VkFormat find_format_with_features(VkPhysicalDevice physical_device, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);

        if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
            return format;
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
            return format;
    }

    ri.Error(ERR_FATAL, "Vulkan error: failed to find format with requested features");
    return VK_FORMAT_UNDEFINED; // never get here
}

static VkFormat find_depth_format(VkPhysicalDevice physical_device) {
    return find_format_with_features(physical_device, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

static uint32_t select_queue_family(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

    // select queue family with presentation and graphics support
    for (uint32_t i = 0; i < queue_family_count; i++) {
        VkBool32 presentation_supported;
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &presentation_supported));

        if (presentation_supported && (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            return i;
    }

    ri.Error(ERR_FATAL, "Vulkan error: failed to find queue family");
    return -1;
}

static VkInstance create_instance() {
    uint32_t count = 0;
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));

    std::vector<VkExtensionProperties> extension_properties(count);
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &count, extension_properties.data()));

    for (auto name : instance_extensions) {
        if (!is_extension_available(extension_properties, name))
            ri.Error(ERR_FATAL, "Vulkan error: required instance extension is not available: %s", name);
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
    VK_CHECK(vkCreateInstance(&desc, nullptr, &instance));
    return instance;
}

static VkPhysicalDevice select_physical_device(VkInstance instance) {
    uint32_t count;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));

    if (count == 0)
        ri.Error(ERR_FATAL, "Vulkan error: no physical device found");

    std::vector<VkPhysicalDevice> physical_devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, physical_devices.data()));
    return physical_devices[0]; // just get the first one
}

static VkSurfaceKHR create_surface(VkInstance instance, HWND hwnd) {
    VkWin32SurfaceCreateInfoKHR desc;
    desc.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.hinstance = ::GetModuleHandle(nullptr);
    desc.hwnd = hwnd;

    VkSurfaceKHR surface;
    VK_CHECK(vkCreateWin32SurfaceKHR(instance, &desc, nullptr, &surface));
    return surface;
}

static VkDevice create_device(VkPhysicalDevice physical_device, uint32_t queue_family_index) {
    uint32_t count = 0;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr));

    std::vector<VkExtensionProperties> extension_properties(count);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extension_properties.data()));

    for (auto name : device_extensions) {
        if (!is_extension_available(extension_properties, name))
            ri.Error(ERR_FATAL, "Vulkan error: required device extension is not available: %s", name);
    }

    const float priority = 1.0;
    VkDeviceQueueCreateInfo queue_desc;
    queue_desc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_desc.pNext = nullptr;
    queue_desc.flags = 0;
    queue_desc.queueFamilyIndex = queue_family_index;
    queue_desc.queueCount = 1;
    queue_desc.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features;
    Com_Memset(&features, 0, sizeof(features));
    features.shaderClipDistance = VK_TRUE;

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
    device_desc.pEnabledFeatures = &features;

    VkDevice device;
    VK_CHECK(vkCreateDevice(physical_device, &device_desc, nullptr, &device));
    return device;
}

static VkSurfaceFormatKHR select_surface_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
    uint32_t format_count;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr));
    assert(format_count > 0);

    std::vector<VkSurfaceFormatKHR> candidates(format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, candidates.data()));

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
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps));

    VkExtent2D image_extent = surface_caps.currentExtent;
    if (image_extent.width == 0xffffffff && image_extent.height == 0xffffffff) {
        image_extent.width = std::min(surface_caps.maxImageExtent.width, std::max(surface_caps.minImageExtent.width, 640u));
        image_extent.height = std::min(surface_caps.maxImageExtent.height, std::max(surface_caps.minImageExtent.height, 480u));
    }

    // transfer destination usage is required by image clear operations
    if ((surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
        ri.Error(ERR_FATAL, "Vulkan error: VK_IMAGE_USAGE_TRANSFER_DST_BIT is not supported by the swapchain");

    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // determine present mode and swapchain image count
    uint32_t present_mode_count;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, nullptr));
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes.data()));

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
    VK_CHECK(vkCreateSwapchainKHR(device, &desc, nullptr, &swapchain));
    return swapchain;
}

static VkRenderPass create_render_pass(VkDevice device, VkFormat color_format, VkFormat depth_format) {
    VkAttachmentDescription attachments[2];
    attachments[0].flags = 0;
	attachments[0].format = color_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].flags = 0;
	attachments[1].format = depth_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment_ref;
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref;
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass;
    subpass.flags = 0;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = nullptr;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pResolveAttachments = nullptr;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = nullptr;

    VkRenderPassCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
	desc.attachmentCount = sizeof(attachments) / sizeof(attachments[0]);
	desc.pAttachments = attachments;
    desc.subpassCount = 1;
    desc.pSubpasses = &subpass;
    desc.dependencyCount = 0;
    desc.pDependencies = nullptr;

    VkRenderPass render_pass;
    VK_CHECK(vkCreateRenderPass(device, &desc, nullptr, &render_pass));
    return render_pass;
}

static void record_and_run_commands(VkCommandPool command_pool, VkQueue queue, std::function<void(VkCommandBuffer)> recorder) {

    VkCommandBufferAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.pNext = nullptr;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    VK_CHECK(vkAllocateCommandBuffers(vk.device, &alloc_info, &command_buffer));

    VkCommandBufferBeginInfo begin_info;
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = nullptr;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = nullptr;

    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
    recorder(command_buffer);
    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = nullptr;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;

    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(vk.device, command_pool, 1, &command_buffer);
}

static void record_image_layout_transition(VkCommandBuffer command_buffer, VkImage image, VkFormat format,
    VkAccessFlags src_access_flags, VkImageLayout old_layout, VkAccessFlags dst_access_flags, VkImageLayout new_layout) {

    auto has_depth_component = [](VkFormat format) {
        switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    };
    auto has_stencil_component = [](VkFormat format) {
        switch (format) {
        case VK_FORMAT_S8_UINT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    };

    VkImageMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = src_access_flags;
    barrier.dstAccessMask = dst_access_flags;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    bool depth = has_depth_component(format);
    bool stencil = has_stencil_component(format);
    if (depth && stencil)
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    else if (depth)
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    else if (stencil)
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    else
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}

static void allocate_and_bind_image_memory(VkImage image) {
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(vk.device, image, &memory_requirements);

    if (memory_requirements.size > IMAGE_CHUNK_SIZE) {
        ri.Error(ERR_FATAL, "Vulkan: could not allocate memory, image is too large.");
    }

    Vk_Resources::Chunk* chunk = nullptr;

    // Try to find an existing chunk of sufficient capacity.
    const auto mask = ~(memory_requirements.alignment - 1);
    for (int i = 0; i < vk_resources.num_image_chunks; i++) {
        // ensure that memory region has proper alignment
        VkDeviceSize offset = (vk_resources.image_chunks[i].used + memory_requirements.alignment - 1) & mask;

        if (offset + memory_requirements.size <= IMAGE_CHUNK_SIZE) {
            chunk = &vk_resources.image_chunks[i];
            chunk->used = offset + memory_requirements.size;
            break;
        }
    }

    // Allocate a new chunk in case we couldn't find suitable existing chunk.
    if (chunk == nullptr) {
        if (vk_resources.num_image_chunks >= MAX_IMAGE_CHUNKS) {
            ri.Error(ERR_FATAL, "Vulkan: image chunk limit has been reached");
        }

        VkMemoryAllocateInfo alloc_info;
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.pNext = nullptr;
        alloc_info.allocationSize = IMAGE_CHUNK_SIZE;
        alloc_info.memoryTypeIndex = find_memory_type(vk.physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkDeviceMemory memory;
        VK_CHECK(vkAllocateMemory(vk.device, &alloc_info, nullptr, &memory));

        chunk = &vk_resources.image_chunks[vk_resources.num_image_chunks];
        vk_resources.num_image_chunks++;
        chunk->memory = memory;
        chunk->used = memory_requirements.size;
    }

    VK_CHECK(vkBindImageMemory(vk.device, image, chunk->memory, chunk->used - memory_requirements.size));
}

static void ensure_staging_buffer_allocation(VkDeviceSize size) {
    if (vk_resources.staging_buffer_size >= size)
        return;

    if (vk_resources.staging_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(vk.device, vk_resources.staging_buffer, nullptr);

    if (vk_resources.staging_buffer_memory != VK_NULL_HANDLE)
        vkFreeMemory(vk.device, vk_resources.staging_buffer_memory, nullptr);

    vk_resources.staging_buffer_size = size;

    VkBufferCreateInfo buffer_desc;
    buffer_desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_desc.pNext = nullptr;
    buffer_desc.flags = 0;
    buffer_desc.size = size;
    buffer_desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_desc.queueFamilyIndexCount = 0;
    buffer_desc.pQueueFamilyIndices = nullptr;
    VK_CHECK(vkCreateBuffer(vk.device, &buffer_desc, nullptr, &vk_resources.staging_buffer));

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(vk.device, vk_resources.staging_buffer, &memory_requirements);

    uint32_t memory_type = find_memory_type(vk.physical_device, memory_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = nullptr;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = memory_type;
    VK_CHECK(vkAllocateMemory(vk.device, &alloc_info, nullptr, &vk_resources.staging_buffer_memory));
    VK_CHECK(vkBindBufferMemory(vk.device, vk_resources.staging_buffer, vk_resources.staging_buffer_memory, 0));

    void* data;
    VK_CHECK(vkMapMemory(vk.device, vk_resources.staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data));
    vk_resources.staging_buffer_ptr = (byte*)data;
}

VkPipeline create_pipeline(const Vk_Pipeline_Def&);

void vk_create_instance(HWND hwnd) {
    vk_log_file = fopen("vk_dev.log", "w");

    vk.instance = create_instance();
    vk.physical_device = select_physical_device(vk.instance);

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(vk.physical_device, &features);
    if (features.shaderClipDistance == VK_FALSE)
        ri.Error(ERR_FATAL, "vk_create_instance: shaderClipDistance feature is not supported");

    vk.surface = create_surface(vk.instance, hwnd);
    vk.surface_format = select_surface_format(vk.physical_device, vk.surface);

    vk.queue_family_index = select_queue_family(vk.physical_device, vk.surface);
    vk.device = create_device(vk.physical_device, vk.queue_family_index);
    vkGetDeviceQueue(vk.device, vk.queue_family_index, 0, &vk.queue);

    vk.swapchain = create_swapchain(vk.physical_device, vk.device, vk.surface, vk.surface_format);

    VK_CHECK(vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.swapchain_image_count, nullptr));

    if (vk.swapchain_image_count > MAX_SWAPCHAIN_IMAGES)
        ri.Error( ERR_FATAL, "initialize_vulkan: swapchain image count (%d) exceeded limit (%d)", vk.swapchain_image_count, MAX_SWAPCHAIN_IMAGES );

    VK_CHECK(vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.swapchain_image_count, vk.swapchain_images));

    for (std::size_t i = 0; i < vk.swapchain_image_count; i++) {
        VkImageViewCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.image = vk.swapchain_images[i];
        desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
        desc.format = vk.surface_format.format;
        desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        desc.subresourceRange.baseMipLevel = 0;
        desc.subresourceRange.levelCount = 1;
        desc.subresourceRange.baseArrayLayer = 0;
        desc.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(vk.device, &desc, nullptr, &vk.swapchain_image_views[i]));
    }

    {
        VkCommandPoolCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        desc.queueFamilyIndex = vk.queue_family_index;

        VK_CHECK(vkCreateCommandPool(vk.device, &desc, nullptr, &vk.command_pool));
    }

    {
        VkCommandBufferAllocateInfo alloc_info;
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.pNext = nullptr;
        alloc_info.commandPool = vk.command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(vk.device, &alloc_info, &vk.command_buffer));
    }

    //
    // Depth attachment image.
    // 
    {
        VkFormat depth_format = find_depth_format(vk.physical_device);

        // create depth image
        {
            VkImageCreateInfo desc;
            desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            desc.pNext = nullptr;
            desc.flags = 0;
            desc.imageType = VK_IMAGE_TYPE_2D;
            desc.format = depth_format;
            desc.extent.width = glConfig.vidWidth;
            desc.extent.height = glConfig.vidHeight;
            desc.extent.depth = 1;
            desc.mipLevels = 1;
            desc.arrayLayers = 1;
            desc.samples = VK_SAMPLE_COUNT_1_BIT;
            desc.tiling = VK_IMAGE_TILING_OPTIMAL;
            desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            desc.queueFamilyIndexCount = 0;
            desc.pQueueFamilyIndices = nullptr;
            desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VK_CHECK(vkCreateImage(vk.device, &desc, nullptr, &vk.depth_image));
        }

        // allocate depth image memory
        {
            VkMemoryRequirements memory_requirements;
            vkGetImageMemoryRequirements(vk.device, vk.depth_image, &memory_requirements);

            VkMemoryAllocateInfo alloc_info;
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.pNext = nullptr;
            alloc_info.allocationSize = memory_requirements.size;
            alloc_info.memoryTypeIndex = find_memory_type(vk.physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            VK_CHECK(vkAllocateMemory(vk.device, &alloc_info, nullptr, &vk.depth_image_memory));
            VK_CHECK(vkBindImageMemory(vk.device, vk.depth_image, vk.depth_image_memory, 0));
        }

        // create depth image view
        {
            VkImageViewCreateInfo desc;
            desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            desc.pNext = nullptr;
            desc.flags = 0;
            desc.image = vk.depth_image;
            desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
            desc.format = depth_format;
            desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            desc.subresourceRange.baseMipLevel = 0;
            desc.subresourceRange.levelCount = 1;
            desc.subresourceRange.baseArrayLayer = 0;
            desc.subresourceRange.layerCount = 1;
            VK_CHECK(vkCreateImageView(vk.device, &desc, nullptr, &vk.depth_image_view));
        }

        record_and_run_commands(vk.command_pool, vk.queue, [&depth_format](VkCommandBuffer command_buffer) {
            record_image_layout_transition(command_buffer, vk.depth_image, depth_format, 0, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        });
    }

    //
    // Renderpass.
    //
    {
	    VkFormat depth_format = find_depth_format(vk.physical_device);
	    vk.render_pass = create_render_pass(vk.device, vk.surface_format.format, depth_format);
    }

    //
    // Framebuffers for each swapchain image.
    //
    {
        VkImageView attachments[2] = {VK_NULL_HANDLE, vk.depth_image_view};

        VkFramebufferCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.renderPass = vk.render_pass;
        desc.attachmentCount = 2;
        desc.pAttachments = attachments;
        desc.width = glConfig.vidWidth;
        desc.height = glConfig.vidHeight;
        desc.layers = 1;

        for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
            attachments[0] = vk.swapchain_image_views[i]; // set color attachment
            VK_CHECK(vkCreateFramebuffer(vk.device, &desc, nullptr, &vk.framebuffers[i]));
        }
    }

    //
    // Descriptor pool.
    //
    {
        VkDescriptorPoolSize pool_size;
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = MAX_DRAWIMAGES;

        VkDescriptorPoolCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // used by the cinematic images
        desc.maxSets = MAX_DRAWIMAGES;
        desc.poolSizeCount = 1;
        desc.pPoolSizes = &pool_size;

        VK_CHECK(vkCreateDescriptorPool(vk.device, &desc, nullptr, &vk.descriptor_pool));
    }

    //
    // Descriptor set layout.
    //
    {
        VkDescriptorSetLayoutBinding descriptor_binding;
        descriptor_binding.binding = 0;
        descriptor_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_binding.descriptorCount = 1;
        descriptor_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        descriptor_binding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.bindingCount = 1;
        desc.pBindings = &descriptor_binding;

        VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &desc, nullptr, &vk.set_layout));
    }

    //
    // Pipeline layout.
    //
    {
        VkPushConstantRange push_range;
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_range.offset = 0;
        push_range.size = 128; // 32 floats

        VkDescriptorSetLayout set_layouts[2] = {vk.set_layout, vk.set_layout};

        VkPipelineLayoutCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.setLayoutCount = 2;
        desc.pSetLayouts = set_layouts;
        desc.pushConstantRangeCount = 1;
        desc.pPushConstantRanges = &push_range;

        VK_CHECK(vkCreatePipelineLayout(vk.device, &desc, nullptr, &vk.pipeline_layout));
    }

    //
    // Geometry buffers.
    //
    {
        VkBufferCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        desc.queueFamilyIndexCount = 0;
        desc.pQueueFamilyIndices = nullptr;

        desc.size = VERTEX_BUFFER_SIZE;
        desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VK_CHECK(vkCreateBuffer(vk.device, &desc, nullptr, &vk.vertex_buffer));

        desc.size = INDEX_BUFFER_SIZE;
        desc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        VK_CHECK(vkCreateBuffer(vk.device, &desc, nullptr, &vk.index_buffer));

        VkMemoryRequirements vb_memory_requirements;
        vkGetBufferMemoryRequirements(vk.device, vk.vertex_buffer, &vb_memory_requirements);

        VkMemoryRequirements ib_memory_requirements;
        vkGetBufferMemoryRequirements(vk.device, vk.index_buffer, &ib_memory_requirements);

        VkDeviceSize mask = ~(ib_memory_requirements.alignment - 1);
        VkDeviceSize index_buffer_offset = (vb_memory_requirements.size + ib_memory_requirements.alignment - 1) & mask;

        uint32_t memory_type_bits = vb_memory_requirements.memoryTypeBits & ib_memory_requirements.memoryTypeBits;
        uint32_t memory_type = find_memory_type(vk.physical_device, memory_type_bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkMemoryAllocateInfo alloc_info;
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.pNext = nullptr;
        alloc_info.allocationSize = index_buffer_offset + ib_memory_requirements.size;
        alloc_info.memoryTypeIndex = memory_type;
        VK_CHECK(vkAllocateMemory(vk.device, &alloc_info, nullptr, &vk.geometry_buffer_memory));

        vkBindBufferMemory(vk.device, vk.vertex_buffer, vk.geometry_buffer_memory, 0);
        vkBindBufferMemory(vk.device, vk.index_buffer, vk.geometry_buffer_memory, index_buffer_offset);

        void* data;
        VK_CHECK(vkMapMemory(vk.device, vk.geometry_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data));
        vk.vertex_buffer_ptr = (byte*)data;
        vk.index_buffer_ptr = (byte*)data + index_buffer_offset;
    }

    //
    // Sync primitives.
    //
    {
        VkSemaphoreCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;

        VK_CHECK(vkCreateSemaphore(vk.device, &desc, nullptr, &vk.image_acquired));
        VK_CHECK(vkCreateSemaphore(vk.device, &desc, nullptr, &vk.rendering_finished));

        VkFenceCreateInfo fence_desc;
        fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_desc.pNext = nullptr;
        fence_desc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(vk.device, &fence_desc, nullptr, &vk.rendering_finished_fence));
    }

    //
    // Shader modules.
    //
    {
        auto create_shader_module = [](uint8_t* bytes, int count) {
            if (count % 4 != 0) {
                ri.Error(ERR_FATAL, "Vulkan error: SPIR-V binary buffer size is not multiple of 4");
            }
            VkShaderModuleCreateInfo desc;
            desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            desc.pNext = nullptr;
            desc.flags = 0;
            desc.codeSize = count;
            desc.pCode = reinterpret_cast<const uint32_t*>(bytes);
               
            VkShaderModule module;
            VK_CHECK(vkCreateShaderModule(vk.device, &desc, nullptr, &module));
            return module;
        };

        extern unsigned char single_texture_vert_spv[];
        extern long long single_texture_vert_spv_size;
        vk.single_texture_vs = create_shader_module(single_texture_vert_spv, single_texture_vert_spv_size);

        extern unsigned char single_texture_clipping_plane_vert_spv[];
        extern long long single_texture_clipping_plane_vert_spv_size;
        vk.single_texture_clipping_plane_vs = create_shader_module(single_texture_clipping_plane_vert_spv, single_texture_clipping_plane_vert_spv_size);

        extern unsigned char single_texture_frag_spv[];
        extern long long single_texture_frag_spv_size;
        vk.single_texture_fs = create_shader_module(single_texture_frag_spv, single_texture_frag_spv_size);

        extern unsigned char multi_texture_vert_spv[];
        extern long long multi_texture_vert_spv_size;
        vk.multi_texture_vs = create_shader_module(multi_texture_vert_spv, multi_texture_vert_spv_size);

        extern unsigned char multi_texture_clipping_plane_vert_spv[];
        extern long long multi_texture_clipping_plane_vert_spv_size;
        vk.multi_texture_clipping_plane_vs = create_shader_module(multi_texture_clipping_plane_vert_spv, multi_texture_clipping_plane_vert_spv_size);

        extern unsigned char multi_texture_mul_frag_spv[];
        extern long long multi_texture_mul_frag_spv_size;
        vk.multi_texture_mul_fs = create_shader_module(multi_texture_mul_frag_spv, multi_texture_mul_frag_spv_size);

        extern unsigned char multi_texture_add_frag_spv[];
        extern long long multi_texture_add_frag_spv_size;
        vk.multi_texture_add_fs = create_shader_module(multi_texture_add_frag_spv, multi_texture_add_frag_spv_size);
    }

    //
    // Standard pipelines.
    //
    {
        // skybox
        {
            Vk_Pipeline_Def def;
            def.shader_type = Vk_Shader_Type::single_texture;
            def.state_bits = 0;
            def.face_culling = CT_FRONT_SIDED;
            def.polygon_offset = false;
            def.clipping_plane = false;
            def.mirror = false;

            vk.skybox_pipeline = create_pipeline(def);
        }

        // fog and dlights
        {
            Vk_Pipeline_Def def;
            def.shader_type = Vk_Shader_Type::single_texture;
            def.clipping_plane = false;
            def.mirror = false;

            unsigned int fog_state_bits[2] = {
                GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL,
                GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA
            };
            unsigned int dlight_state_bits[2] = {
                GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL,
                GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL
            };
            bool polygon_offset[2] = {false, true};

            for (int i = 0; i < 2; i++) {
                unsigned fog_state = fog_state_bits[i];
                unsigned dlight_state = dlight_state_bits[i];

                for (int j = 0; j < 3; j++) {
                    def.face_culling = j; // cullType_t value

                    for (int k = 0; k < 2; k++) {
                        def.polygon_offset = polygon_offset[k];

                        def.state_bits = fog_state;
                        vk.fog_pipelines[i][j][k] = create_pipeline(def);

                        def.state_bits = dlight_state;
                        vk.dlight_pipelines[i][j][k] = create_pipeline(def);
                    }
                }
            }
        }
    }
}

void vk_destroy_instance() {
    fclose(vk_log_file);
    vk_log_file = nullptr;

    vkDestroyImage(vk.device, vk.depth_image, nullptr);
    vkFreeMemory(vk.device, vk.depth_image_memory, nullptr);
    vkDestroyImageView(vk.device, vk.depth_image_view, nullptr);

    for (uint32_t i = 0; i < vk.swapchain_image_count; i++)
        vkDestroyFramebuffer(vk.device, vk.framebuffers[i], nullptr);

    vkDestroyRenderPass(vk.device, vk.render_pass, nullptr);

    vkDestroyCommandPool(vk.device, vk.command_pool, nullptr);

    for (uint32_t i = 0; i < vk.swapchain_image_count; i++)
        vkDestroyImageView(vk.device, vk.swapchain_image_views[i], nullptr);

    vkDestroyDescriptorPool(vk.device, vk.descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(vk.device, vk.set_layout, nullptr);
    vkDestroyPipelineLayout(vk.device, vk.pipeline_layout, nullptr);
    vkDestroyBuffer(vk.device, vk.vertex_buffer, nullptr);
    vkDestroyBuffer(vk.device, vk.index_buffer, nullptr);
    vkFreeMemory(vk.device, vk.geometry_buffer_memory, nullptr);
    vkDestroySemaphore(vk.device, vk.image_acquired, nullptr);
    vkDestroySemaphore(vk.device, vk.rendering_finished, nullptr);
    vkDestroyFence(vk.device, vk.rendering_finished_fence, nullptr);

    vkDestroyShaderModule(vk.device, vk.single_texture_vs, nullptr);
    vkDestroyShaderModule(vk.device, vk.single_texture_clipping_plane_vs, nullptr);
    vkDestroyShaderModule(vk.device, vk.single_texture_fs, nullptr);
    vkDestroyShaderModule(vk.device, vk.multi_texture_vs, nullptr);
    vkDestroyShaderModule(vk.device, vk.multi_texture_clipping_plane_vs, nullptr);
    vkDestroyShaderModule(vk.device, vk.multi_texture_mul_fs, nullptr);
    vkDestroyShaderModule(vk.device, vk.multi_texture_add_fs, nullptr);

    vkDestroyPipeline(vk.device, vk.skybox_pipeline, nullptr);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 2; k++) {
                vkDestroyPipeline(vk.device, vk.fog_pipelines[i][j][k], nullptr);
                vkDestroyPipeline(vk.device, vk.dlight_pipelines[i][j][k], nullptr);
            }

    vkDestroySwapchainKHR(vk.device, vk.swapchain, nullptr);
    vkDestroyDevice(vk.device, nullptr);
    vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);
    vkDestroyInstance(vk.instance, nullptr);

    Com_Memset(&vk, 0, sizeof(vk));
}

static float pipeline_create_time;

void vk_destroy_resources() {
    vkDeviceWaitIdle(vk.device);
    auto& res = vk_resources;

    for (int i = 0; i < res.num_image_chunks; i++)
        vkFreeMemory(vk.device, res.image_chunks[i].memory, nullptr);

    if (res.staging_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(vk.device, res.staging_buffer, nullptr);

    if (res.staging_buffer_memory != VK_NULL_HANDLE)
        vkFreeMemory(vk.device, res.staging_buffer_memory, nullptr);

    for (int i = 0; i < res.num_samplers; i++)
        vkDestroySampler(vk.device, res.samplers[i], nullptr);

    for (int i = 0; i < res.num_pipelines; i++)
        vkDestroyPipeline(vk.device, res.pipelines[i], nullptr);

    pipeline_create_time = 0.0f;

    for (int i = 0; i < MAX_VK_IMAGES; i++) {
        Vk_Image& image = res.images[i];

        if (image.handle != VK_NULL_HANDLE) {
            vkDestroyImage(vk.device, image.handle, nullptr);
            vkDestroyImageView(vk.device, image.view, nullptr);
        }
    }

    Com_Memset(&res, 0, sizeof(res));

    VK_CHECK(vkResetDescriptorPool(vk.device, vk.descriptor_pool, 0));

    // Reset geometry buffer's current offsets.
    vk.xyz_elements = 0;
    vk.color_st_elements = 0;
    vk.index_buffer_offset = 0;
}

static void record_buffer_memory_barrier(VkCommandBuffer cb, VkBuffer buffer,
        VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
        VkAccessFlags src_access, VkAccessFlags dst_access) {

    VkBufferMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cb, src_stages, dst_stages, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

Vk_Image vk_create_image(int width, int height, int mip_levels, bool repeat_texture) {
    Vk_Image image;

    // create image
    {
        VkImageCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.imageType = VK_IMAGE_TYPE_2D;
        desc.format = VK_FORMAT_R8G8B8A8_UNORM;
        desc.extent.width = width;
        desc.extent.height = height;
        desc.extent.depth = 1;
        desc.mipLevels = mip_levels;
        desc.arrayLayers = 1;
        desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.tiling = VK_IMAGE_TILING_OPTIMAL;
        desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        desc.queueFamilyIndexCount = 0;
        desc.pQueueFamilyIndices = nullptr;
        desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VK_CHECK(vkCreateImage(vk.device, &desc, nullptr, &image.handle));
        allocate_and_bind_image_memory(image.handle);
    }

    // create image view
    {
        VkImageViewCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.image = image.handle;
        desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
        desc.format = VK_FORMAT_R8G8B8A8_UNORM;
        desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        desc.subresourceRange.baseMipLevel = 0;
        desc.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        desc.subresourceRange.baseArrayLayer = 0;
        desc.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(vk.device, &desc, nullptr, &image.view));
    }

    // create associated descriptor set
    {
        VkDescriptorSetAllocateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        desc.pNext = nullptr;
        desc.descriptorPool = vk.descriptor_pool;
        desc.descriptorSetCount = 1;
        desc.pSetLayouts = &vk.set_layout;
        VK_CHECK(vkAllocateDescriptorSets(vk.device, &desc, &image.descriptor_set));

        vk_update_descriptor_set(image.descriptor_set, image.view, mip_levels > 1, repeat_texture);
    }

    return image;
}

void vk_upload_image_data(VkImage image, int width, int height, bool mipmap, const uint8_t* rgba_pixels) {
    VkBufferImageCopy regions[16];
    int num_regions = 0;

    int buffer_size = 0;

    while (true) {
        VkBufferImageCopy region;
        region.bufferOffset = buffer_size;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = num_regions;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = VkOffset3D{ 0, 0, 0 };
        region.imageExtent = VkExtent3D{ (uint32_t)width, (uint32_t)height, 1 };

        regions[num_regions] = region;
        num_regions++;

        buffer_size += width * height * 4;

        if (!mipmap || (width == 1 && height == 1))
            break;

        width >>= 1;
        if (width < 1) width = 1;

        height >>= 1;
        if (height < 1) height = 1;
    }

    ensure_staging_buffer_allocation(buffer_size);
    Com_Memcpy(vk_resources.staging_buffer_ptr, rgba_pixels, buffer_size);

    record_and_run_commands(vk.command_pool, vk.queue,
        [&image, &num_regions, &regions](VkCommandBuffer command_buffer) {

        record_buffer_memory_barrier(command_buffer, vk_resources.staging_buffer,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        record_image_layout_transition(command_buffer, image, VK_FORMAT_R8G8B8A8_UNORM,
            0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdCopyBufferToImage(command_buffer, vk_resources.staging_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_regions, regions);

        record_image_layout_transition(command_buffer, image, VK_FORMAT_R8G8B8A8_UNORM,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
}

void vk_update_descriptor_set(VkDescriptorSet set, VkImageView image_view, bool mipmap, bool repeat_texture) {
    Vk_Sampler_Def sampler_def;
    sampler_def.repeat_texture = repeat_texture;
    if (mipmap) {
        sampler_def.gl_mag_filter = gl_filter_max;
        sampler_def.gl_min_filter = gl_filter_min;
    } else {
        sampler_def.gl_mag_filter = GL_LINEAR;
        sampler_def.gl_min_filter = GL_LINEAR;
    }

    VkDescriptorImageInfo image_info;
    image_info.sampler = vk_find_sampler(sampler_def);
    image_info.imageView = image_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet descriptor_write;
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstSet = set;
    descriptor_write.dstBinding = 0;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pNext = nullptr;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.pImageInfo = &image_info;
    descriptor_write.pBufferInfo = nullptr;
    descriptor_write.pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(vk.device, 1, &descriptor_write, 0, nullptr);
}

static VkPipeline create_pipeline(const Vk_Pipeline_Def& def) {
    auto get_shader_stage_desc = [](VkShaderStageFlagBits stage, VkShaderModule shader_module, const char* entry) {
        VkPipelineShaderStageCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.stage = stage;
        desc.module = shader_module;
        desc.pName = entry;
        desc.pSpecializationInfo = nullptr;
        return desc;
    };

    struct Specialization_Data {
        int32_t alpha_test_func;
    } specialization_data;

    if ((def.state_bits & GLS_ATEST_BITS) == 0)
        specialization_data.alpha_test_func = 0;
    else if (def.state_bits & GLS_ATEST_GT_0)
        specialization_data.alpha_test_func = 1;
    else if (def.state_bits & GLS_ATEST_LT_80)
        specialization_data.alpha_test_func = 2;
    else if (def.state_bits & GLS_ATEST_GE_80)
        specialization_data.alpha_test_func = 3;
    else
        ri.Error(ERR_DROP, "create_pipeline: invalid alpha test state bits\n");

    VkSpecializationMapEntry specialization_entries[1];
    specialization_entries[0].constantID = 0;
    specialization_entries[0].offset = offsetof(struct Specialization_Data, alpha_test_func);
    specialization_entries[0].size = sizeof(int32_t);

    VkSpecializationInfo specialization_info;
    specialization_info.mapEntryCount = 1;
    specialization_info.pMapEntries = specialization_entries;
    specialization_info.dataSize = sizeof(Specialization_Data);
    specialization_info.pData = &specialization_data;

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages_state;

    VkShaderModule* vs_module, *fs_module;
    if (def.shader_type == Vk_Shader_Type::single_texture) {
        vs_module = def.clipping_plane ? &vk.single_texture_clipping_plane_vs :  &vk.single_texture_vs;
        fs_module = &vk.single_texture_fs;
    } else if (def.shader_type == Vk_Shader_Type::multi_texture_mul) {
        vs_module = def.clipping_plane ? &vk.multi_texture_clipping_plane_vs : &vk.multi_texture_vs;
        fs_module = &vk.multi_texture_mul_fs;
    } else if (def.shader_type == Vk_Shader_Type::multi_texture_add) {
        vs_module = def.clipping_plane ? &vk.multi_texture_clipping_plane_vs : &vk.multi_texture_vs;
        fs_module = &vk.multi_texture_add_fs;
    }
    shader_stages_state.push_back(get_shader_stage_desc(VK_SHADER_STAGE_VERTEX_BIT, *vs_module, "main"));
    shader_stages_state.push_back(get_shader_stage_desc(VK_SHADER_STAGE_FRAGMENT_BIT, *fs_module, "main"));

    if (def.state_bits & GLS_ATEST_BITS)
        shader_stages_state.back().pSpecializationInfo = &specialization_info;

    //
    // Vertex input
    //
    VkVertexInputBindingDescription bindings[4];
    // xyz array
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(vec4_t);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // color array
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(color4ub_t);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // st0 array
    bindings[2].binding = 2;
    bindings[2].stride = sizeof(vec2_t);
    bindings[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // st1 array
    bindings[3].binding = 3;
    bindings[3].stride = sizeof(vec2_t);
    bindings[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribs[4];
    // xyz
    attribs[0].location = 0;
    attribs[0].binding = 0;
    attribs[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[0].offset = 0;

    // color
    attribs[1].location = 1;
    attribs[1].binding = 1;
    attribs[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attribs[1].offset = 0;

    // st0
    attribs[2].location = 2;
    attribs[2].binding = 2;
    attribs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[2].offset = 0;

    // st1
    attribs[3].location = 3;
    attribs[3].binding = 3;
    attribs[3].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[3].offset = 0;

    VkPipelineVertexInputStateCreateInfo vertex_input_state;
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.pNext = nullptr;
    vertex_input_state.flags = 0;
    vertex_input_state.vertexBindingDescriptionCount = (def.shader_type == Vk_Shader_Type::single_texture) ? 3 : 4;
    vertex_input_state.pVertexBindingDescriptions = bindings;
    vertex_input_state.vertexAttributeDescriptionCount = (def.shader_type == Vk_Shader_Type::single_texture) ? 3 : 4;
    vertex_input_state.pVertexAttributeDescriptions = attribs;

    //
    // Primitive assembly.
    //
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
    input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.pNext = nullptr;
    input_assembly_state.flags = 0;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    //
    // Viewport.
    //
    VkPipelineViewportStateCreateInfo viewport_state;
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.pNext = nullptr;
    viewport_state.flags = 0;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = nullptr; // dynamic viewport state
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = nullptr; // dynamic scissor state

    //
    // Rasterization.
    //
    VkPipelineRasterizationStateCreateInfo rasterization_state;
    rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.pNext = nullptr;
    rasterization_state.flags = 0;
    rasterization_state.depthClampEnable = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode = (def.state_bits & GLS_POLYMODE_LINE) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;

    if (def.face_culling == CT_TWO_SIDED)
        rasterization_state.cullMode = VK_CULL_MODE_NONE;
    else if (def.face_culling == CT_FRONT_SIDED)
        rasterization_state.cullMode = (def.mirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
    else if (def.face_culling == CT_BACK_SIDED)
        rasterization_state.cullMode = (def.mirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT);
    else
        ri.Error(ERR_DROP, "create_pipeline: invalid face culling mode\n");

    rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE; // Q3 defaults to clockwise vertex order

    rasterization_state.depthBiasEnable = def.polygon_offset ? VK_TRUE : VK_FALSE;
    rasterization_state.depthBiasConstantFactor = 0.0f; // dynamic depth bias state
    rasterization_state.depthBiasClamp = 0.0f; // dynamic depth bias state
    rasterization_state.depthBiasSlopeFactor = 0.0f; // dynamic depth bias state
    rasterization_state.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state;
    multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.pNext = nullptr;
    multisample_state.flags = 0;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.sampleShadingEnable = VK_FALSE;
    multisample_state.minSampleShading = 1.0f;
    multisample_state.pSampleMask = nullptr;
    multisample_state.alphaToCoverageEnable = VK_FALSE;
    multisample_state.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.pNext = nullptr;
    depth_stencil_state.flags = 0;
    depth_stencil_state.depthTestEnable = (def.state_bits & GLS_DEPTHTEST_DISABLE) ? VK_FALSE : VK_TRUE;
    depth_stencil_state.depthWriteEnable = (def.state_bits & GLS_DEPTHMASK_TRUE) ? VK_TRUE : VK_FALSE;
    depth_stencil_state.depthCompareOp = (def.state_bits & GLS_DEPTHFUNC_EQUAL) ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable = VK_FALSE;
    depth_stencil_state.front = {};
    depth_stencil_state.back = {};
    depth_stencil_state.minDepthBounds = 0.0;
    depth_stencil_state.maxDepthBounds = 0.0;

    VkPipelineColorBlendAttachmentState attachment_blend_state = {};
    attachment_blend_state.blendEnable = (def.state_bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) ? VK_TRUE : VK_FALSE;
    attachment_blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    if (attachment_blend_state.blendEnable) {
        switch (def.state_bits & GLS_SRCBLEND_BITS) {
            case GLS_SRCBLEND_ZERO:
                attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
                break;
            case GLS_SRCBLEND_ONE:
                attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                break;
            case GLS_SRCBLEND_DST_COLOR:
                attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
                break;
            case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
                attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
                break;
            case GLS_SRCBLEND_SRC_ALPHA:
                attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                break;
            case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
                attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                break;
            case GLS_SRCBLEND_DST_ALPHA:
                attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
                break;
            case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
                attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
                break;
            case GLS_SRCBLEND_ALPHA_SATURATE:
                attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
                break;
            default:
                ri.Error( ERR_DROP, "create_pipeline: invalid src blend state bits\n" );
                break;
        }
        switch (def.state_bits & GLS_DSTBLEND_BITS) {
            case GLS_DSTBLEND_ZERO:
                attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
                break;
            case GLS_DSTBLEND_ONE:
                attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                break;
            case GLS_DSTBLEND_SRC_COLOR:
                attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
                break;
            case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
                attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
                break;
            case GLS_DSTBLEND_SRC_ALPHA:
                attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                break;
            case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
                attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                break;
            case GLS_DSTBLEND_DST_ALPHA:
                attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
                break;
            case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
                attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
                break;
            default:
                ri.Error( ERR_DROP, "create_pipeline: invalid dst blend state bits\n" );
                break;
        }

        attachment_blend_state.srcAlphaBlendFactor = attachment_blend_state.srcColorBlendFactor;
        attachment_blend_state.dstAlphaBlendFactor = attachment_blend_state.dstColorBlendFactor;
        attachment_blend_state.colorBlendOp = VK_BLEND_OP_ADD;
        attachment_blend_state.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo blend_state;
    blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_state.pNext = nullptr;
    blend_state.flags = 0;
    blend_state.logicOpEnable = VK_FALSE;
    blend_state.logicOp = VK_LOGIC_OP_COPY;
    blend_state.attachmentCount = 1;
    blend_state.pAttachments = &attachment_blend_state;
    blend_state.blendConstants[0] = 0.0f;
    blend_state.blendConstants[1] = 0.0f;
    blend_state.blendConstants[2] = 0.0f;
    blend_state.blendConstants[3] = 0.0f;

    VkPipelineDynamicStateCreateInfo dynamic_state;
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.pNext = nullptr;
    dynamic_state.flags = 0;
    dynamic_state.dynamicStateCount = 3;
    VkDynamicState dynamic_state_array[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
    dynamic_state.pDynamicStates = dynamic_state_array;

    VkGraphicsPipelineCreateInfo create_info;
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.pNext = nullptr;
    create_info.flags = 0;
    create_info.stageCount = static_cast<uint32_t>(shader_stages_state.size());
    create_info.pStages = shader_stages_state.data();
    create_info.pVertexInputState = &vertex_input_state;
    create_info.pInputAssemblyState = &input_assembly_state;
    create_info.pTessellationState = nullptr;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &rasterization_state;
    create_info.pMultisampleState = &multisample_state;
    create_info.pDepthStencilState = &depth_stencil_state;
    create_info.pColorBlendState = &blend_state;
    create_info.pDynamicState = &dynamic_state;
    create_info.layout = vk.pipeline_layout;
    create_info.renderPass = vk.render_pass;
    create_info.subpass = 0;
    create_info.basePipelineHandle = VK_NULL_HANDLE;
    create_info.basePipelineIndex = -1;

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &create_info, nullptr, &pipeline));
    return pipeline;
}

struct Timer {
    using Clock = std::chrono::high_resolution_clock;
    using Second = std::chrono::duration<double, std::ratio<1>>;

    Clock::time_point start = Clock::now();
    double Elapsed_Seconds() const {
        const auto duration = Clock::now() - start;
        double seconds = std::chrono::duration_cast<Second>(duration).count();
        return seconds;
    }
};

VkSampler vk_find_sampler(const Vk_Sampler_Def& def) {
    // Look for sampler among existing samplers.
    for (int i = 0; i < vk_resources.num_samplers; i++) {
        const auto& cur_def = vk_resources.sampler_defs[i];

        if (cur_def.repeat_texture == def.repeat_texture &&
            cur_def.gl_mag_filter == def.gl_mag_filter && 
            cur_def.gl_min_filter == def.gl_min_filter)
        {
            return vk_resources.samplers[i];
        }
    }

    // Create new sampler.
    if (vk_resources.num_samplers >= MAX_VK_SAMPLERS) {
        ri.Error(ERR_DROP, "vk_find_sampler: MAX_VK_SAMPLERS hit\n");
    }

    VkSamplerAddressMode address_mode = def.repeat_texture ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    VkFilter mag_filter;
    if (def.gl_mag_filter == GL_NEAREST) {
        mag_filter = VK_FILTER_NEAREST;
    } else if (def.gl_mag_filter == GL_LINEAR) {
        mag_filter = VK_FILTER_LINEAR;
    } else {
        ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_mag_filter");
    }

    VkFilter min_filter;
    VkSamplerMipmapMode mipmap_mode;
    bool max_lod_0_25 = false; // used to emulate OpenGL's GL_LINEAR/GL_NEAREST minification filter
    if (def.gl_min_filter == GL_NEAREST) {
        min_filter = VK_FILTER_NEAREST;
        mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        max_lod_0_25 = true;
    } else if (def.gl_min_filter == GL_LINEAR) {
        min_filter = VK_FILTER_LINEAR;
        mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        max_lod_0_25 = true;
    } else if (def.gl_min_filter == GL_NEAREST_MIPMAP_NEAREST) {
        min_filter = VK_FILTER_NEAREST;
        mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    } else if (def.gl_min_filter == GL_LINEAR_MIPMAP_NEAREST) {
        min_filter = VK_FILTER_LINEAR;
        mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    } else if (def.gl_min_filter == GL_NEAREST_MIPMAP_LINEAR) {
        min_filter = VK_FILTER_NEAREST;
        mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    } else if (def.gl_min_filter == GL_LINEAR_MIPMAP_LINEAR) {
        min_filter = VK_FILTER_LINEAR;
        mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    } else {
        ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_min_filter");
    }

    VkSamplerCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.magFilter = mag_filter;
    desc.minFilter = min_filter;
    desc.mipmapMode = mipmap_mode;
    desc.addressModeU = address_mode;
    desc.addressModeV = address_mode;
    desc.addressModeW = address_mode;
    desc.mipLodBias = 0.0f;
    desc.anisotropyEnable = VK_TRUE;
    desc.maxAnisotropy = 1;
    desc.compareEnable = VK_FALSE;
    desc.compareOp = VK_COMPARE_OP_ALWAYS;
    desc.minLod = 0.0f;
    desc.maxLod = max_lod_0_25 ? 0.25f : 12.0f;
    desc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    desc.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler;
    VK_CHECK(vkCreateSampler(vk.device, &desc, nullptr, &sampler));

    vk_resources.sampler_defs[vk_resources.num_samplers] = def;
    vk_resources.samplers[vk_resources.num_samplers] = sampler;
    vk_resources.num_samplers++;
    return sampler;
}

VkPipeline vk_find_pipeline(const Vk_Pipeline_Def& def) {
    for (int i = 0; i < vk_resources.num_pipelines; i++) {
        const auto& cur_def = vk_resources.pipeline_defs[i];

        if (cur_def.shader_type == def.shader_type &&
            cur_def.state_bits == def.state_bits &&
            cur_def.face_culling == def.face_culling &&
            cur_def.polygon_offset == def.polygon_offset &&
            cur_def.clipping_plane == def.clipping_plane &&
            cur_def.mirror == def.mirror)
        {
            return vk_resources.pipelines[i];
        }
    }

    if (vk_resources.num_pipelines >= MAX_VK_PIPELINES) {
        ri.Error(ERR_DROP, "vk_find_pipeline: MAX_VK_PIPELINES hit\n");
    }

    Timer t;
    VkPipeline pipeline = create_pipeline(def);
    pipeline_create_time += t.Elapsed_Seconds();

    vk_resources.pipeline_defs[vk_resources.num_pipelines] = def;
    vk_resources.pipelines[vk_resources.num_pipelines] = pipeline;
    vk_resources.num_pipelines++;
    return pipeline;
}

VkRect2D vk_get_viewport_rect() {
    VkRect2D r;

    if (backEnd.projection2D) {
        r.offset.x = 0.0f;
        r.offset.y = 0.0f;
        r.extent.width = glConfig.vidWidth;
        r.extent.height = glConfig.vidHeight;
    } else {
        r.offset.x = backEnd.viewParms.viewportX;
        if (r.offset.x < 0)
            r.offset.x = 0;

        r.offset.y = glConfig.vidHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight);
        if (r.offset.y < 0)
            r.offset.y = 0;

        r.extent.width = backEnd.viewParms.viewportWidth;
        if (r.offset.x + r.extent.width > glConfig.vidWidth)
            r.extent.width = glConfig.vidWidth - r.offset.x;

        r.extent.height = backEnd.viewParms.viewportHeight;
        if (r.offset.y + r.extent.height > glConfig.vidHeight)
            r.extent.height = glConfig.vidHeight - r.offset.y;
    }
    return r;
}

static void get_mvp_transform(float* mvp) {
    if (backEnd.projection2D) {
        float mvp0 = 2.0f / glConfig.vidWidth;
        float mvp5 = 2.0f / glConfig.vidHeight;

        mvp[0]  =  mvp0; mvp[1]  =  0.0f; mvp[2]  = 0.0f; mvp[3]  = 0.0f;
        mvp[4]  =  0.0f; mvp[5]  =  mvp5; mvp[6]  = 0.0f; mvp[7]  = 0.0f;
        mvp[8]  =  0.0f; mvp[9]  =  0.0f; mvp[10] = 1.0f; mvp[11] = 0.0f;
        mvp[12] = -1.0f; mvp[13] = -1.0f; mvp[14] = 0.0f; mvp[15] = 1.0f;

    } else {
        const float* p = backEnd.viewParms.projectionMatrix;

        // update q3's proj matrix (opengl) to vulkan conventions: z - [0, 1] instead of [-1, 1] and invert y direction
        float zNear	= r_znear->value;
        float zFar = backEnd.viewParms.zFar;
        float P10 = -zFar / (zFar - zNear);
        float P14 = -zFar*zNear / (zFar - zNear);
        float P5 = -p[5];

        float proj[16] = {
            p[0],  p[1],  p[2], p[3],
            p[4],  P5,    p[6], p[7],
            p[8],  p[9],  P10,  p[11],
            p[12], p[13], P14,  p[15]
        };

        myGlMultMatrix(backEnd.or.modelMatrix, proj, mvp);
    }
}

void vk_bind_resources_shared_between_stages() {
    // xyz
    {
        if ((vk.xyz_elements + tess.numVertexes) * sizeof(vec4_t) > XYZ_SIZE)
            ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (xyz)\n");

        byte* dst = vk.vertex_buffer_ptr + XYZ_OFFSET + vk.xyz_elements * sizeof(vec4_t);
        Com_Memcpy(dst, tess.xyz, tess.numVertexes * sizeof(vec4_t));
    }

    std::size_t indexes_size = tess.numIndexes * sizeof(uint32_t);        

    // update index buffer
    {
        if (vk.index_buffer_offset + indexes_size > INDEX_BUFFER_SIZE)
            ri.Error(ERR_DROP, "vk_draw: index buffer overflow\n");

        byte* dst = vk.index_buffer_ptr + vk.index_buffer_offset;
        Com_Memcpy(dst, tess.indexes, indexes_size);
    }

    // configure indexes stream
    vkCmdBindIndexBuffer(vk.command_buffer, vk.index_buffer, vk.index_buffer_offset, VK_INDEX_TYPE_UINT32);
    vk.index_buffer_offset += indexes_size;

    //
    // Specify push constants.
    //
    float push_constants[16 + 12 + 4]; // mvp transform + eye transform + clipping plane in eye space

    get_mvp_transform(push_constants);
    int push_constants_size = 64;

    if (backEnd.viewParms.isPortal) {
        // Eye space transform.
        // NOTE: backEnd.or.modelMatrix incorporates s_flipMatrix, so it should be taken into account 
        // when computing clipping plane too.
        float* eye_xform = push_constants + 16;
        for (int i = 0; i < 12; i++) {
            eye_xform[i] = backEnd.or.modelMatrix[(i%4)*4 + i/4 ];
        }

        // Clipping plane in eye coordinates.
        float world_plane[4];
        world_plane[0] = backEnd.viewParms.portalPlane.normal[0];
        world_plane[1] = backEnd.viewParms.portalPlane.normal[1];
        world_plane[2] = backEnd.viewParms.portalPlane.normal[2];
        world_plane[3] = backEnd.viewParms.portalPlane.dist;

        float eye_plane[4];
        eye_plane[0] = DotProduct (backEnd.viewParms.or.axis[0], world_plane);
        eye_plane[1] = DotProduct (backEnd.viewParms.or.axis[1], world_plane);
        eye_plane[2] = DotProduct (backEnd.viewParms.or.axis[2], world_plane);
        eye_plane[3] = DotProduct (world_plane, backEnd.viewParms.or.origin) - world_plane[3];

        // Apply s_flipMatrix to be in the same coordinate system as eye_xfrom.
        push_constants[28] = -eye_plane[1];
        push_constants[29] =  eye_plane[2];
        push_constants[30] = -eye_plane[0];
        push_constants[31] =  eye_plane[3];

        push_constants_size += 64;
    }

    vkCmdPushConstants(vk.command_buffer, vk.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, push_constants_size, push_constants);
}

void vk_bind_stage_specific_resources(VkPipeline pipeline, bool multitexture, bool sky) {
    //
    // Specify color/st for each draw call since they are regenerated for each Q3 shader's stage.
    // xyz are specified only once for all stages.
    //

    // color
    {
        if ((vk.color_st_elements + tess.numVertexes) * sizeof(color4ub_t) > COLOR_SIZE)
            ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (color)\n");

        byte* dst = vk.vertex_buffer_ptr + COLOR_OFFSET + vk.color_st_elements * sizeof(color4ub_t);
        Com_Memcpy(dst, tess.svars.colors, tess.numVertexes * sizeof(color4ub_t));
    }

    // st0
    {
        if ((vk.color_st_elements + tess.numVertexes) * sizeof(vec2_t) > ST0_SIZE)
            ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (st0)\n");

        byte* dst = vk.vertex_buffer_ptr + ST0_OFFSET + vk.color_st_elements * sizeof(vec2_t);
        Com_Memcpy(dst, tess.svars.texcoords[0], tess.numVertexes * sizeof(vec2_t));
    }

    // st1
    if (multitexture) {
        if ((vk.color_st_elements + tess.numVertexes) * sizeof(vec2_t) > ST1_SIZE)
            ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (st1)\n");

        byte* dst = vk.vertex_buffer_ptr + ST1_OFFSET + vk.color_st_elements * sizeof(vec2_t);
        Com_Memcpy(dst, tess.svars.texcoords[1], tess.numVertexes * sizeof(vec2_t));
    }

    // configure vertex data stream
    VkBuffer bufs[4] = { vk.vertex_buffer, vk.vertex_buffer, vk.vertex_buffer, vk.vertex_buffer }; // turtles all the way down
    VkDeviceSize offs[4] = {
        XYZ_OFFSET   + vk.xyz_elements * sizeof(vec4_t),
        COLOR_OFFSET + vk.color_st_elements * sizeof(color4ub_t),
        ST0_OFFSET   + vk.color_st_elements * sizeof(vec2_t),
        ST1_OFFSET   + vk.color_st_elements * sizeof(vec2_t)
    };

    vkCmdBindVertexBuffers(vk.command_buffer, 0, multitexture ? 4 : 3, bufs, offs);
    vk.color_st_elements += tess.numVertexes;

    // bind descriptor sets
    uint32_t set_count = multitexture ? 2 : 1;
    VkDescriptorSet sets[2];
    sets[0] = vk_resources.images[glState.vk_current_images[0]->index].descriptor_set;
    if (multitexture) {
        sets[1] = vk_resources.images[glState.vk_current_images[1]->index].descriptor_set;
    }
    vkCmdBindDescriptorSets(vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0, set_count, sets, 0, nullptr);

    // bind pipeline
    vkCmdBindPipeline(vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // configure pipeline's dynamic state
    VkRect2D r = vk_get_viewport_rect();
    vkCmdSetScissor(vk.command_buffer, 0, 1, &r);

    VkViewport viewport;
    viewport.x = (float)r.offset.x;
    viewport.y = (float)r.offset.y;
    viewport.width = (float)r.extent.width;
    viewport.height = (float)r.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

	if (backEnd.currentEntity->e.renderfx & RF_DEPTHHACK) {
		viewport.maxDepth = 0.3f;
	}

	if (sky) {
		if (r_showsky->integer) {
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 0.0f;
		} else {
			viewport.minDepth = 1.0f;
			viewport.maxDepth = 1.0f;
		}
	}

    vkCmdSetViewport(vk.command_buffer, 0, 1, &viewport);

    if (tess.shader->polygonOffset) {
        vkCmdSetDepthBias(vk.command_buffer, r_offsetUnits->value, 0.0f, r_offsetFactor->value);
    }
}

void vk_begin_frame() {
	if (r_logFile->integer)
		fprintf(vk_log_file, "vk_begin_frame\n");

    VK_CHECK(vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX, vk.image_acquired, VK_NULL_HANDLE, &vk.swapchain_image_index));

    VK_CHECK(vkWaitForFences(vk.device, 1, &vk.rendering_finished_fence, VK_FALSE, 1e9));
    VK_CHECK(vkResetFences(vk.device, 1, &vk.rendering_finished_fence));

	VkCommandBufferBeginInfo begin_info;
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = nullptr;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = nullptr;

    VK_CHECK(vkBeginCommandBuffer(vk.command_buffer, &begin_info));

	VkClearValue clear_values[2];
	/// ignore clear_values[0] which corresponds to color attachment
	clear_values[1].depthStencil.depth = 1.0;
	clear_values[1].depthStencil.stencil = 0;

	VkRenderPassBeginInfo render_pass_begin_info;
	render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_begin_info.pNext = nullptr;
	render_pass_begin_info.renderPass = vk.render_pass;
	render_pass_begin_info.framebuffer = vk.framebuffers[vk.swapchain_image_index];
	render_pass_begin_info.renderArea.offset = { 0, 0 };
	render_pass_begin_info.renderArea.extent = { (uint32_t)glConfig.vidWidth, (uint32_t)glConfig.vidHeight };
	render_pass_begin_info.clearValueCount = 2;
	render_pass_begin_info.pClearValues = clear_values;

	vkCmdBeginRenderPass(vk.command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	vk.xyz_elements = 0;
	vk.color_st_elements = 0;
	vk.index_buffer_offset = 0;

	glState.vk_dirty_attachments = false;
}

void vk_end_frame() {
    if (r_logFile->integer)
        fprintf(vk_log_file, "end_frame (vb_size %d, ib_size %d)\n", 
            vk.xyz_elements * 16 + vk.color_st_elements * 20,
            (int)vk.index_buffer_offset);

    vkCmdEndRenderPass(vk.command_buffer);

    VK_CHECK(vkEndCommandBuffer(vk.command_buffer));

    if (r_logFile->integer) {
        fprintf(vk_log_file, "present\n");
        fflush(vk_log_file);
    }

    VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &vk.image_acquired;
    submit_info.pWaitDstStageMask = &wait_dst_stage_mask;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &vk.command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &vk.rendering_finished;

    VK_CHECK(vkQueueSubmit(vk.queue, 1, &submit_info, vk.rendering_finished_fence));

    VkPresentInfoKHR present_info;
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &vk.rendering_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &vk.swapchain;
    present_info.pImageIndices = &vk.swapchain_image_index;
    present_info.pResults = nullptr;
    VK_CHECK(vkQueuePresentKHR(vk.queue, &present_info));
}
