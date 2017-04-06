#pragma once

#include <array>
#include <map>
#include <memory>
#include <vector>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/hash.hpp"

#include "vk.h"
#include "tr_local.h"

struct Vertex {
    glm::vec3 pos;
    glm::vec4 color;
    glm::vec2 tex_coord;
    glm::vec2 tex_coord2;

    static std::array<VkVertexInputBindingDescription, 1> get_bindings() {
        VkVertexInputBindingDescription binding_desc;
        binding_desc.binding = 0;
        binding_desc.stride = sizeof(Vertex);
        binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return {binding_desc};
    }

    static std::array<VkVertexInputAttributeDescription, 4> get_attributes() {
        VkVertexInputAttributeDescription position_attrib;
        position_attrib.location = 0;
        position_attrib.binding = 0;
        position_attrib.format = VK_FORMAT_R32G32B32_SFLOAT;
        position_attrib.offset = offsetof(struct Vertex, pos);

        VkVertexInputAttributeDescription color_attrib;
        color_attrib.location = 1;
        color_attrib.binding = 0;
        color_attrib.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        color_attrib.offset = offsetof(struct Vertex, color);

        VkVertexInputAttributeDescription tex_coord_attrib;
        tex_coord_attrib.location = 2;
        tex_coord_attrib.binding = 0;
        tex_coord_attrib.format = VK_FORMAT_R32G32_SFLOAT;
        tex_coord_attrib.offset = offsetof(struct Vertex, tex_coord);

        VkVertexInputAttributeDescription tex_coord2_attrib;
        tex_coord2_attrib.location = 3;
        tex_coord2_attrib.binding = 0;
        tex_coord2_attrib.format = VK_FORMAT_R32G32_SFLOAT;
        tex_coord2_attrib.offset = offsetof(struct Vertex, tex_coord2);

        return {position_attrib, color_attrib, tex_coord_attrib, tex_coord2_attrib};
    }
};

class Vulkan_Demo {
public:
    Vulkan_Demo(int window_width, int window_height);
    ~Vulkan_Demo();

    void begin_frame();
    void end_frame();
    void render_tess(const shaderStage_t* stage);
    void render_tess_multi(const shaderStage_t* stage);
    void render_cinematic_frame();

public:
    void create_command_pool();
    void create_descriptor_pool();

    void create_uniform_buffer();
    VkImage create_texture(const uint8_t* pixels, int bytes_per_pixel, int width, int height, VkImageView& image_view);
    void create_texture_sampler();
    void create_depth_buffer_resources();

    void create_descriptor_set_layout();
    void create_descriptor_set();
    void create_image_descriptor_set(const image_t* image);
    void create_multitexture_descriptor_set(const image_t* image, const image_t* image2);
    void create_render_pass();
    void create_framebuffers();
    void create_pipeline_layout();
    VkPipeline create_pipeline(bool depth_test, bool multitexture);

    void upload_geometry();
    void update_ubo_descriptor(VkDescriptorSet set);
    void update_uniform_buffer();

public:
    const int window_width = 0;
    const int window_height = 0;

    VkSemaphore image_acquired = VK_NULL_HANDLE;
    VkSemaphore rendering_finished = VK_NULL_HANDLE;
    VkFence rendering_finished_fence = VK_NULL_HANDLE;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

    VkBuffer uniform_staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory uniform_staging_buffer_memory = VK_NULL_HANDLE;
    VkBuffer uniform_buffer = VK_NULL_HANDLE;

    VkSampler texture_image_sampler = VK_NULL_HANDLE;
    VkImage depth_image = VK_NULL_HANDLE;
    VkImageView depth_image_view = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

    VkPipeline pipeline_2d = VK_NULL_HANDLE;

    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    uint32_t model_indices_count = 0;

    VkBuffer tess_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory tess_vertex_buffer_memory = VK_NULL_HANDLE;
    VkDeviceSize tess_vertex_buffer_offset = 0;
    VkBuffer tess_index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory tess_index_buffer_memory = VK_NULL_HANDLE;
    VkDeviceSize tess_index_buffer_offset = 0;

    uint32_t tess_ubo_offset = 0;
    uint32_t tess_ubo_offset_step = -1;

    std::map<const image_t*, VkDescriptorSet> image_descriptor_sets; // quick UI prototyping
    std::map<std::pair<const image_t*, const image_t*>, VkDescriptorSet> multitexture_descriptor_sets;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    uint32_t swapchain_image_index = -1;
    VkImageView cinematic_image_view = VK_NULL_HANDLE;
};
