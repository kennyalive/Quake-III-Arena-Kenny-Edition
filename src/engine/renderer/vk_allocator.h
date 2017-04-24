#pragma once

#include "vk.h"
#include <vector>

// NOTE: in this implementation I do memory allocation for each allocation request.
// TODO: sub-allocate from larger chunks and return chunk handle plus offset withing corresponding chunk.
class Device_Memory_Allocator {
public:
    void deallocate_all();

    VkDeviceMemory allocate_staging_memory(VkBuffer buffer);

    void ensure_allocation_for_staging_buffer(VkBuffer buffer);
    VkDeviceMemory get_staging_buffer_memory() const;

private:
    std::vector<VkDeviceMemory> chunks;

    VkDeviceMemory staging_buffer_memory;
    VkDeviceSize staging_buffer_size = 0;
};

Device_Memory_Allocator* get_allocator();
