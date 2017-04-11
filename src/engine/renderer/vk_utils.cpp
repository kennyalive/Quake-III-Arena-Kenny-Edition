#include "vk_allocator.h"
#include "vk_resource_manager.h"
#include "vk.h"
#include "vk_utils.h"
#include <iostream>
#include <fstream>

#include "tr_local.h"

void check_vk_result(VkResult result, const std::string& functionName) {
    if (result < 0) {
        error(functionName + " has returned error code with value " + std::to_string(result));
    }
}

void error(const std::string& message) {
    std::cout << message << std::endl;
    throw std::runtime_error(message);
}

Shader_Module::Shader_Module(uint8_t bytes[], int size) {
    std::vector<uint8_t> data(bytes, bytes + size);
    if (data.size() % 4 != 0)
        error("SPIR-V binary file size is not multiple of 4");

    VkShaderModuleCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.codeSize = data.size();
    desc.pCode = reinterpret_cast<const uint32_t*>(data.data());

    VkResult result = vkCreateShaderModule(vk_instance.device, &desc, nullptr, &handle);
    check_vk_result(result, "vkCreateShaderModule");
}

Shader_Module::~Shader_Module() {
    vkDestroyShaderModule(vk_instance.device, handle, nullptr);
}

void record_and_run_commands(VkCommandPool command_pool, VkQueue queue, std::function<void(VkCommandBuffer)> recorder) {

    VkCommandBufferAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.pNext = nullptr;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    VkResult result = vkAllocateCommandBuffers(vk_instance.device, &alloc_info, &command_buffer);
    check_vk_result(result, "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin_info;
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = nullptr;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = nullptr;

    result = vkBeginCommandBuffer(command_buffer, &begin_info);
    check_vk_result(result, "vkBeginCommandBuffer");
    recorder(command_buffer);
    result = vkEndCommandBuffer(command_buffer);
    check_vk_result(result, "vkEndCommandBuffer");

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

    result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    check_vk_result(result, "vkQueueSubmit");
    result = vkQueueWaitIdle(queue);
    check_vk_result(result, "vkQueueWaitIdle");
    vkFreeCommandBuffers(vk_instance.device, command_pool, 1, &command_buffer);
}

static bool has_depth_component(VkFormat format) {
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
}

static bool has_stencil_component(VkFormat format) {
    switch (format) {
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

void record_image_layout_transition(VkCommandBuffer command_buffer, VkImage image, VkFormat format,
    VkAccessFlags src_access_flags, VkImageLayout old_layout, VkAccessFlags dst_access_flags, VkImageLayout new_layout) {

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

VkImage create_texture(int image_width, int image_height, VkFormat format) {
    VkImageCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.imageType = VK_IMAGE_TYPE_2D;
    desc.format = format;
    desc.extent.width = image_width;
    desc.extent.height = image_height;
    desc.extent.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = nullptr;
    desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = get_resource_manager()->create_image(desc);

    VkDeviceMemory memory = get_allocator()->allocate_memory(image);
    VkResult result = vkBindImageMemory(vk_instance.device, image, memory, 0);
    check_vk_result(result, "vkBindImageMemory");
    return image;
}

VkImage create_staging_texture(int image_width, int image_height, VkFormat format, const uint8_t* pixels, int bytes_per_pixel) {
    VkImageCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.imageType = VK_IMAGE_TYPE_2D;
    desc.format = format;
    desc.extent.width = image_width;
    desc.extent.height = image_height;
    desc.extent.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    desc.tiling = VK_IMAGE_TILING_LINEAR;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = nullptr;
    desc.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    VkImage image;
    VkResult result = vkCreateImage(vk_instance.device, &desc, nullptr, &image);
    check_vk_result(result, "vkCreateImage");

    get_allocator()->get_shared_staging_memory().ensure_allocation_for_object(image);
    VkDeviceMemory memory = get_allocator()->get_shared_staging_memory().get_handle();
    result = vkBindImageMemory(vk_instance.device, image, memory, 0);
    check_vk_result(result, "vkBindImageMemory");

    VkImageSubresource staging_image_subresource;
    staging_image_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    staging_image_subresource.mipLevel = 0;
    staging_image_subresource.arrayLayer = 0;
    VkSubresourceLayout staging_image_layout;
    vkGetImageSubresourceLayout(vk_instance.device, image, &staging_image_subresource, &staging_image_layout);

    void* data;
    result = vkMapMemory(vk_instance.device, memory, 0, staging_image_layout.size, 0, &data);
    check_vk_result(result, "vkMapMemory");

    const int bytes_per_row = image_width * bytes_per_pixel;
    if (staging_image_layout.rowPitch == bytes_per_row) {
        memcpy(data, pixels, bytes_per_row * image_height);
    } else {
        auto bytes = static_cast<uint8_t*>(data);
        for (int i = 0; i < image_height; i++) {
            memcpy(&bytes[i * staging_image_layout.rowPitch], &pixels[i * bytes_per_row], bytes_per_row);
        }
    }
    vkUnmapMemory(vk_instance.device, memory);
    return image;
}

VkImage create_depth_attachment_image(int image_width, int image_height, VkFormat format) {
    VkImageCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.imageType = VK_IMAGE_TYPE_2D;
    desc.format = format;
    desc.extent.width = image_width;
    desc.extent.height = image_height;
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

    VkImage image = get_resource_manager()->create_image(desc);

    VkDeviceMemory memory = get_allocator()->allocate_memory(image);
    VkResult result = vkBindImageMemory(vk_instance.device, image, memory, 0);
    check_vk_result(result, "vkBindImageMemory");
    return image;
}

VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags) {
    VkImageViewCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.image = image;
    desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    desc.format = format;
    desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.subresourceRange.aspectMask = aspect_flags;
    desc.subresourceRange.baseMipLevel = 0;
    desc.subresourceRange.levelCount = 1;
    desc.subresourceRange.baseArrayLayer = 0;
    desc.subresourceRange.layerCount = 1;

    return get_resource_manager()->create_image_view(desc);
}

VkBuffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.size = size;
    desc.usage = usage;
    desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = nullptr;

    VkBuffer buffer = get_resource_manager()->create_buffer(desc);

    VkDeviceMemory memory = get_allocator()->allocate_memory(buffer);
    VkResult result = vkBindBufferMemory(vk_instance.device, buffer, memory, 0);
    check_vk_result(result, "vkBindBufferMemory");
    return buffer;
}

VkBuffer create_staging_buffer(VkDeviceSize size, const void* data) {
    VkBufferCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.size = size;
    desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = nullptr;

    VkBuffer buffer;
    VkResult result = vkCreateBuffer(vk_instance.device, &desc, nullptr, &buffer);
    check_vk_result(result, "vkCreateBuffer");

    get_allocator()->get_shared_staging_memory().ensure_allocation_for_object(buffer);
    VkDeviceMemory memory = get_allocator()->get_shared_staging_memory().get_handle();
    result = vkBindBufferMemory(vk_instance.device, buffer, memory, 0);
    check_vk_result(result, "vkBindBufferMemory");

    void* buffer_data;
    result = vkMapMemory(vk_instance.device, memory, 0, size, 0, &buffer_data);
    check_vk_result(result, "vkMapMemory");
    memcpy(buffer_data, data, size);
    vkUnmapMemory(vk_instance.device, memory);
    return buffer;
}

VkBuffer create_permanent_staging_buffer(VkDeviceSize size, VkDeviceMemory& memory) {
    VkBufferCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.size = size;
    desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = nullptr;

    VkBuffer buffer = get_resource_manager()->create_buffer(desc);

    memory = get_allocator()->allocate_staging_memory(buffer);
    VkResult result = vkBindBufferMemory(vk_instance.device, buffer, memory, 0);
    check_vk_result(result, "vkBindBufferMemory");
    return buffer;
}
