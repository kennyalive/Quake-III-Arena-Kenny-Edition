#include "vk_allocator.h"
#include "vk_utils.h"

static Device_Memory_Allocator allocator;

Device_Memory_Allocator* get_allocator() {
    return &allocator;
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
    error("failed to find matching memory type with requested properties");
    return -1;
}

void Shared_Staging_Memory::initialize(VkPhysicalDevice physical_device, VkDevice device) {
    this->physical_device = physical_device;
    this->device = device;
}

void Shared_Staging_Memory::deallocate_all() {
    if (handle != VK_NULL_HANDLE) {
        vkFreeMemory(device, handle, nullptr);
    }
    handle = VK_NULL_HANDLE;
    size = 0;
    memory_type_index = -1;
}

void Shared_Staging_Memory::ensure_allocation_for_object(VkImage image) {
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(device, image, &memory_requirements);
    ensure_allocation(memory_requirements);
}

void Shared_Staging_Memory::ensure_allocation_for_object(VkBuffer buffer) {
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);
    ensure_allocation(memory_requirements);
}

VkDeviceMemory Shared_Staging_Memory::get_handle() const {
    return handle;
}

void Shared_Staging_Memory::ensure_allocation(const VkMemoryRequirements& memory_requirements) {
    uint32_t required_memory_type_index = find_memory_type(physical_device, memory_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (size < memory_requirements.size || memory_type_index != required_memory_type_index) {
        if (handle != VK_NULL_HANDLE) {
            vkFreeMemory(device, handle, nullptr);
        }
        handle = VK_NULL_HANDLE;
        size = 0;
        memory_type_index = -1;

        VkMemoryAllocateInfo alloc_info;
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.pNext = nullptr;
        alloc_info.allocationSize = memory_requirements.size;
        alloc_info.memoryTypeIndex = required_memory_type_index;

        VkResult result = vkAllocateMemory(device, &alloc_info, nullptr, &handle);
        check_vk_result(result, "vkAllocateMemory");
        size = memory_requirements.size;
        memory_type_index = required_memory_type_index;
    }
}

void Device_Memory_Allocator::initialize(VkPhysicalDevice physical_device, VkDevice device) {
    this->physical_device = physical_device;
    this->device = device;
    shared_staging_memory.initialize(physical_device, device);
}

void Device_Memory_Allocator::deallocate_all() {
    for (auto chunk : chunks) {
        vkFreeMemory(device, chunk, nullptr);
    }
    chunks.clear();
    shared_staging_memory.deallocate_all();
}

VkDeviceMemory Device_Memory_Allocator::allocate_memory(VkImage image) {
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(device, image, &memory_requirements);
    return allocate_memory(memory_requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

VkDeviceMemory Device_Memory_Allocator::allocate_memory(VkBuffer buffer) {
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);
    return allocate_memory(memory_requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

VkDeviceMemory Device_Memory_Allocator::allocate_staging_memory(VkImage image) {
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(device, image, &memory_requirements);
    return allocate_memory(memory_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

VkDeviceMemory Device_Memory_Allocator::allocate_staging_memory(VkBuffer buffer) {
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);
    return allocate_memory(memory_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

Shared_Staging_Memory& Device_Memory_Allocator::get_shared_staging_memory() {
    return shared_staging_memory;
}

VkDeviceMemory Device_Memory_Allocator::allocate_memory(const VkMemoryRequirements& memory_requirements, VkMemoryPropertyFlags properties) {
    VkMemoryAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = nullptr;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device, memory_requirements.memoryTypeBits, properties);

    VkDeviceMemory chunk;
    VkResult result = vkAllocateMemory(device, &alloc_info, nullptr, &chunk);
    check_vk_result(result, "vkAllocateMemory");
    chunks.push_back(chunk);
    return chunk;
}
