#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#include "vulkan/vulkan.h"
#include "../../game/q_shared.h"

const int MAX_SWAPCHAIN_IMAGES = 8;
const int MAX_VK_PIPELINES = 1024;
const int MAX_VK_IMAGES = 2048; // should be the same as MAX_DRAWIMAGES

enum class Vk_Shader_Type {
    single_texture,
    multi_texture_mul,
    multi_texture_add
};

struct Vk_Pipeline_Desc {
    Vk_Shader_Type  shader_type     = Vk_Shader_Type::single_texture;
    unsigned int    state_bits      = 0; // GLS_XXX flags
    int             face_culling    = 0;// cullType_t
    bool            polygon_offset  = false;

    bool operator==(const Vk_Pipeline_Desc& other) const {
        return
            shader_type == other.shader_type &&
            state_bits == other.state_bits &&
            face_culling == other.face_culling &&
            polygon_offset == other.polygon_offset;
    }
};

struct Vk_Staging_Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE; // memory associated with a buffer
    VkDeviceSize offset = -1;
    VkDeviceSize size = 0;
};

struct Vk_Image {
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;

    // One to one correspondence between images and descriptor sets.
    // We update descriptor set during image initialization and then never touch it again (except for cinematic images).
    VkDescriptorSet descriptor_set;

    // Staging buffer for cinematic images.
    Vk_Staging_Buffer staging_buffer;
};

bool vk_initialize(HWND hwnd);
void vk_deinitialize();
void vk_destroy_resources();

VkImage vk_create_texture(const uint8_t* pixels, int bytes_per_pixel, int width, int height, VkImageView& image_view);
VkImage vk_create_cinematic_image(int width, int height, Vk_Staging_Buffer& staging_buffer);
void vk_update_cinematic_image(VkImage image, const Vk_Staging_Buffer& staging_buffer, int width, int height, const uint8_t* rgba_pixels);
VkPipeline vk_find_pipeline(const Vk_Pipeline_Desc& desc);
VkDescriptorSet vk_create_descriptor_set(VkImageView image_view);

VkRect2D vk_get_viewport_rect();
void vk_get_mvp_transform(float mvp[16]);

void vk_bind_resources_shared_between_stages();
void vk_bind_stage_specific_resources(VkPipeline pipeline, bool multitexture, bool sky);

void vk_begin_frame();
void vk_end_frame();


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

// Vulkan specific structures used by the engine.
struct Vulkan_Instance {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format = {};

    uint32_t queue_family_index = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    uint32_t swapchain_image_count = 0;
    VkImage swapchain_images[MAX_SWAPCHAIN_IMAGES];
    VkImageView swapchain_image_views[MAX_SWAPCHAIN_IMAGES];

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    VkImage depth_image = VK_NULL_HANDLE;
    VkImageView depth_image_view = VK_NULL_HANDLE;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_buffer_memory = VK_NULL_HANDLE;
    byte* vertex_buffer_ptr = nullptr; // pointer to mapped vertex buffer
    int xyz_elements = 0;
    int color_st_elements = 0;

    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory index_buffer_memory = VK_NULL_HANDLE;
    byte* index_buffer_ptr = nullptr; // pointer to mapped index buffer
    VkDeviceSize index_buffer_offset = 0;

    VkSemaphore image_acquired = VK_NULL_HANDLE;
    uint32_t swapchain_image_index = -1;

    VkSemaphore rendering_finished = VK_NULL_HANDLE;
    VkFence rendering_finished_fence = VK_NULL_HANDLE;

    VkShaderModule single_texture_vs = VK_NULL_HANDLE;
    VkShaderModule single_texture_fs = VK_NULL_HANDLE;
    VkShaderModule multi_texture_vs = VK_NULL_HANDLE;
    VkShaderModule multi_texture_mul_fs = VK_NULL_HANDLE;
    VkShaderModule multi_texture_add_fs = VK_NULL_HANDLE;

    VkSampler sampler = VK_NULL_HANDLE;
    VkPipeline skybox_pipeline = VK_NULL_HANDLE;
};

struct Vulkan_Resources {
    int num_pipelines = 0;
    Vk_Pipeline_Desc pipeline_desc[MAX_VK_PIPELINES];
    VkPipeline pipelines[MAX_VK_PIPELINES];

    Vk_Image images[MAX_VK_IMAGES];
};
