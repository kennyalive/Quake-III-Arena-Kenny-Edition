#pragma once

// Uncomment the following line to enable DX12 backend
//#define ENABLE_DX12

struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandQueue;
struct ID3D12Device;
struct ID3D12DescriptorHeap;
struct ID3D12Fence;
struct ID3D12PipelineState;
struct ID3D12Resource;
struct ID3D12RootSignature;
struct IDXGISwapChain3;

constexpr int SWAPCHAIN_BUFFER_COUNT = 2;

enum Dx_Sampler_Index {
	SAMPLER_MIP_REPEAT,
	SAMPLER_MIP_CLAMP,
	SAMPLER_NOMIP_REPEAT,
	SAMPLER_NOMIP_CLAMP,
	SAMPLER_COUNT
};

enum Dx_Image_Format {
	IMAGE_FORMAT_RGBA8,
	IMAGE_FORMAT_BGRA4,
	IMAGE_FORMAT_BGR5A1
};

struct Dx_Image {
	ID3D12Resource* texture = nullptr;
	Dx_Sampler_Index sampler_index = SAMPLER_COUNT;
};

//
// Initialization.
//
void dx_initialize();
void dx_shutdown();
void dx_release_resources();
void dx_wait_device_idle();

//
// Resources allocation.
//
Dx_Image dx_create_image(int width, int height, Dx_Image_Format format, int mip_levels,  bool repeat_texture, int image_index);
void dx_upload_image_data(ID3D12Resource* texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel);
void dx_create_sampler_descriptor(const Vk_Sampler_Def& def, Dx_Sampler_Index sampler_index);
ID3D12PipelineState* dx_find_pipeline(const Vk_Pipeline_Def& def);

//
// Rendering setup.
//
void dx_clear_attachments(bool clear_depth_stencil, bool clear_color, vec4_t color);
void dx_bind_geometry();
void dx_shade_geometry(ID3D12PipelineState* pipeline, bool multitexture, Vk_Depth_Range depth_range, bool indexed, bool lines);
void dx_begin_frame();
void dx_end_frame();

struct Dx_Instance {
	bool active = false;

	ID3D12Device* device = nullptr;
	ID3D12CommandQueue* command_queue = nullptr;
	IDXGISwapChain3* swapchain = nullptr;
	UINT frame_index = 0;

	ID3D12CommandAllocator* command_allocator = nullptr;
	ID3D12CommandAllocator* helper_command_allocator = nullptr;
	ID3D12GraphicsCommandList* command_list = nullptr;
	
	ID3D12Fence* fence = nullptr;
	UINT64 fence_value = 0;
	HANDLE fence_event = NULL;

	ID3D12Resource* render_targets[SWAPCHAIN_BUFFER_COUNT];
	ID3D12Resource* depth_stencil_buffer = nullptr;

	ID3D12RootSignature* root_signature = nullptr;

	//
	// Descriptor heaps.
	//
	ID3D12DescriptorHeap* rtv_heap = nullptr;
	UINT rtv_descriptor_size = 0;

	ID3D12DescriptorHeap* dsv_heap = nullptr;

	ID3D12DescriptorHeap* srv_heap = nullptr;
	UINT srv_descriptor_size = 0;

	ID3D12DescriptorHeap* sampler_heap = nullptr;
	UINT sampler_descriptor_size = 0;

	//
	// Geometry buffers.
	//
	byte* vertex_buffer_ptr = nullptr; // pointer to mapped vertex buffer
	int xyz_elements = 0;
	int color_st_elements = 0;

	byte* index_buffer_ptr = nullptr; // pointer to mapped index buffer
	int index_buffer_offset = 0;

	ID3D12Resource* geometry_buffer = nullptr;

	//
	// Standard pipelines.
	//
	ID3D12PipelineState* skybox_pipeline = nullptr;

	// dim 0: 0 - front side, 1 - back size
	// dim 1: 0 - normal view, 1 - mirror view
	ID3D12PipelineState* shadow_volume_pipelines[2][2];
	ID3D12PipelineState* shadow_finish_pipeline = nullptr;

	// dim 0 is based on fogPass_t: 0 - corresponds to FP_EQUAL, 1 - corresponds to FP_LE.
	// dim 1 is directly a cullType_t enum value.
	// dim 2 is a polygon offset value (0 - off, 1 - on).
	ID3D12PipelineState* fog_pipelines[2][3][2];

	// dim 0 is based on dlight additive flag: 0 - not additive, 1 - additive
	// dim 1 is directly a cullType_t enum value.
	// dim 2 is a polygon offset value (0 - off, 1 - on).
	ID3D12PipelineState* dlight_pipelines[2][3][2];

	// debug visualization pipelines
	ID3D12PipelineState* tris_debug_pipeline = nullptr;
	ID3D12PipelineState* tris_mirror_debug_pipeline = nullptr;
	ID3D12PipelineState* normals_debug_pipeline = nullptr;
	ID3D12PipelineState* surface_debug_pipeline_solid = nullptr;
	ID3D12PipelineState* surface_debug_pipeline_outline = nullptr;
	ID3D12PipelineState* images_debug_pipeline = nullptr;
};

struct Dx_World {
	//
	// Resources.
	//
	int num_pipelines = 0;
	Vk_Pipeline_Def pipeline_defs[MAX_VK_PIPELINES];
	ID3D12PipelineState* pipelines[MAX_VK_PIPELINES];
	float pipeline_create_time;

	Dx_Image images[MAX_VK_IMAGES];

	//
	// State.
	//
	int current_image_indices[2];
	float modelview_transform[16];
};
