#include "tr_local.h"

//
// Initialization
//
void wgpu_initialize() {
	// WGPU_TODO: Initialize WebGPU rendering and set wgpu.active after initialization succeeds
}

void wgpu_shutdown() {
	// WGPU_TODO: Release the WebGPU instance resources and clear wgpu.active
}

void wgpu_release_resources() {
	// WGPU_TODO: Release WebGPU resources allocated during registration
}

void wgpu_wait_device_idle() {
	// WGPU_TODO: Wait for submitted WebGPU work to complete
}

//
// Resources allocation
//
Wgpu_Image wgpu_create_image(int width, int height, WGPUTextureFormat format, int mip_levels, bool repeat_texture) {
	// WGPU_TODO: Create the WebGPU texture, view, and bind group
	return Wgpu_Image{};
}

void wgpu_upload_image_data(WGPUTexture texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel) {
	// WGPU_TODO: Upload the image pixels and mip levels to the WebGPU texture
}

void wgpu_update_bind_group(Wgpu_Image& image, bool mipmap, bool repeat_texture) {
	// WGPU_TODO: Replace the image bind group using the current sampler settings
}

WGPUSampler wgpu_find_sampler(const Vk_Sampler_Def& def) {
	// WGPU_TODO: Find or create the WebGPU sampler matching def
	return nullptr;
}

WGPURenderPipeline wgpu_find_pipeline(const Vk_Pipeline_Def& def) {
	// WGPU_TODO: Find or create the WebGPU render pipeline matching def
	return nullptr;
}

//
// Rendering setup
//
void wgpu_clear_attachments(bool clear_depth_stencil, bool clear_color, vec4_t color) {
	// WGPU_TODO: Clear the requested WebGPU attachments
}

void wgpu_bind_geometry() {
	// WGPU_TODO: Upload and bind the current tessellated geometry
}

void wgpu_shade_geometry(WGPURenderPipeline pipeline, bool multitexture, Vk_Depth_Range depth_range, bool indexed) {
	// WGPU_TODO: Bind the pipeline and resources and draw the current geometry
}

void wgpu_begin_frame() {
	// WGPU_TODO: Acquire the surface texture and begin recording the WebGPU frame
}

void wgpu_end_frame() {
	// WGPU_TODO: Submit the recorded WebGPU commands and present the frame
}

void wgpu_update_gamma_buffer(float gamma_table[256]) {
	// WGPU_TODO: Upload the gamma table for the WebGPU shader gamma path
}
