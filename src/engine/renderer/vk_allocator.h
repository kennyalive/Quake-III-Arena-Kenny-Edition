#pragma once

#include "vk.h"
#include <vector>

class Shared_Staging_Memory {
public:
    void initialize(VkPhysicalDevice physical_device, VkDevice device);
    void deallocate_all();

    void ensure_allocation_for_object(VkImage image);
    void ensure_allocation_for_object(VkBuffer buffer);
    VkDeviceMemory get_handle() const;

private:
    void ensure_allocation(const VkMemoryRequirements& memory_requirements);

private:
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VkDeviceMemory handle = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    uint32_t memory_type_index = -1;
};

// NOTE: in this implementation I do memory allocation for each allocation request.
// TODO: sub-allocate from larger chunks and return chunk handle plus offset withing corresponding chunk.
class Device_Memory_Allocator {
public:
    void initialize(VkPhysicalDevice physical_device, VkDevice device);
    void deallocate_all();

    VkDeviceMemory allocate_memory(VkImage image);
    VkDeviceMemory allocate_memory(VkBuffer buffer);
    VkDeviceMemory allocate_staging_memory(VkBuffer buffer);

    Shared_Staging_Memory& get_shared_staging_memory();

private:
    VkDeviceMemory allocate_memory(const VkMemoryRequirements& memory_requirements, VkMemoryPropertyFlags properties);

private:
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    std::vector<VkDeviceMemory> chunks;
    Shared_Staging_Memory shared_staging_memory;
};

Device_Memory_Allocator* get_allocator();
