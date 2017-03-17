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

    for (auto command_pool : command_pools) {
        vkDestroyCommandPool(device, command_pool, nullptr);
    }
    command_pools.clear();

    for (auto descriptor_pool : descriptor_pools) {
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    }
    descriptor_pools.clear();

    for (auto buffer : buffers) {
        vkDestroyBuffer(device, buffer, nullptr);
    }
    buffers.clear();

    for (auto image : images) {
        vkDestroyImage(device, image, nullptr);
    }
    images.clear();

    for (auto image_view : image_views) {
        vkDestroyImageView(device, image_view, nullptr);
    }
    image_views.clear();

    for (auto sampler : samplers) {
        vkDestroySampler(device, sampler, nullptr);
    }
    samplers.clear();

    for (auto render_pass : render_passes) {
        vkDestroyRenderPass(device, render_pass, nullptr);
    }
    render_passes.clear();

    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    framebuffers.clear();

    for (auto descriptor_set_layout : descriptor_set_layouts) {
        vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
    }
    descriptor_set_layouts.clear();

    for (auto pipeline_layout : pipeline_layouts) {
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    }
    pipeline_layouts.clear();

    for (auto pipeline : graphics_pipelines) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    graphics_pipelines.clear();
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

VkCommandPool Resource_Manager::create_command_pool(const VkCommandPoolCreateInfo& desc) {
    VkCommandPool command_pool;
    VkResult result = vkCreateCommandPool(device, &desc, nullptr, &command_pool);
    check_vk_result(result, "vkCreateCommandPool");
    command_pools.push_back(command_pool);
    return command_pool;
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

VkImage Resource_Manager::create_image(const VkImageCreateInfo& desc) {
    VkImage image;
    VkResult result = vkCreateImage(device, &desc, nullptr, &image);
    check_vk_result(result, "vkCreateImage");
    images.push_back(image);
    return image;
}

VkImageView Resource_Manager::create_image_view(const VkImageViewCreateInfo& desc) {
    VkImageView image_view;
    VkResult result = vkCreateImageView(device, &desc, nullptr, &image_view);
    check_vk_result(result, "vkCreateImageView");
    image_views.push_back(image_view);
    return image_view;
}

VkSampler Resource_Manager::create_sampler(const VkSamplerCreateInfo& desc) {
    VkSampler sampler;
    VkResult result = vkCreateSampler(device, &desc, nullptr, &sampler);
    check_vk_result(result, "vkCreateSampler");
    samplers.push_back(sampler);
    return sampler;
}

VkRenderPass Resource_Manager::create_render_pass(const VkRenderPassCreateInfo& desc) {
    VkRenderPass render_pass;
    VkResult result = vkCreateRenderPass(device, &desc, nullptr, &render_pass);
    check_vk_result(result, "vkCreateRenderPass");
    render_passes.push_back(render_pass);
    return render_pass;
}

VkFramebuffer Resource_Manager::create_framebuffer(const VkFramebufferCreateInfo& desc) {
    VkFramebuffer framebuffer;
    VkResult result = vkCreateFramebuffer(device, &desc, nullptr, &framebuffer);
    check_vk_result(result, "vkCreateFramebuffer");
    framebuffers.push_back(framebuffer);
    return framebuffer;
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

VkPipeline Resource_Manager::create_graphics_pipeline(const VkGraphicsPipelineCreateInfo& desc) {
    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &desc, nullptr, &pipeline);
    check_vk_result(result, "vkCreateGraphicsPipelines");
    graphics_pipelines.push_back(pipeline);
    return pipeline;
}
