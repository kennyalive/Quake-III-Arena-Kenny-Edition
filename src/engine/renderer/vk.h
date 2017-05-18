#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#define VK_NO_PROTOTYPES
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

// used with cg_shadows == 2
enum class Vk_Shadow_Phase {
	disabled,
	shadow_edges_rendering,
	fullscreen_quad_rendering
};

enum class Vk_Depth_Range {
	normal, // [0..1]
	force_zero, // [0..0]
	force_one // [1..1]
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
	Vk_Shadow_Phase shadow_phase = Vk_Shadow_Phase::disabled;
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

// Initializes VK_Instance structure.
// After calling this function we get fully functional vulkan subsystem.
void vk_initialize();

// Shutdown vulkan subsystem by releasing resources acquired by Vk_Instance.
void vk_shutdown();

// Releases vulkan resources allocated during program execution.
// This effectively puts vulkan subsystem into initial state (the state we have after vk_initialize call).
void vk_release_resources();

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
VkRect2D vk_get_scissor_rect();
void vk_clear_attachments(bool clear_stencil, bool fast_sky);
void vk_bind_resources_shared_between_stages();
void vk_bind_stage_specific_resources(VkPipeline pipeline, bool multitexture, Vk_Depth_Range depth_range);
void vk_begin_frame();
void vk_end_frame();

void vk_read_pixels(byte* buffer);

// Vulkan specific structures used by the engine.
struct Vk_Instance {
    bool active = false;
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

	// dim 0: 0 - front side, 1 - back size
	// dim 1: 0 - normal view, 1 - mirror view
	VkPipeline shadow_volume_pipelines[2][2];
	VkPipeline shadow_finish_pipeline;

    // dim 0 is based on fogPass_t: 0 - corresponds to FP_EQUAL, 1 - corresponds to FP_LE.
    // dim 1 is directly a cullType_t enum value.
    // dim 2 is a polygon offset value (0 - off, 1 - on).
    VkPipeline fog_pipelines[2][3][2];

    // dim 0 is based on dlight additive flag: 0 - not additive, 1 - additive
    // dim 1 is directly a cullType_t enum value.
    // dim 2 is a polygon offset value (0 - off, 1 - on).
    VkPipeline dlight_pipelines[2][3][2];

	VkPipeline tris_debug_pipeline;
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

    //
    // State.
    //

    // Descriptor sets corresponding to bound texture images.
    VkDescriptorSet current_descriptor_sets[2];

