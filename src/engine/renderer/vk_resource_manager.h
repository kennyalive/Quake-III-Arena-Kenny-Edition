#pragma once

#include "vk_utils.h"
#include <vector>

class Resource_Manager {
public:
    void initialize(VkDevice device);
    void release_resources(); 

    VkSemaphore create_semaphore();
    VkBuffer create_buffer(const VkBufferCreateInfo& desc);
    VkSampler create_sampler(const VkSamplerCreateInfo& desc);

private:
    VkDevice device = VK_NULL_HANDLE;
    std::vector<VkSemaphore> semaphores;
    std::vector<VkBuffer> buffers;
    std::vector<VkSampler> samplers;
};

Resource_Manager* get_resource_manager();
