#pragma once

#include "vk.h"
#include <vector>

// NOTE: in this implementation I do memory allocation for each allocation request.
// TODO: sub-allocate from larger chunks and return chunk handle plus offset withing corresponding chunk.
class Device_Memory_Allocator {
public:
    void deallocate_all();

    VkDeviceMemory allocate_staging_memory(VkBuffer buffer);

private:
    std::vector<VkDeviceMemory> chunks;
};

Device_Memory_Allocator* get_allocator();
