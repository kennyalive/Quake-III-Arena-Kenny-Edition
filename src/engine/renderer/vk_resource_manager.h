#pragma once

#include "vk_utils.h"
#include <vector>

class Resource_Manager {
public:
    void initialize(VkDevice device);
    void release_resources();

    VkSemaphore create_semaphore();
    VkDescriptorPool create_descriptor_pool(const VkDescriptorPoolCreateInfo& desc);
    VkBuffer create_buffer(const VkBufferCreateInfo& desc);
    VkSampler create_sampler(const VkSamplerCreateInfo& desc);
    VkDescriptorSetLayout create_descriptor_set_layout(const VkDescriptorSetLayoutCreateInfo& desc);
    VkPipelineLayout create_pipeline_layout(const VkPipelineLayoutCreateInfo& desc);

private:
    VkDevice device = VK_NULL_HANDLE;
    std::vector<VkSemaphore> semaphores;
    std::vector<VkDescriptorPool> descriptor_pools;
    std::vector<VkBuffer> buffers;
    std::vector<VkSampler> samplers;
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
    std::vector<VkPipelineLayout> pipeline_layouts;
};

Resource_Manager* get_resource_manager();
