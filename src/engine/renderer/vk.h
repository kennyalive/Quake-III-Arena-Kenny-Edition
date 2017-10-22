#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

#include "D3d12.h"
#include "D3d12SDKLayers.h"
#include "DXGI1_4.h"
#include "wrl.h"
#include "d3dx12.h"
#include "D3Dcompiler.h"
#include <DirectXMath.h>
#include "../../engine/platform/win_local.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

const int MAX_SWAPCHAIN_IMAGES = 8;
const int MAX_VK_SAMPLERS = 32;
const int MAX_VK_PIPELINES = 1024;
const int MAX_VK_IMAGES = 2048; // should be the same as MAX_DRAWIMAGES

const int IMAGE_CHUNK_SIZE = 32 * 1024 * 1024;
const int MAX_IMAGE_CHUNKS = 16;

const int D3D_FRAME_COUNT = 2;

#define VK_CHECK(function_call) { \
	VkResult result = function_call; \
	if (result < 0) \
		ri.Error(ERR_FATAL, "Vulkan: error code %d returned by %s", result, #function_call); \
}

#define DX_CHECK(function_call) { \
	HRESULT hr = function_call; \
	if (FAILED(hr)) \
		ri.Error(ERR_FATAL, "Direct3D: error returned by %s", #function_call); \
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
	force_one, // [1..1]
	weapon // [0..0.3]
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
	bool line_primitives = false;
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

void dx_initialize();

// Shutdown vulkan subsystem by releasing resources acquired by Vk_Instance.
void vk_shutdown();

void dx_shutdown();

// Releases vulkan resources allocated during program execution.
// This effectively puts vulkan subsystem into initial state (the state we have after vk_initialize call).
void vk_release_resources();

//
// Resources allocation.
//
Vk_Image vk_create_image(int width, int height, VkFormat format, int mip_levels, bool repeat_texture);
void vk_upload_image_data(VkImage image, int width, int height, bool mipmap, const uint8_t* pixels, int bytes_per_pixel);
void vk_update_descriptor_set(VkDescriptorSet set, VkImageView image_view, bool mipmap, bool repeat_texture);
VkSampler vk_find_sampler(const Vk_Sampler_Def& def);
VkPipeline vk_find_pipeline(const Vk_Pipeline_Def& def);

//
// Rendering setup.
//
void vk_clear_attachments(bool clear_depth_stencil, bool clear_color, vec4_t color);
void vk_bind_geometry();
void vk_shade_geometry(VkPipeline pipeline, bool multitexture, Vk_Depth_Range depth_range, bool indexed = true);
void vk_begin_frame();
void vk_end_frame();

void dx_begin_frame();
void dx_end_frame();

void vk_read_pixels(byte* buffer); // screenshots

// Vk_Instance contains engine-specific vulkan resources that persist entire renderer lifetime.
// This structure is initialized/deinitialized by vk_initialize/vk_shutdown functions correspondingly.
struct Vk_Instance {
	bool active = false;

	ID3D12Device* dx_device = nullptr;
	ID3D12CommandQueue* dx_command_queue = nullptr;
	ComPtr<IDXGISwapChain3> dx_swapchain;
	ID3D12DescriptorHeap* dx_rtv_heap = nullptr;
	UINT dx_rtv_descriptor_size = 0;
	ID3D12Resource* dx_render_targets[D3D_FRAME_COUNT];
	ID3D12RootSignature* dx_root_signature = nullptr;
	ID3D12CommandAllocator* dx_command_allocator = nullptr;
	ID3D12GraphicsCommandList* dx_command_list = nullptr;
	ID3D12PipelineState* dx_pipeline_state = nullptr;

	UINT dx_frame_index = 0;
	ID3D12Fence* dx_fence = nullptr;
	UINT64 dx_fence_value = 0;
	HANDLE dx_fence_event = NULL;

	ID3D12Resource* dx_vertex_buffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW dx_vertex_buffer_view;

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
	uint32_t swapchain_image_index = -1;

	VkSemaphore image_acquired = VK_NULL_HANDLE;
	VkSemaphore rendering_finished = VK_NULL_HANDLE;
	VkFence rendering_finished_fence = VK_NULL_HANDLE;

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

	//
	// Shader modules.
	//
	VkShaderModule single_texture_vs = VK_NULL_HANDLE;
	VkShaderModule single_texture_clipping_plane_vs = VK_NULL_HANDLE;
	VkShaderModule single_texture_fs = VK_NULL_HANDLE;
	VkShaderModule multi_texture_vs = VK_NULL_HANDLE;
	VkShaderModule multi_texture_clipping_plane_vs = VK_NULL_HANDLE;
	VkShaderModule multi_texture_mul_fs = VK_NULL_HANDLE;
	VkShaderModule multi_texture_add_fs = VK_NULL_HANDLE;

	//
	// Standard pipelines.
	//
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

	// debug visualization pipelines
	VkPipeline tris_debug_pipeline;
	VkPipeline tris_mirror_debug_pipeline;
	VkPipeline normals_debug_pipeline;
	VkPipeline surface_debug_pipeline_solid;
	VkPipeline surface_debug_pipeline_outline;
	VkPipeline images_debug_pipeline;
};

// Vk_World contains vulkan resources/state requested by the game code.
// It is reinitialized on a map change.
struct Vk_World {
	//
	// Resources.
	//
	int num_samplers = 0;
	Vk_Sampler_Def sampler_defs[MAX_VK_SAMPLERS];
	VkSampler samplers[MAX_VK_SAMPLERS];

	int num_pipelines = 0;
	Vk_Pipeline_Def pipeline_defs[MAX_VK_PIPELINES];
	VkPipeline pipelines[MAX_VK_PIPELINES];
	float pipeline_create_time;

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

	// This flag is used to decide whether framebuffer's depth attachment should be cleared
	// with vmCmdClearAttachment (dirty_depth_attachment == true), or it have just been
	// cleared by render pass instance clear op (dirty_depth_attachment == false).
	bool dirty_depth_attachment;

	float modelview_transform[16];
};

// Most of the renderer's code uses Vulkan API via function provides in this file but 
// there are few places outside of vk.cpp where we use Vulkan commands directly.
extern PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
extern PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
extern PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
extern PFN_vkDestroyImage vkDestroyImage;
extern PFN_vkDestroyImageView vkDestroyImageView;
extern PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
