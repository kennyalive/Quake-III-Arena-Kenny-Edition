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

    for (auto sampler : samplers) {
        vkDestroySampler(device, sampler, nullptr);
    }
    samplers.clear();
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

VkSampler Resource_Manager::create_sampler(const VkSamplerCreateInfo& desc) {
    VkSampler sampler;
    VkResult result = vkCreateSampler(device, &desc, nullptr, &sampler);
    check_vk_result(result, "vkCreateSampler");
    samplers.push_back(sampler);
    return sampler;
}