    // This flag is used to decide whether framebuffer's attachments should be cleared
    // with vmCmdClearAttachment (dirty_attachments == true), or they have just been
    // cleared by render pass instance clear op (dirty_attachments == false).
    bool dirty_attachments;
};

// Vulkan API function pointers.
extern PFN_vkGetInstanceProcAddr						vkGetInstanceProcAddr;

extern PFN_vkCreateInstance								vkCreateInstance;
extern PFN_vkEnumerateInstanceExtensionProperties		vkEnumerateInstanceExtensionProperties;

extern PFN_vkCreateDevice								vkCreateDevice;
extern PFN_vkEnumerateDeviceExtensionProperties			vkEnumerateDeviceExtensionProperties;
extern PFN_vkEnumeratePhysicalDevices					vkEnumeratePhysicalDevices;
extern PFN_vkGetDeviceProcAddr							vkGetDeviceProcAddr;
extern PFN_vkGetPhysicalDeviceFeatures					vkGetPhysicalDeviceFeatures;
extern PFN_vkGetPhysicalDeviceFormatProperties			vkGetPhysicalDeviceFormatProperties;
extern PFN_vkGetPhysicalDeviceMemoryProperties			vkGetPhysicalDeviceMemoryProperties;
extern PFN_vkGetPhysicalDeviceProperties				vkGetPhysicalDeviceProperties;
extern PFN_vkGetPhysicalDeviceQueueFamilyProperties		vkGetPhysicalDeviceQueueFamilyProperties;
extern PFN_vkCreateWin32SurfaceKHR						vkCreateWin32SurfaceKHR;
extern PFN_vkDestroySurfaceKHR							vkDestroySurfaceKHR;
extern PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR	vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
extern PFN_vkGetPhysicalDeviceSurfaceFormatsKHR			vkGetPhysicalDeviceSurfaceFormatsKHR;
extern PFN_vkGetPhysicalDeviceSurfacePresentModesKHR	vkGetPhysicalDeviceSurfacePresentModesKHR;
extern PFN_vkGetPhysicalDeviceSurfaceSupportKHR			vkGetPhysicalDeviceSurfaceSupportKHR;

extern PFN_vkAllocateCommandBuffers						vkAllocateCommandBuffers;
extern PFN_vkAllocateDescriptorSets						vkAllocateDescriptorSets;
extern PFN_vkAllocateMemory								vkAllocateMemory;
extern PFN_vkBeginCommandBuffer							vkBeginCommandBuffer;
extern PFN_vkBindBufferMemory							vkBindBufferMemory;
extern PFN_vkBindImageMemory							vkBindImageMemory;
extern PFN_vkCmdBeginRenderPass							vkCmdBeginRenderPass;
extern PFN_vkCmdBindDescriptorSets						vkCmdBindDescriptorSets;
extern PFN_vkCmdBindIndexBuffer							vkCmdBindIndexBuffer;
extern PFN_vkCmdBindPipeline							vkCmdBindPipeline;
extern PFN_vkCmdBindVertexBuffers						vkCmdBindVertexBuffers;
extern PFN_vkCmdBlitImage								vkCmdBlitImage;
extern PFN_vkCmdClearAttachments						vkCmdClearAttachments;
extern PFN_vkCmdCopyBufferToImage						vkCmdCopyBufferToImage;
extern PFN_vkCmdCopyImage								vkCmdCopyImage;
extern PFN_vkCmdDrawIndexed								vkCmdDrawIndexed;
extern PFN_vkCmdEndRenderPass							vkCmdEndRenderPass;
extern PFN_vkCmdPipelineBarrier							vkCmdPipelineBarrier;
extern PFN_vkCmdPushConstants							vkCmdPushConstants;
extern PFN_vkCmdSetDepthBias							vkCmdSetDepthBias;
extern PFN_vkCmdSetScissor								vkCmdSetScissor;
extern PFN_vkCmdSetViewport								vkCmdSetViewport;
extern PFN_vkCreateBuffer								vkCreateBuffer;
extern PFN_vkCreateCommandPool							vkCreateCommandPool;
extern PFN_vkCreateDescriptorPool						vkCreateDescriptorPool;
extern PFN_vkCreateDescriptorSetLayout					vkCreateDescriptorSetLayout;
extern PFN_vkCreateFence								vkCreateFence;
extern PFN_vkCreateFramebuffer							vkCreateFramebuffer;
extern PFN_vkCreateGraphicsPipelines					vkCreateGraphicsPipelines;
extern PFN_vkCreateImage								vkCreateImage;
extern PFN_vkCreateImageView							vkCreateImageView;
extern PFN_vkCreatePipelineLayout						vkCreatePipelineLayout;
extern PFN_vkCreateRenderPass							vkCreateRenderPass;
extern PFN_vkCreateSampler								vkCreateSampler;
extern PFN_vkCreateSemaphore							vkCreateSemaphore;
extern PFN_vkCreateShaderModule							vkCreateShaderModule;
extern PFN_vkDestroyBuffer								vkDestroyBuffer;
extern PFN_vkDestroyCommandPool							vkDestroyCommandPool;
extern PFN_vkDestroyDescriptorPool						vkDestroyDescriptorPool;
extern PFN_vkDestroyDescriptorSetLayout					vkDestroyDescriptorSetLayout;
extern PFN_vkDestroyDevice								vkDestroyDevice;
extern PFN_vkDestroyFence								vkDestroyFence;
extern PFN_vkDestroyFramebuffer							vkDestroyFramebuffer;
extern PFN_vkDestroyImage								vkDestroyImage;
extern PFN_vkDestroyImageView							vkDestroyImageView;
extern PFN_vkDestroyInstance							vkDestroyInstance;
extern PFN_vkDestroyPipeline							vkDestroyPipeline;
extern PFN_vkDestroyPipelineLayout						vkDestroyPipelineLayout;
extern PFN_vkDestroyRenderPass							vkDestroyRenderPass;
extern PFN_vkDestroySampler								vkDestroySampler;
extern PFN_vkDestroySemaphore							vkDestroySemaphore;
extern PFN_vkDestroyShaderModule						vkDestroyShaderModule;
extern PFN_vkDeviceWaitIdle								vkDeviceWaitIdle;
extern PFN_vkEndCommandBuffer							vkEndCommandBuffer;
extern PFN_vkFreeCommandBuffers							vkFreeCommandBuffers;
extern PFN_vkFreeDescriptorSets							vkFreeDescriptorSets;
extern PFN_vkFreeMemory									vkFreeMemory;
extern PFN_vkGetBufferMemoryRequirements				vkGetBufferMemoryRequirements;
extern PFN_vkGetDeviceQueue								vkGetDeviceQueue;
extern PFN_vkGetImageMemoryRequirements					vkGetImageMemoryRequirements;
extern PFN_vkGetImageSubresourceLayout					vkGetImageSubresourceLayout;
extern PFN_vkMapMemory									vkMapMemory;
extern PFN_vkQueueSubmit								vkQueueSubmit;
extern PFN_vkQueueWaitIdle								vkQueueWaitIdle;
extern PFN_vkResetDescriptorPool						vkResetDescriptorPool;
extern PFN_vkResetFences								vkResetFences;
extern PFN_vkUpdateDescriptorSets						vkUpdateDescriptorSets;
extern PFN_vkWaitForFences								vkWaitForFences;
extern PFN_vkAcquireNextImageKHR						vkAcquireNextImageKHR;
extern PFN_vkCreateSwapchainKHR							vkCreateSwapchainKHR;
extern PFN_vkDestroySwapchainKHR						vkDestroySwapchainKHR;
extern PFN_vkGetSwapchainImagesKHR						vkGetSwapchainImagesKHR;
extern PFN_vkQueuePresentKHR							vkQueuePresentKHR;
