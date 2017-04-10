#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#include "vulkan/vulkan.h"

#include <array>
#include <vector>

#include "../../game/q_shared.h"

bool initialize_vulkan(HWND hwnd);
void deinitialize_vulkan();

VkPhysicalDevice get_physical_device();
VkDevice get_device();
uint32_t get_queue_family_index();
VkQueue get_queue();
VkSwapchainKHR get_swapchain();
VkFormat get_swapchain_image_format();
const std::vector<VkImageView>& get_swapchain_image_views();

struct Vk_Staging_Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE; // memory associated with a buffer
    VkDeviceSize offset = -1;
    VkDeviceSize size = 0;
};

VkImage vk_create_cinematic_image(int width, int height, Vk_Staging_Buffer& staging_buffer);
void vk_update_cinematic_image(VkImage image, const Vk_Staging_Buffer& staging_buffer, int width, int height, const uint8_t* rgba_pixels);

// Pipelines.
enum class Vk_Shader_Type {
    single_texture,
    multi_texture_mul,
    multi_texture_add
};

struct Vk_Pipeline_Desc {
    Vk_Shader_Type shader_type;
    unsigned int state_bits; // GLS_XXX flags
    bool polygon_offset;

    bool operator==(const Vk_Pipeline_Desc& other) const {
        return
            shader_type == other.shader_type &&
            state_bits == other.state_bits &&
            polygon_offset == other.polygon_offset;
    }
};

VkPipeline vk_find_pipeline(const Vk_Pipeline_Desc& desc);
void vk_destroy_pipelines();

// Vertex formats
struct Vk_Vertex {
    vec3_t pos;
    vec4_t color;
    vec2_t st;

    static std::array<VkVertexInputBindingDescription, 1> get_bindings() {
        VkVertexInputBindingDescription binding_desc;
        binding_desc.binding = 0;
        binding_desc.stride = sizeof(Vk_Vertex);
        binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return {binding_desc};
    }
    static std::array<VkVertexInputAttributeDescription, 3> get_attributes() {
        VkVertexInputAttributeDescription position_attrib;
        position_attrib.location = 0;
        position_attrib.binding = 0;
        position_attrib.format = VK_FORMAT_R32G32B32_SFLOAT;
        position_attrib.offset = offsetof(struct Vk_Vertex, pos);

        VkVertexInputAttributeDescription color_attrib;
        color_attrib.location = 1;
        color_attrib.binding = 0;
        color_attrib.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        color_attrib.offset = offsetof(struct Vk_Vertex, color);

        VkVertexInputAttributeDescription st_attrib;
        st_attrib.location = 2;
        st_attrib.binding = 0;
        st_attrib.format = VK_FORMAT_R32G32_SFLOAT;
        st_attrib.offset = offsetof(struct Vk_Vertex, st);

        return {position_attrib, color_attrib, st_attrib};
    }
};

struct Vk_Vertex2 : Vk_Vertex {
    vec2_t st2;

    static std::array<VkVertexInputBindingDescription, 1> get_bindings() {
        VkVertexInputBindingDescription binding_desc;
        binding_desc.binding = 0;
        binding_desc.stride = sizeof(Vk_Vertex2);
        binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return {binding_desc};
    }
    static std::array<VkVertexInputAttributeDescription, 4> get_attributes() {
        auto vk_vertex_attribs = Vk_Vertex::get_attributes();

        std::array<VkVertexInputAttributeDescription, 4> result;
        result[0] = vk_vertex_attribs[0];
        result[1] = vk_vertex_attribs[1];
        result[2] = vk_vertex_attribs[2];

        VkVertexInputAttributeDescription st2_attrib;
        st2_attrib.location = 3;
        st2_attrib.binding = 0;
        st2_attrib.format = VK_FORMAT_R32G32_SFLOAT;
        st2_attrib.offset = offsetof(struct Vk_Vertex2, st2);
        result[3] = st2_attrib;

        return result;
    }
};

// Shaders.
extern unsigned char single_texture_vert_spv[];
extern long long single_texture_vert_spv_size;

extern unsigned char single_texture_frag_spv[];
extern long long single_texture_frag_spv_size;

extern unsigned char multi_texture_vert_spv[];
extern long long multi_texture_vert_spv_size;

extern unsigned char multi_texture_add_frag_spv[];
extern long long multi_texture_add_frag_spv_size;

extern unsigned char multi_texture_mul_frag_spv[];
extern long long multi_texture_mul_frag_spv_size;
