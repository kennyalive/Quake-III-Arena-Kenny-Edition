#pragma once

#include "webgpu/webgpu.h"

struct Wgpu_Image {
	WGPUTexture texture = nullptr;
	WGPUTextureView view = nullptr;
	WGPUBindGroup bind_group = nullptr;
};

//
// Initialization
//
void wgpu_initialize();
void wgpu_shutdown();
void wgpu_release_resources();
void wgpu_wait_device_idle();

//
// Resources allocation
//
Wgpu_Image wgpu_create_image(int width, int height, WGPUTextureFormat format, int mip_levels, bool repeat_texture);
void wgpu_destroy_image(Wgpu_Image& image);
void wgpu_upload_image_data(WGPUTexture texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel);
void wgpu_update_bind_group(Wgpu_Image& image, bool mipmap, bool repeat_texture);
WGPUSampler wgpu_find_sampler(const Vk_Sampler_Def& def);
WGPURenderPipeline wgpu_find_pipeline(const Vk_Pipeline_Def& def);

//
// Rendering setup
//
void wgpu_clear_attachments(bool clear_depth_stencil, bool clear_color, vec4_t color);
void wgpu_bind_geometry();
void wgpu_shade_geometry(WGPURenderPipeline pipeline, bool multitexture, Vk_Depth_Range depth_range, bool indexed = true);
void wgpu_begin_frame();
void wgpu_end_frame();

void wgpu_update_gamma_buffer(float gamma_table[256]);

struct Wgpu_Instance {
	bool active = false;

	WGPUInstance instance = nullptr;
	WGPUAdapter adapter = nullptr;
	WGPUDevice device = nullptr;
	WGPUQueue queue = nullptr;

	WGPUBindGroupLayout image_bind_group_layout = nullptr;
	WGPUPipelineLayout pipeline_layouts[2] = {}; // single texture, multitexture
	WGPUBindGroupLayout clipping_bind_group_layout = nullptr;
	WGPUBindGroup clipping_bind_group = nullptr;
	WGPUPipelineLayout clipping_pipeline_layouts[2] = {}; // single texture, multitexture

	//
	// Surface and frame resources
	//
	WGPUSurface surface = nullptr;
	WGPUTextureFormat surface_format = WGPUTextureFormat_Undefined;
	WGPUTexture frame_texture = nullptr;
	WGPUTextureView frame_view = nullptr;
	WGPUCommandEncoder command_encoder = nullptr;
	WGPURenderPassEncoder render_pass = nullptr;

	WGPUTexture depth_texture = nullptr;
	WGPUTextureView depth_view = nullptr;

	//
	// Geometry buffer
	//
	WGPUBuffer geometry_buffer = nullptr;
	uint64_t geometry_buffer_offset = 0;
	uint32_t uniform_buffer_alignment = 0;
	uint32_t clipping_data_offset = 0;

	//
	// Shader modules
	//
	WGPUShaderModule single_texture_vs = nullptr;
	WGPUShaderModule single_texture_clipping_plane_vs = nullptr;
	WGPUShaderModule single_texture_fs = nullptr;
	WGPUShaderModule multi_texture_vs = nullptr;
	WGPUShaderModule multi_texture_clipping_plane_vs = nullptr;
	WGPUShaderModule multi_texture_mul_fs = nullptr;
	WGPUShaderModule multi_texture_add_fs = nullptr;

	//
	// Standard pipelines
	//
	WGPURenderPipeline skybox_pipeline = nullptr;

	// dim 0: 0 - front side, 1 - back side
	// dim 1: 0 - normal view, 1 - mirror view
	WGPURenderPipeline shadow_volume_pipelines[2][2] = {};
	WGPURenderPipeline shadow_finish_pipeline = nullptr;

	// dim 0 is based on fogPass_t: 0 - corresponds to FP_EQUAL, 1 - corresponds to FP_LE
	// dim 1 is directly a cullType_t enum value
	// dim 2 is a polygon offset value (0 - off, 1 - on)
	WGPURenderPipeline fog_pipelines[2][3][2] = {};

	// dim 0 is based on dlight additive flag: 0 - not additive, 1 - additive
	// dim 1 is directly a cullType_t enum value
	// dim 2 is a polygon offset value (0 - off, 1 - on)
	WGPURenderPipeline dlight_pipelines[2][3][2] = {};

	// debug visualization pipelines
	WGPURenderPipeline tris_debug_pipeline = nullptr;
	WGPURenderPipeline tris_mirror_debug_pipeline = nullptr;
	WGPURenderPipeline normals_debug_pipeline = nullptr;
	WGPURenderPipeline surface_debug_pipeline_solid = nullptr;
	WGPURenderPipeline surface_debug_pipeline_outline = nullptr;
	WGPURenderPipeline images_debug_pipeline = nullptr;
};

struct Wgpu_World {
    //
    // Resources
    //
    int num_samplers = 0;
    Vk_Sampler_Def sampler_defs[MAX_VK_SAMPLERS];
    WGPUSampler samplers[MAX_VK_SAMPLERS] = {};

	int num_pipelines = 0;
	Vk_Pipeline_Def pipeline_defs[MAX_VK_PIPELINES];
	WGPURenderPipeline pipelines[MAX_VK_PIPELINES] = {};

    Wgpu_Image images[MAX_VK_IMAGES];

	//
	// State
	//
    int current_image_indices[2] = {};
    float modelview_transform[16];
};
