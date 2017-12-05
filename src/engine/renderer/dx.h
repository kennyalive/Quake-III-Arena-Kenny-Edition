#pragma once

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

const int D3D_FRAME_COUNT = 2;

#define DX_CHECK(function_call) { \
	HRESULT hr = function_call; \
	if (FAILED(hr)) \
		ri.Error(ERR_FATAL, "Direct3D: error returned by %s", #function_call); \
}

struct Dx_Image {
	ID3D12Resource* texture = nullptr;
};

//
// Initialization.
//
void dx_initialize();
void dx_shutdown();

void dx_release_resources();

//
// Resources allocation.
//
Dx_Image dx_create_image(int width, int height, DXGI_FORMAT format, int mip_levels,  bool repeat_texture, int image_index);
void dx_upload_image_data(ID3D12Resource* texture, int width, int height, bool mipmap, const uint8_t* pixels, int bytes_per_pixel);
ID3D12PipelineState* dx_find_pipeline(const Vk_Pipeline_Def& def);

//
// Rendering setup.
//
void dx_bind_geometry();
void dx_shade_geometry(ID3D12PipelineState* pipeline_state, bool multitexture, Vk_Depth_Range depth_range, bool indexed = true);
void dx_begin_frame();
void dx_end_frame();

struct Dx_Instance {
	bool active = false;

	ID3D12Device* device = nullptr;
	ID3D12CommandQueue* command_queue = nullptr;
	ComPtr<IDXGISwapChain3> swapchain;
	ID3D12DescriptorHeap* rtv_heap = nullptr;
	UINT rtv_descriptor_size = 0;
	ID3D12Resource* render_targets[D3D_FRAME_COUNT];
	ID3D12RootSignature* root_signature = nullptr;
	ID3D12CommandAllocator* command_allocator = nullptr;
	ID3D12CommandAllocator* helper_command_allocator = nullptr;
	ID3D12GraphicsCommandList* command_list = nullptr;
	ID3D12PipelineState* pipeline_state = nullptr;

	UINT frame_index = 0;
	ID3D12Fence* fence = nullptr;
	UINT64 fence_value = 0;
	HANDLE fence_event = NULL;

	ID3D12DescriptorHeap* srv_heap = nullptr;
	UINT srv_descriptor_size = 0;

	byte* vertex_buffer_ptr = nullptr; // pointer to mapped vertex buffer
	int xyz_elements = 0;
	int color_st_elements = 0;

	byte* index_buffer_ptr = nullptr; // pointer to mapped index buffer
	int index_buffer_offset = 0;

	ID3D12Resource* geometry_buffer = nullptr;
};

struct Dx_World {
	//
	// Resources.
	//
	Dx_Image images[MAX_VK_IMAGES];

	//
	// State.
	//
	int current_image_indices[2];
};
