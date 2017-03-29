#include "vk_allocator.h"
#include "vk_resource_manager.h"
#include "vk_demo.h"
#include "vk.h"
#include "vk_utils.h"

#include "stb_image.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/hash.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <functional>
#include <unordered_map>

#include "tr_local.h"

struct Uniform_Buffer_Object {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 tex_coord;

    bool operator==(const Vertex& other) const {
        return pos == other.pos && color == other.color && tex_coord == other.tex_coord;
    }

    static std::array<VkVertexInputBindingDescription, 1> get_bindings() {
        VkVertexInputBindingDescription binding_desc;
        binding_desc.binding = 0;
        binding_desc.stride = sizeof(Vertex);
        binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return {binding_desc};
    }

    static std::array<VkVertexInputAttributeDescription, 3> get_attributes() {
        VkVertexInputAttributeDescription position_attrib;
        position_attrib.location = 0;
        position_attrib.binding = 0;
        position_attrib.format = VK_FORMAT_R32G32B32_SFLOAT;
        position_attrib.offset = offsetof(struct Vertex, pos);

        VkVertexInputAttributeDescription color_attrib;
        color_attrib.location = 1;
        color_attrib.binding = 0;
        color_attrib.format = VK_FORMAT_R32G32B32_SFLOAT;
        color_attrib.offset = offsetof(struct Vertex, color);

        VkVertexInputAttributeDescription tex_coord_attrib;
        tex_coord_attrib.location = 2;
        tex_coord_attrib.binding = 0;
        tex_coord_attrib.format = VK_FORMAT_R32G32_SFLOAT;
        tex_coord_attrib.offset = offsetof(struct Vertex, tex_coord);

        return {position_attrib, color_attrib, tex_coord_attrib};
    }
};

struct Model {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static Model load_model() {
    Model model;

    /*float s = 1;
    model.vertices = {
        { {-s, -s, 0}, {1, 1, 1}, {0, 1} },
        { { s, -s, 0}, {1, 1, 1}, {1, 1} },
        { { s,  s, 0}, {1, 1, 1}, {1, 0} },
        { {-s,  s, 0}, {1, 1, 1}, {0, 0} },
    };*/
    
    model.vertices = {
        { {0, glConfig.vidHeight, 0},
          {1, 1, 1}, {0, 1} },

        { {glConfig.vidWidth, glConfig.vidHeight, 0},
          {1, 1, 1}, {1, 1} },

        { {glConfig.vidWidth,  0, 0},
          {1, 1, 1}, {1, 0} },

        { {0, 0, 0},
          {1, 1, 1}, {0, 0} },
    };

    model.indices = { 0, 1, 2, 0, 2, 3 };
    return model;
}

static VkFormat find_format_with_features(VkPhysicalDevice physical_device, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);

        if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
            return format;
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
            return format;
    }
    error("failed to find format with requested features");
    return VK_FORMAT_UNDEFINED; // never get here
}

