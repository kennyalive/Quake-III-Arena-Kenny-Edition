#include "vk_allocator.h"
#include "tr_local.h"

static Device_Memory_Allocator allocator;

Device_Memory_Allocator* get_allocator() {
    return &allocator;
}

void Device_Memory_Allocator::deallocate_all() {
    for (auto chunk : chunks) {
        vkFreeMemory(vk.device, chunk, nullptr);
    }
    chunks.clear();
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

VkDeviceMemory Device_Memory_Allocator::allocate_staging_memory(VkBuffer buffer) {
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(vk.device, buffer, &memory_requirements);

    VkMemoryAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = nullptr;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(vk.physical_device, memory_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory chunk;
    VK_CHECK(vkAllocateMemory(vk.device, &alloc_info, nullptr, &chunk));
    chunks.push_back(chunk);
    return chunk;
}
