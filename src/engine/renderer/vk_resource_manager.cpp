#include "vk_resource_manager.h"

static Resource_Manager resource_manager;

Resource_Manager* get_resource_manager() {
    return &resource_manager;
}

void Resource_Manager::initialize(VkDevice device) {
    this->device = device;
}

void Resource_Manager::release_resources() {
    for (auto semaphore : semaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    semaphores.clear();

    for (auto descriptor_pool : descriptor_pools) {
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    }
    descriptor_pools.clear();

    for (auto buffer : buffers) {
        vkDestroyBuffer(device, buffer, nullptr);
    }
    buffers.clear();

    for (auto sampler : samplers) {
        vkDestroySampler(device, sampler, nullptr);
    }
    samplers.clear();

    for (auto descriptor_set_layout : descriptor_set_layouts) {
        vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
    }
    descriptor_set_layouts.clear();

    for (auto pipeline_layout : pipeline_layouts) {
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    }
    pipeline_layouts.clear();
}

VkSemaphore Resource_Manager::create_semaphore() {
    VkSemaphoreCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;

    VkSemaphore semaphore;
    VkResult result = vkCreateSemaphore(device, &desc, nullptr, &semaphore);
    check_vk_result(result, "vkCreateSemaphore");
    semaphores.push_back(semaphore);
    return semaphore;
}

VkDescriptorPool Resource_Manager::create_descriptor_pool(const VkDescriptorPoolCreateInfo& desc) {
    VkDescriptorPool descriptor_pool;
    VkResult result = vkCreateDescriptorPool(device, &desc, nullptr, &descriptor_pool);
    check_vk_result(result, "vkCreateDescriptorPool");
    descriptor_pools.push_back(descriptor_pool);
    return descriptor_pool;
}

VkBuffer Resource_Manager::create_buffer(const VkBufferCreateInfo& desc) {
    VkBuffer buffer;
    VkResult result = vkCreateBuffer(device, &desc, nullptr, &buffer);
    check_vk_result(result, "vkCreateBuffer");
    buffers.push_back(buffer);
    return buffer;
}

VkSampler Resource_Manager::create_sampler(const VkSamplerCreateInfo& desc) {
    VkSampler sampler;
    VkResult result = vkCreateSampler(device, &desc, nullptr, &sampler);
    check_vk_result(result, "vkCreateSampler");
    samplers.push_back(sampler);
    return sampler;
}

VkDescriptorSetLayout Resource_Manager::create_descriptor_set_layout(const VkDescriptorSetLayoutCreateInfo& desc) {
    VkDescriptorSetLayout descriptor_set_layout;
    VkResult result = vkCreateDescriptorSetLayout(device, &desc, nullptr, &descriptor_set_layout);
    check_vk_result(result, "vkCreateDescriptorSetLayout");
    descriptor_set_layouts.push_back(descriptor_set_layout);
    return descriptor_set_layout;
}

VkPipelineLayout Resource_Manager::create_pipeline_layout(const VkPipelineLayoutCreateInfo& desc) {
    VkPipelineLayout pipeline_layout;
    VkResult result = vkCreatePipelineLayout(device, &desc, nullptr, &pipeline_layout);
    check_vk_result(result, "vkCreatePipelineLayout");
    pipeline_layouts.push_back(pipeline_layout);
    return pipeline_layout;
}