static VkFormat find_depth_format(VkPhysicalDevice physical_device) {
    return find_format_with_features(physical_device, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

FILE* logfile;

Vulkan_Demo::Vulkan_Demo(int window_width, int window_height, const SDL_SysWMinfo& window_sys_info)
: window_width(window_width) 
, window_height(window_height)
{
    logfile = fopen("vk_dev.log", "w");

    initialize_vulkan(window_sys_info);
    get_allocator()->initialize(get_physical_device(), get_device());
    get_resource_manager()->initialize(get_device());
    create_command_pool();

    image_acquired = get_resource_manager()->create_semaphore();
    rendering_finished = get_resource_manager()->create_semaphore();

    VkFenceCreateInfo fence_desc;
    fence_desc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_desc.pNext = nullptr;
    fence_desc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkResult result = vkCreateFence(get_device(), &fence_desc, nullptr, &rendering_finished_fence);
    check_vk_result(result, "vkCreateFence");

    create_descriptor_pool();

    create_uniform_buffer();

    create_texture_sampler();
    create_depth_buffer_resources();

    create_descriptor_set_layout();
    create_descriptor_set();
    create_render_pass();
    create_framebuffers();
    create_pipeline_layout();
    create_pipeline();

    upload_geometry();
    update_ubo_descriptor(descriptor_set);

    {
        VkCommandBufferAllocateInfo alloc_info;
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.pNext = nullptr;
        alloc_info.commandPool = command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(get_device(), &alloc_info, &command_buffer);
        check_vk_result(result, "vkAllocateCommandBuffers");
    }
}

Vulkan_Demo::~Vulkan_Demo() {
    VkResult result = vkDeviceWaitIdle(get_device());
    if (result < 0)
        std::cerr << "vkDeviceWaitIdle returned an error code: " + result;

    get_resource_manager()->release_resources();
    get_allocator()->deallocate_all();

    deinitialize_vulkan();
}

void Vulkan_Demo::create_command_pool() {
    VkCommandPoolCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    desc.queueFamilyIndex = get_queue_family_index();
    command_pool = get_resource_manager()->create_command_pool(desc);
}

void Vulkan_Demo::create_descriptor_pool() {
    std::array<VkDescriptorPoolSize, 2> pool_sizes;
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    pool_sizes[0].descriptorCount = 1024;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = 1024;

    VkDescriptorPoolCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.maxSets = 1024;
    desc.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    desc.pPoolSizes = pool_sizes.data();

    descriptor_pool = get_resource_manager()->create_descriptor_pool(desc);
}

void Vulkan_Demo::create_uniform_buffer() {
    auto size = static_cast<VkDeviceSize>(sizeof(Uniform_Buffer_Object)) * 1024;
    uniform_staging_buffer = create_permanent_staging_buffer(size, uniform_staging_buffer_memory);
    uniform_buffer = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(get_physical_device(), &props);
    VkDeviceSize offset_align = props.limits.minUniformBufferOffsetAlignment;
    tess_ubo_offset_step = (uint32_t)((sizeof(Uniform_Buffer_Object) + offset_align - 1) / offset_align * offset_align);
}

VkImage Vulkan_Demo::create_texture(const uint8_t* pixels, int bytes_per_pixel, int image_width, int image_height, VkImageView& image_view) {
    VkImage staging_image = create_staging_texture(image_width, image_height,
        bytes_per_pixel == 3 ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8A8_UNORM, pixels, bytes_per_pixel);

    Defer_Action destroy_staging_image([this, &staging_image]() {
        vkDestroyImage(get_device(), staging_image, nullptr);
    });

    VkImage texture_image = ::create_texture(image_width, image_height,
        bytes_per_pixel == 3 ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8A8_UNORM);

    record_and_run_commands(command_pool, get_queue(),
        [&texture_image, &staging_image, &image_width, &image_height, this](VkCommandBuffer command_buffer) {

        record_image_layout_transition(command_buffer, staging_image, VK_FORMAT_R8G8B8A8_UNORM,
            VK_ACCESS_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        record_image_layout_transition(command_buffer, texture_image, VK_FORMAT_R8G8B8A8_UNORM,
            0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // copy staging image's data to device local image
        VkImageSubresourceLayers subresource_layers;
        subresource_layers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource_layers.mipLevel = 0;
        subresource_layers.baseArrayLayer = 0;
        subresource_layers.layerCount = 1;

        VkImageCopy region;
        region.srcSubresource = subresource_layers;
        region.srcOffset = {0, 0, 0};
        region.dstSubresource = subresource_layers;
        region.dstOffset = {0, 0, 0};
        region.extent.width = image_width;
        region.extent.height = image_height;

        vkCmdCopyImage(command_buffer,
            staging_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);

        record_image_layout_transition(command_buffer, texture_image, VK_FORMAT_R8G8B8A8_UNORM,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    image_view = create_image_view(texture_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    return texture_image;
}

void Vulkan_Demo::create_texture_sampler() {
    VkSamplerCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.magFilter = VK_FILTER_LINEAR;
    desc.minFilter = VK_FILTER_LINEAR;
    desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.mipLodBias = 0.0f;
    desc.anisotropyEnable = VK_TRUE;
    desc.maxAnisotropy = 16;
    desc.compareEnable = VK_FALSE;
    desc.compareOp = VK_COMPARE_OP_ALWAYS;
    desc.minLod = 0.0f;
    desc.maxLod = 0.0f;
    desc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    desc.unnormalizedCoordinates = VK_FALSE;

    texture_image_sampler = get_resource_manager()->create_sampler(desc);
}

void Vulkan_Demo::create_depth_buffer_resources() {
    VkFormat depth_format = find_depth_format(get_physical_device());
    depth_image = create_depth_attachment_image(window_width, window_height, depth_format);
    depth_image_view = create_image_view(depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);

    record_and_run_commands(command_pool, get_queue(), [&depth_format, this](VkCommandBuffer command_buffer) {
        record_image_layout_transition(command_buffer, depth_image, depth_format, 0, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    });
}

void Vulkan_Demo::create_descriptor_set_layout() {
    std::array<VkDescriptorSetLayoutBinding, 2> descriptor_bindings;
    descriptor_bindings[0].binding = 0;
    descriptor_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descriptor_bindings[0].descriptorCount = 1;
    descriptor_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    descriptor_bindings[0].pImmutableSamplers = nullptr;

    descriptor_bindings[1].binding = 1;
    descriptor_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_bindings[1].descriptorCount = 1;
    descriptor_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptor_bindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.bindingCount = static_cast<uint32_t>(descriptor_bindings.size());
    desc.pBindings = descriptor_bindings.data();

    descriptor_set_layout = get_resource_manager()->create_descriptor_set_layout(desc);
}

void Vulkan_Demo::create_descriptor_set() {
    VkDescriptorSetAllocateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    desc.pNext = nullptr;
    desc.descriptorPool = descriptor_pool;
    desc.descriptorSetCount = 1;
    desc.pSetLayouts = &descriptor_set_layout;

    VkResult result = vkAllocateDescriptorSets(get_device(), &desc, &descriptor_set);
    check_vk_result(result, "vkAllocateDescriptorSets");
}

void Vulkan_Demo::create_image_descriptor_set(const image_t* image) {
    VkDescriptorSetAllocateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    desc.pNext = nullptr;
    desc.descriptorPool = descriptor_pool;
    desc.descriptorSetCount = 1;
    desc.pSetLayouts = &descriptor_set_layout;

    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(get_device(), &desc, &set);
    check_vk_result(result, "vkAllocateDescriptorSets");

    VkDescriptorImageInfo image_info;
    image_info.sampler = texture_image_sampler;
    image_info.imageView = image->vk_image_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 1> descriptor_writes;
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = set;
    descriptor_writes[0].dstBinding = 1;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].pNext = nullptr;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[0].pImageInfo = &image_info;
    descriptor_writes[0].pBufferInfo = nullptr;
    descriptor_writes[0].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(get_device(), (uint32_t)descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);

    update_ubo_descriptor(set);

    image_descriptor_sets[image] = set;
}

void Vulkan_Demo::create_render_pass() {
    VkAttachmentDescription color_attachment;
    color_attachment.flags = 0;
    color_attachment.format = get_swapchain_image_format();
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment;
    depth_attachment.flags = 0;
    depth_attachment.format = find_depth_format(get_physical_device());
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment_ref;
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref;
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass;
    subpass.flags = 0;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = nullptr;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pResolveAttachments = nullptr;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = nullptr;

    std::array<VkAttachmentDescription, 2> attachments{color_attachment, depth_attachment};
    VkRenderPassCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.attachmentCount = static_cast<uint32_t>(attachments.size());
    desc.pAttachments = attachments.data();
    desc.subpassCount = 1;
    desc.pSubpasses = &subpass;
    desc.dependencyCount = 0;
    desc.pDependencies = nullptr;

    render_pass = get_resource_manager()->create_render_pass(desc);
}

void Vulkan_Demo::create_framebuffers() {
    std::array<VkImageView, 2> attachments = {VK_NULL_HANDLE, depth_image_view};

    VkFramebufferCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.renderPass = render_pass;
    desc.attachmentCount = static_cast<uint32_t>(attachments.size());
    desc.pAttachments = attachments.data();
    desc.width = window_width;
    desc.height = window_height;
    desc.layers = 1;

    const auto& swapchain_image_views = get_swapchain_image_views();
    framebuffers.resize(swapchain_image_views.size());
    for (std::size_t i = 0; i < framebuffers.size(); i++) {
        attachments[0] = swapchain_image_views[i]; // set color attachment
        framebuffers[i] = get_resource_manager()->create_framebuffer(desc);
    }
}

void Vulkan_Demo::create_pipeline_layout() {
    VkPipelineLayoutCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.setLayoutCount = 1;
    desc.pSetLayouts = &descriptor_set_layout;
    desc.pushConstantRangeCount = 0;
    desc.pPushConstantRanges = nullptr;

    pipeline_layout = get_resource_manager()->create_pipeline_layout(desc);
}

void Vulkan_Demo::create_pipeline() {
    Shader_Module vertex_shader("../../data/vert.spv");
    Shader_Module fragment_shader("../../data/frag.spv");

    auto get_shader_stage_desc = [](VkShaderStageFlagBits stage, VkShaderModule shader_module, const char* entry) {
        VkPipelineShaderStageCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.stage = stage;
        desc.module = shader_module;
        desc.pName = entry;
        desc.pSpecializationInfo = nullptr;
        return desc;
    };
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages_state {
        get_shader_stage_desc(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader.handle, "main"),
        get_shader_stage_desc(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader.handle, "main")
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state;
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.pNext = nullptr;
    vertex_input_state.flags = 0;
    auto bindings = Vertex::get_bindings();
    vertex_input_state.vertexBindingDescriptionCount = (uint32_t)bindings.size();
    vertex_input_state.pVertexBindingDescriptions = bindings.data();
    auto attribs = Vertex::get_attributes();
    vertex_input_state.vertexAttributeDescriptionCount = (uint32_t)attribs.size();
    vertex_input_state.pVertexAttributeDescriptions = attribs.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
    input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.pNext = nullptr;
    input_assembly_state.flags = 0;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)window_width;
    viewport.height = (float)window_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset = {0, 0};
    scissor.extent = {(uint32_t)window_width, (uint32_t)window_height};

    VkPipelineViewportStateCreateInfo viewport_state;
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.pNext = nullptr;
    viewport_state.flags = 0;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization_state;
    rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.pNext = nullptr;
    rasterization_state.flags = 0;
    rasterization_state.depthClampEnable = VK_FALSE;
    rasterization_state.rasterizerDiscardEnable = VK_FALSE;
    rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode = VK_CULL_MODE_NONE/*VK_CULL_MODE_BACK_BIT*/;
    rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasEnable = VK_FALSE;
    rasterization_state.depthBiasConstantFactor = 0.0f;
    rasterization_state.depthBiasClamp = 0.0f;
    rasterization_state.depthBiasSlopeFactor = 0.0f;
    rasterization_state.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state;
    multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.pNext = nullptr;
    multisample_state.flags = 0;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.sampleShadingEnable = VK_FALSE;
    multisample_state.minSampleShading = 1.0f;
    multisample_state.pSampleMask = nullptr;
    multisample_state.alphaToCoverageEnable = VK_FALSE;
    multisample_state.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.pNext = nullptr;
    depth_stencil_state.flags = 0;
    depth_stencil_state.depthTestEnable = VK_FALSE /*VK_TRUE*/;
    depth_stencil_state.depthWriteEnable = VK_TRUE;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable = VK_FALSE;
    depth_stencil_state.front = {};
    depth_stencil_state.back = {};
    depth_stencil_state.minDepthBounds = 0.0;
    depth_stencil_state.maxDepthBounds = 0.0;

    VkPipelineColorBlendAttachmentState attachment_blend_state;
    attachment_blend_state.blendEnable = VK_FALSE;
    attachment_blend_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment_blend_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    attachment_blend_state.colorBlendOp = VK_BLEND_OP_ADD;
    attachment_blend_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment_blend_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    attachment_blend_state.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment_blend_state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend_state;
    blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_state.pNext = nullptr;
    blend_state.flags = 0;
    blend_state.logicOpEnable = VK_FALSE;
    blend_state.logicOp = VK_LOGIC_OP_COPY;
    blend_state.attachmentCount = 1;
    blend_state.pAttachments = &attachment_blend_state;
    blend_state.blendConstants[0] = 0.0f;
    blend_state.blendConstants[1] = 0.0f;
    blend_state.blendConstants[2] = 0.0f;
    blend_state.blendConstants[3] = 0.0f;

    VkGraphicsPipelineCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.stageCount = static_cast<uint32_t>(shader_stages_state.size());
    desc.pStages = shader_stages_state.data();
    desc.pVertexInputState = &vertex_input_state;
    desc.pInputAssemblyState = &input_assembly_state;
    desc.pTessellationState = nullptr;
    desc.pViewportState = &viewport_state;
    desc.pRasterizationState = &rasterization_state;
    desc.pMultisampleState = &multisample_state;
    desc.pDepthStencilState = &depth_stencil_state;
    desc.pColorBlendState = &blend_state;
    desc.pDynamicState = nullptr;
    desc.layout = pipeline_layout;
    desc.renderPass = render_pass;
    desc.subpass = 0;
    desc.basePipelineHandle = VK_NULL_HANDLE;
    desc.basePipelineIndex = -1;

    pipeline = get_resource_manager()->create_graphics_pipeline(desc);
}

void Vulkan_Demo::upload_geometry() {
    Model model = load_model();
    model_indices_count = static_cast<uint32_t>(model.indices.size());

    {
        const VkDeviceSize size = model.vertices.size() * sizeof(model.vertices[0]);
        vertex_buffer = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        VkBuffer staging_buffer = create_staging_buffer(size, model.vertices.data());
        Defer_Action destroy_staging_buffer([&staging_buffer, this]() {
            vkDestroyBuffer(get_device(), staging_buffer, nullptr);
        });
        record_and_run_commands(command_pool, get_queue(), [&staging_buffer, &size, this](VkCommandBuffer command_buffer) {
            VkBufferCopy region;
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = size;
            vkCmdCopyBuffer(command_buffer, staging_buffer, vertex_buffer, 1, &region);
        });
    }
    {
        const VkDeviceSize size = model.indices.size() * sizeof(model.indices[0]);
        index_buffer = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        VkBuffer staging_buffer = create_staging_buffer(size, model.indices.data());
        Defer_Action destroy_staging_buffer([&staging_buffer, this]() {
            vkDestroyBuffer(get_device(), staging_buffer, nullptr);
        });
        record_and_run_commands(command_pool, get_queue(), [&staging_buffer, &size, this](VkCommandBuffer command_buffer) {
            VkBufferCopy region;
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = size;
            vkCmdCopyBuffer(command_buffer, staging_buffer, index_buffer, 1, &region);
        });
    }

    {
        VkBufferCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.size = 16 * 1024 * 1024;
        desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        desc.queueFamilyIndexCount = 0;
        desc.pQueueFamilyIndices = nullptr;

        tess_vertex_buffer = get_resource_manager()->create_buffer(desc);
        tess_vertex_buffer_memory = get_allocator()->allocate_staging_memory(tess_vertex_buffer);
        VkResult result = vkBindBufferMemory(get_device(), tess_vertex_buffer, tess_vertex_buffer_memory, 0);
        check_vk_result(result, "vkBindBufferMemory");
    }

    {
        VkBufferCreateInfo desc;
        desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        desc.pNext = nullptr;
        desc.flags = 0;
        desc.size = 16 * 1024 * 1024;
        desc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        desc.queueFamilyIndexCount = 0;
        desc.pQueueFamilyIndices = nullptr;

        tess_index_buffer = get_resource_manager()->create_buffer(desc);
        tess_index_buffer_memory = get_allocator()->allocate_staging_memory(tess_index_buffer);
        VkResult result = vkBindBufferMemory(get_device(), tess_index_buffer, tess_index_buffer_memory, 0);
        check_vk_result(result, "vkBindBufferMemory");
    }
}

void Vulkan_Demo::update_ubo_descriptor(VkDescriptorSet set) {
    VkDescriptorBufferInfo buffer_info;
    buffer_info.buffer = uniform_buffer;
    buffer_info.offset = 0;
    buffer_info.range = sizeof(Uniform_Buffer_Object);

    std::array<VkWriteDescriptorSet, 1> descriptor_writes;
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].pNext = nullptr;
    descriptor_writes[0].dstSet = set;
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descriptor_writes[0].pImageInfo = nullptr;
    descriptor_writes[0].pBufferInfo = &buffer_info;
    descriptor_writes[0].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(get_device(), (uint32_t)descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);
}

void Vulkan_Demo::update_uniform_buffer() {
    Uniform_Buffer_Object ubo;

    ubo.model = glm::mat4();
    ubo.view = glm::mat4();

    const glm::mat4 ortho_proj(
        2.0f / glConfig.vidWidth, 0.0f, 0.f, 0.0f,
        0.0, 2.0f / glConfig.vidHeight, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f
    );
    ubo.proj = ortho_proj;

    void* data;
    VkResult result = vkMapMemory(get_device(), uniform_staging_buffer_memory, tess_ubo_offset, sizeof(ubo), 0, &data);
    check_vk_result(result, "vkMapMemory");
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(get_device(), uniform_staging_buffer_memory);
}

void Vulkan_Demo::begin_frame() {
    fprintf(logfile, "begin_frame\n");
    fflush(logfile);

    VkBufferCopy region;
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = sizeof(Uniform_Buffer_Object) * 1024;
    vkCmdCopyBuffer(command_buffer, uniform_staging_buffer, uniform_buffer, 1, &region);

    VkBufferMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = uniform_buffer;
    barrier.offset = 0;
    barrier.size = sizeof(Uniform_Buffer_Object) * 1024;

    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
        0, nullptr, 1, &barrier, 0, nullptr);

    std::array<VkClearValue, 2> clear_values;
    clear_values[0].color = {1.0f, 0.3f, 0.3f, 0.0f};
    clear_values[1].depthStencil = {1.0, 0};

    VkRenderPassBeginInfo render_pass_begin_info;
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.pNext = nullptr;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.framebuffer = framebuffers[swapchain_image_index];
    render_pass_begin_info.renderArea.offset = {0, 0};
    render_pass_begin_info.renderArea.extent = {(uint32_t)window_width, (uint32_t)window_height};
    render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_begin_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    tess_vertex_buffer_offset = 0;
    tess_index_buffer_offset = 0;
    tess_ubo_offset = 0;
}

void Vulkan_Demo::end_frame() {
    fprintf(logfile, "end_frame (vb_size %d, ib_size %d, ubo_size %d)\n", (int)tess_vertex_buffer_offset, (int)tess_index_buffer_offset, (int)tess_ubo_offset);
    fflush(logfile);
    vkCmdEndRenderPass(command_buffer);
}

void Vulkan_Demo::render_tess(const image_t* image) {
    fprintf(logfile, "render_tess (vert %d, inds %d)\n", tess.numVertexes, tess.numIndexes);
    fflush(logfile);

    void* data;
    VkResult result = vkMapMemory(get_device(), tess_vertex_buffer_memory, tess_vertex_buffer_offset, tess.numVertexes * sizeof(Vertex), 0, &data);
    check_vk_result(result, "vkMapMemory");
    Vertex* v = (Vertex*)data;
    for (int i = 0; i < tess.numVertexes; i++, v++) {
        v->pos.x = tess.xyz[i][0];
        v->pos.y = tess.xyz[i][1];
        v->pos.z = tess.xyz[i][2];
        v->tex_coord[0] = tess.texCoords[i][0][0];
        v->tex_coord[1] = tess.texCoords[i][0][1];
    }
    vkUnmapMemory(get_device(), tess_vertex_buffer_memory);

    result = vkMapMemory(get_device(), tess_index_buffer_memory, tess_index_buffer_offset, tess.numIndexes * sizeof(uint32_t), 0, &data);
    check_vk_result(result, "vkMapMemory");
    uint32_t* ind = (uint32_t*)data;
    for (int i = 0; i < tess.numIndexes; i++, ind++) {
        *ind = tess.indexes[i];
    }
    vkUnmapMemory(get_device(), tess_index_buffer_memory);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &tess_vertex_buffer, &tess_vertex_buffer_offset);
    vkCmdBindIndexBuffer(command_buffer, tess_index_buffer, tess_index_buffer_offset, VK_INDEX_TYPE_UINT32);

    VkDescriptorSet* set = &descriptor_set;
    VkDescriptorSet image_set;
    if (image != nullptr) {
        image_set = image_descriptor_sets[image];
        set = &image_set;
    }

    update_uniform_buffer();

    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, set, 1, &tess_ubo_offset);
    tess_ubo_offset += tess_ubo_offset_step;

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdDrawIndexed(command_buffer, tess.numIndexes, 1, 0, 0, 0);
    tess_vertex_buffer_offset += tess.numVertexes * sizeof(Vertex);
    tess_index_buffer_offset += tess.numIndexes * sizeof(uint32_t);
}

void Vulkan_Demo::render_cinematic_frame() {
    fprintf(logfile, "render_cinematic_frame\n");
    fflush(logfile);

    VkDescriptorImageInfo image_info;
    image_info.sampler = texture_image_sampler;
    image_info.imageView = cinematic_image_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 1> descriptor_writes;
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = descriptor_set;
    descriptor_writes[0].dstBinding = 1;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].pNext = nullptr;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[0].pImageInfo = &image_info;
    descriptor_writes[0].pBufferInfo = nullptr;
    descriptor_writes[0].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(get_device(), (uint32_t)descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &offset);
    vkCmdBindIndexBuffer(command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

    update_uniform_buffer();
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 1, &tess_ubo_offset);
    tess_ubo_offset += tess_ubo_offset_step;

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDrawIndexed(command_buffer, model_indices_count, 1, 0, 0, 0);
}
