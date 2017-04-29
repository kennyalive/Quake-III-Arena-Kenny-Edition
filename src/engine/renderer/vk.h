#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#include "vulkan/vulkan.h"

const int MAX_SWAPCHAIN_IMAGES = 8;
const int MAX_VK_SAMPLERS = 32;
const int MAX_VK_PIPELINES = 1024;
const int MAX_VK_IMAGES = 2048; // should be the same as MAX_DRAWIMAGES

const int IMAGE_CHUNK_SIZE = 32 * 1024 * 1024;
const int MAX_IMAGE_CHUNKS = 16;

#define VK_CHECK(function_call) { \
    VkResult result = function_call; \
    if (result < 0) \
        ri.Error(ERR_FATAL, "Vulkan error: function %s, error code %d", #function_call, result); \
}

enum class Vk_Shader_Type {
    single_texture,
    multi_texture_mul,
    multi_texture_add
};

struct Vk_Sampler_Def {
    bool repeat_texture = false; // clamp/repeat texture addressing mode
    int gl_mag_filter = 0; // GL_XXX mag filter
    int gl_min_filter = 0; // GL_XXX min filter
};

struct Vk_Pipeline_Def {
    Vk_Shader_Type shader_type = Vk_Shader_Type::single_texture;
    unsigned int state_bits = 0; // GLS_XXX flags
    int face_culling = 0;// cullType_t
    bool polygon_offset = false;
    bool clipping_plane = false;
    bool mirror = false;
};

struct Vk_Image {
    VkImage handle = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    // Descriptor set that contains single descriptor used to access the given image.
    // It is updated only once during image initialization.
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};

//
// Initialization.
//
void vk_create_instance(HWND hwnd); // initializes VK_Instance
void vk_destroy_instance(); // destroys Vk_Instance resources
void vk_destroy_resources(); // destroys Vk_Resources resources

//
// Resources allocation.
//
Vk_Image vk_create_image(int width, int height, int mip_levels, bool repeat_texture);
void vk_upload_image_data(VkImage image, int width, int height, bool mipmap, const uint8_t* rgba_pixels);
void vk_update_descriptor_set(VkDescriptorSet set, VkImageView image_view, bool mipmap, bool repeat_texture);
VkSampler vk_find_sampler(const Vk_Sampler_Def& def);
VkPipeline vk_find_pipeline(const Vk_Pipeline_Def& def);

//
// Rendering setup.
//
VkRect2D vk_get_viewport_rect();
void vk_bind_resources_shared_between_stages();
void vk_bind_stage_specific_resources(VkPipeline pipeline, bool multitexture, bool sky);
void vk_begin_frame();
void vk_end_frame();

// Vulkan specific structures used by the engine.
struct Vk_Instance {
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
    VkDeviceMemory depth_image_memory = VK_NULL_HANDLE;
    VkImageView depth_image_view = VK_NULL_HANDLE;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    byte* vertex_buffer_ptr = nullptr; // pointer to mapped vertex buffer
    int xyz_elements = 0;
    int color_st_elements = 0;

    VkBuffer index_buffer = VK_NULL_HANDLE;
    byte* index_buffer_ptr = nullptr; // pointer to mapped index buffer
    VkDeviceSize index_buffer_offset = 0;

    // host visible memory that holds both vertex and index data
    VkDeviceMemory geometry_buffer_memory = VK_NULL_HANDLE;

    VkSemaphore image_acquired = VK_NULL_HANDLE;
    uint32_t swapchain_image_index = -1;

    VkSemaphore rendering_finished = VK_NULL_HANDLE;
    VkFence rendering_finished_fence = VK_NULL_HANDLE;

    VkShaderModule single_texture_vs = VK_NULL_HANDLE;
    VkShaderModule single_texture_clipping_plane_vs = VK_NULL_HANDLE;
    VkShaderModule single_texture_fs = VK_NULL_HANDLE;
    VkShaderModule multi_texture_vs = VK_NULL_HANDLE;
    VkShaderModule multi_texture_clipping_plane_vs = VK_NULL_HANDLE;
    VkShaderModule multi_texture_mul_fs = VK_NULL_HANDLE;
    VkShaderModule multi_texture_add_fs = VK_NULL_HANDLE;

    VkPipeline skybox_pipeline = VK_NULL_HANDLE;

    // dim 0 is based on fogPass_t: 0 - corresponds to FP_EQUAL, 1 - corresponds to FP_LE.
    // dim 1 is directly a cullType_t enum value.
    // dim 2 is a polygon offset value (0 - off, 1 - on).
    VkPipeline fog_pipelines[2][3][2];
};

struct Vk_Resources {
    //
    // Resources.
    //
    int num_samplers = 0;
    Vk_Sampler_Def sampler_defs[MAX_VK_SAMPLERS];
    VkSampler samplers[MAX_VK_SAMPLERS];

    int num_pipelines = 0;
    Vk_Pipeline_Def pipeline_defs[MAX_VK_PIPELINES];
    VkPipeline pipelines[MAX_VK_PIPELINES];

    Vk_Image images[MAX_VK_IMAGES];

    //
    // Memory allocations.
    //
    struct Chunk {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize used = 0;
    };

    int num_image_chunks = 0;
    Chunk image_chunks[MAX_IMAGE_CHUNKS];

    // Host visible memory used to copy image data to device local memory.
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_buffer_memory = VK_NULL_HANDLE;
    VkDeviceSize staging_buffer_size = 0;
    byte* staging_buffer_ptr = nullptr; // pointer to mapped staging buffer
};
