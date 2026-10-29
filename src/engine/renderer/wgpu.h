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
	Wgpu_Image images[MAX_VK_IMAGES];
};
