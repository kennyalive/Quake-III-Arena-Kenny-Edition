#include "tr_local.h"

#include <chrono>
#include <functional>

#include "D3d12.h"
#include "D3d12SDKLayers.h"
#include "DXGI1_4.h"
#include "wrl.h"
#include "d3dx12.h"
#include <DirectXMath.h>

const int VERTEX_CHUNK_SIZE = 512 * 1024;

const int XYZ_SIZE      = 4 * VERTEX_CHUNK_SIZE;
const int COLOR_SIZE    = 1 * VERTEX_CHUNK_SIZE;
const int ST0_SIZE      = 2 * VERTEX_CHUNK_SIZE;
const int ST1_SIZE      = 2 * VERTEX_CHUNK_SIZE;

const int XYZ_OFFSET    = 0;
const int COLOR_OFFSET  = XYZ_OFFSET + XYZ_SIZE;
const int ST0_OFFSET    = COLOR_OFFSET + COLOR_SIZE;
const int ST1_OFFSET    = ST0_OFFSET + ST0_SIZE;

static const int VERTEX_BUFFER_SIZE = XYZ_SIZE + COLOR_SIZE + ST0_SIZE + ST1_SIZE;
static const int INDEX_BUFFER_SIZE = 2 * 1024 * 1024;

static DXGI_FORMAT get_depth_format() {
	if (r_stencilbits->integer > 0) {
		glConfig.stencilBits = 8;
		return DXGI_FORMAT_D24_UNORM_S8_UINT;
	} else {
		glConfig.stencilBits = 0;
		return DXGI_FORMAT_D32_FLOAT;
	}
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
static void get_hardware_adapter(IDXGIFactory4* p_factory, IDXGIAdapter1** pp_adapter) {
	ComPtr<IDXGIAdapter1> adapter;
	*pp_adapter = nullptr;

	for (UINT adapter_index = 0; DXGI_ERROR_NOT_FOUND != p_factory->EnumAdapters1(adapter_index, &adapter); ++adapter_index) {
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			// Don't select the Basic Render Driver adapter.
			// If you want a software adapter, pass in "/warp" on the command line.
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the
		// actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
			break;
		}
	}
	*pp_adapter = adapter.Detach();
}

void wait_for_previous_frame()
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

	// Signal and increment the fence value.
	const UINT64 fence = dx.fence_value;
	DX_CHECK(dx.command_queue->Signal(dx.fence, fence));
	dx.fence_value++;

	// Wait until the previous frame is finished.
	if (dx.fence->GetCompletedValue() < fence)
	{
		DX_CHECK(dx.fence->SetEventOnCompletion(fence, dx.fence_event));
		WaitForSingleObject(dx.fence_event, INFINITE);
	}

	dx.frame_index = dx.swapchain->GetCurrentBackBufferIndex();
}

static void wait_for_queue_idle(ID3D12CommandQueue* command_queue) {
	const UINT64 fence = dx.fence_value;
	DX_CHECK(command_queue->Signal(dx.fence, fence));
	dx.fence_value++;

	if (dx.fence->GetCompletedValue() < fence) {
		DX_CHECK(dx.fence->SetEventOnCompletion(fence, dx.fence_event));
		WaitForSingleObject(dx.fence_event, INFINITE);
	}
}

static void record_and_run_commands(ID3D12CommandQueue* command_queue, std::function<void(ID3D12GraphicsCommandList*)> recorder) {
	dx.helper_command_allocator->Reset();

	ID3D12GraphicsCommandList* command_list;
	DX_CHECK(dx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, dx.helper_command_allocator,
		nullptr, IID_PPV_ARGS(&command_list)));

	recorder(command_list);
	DX_CHECK(command_list->Close());

	ID3D12CommandList* command_lists[] = { command_list };
	command_queue->ExecuteCommandLists(1, command_lists);
	wait_for_queue_idle(command_queue);
	command_list->Release();
}

void dx_initialize() {
#if defined(_DEBUG)
	// Enable the D3D12 debug layer
	{
		ComPtr<ID3D12Debug> debug_controller;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
			debug_controller->EnableDebugLayer();
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
    DX_CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	ComPtr<IDXGIAdapter1> hardware_adapter;
	get_hardware_adapter(factory.Get(), &hardware_adapter);
	DX_CHECK(D3D12CreateDevice(hardware_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dx.device)));

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	DX_CHECK(dx.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&dx.command_queue)));

	// Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = D3D_FRAME_COUNT;
    swapChainDesc.Width = glConfig.vidWidth;
    swapChainDesc.Height = glConfig.vidHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapchain;
    DX_CHECK(factory->CreateSwapChainForHwnd(
        dx.command_queue,        // Swap chain needs the queue so that it can force a flush on it.
		g_wv.hWnd_dx,
        &swapChainDesc,
		nullptr,
		nullptr,
        &swapchain
        ));

	// This sample does not support fullscreen transitions.
	DX_CHECK(factory->MakeWindowAssociation(g_wv.hWnd_dx, DXGI_MWA_NO_ALT_ENTER));
	DX_CHECK(swapchain.As(&dx.swapchain));
	dx.frame_index = dx.swapchain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// RTV heap.
		{
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
			heap_desc.NumDescriptors = D3D_FRAME_COUNT;
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			heap_desc.NodeMask = 0;
			DX_CHECK(dx.device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&dx.rtv_heap)));
			dx.rtv_descriptor_size = dx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}

		// SRV heap.
		{
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
			heap_desc.NumDescriptors = MAX_DRAWIMAGES;
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heap_desc.NodeMask = 0;
			DX_CHECK(dx.device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&dx.srv_heap)));
			dx.srv_descriptor_size = dx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		// Sampler heap.
		{
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
			heap_desc.NumDescriptors = SAMPLER_COUNT;
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heap_desc.NodeMask = 0;
			DX_CHECK(dx.device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&dx.sampler_heap)));
			dx.sampler_descriptor_size = dx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		}
	}

	//
	// Create descriptors.
	//
	{
		// RTV descriptors.
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(dx.rtv_heap->GetCPUDescriptorHandleForHeapStart());
			for (UINT i = 0; i < D3D_FRAME_COUNT; i++)
			{
				DX_CHECK(dx.swapchain->GetBuffer(i, IID_PPV_ARGS(&dx.render_targets[i])));
				dx.device->CreateRenderTargetView(dx.render_targets[i], nullptr, rtv_handle);
				rtv_handle.Offset(1, dx.rtv_descriptor_size);
			}
		}

		// Samplers.
		{
			{
				Vk_Sampler_Def def;
				def.repeat_texture = true;
				def.gl_mag_filter = gl_filter_max;
				def.gl_min_filter = gl_filter_min;
				dx_create_sampler_descriptor(def, SAMPLER_MIP_REPEAT);
			}
			{
				Vk_Sampler_Def def;
				def.repeat_texture = false;
				def.gl_mag_filter = gl_filter_max;
				def.gl_min_filter = gl_filter_min;
				dx_create_sampler_descriptor(def, SAMPLER_MIP_CLAMP);
			}
			{
				Vk_Sampler_Def def;
				def.repeat_texture = true;
				def.gl_mag_filter = GL_LINEAR;
				def.gl_min_filter = GL_LINEAR;
				dx_create_sampler_descriptor(def, SAMPLER_NOMIP_REPEAT);
			}
			{
				Vk_Sampler_Def def;
				def.repeat_texture = false;
				def.gl_mag_filter = GL_LINEAR;
				def.gl_min_filter = GL_LINEAR;
				dx_create_sampler_descriptor(def, SAMPLER_NOMIP_CLAMP);
			}
		}
	}

	// Create depth buffer resources.
	{
		D3D12_RESOURCE_DESC depth_desc = {};
		depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depth_desc.Alignment = 0;
		depth_desc.Width = glConfig.vidWidth;
		depth_desc.Height = glConfig.vidHeight;
		depth_desc.DepthOrArraySize = 1;
		depth_desc.MipLevels = 1;
		depth_desc.Format = get_depth_format();
		depth_desc.SampleDesc.Count = 1;
		depth_desc.SampleDesc.Quality = 0;
		depth_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE optimized_clear_value = {};
		optimized_clear_value.Format = get_depth_format();
		optimized_clear_value.DepthStencil.Depth = 1.0f;
		optimized_clear_value.DepthStencil.Stencil = 0;

		DX_CHECK(dx.device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&depth_desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&optimized_clear_value,
			IID_PPV_ARGS(&dx.depth_stencil_buffer)));

		D3D12_DESCRIPTOR_HEAP_DESC ds_heap_desc = {};
		ds_heap_desc.NumDescriptors = 1;
		ds_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		ds_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		DX_CHECK(dx.device->CreateDescriptorHeap(&ds_heap_desc, IID_PPV_ARGS(&dx.dsv_heap)));

		D3D12_DEPTH_STENCIL_VIEW_DESC view_desc = {};
		view_desc.Format = get_depth_format();
		view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		view_desc.Flags = D3D12_DSV_FLAG_NONE;

		dx.device->CreateDepthStencilView(dx.depth_stencil_buffer, &view_desc,
			dx.dsv_heap->GetCPUDescriptorHandleForHeapStart());
	}

	DX_CHECK(dx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&dx.command_allocator)));
	DX_CHECK(dx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&dx.helper_command_allocator)));

	//
	// Create root signature.
	//
	{
		CD3DX12_DESCRIPTOR_RANGE ranges[4];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,		1, 0);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,		1, 1);
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 1);

		CD3DX12_ROOT_PARAMETER root_parameters[5];
		root_parameters[0].InitAsConstants(32, 0);
		root_parameters[1].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		root_parameters[2].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		root_parameters[3].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
		root_parameters[4].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);

		CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init(_countof(root_parameters), root_parameters, 0, nullptr,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature, error;
		DX_CHECK(D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1,
			&signature, &error));
		DX_CHECK(dx.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
			IID_PPV_ARGS(&dx.root_signature)));
	}

	DX_CHECK(dx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, dx.command_allocator, nullptr,
		IID_PPV_ARGS(&dx.command_list)));

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	DX_CHECK(dx.command_list->Close());

	// Create synchronization objects.
	{
		DX_CHECK(dx.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dx.fence)));
		dx.fence_value = 1;

		// Create an event handle to use for frame synchronization.
		dx.fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (dx.fence_event == NULL)
		{
			DX_CHECK(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		wait_for_previous_frame();
	}

	//
	// Geometry buffers.
	//
	{
		// store geometry in upload heap since Q3 regenerates it every frame
		DX_CHECK(dx.device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(VERTEX_BUFFER_SIZE + INDEX_BUFFER_SIZE),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&dx.geometry_buffer)));

		void* p_data;
		CD3DX12_RANGE read_range(0, 0);
        DX_CHECK(dx.geometry_buffer->Map(0, &read_range, &p_data));

		dx.vertex_buffer_ptr = static_cast<byte*>(p_data);
		assert(((size_t)dx.vertex_buffer_ptr & 0xffff) == 0);

		dx.index_buffer_ptr = static_cast<byte*>(p_data) + VERTEX_BUFFER_SIZE;
		assert(((size_t)dx.index_buffer_ptr & 0xffff) == 0);
	}

	dx.active = true;
}

void dx_shutdown() {
	dx.swapchain.Reset();

	dx.command_allocator->Release();
	dx.command_allocator = nullptr;

	dx.helper_command_allocator->Release();
	dx.helper_command_allocator = nullptr;

	for (int i = 0; i < D3D_FRAME_COUNT; i++) {
		dx.render_targets[i]->Release();
	}

	dx.rtv_heap->Release();
	dx.rtv_heap = nullptr;

	dx.srv_heap->Release();
	dx.srv_heap = nullptr;


	dx.sampler_heap->Release();
	dx.sampler_heap = nullptr;

	dx.root_signature->Release();
	dx.root_signature = nullptr;

	dx.command_queue->Release();
	dx.command_queue = nullptr;

	dx.command_list->Release();
	dx.command_list = nullptr;

	dx.fence->Release();
	dx.fence = nullptr;

	::CloseHandle(dx.fence_event);
	dx.fence_event = NULL;

	dx.depth_stencil_buffer->Release();
	dx.depth_stencil_buffer = nullptr;
	dx.dsv_heap->Release();
	dx.dsv_heap = nullptr;

	dx.geometry_buffer->Release();
	dx.geometry_buffer = nullptr;

	dx.device->Release();
	dx.device = nullptr;
}

void dx_release_resources() {
	dx_world.pipeline_create_time = 0.0f;
	for (int i = 0; i < dx_world.num_pipeline_states; i++) {
		dx_world.pipeline_states[i]->Release();
	}

	for (int i = 0; i < MAX_VK_IMAGES; i++) {
		if (dx_world.images[i].texture != nullptr) {
			dx_world.images[i].texture->Release();
		}
	}

	Com_Memset(&dx_world, 0, sizeof(dx_world));

	// Reset geometry buffer's current offsets.
	dx.xyz_elements = 0;
	dx.color_st_elements = 0;
	dx.index_buffer_offset = 0;
}

void dx_wait_device_idle() {
	wait_for_queue_idle(dx.command_queue);
}

Dx_Image dx_create_image(int width, int height, DXGI_FORMAT format, int mip_levels,  bool repeat_texture, int image_index) {
	Dx_Image image;

	// create texture
	{
		D3D12_RESOURCE_DESC desc;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Alignment = 0;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = mip_levels;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		DX_CHECK(dx.device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&image.texture)));
	}

	// create texture descriptor
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
		srv_desc.Format = format;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.Texture2D.MipLevels = mip_levels;

		D3D12_CPU_DESCRIPTOR_HANDLE handle;
		handle.ptr = dx.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + image_index * dx.srv_descriptor_size;
		dx.device->CreateShaderResourceView(image.texture, &srv_desc, handle);

		dx_world.current_image_indices[glState.currenttmu] = image_index;
	}

	if (mip_levels > 0)
		image.sampler_index = repeat_texture ? SAMPLER_MIP_REPEAT : SAMPLER_MIP_CLAMP;
	else
		image.sampler_index = repeat_texture ? SAMPLER_NOMIP_REPEAT : SAMPLER_NOMIP_CLAMP;

	return image;
}

void dx_upload_image_data(ID3D12Resource* texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel) {
	const UINT64 upload_buffer_size = GetRequiredIntermediateSize(texture, 0, mip_levels);

	ComPtr<ID3D12Resource> upload_texture;
	DX_CHECK(dx.device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(upload_buffer_size),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&upload_texture)));

	D3D12_SUBRESOURCE_DATA texture_data[16];
	
	for (int i = 0; i < mip_levels; i++) {
		texture_data[i].pData = pixels;
		texture_data[i].RowPitch = width * bytes_per_pixel;
		texture_data[i].SlicePitch = texture_data[i].RowPitch * height;
		
		pixels += texture_data[i].SlicePitch;

		width >>= 1;
		if (width < 1) width = 1;

		height >>= 1;
		if (height < 1) height = 1;
	}

	record_and_run_commands(dx.command_queue, [&texture, &upload_texture, &texture_data, &mip_levels]
		(ID3D12GraphicsCommandList* command_list)
	{
		command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));

		UpdateSubresources(command_list, texture, upload_texture.Get(), 0, 0, mip_levels, texture_data);

		command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	});
}

static ID3D12PipelineState* create_pipeline(const Vk_Pipeline_Def& def) {
	extern unsigned char single_texture_vs[];
	extern long long single_texture_vs_size;

	extern unsigned char multi_texture_vs[];
	extern long long multi_texture_vs_size;

	extern unsigned char single_texture_ps[];
	extern long long single_texture_ps_size;

	extern unsigned char multi_texture_mul_ps[];
	extern long long multi_texture_mul_ps_size;

	extern unsigned char multi_texture_add_ps[];
	extern long long multi_texture_add_ps_size;

	D3D12_SHADER_BYTECODE vs_bytecode;
	D3D12_SHADER_BYTECODE ps_bytecode;
	if (def.shader_type == Vk_Shader_Type::single_texture) {
		if (def.clipping_plane) {
			vs_bytecode = CD3DX12_SHADER_BYTECODE(single_texture_vs, single_texture_vs_size);
		} else {
			vs_bytecode = CD3DX12_SHADER_BYTECODE(single_texture_vs, single_texture_vs_size);
		}
		ps_bytecode = CD3DX12_SHADER_BYTECODE(single_texture_ps, single_texture_ps_size);
	} else if (def.shader_type == Vk_Shader_Type::multi_texture_mul) {
		if (def.clipping_plane) {
			vs_bytecode = CD3DX12_SHADER_BYTECODE(multi_texture_vs, multi_texture_vs_size);
		} else {
			vs_bytecode = CD3DX12_SHADER_BYTECODE(multi_texture_vs, multi_texture_vs_size);
		}
		ps_bytecode = CD3DX12_SHADER_BYTECODE(multi_texture_mul_ps, multi_texture_mul_ps_size);
	} else if (def.shader_type == Vk_Shader_Type::multi_texture_add) {
		if (def.clipping_plane) {
			vs_bytecode = CD3DX12_SHADER_BYTECODE(multi_texture_vs, multi_texture_vs_size);
		} else {
			vs_bytecode = CD3DX12_SHADER_BYTECODE(multi_texture_vs, multi_texture_vs_size);
		}
		ps_bytecode = CD3DX12_SHADER_BYTECODE(multi_texture_add_ps, multi_texture_add_ps_size);
	}

	// Vertex elements.
	D3D12_INPUT_ELEMENT_DESC input_element_desc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	//
	// Blend.
	//
	D3D12_BLEND_DESC blend_state;
	blend_state.AlphaToCoverageEnable = FALSE;
	blend_state.IndependentBlendEnable = FALSE;
	auto& rt_blend_desc = blend_state.RenderTarget[0];
	rt_blend_desc.BlendEnable = (def.state_bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) ? TRUE : FALSE;
	rt_blend_desc.LogicOpEnable = FALSE;

	if (rt_blend_desc.BlendEnable) {
		switch (def.state_bits & GLS_SRCBLEND_BITS) {
			case GLS_SRCBLEND_ZERO:
				rt_blend_desc.SrcBlend = D3D12_BLEND_ZERO;
				rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_ZERO;
				break;
			case GLS_SRCBLEND_ONE:
				rt_blend_desc.SrcBlend = D3D12_BLEND_ONE;
				rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
				break;
			case GLS_SRCBLEND_DST_COLOR:
				rt_blend_desc.SrcBlend = D3D12_BLEND_DEST_COLOR;
				rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_DEST_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
				rt_blend_desc.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
				rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
				break;
			case GLS_SRCBLEND_SRC_ALPHA:
				rt_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
				rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
				rt_blend_desc.SrcBlend = D3D12_BLEND_INV_SRC_ALPHA;
				rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_DST_ALPHA:
				rt_blend_desc.SrcBlend = D3D12_BLEND_DEST_ALPHA;
				rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_DEST_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
				rt_blend_desc.SrcBlend = D3D12_BLEND_INV_DEST_ALPHA;
				rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
				break;
			case GLS_SRCBLEND_ALPHA_SATURATE:
				rt_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA_SAT;
				rt_blend_desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA_SAT;
				break;
			default:
				ri.Error( ERR_DROP, "create_pipeline: invalid src blend state bits\n" );
				break;
		}
		switch (def.state_bits & GLS_DSTBLEND_BITS) {
			case GLS_DSTBLEND_ZERO:
				rt_blend_desc.DestBlend = D3D12_BLEND_ZERO;
				rt_blend_desc.DestBlendAlpha = D3D12_BLEND_ZERO;
				break;
			case GLS_DSTBLEND_ONE:
				rt_blend_desc.DestBlend = D3D12_BLEND_ONE;
				rt_blend_desc.DestBlendAlpha = D3D12_BLEND_ONE;
				break;
			case GLS_DSTBLEND_SRC_COLOR:
				rt_blend_desc.DestBlend = D3D12_BLEND_SRC_COLOR;
				rt_blend_desc.DestBlendAlpha = D3D12_BLEND_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
				rt_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
				rt_blend_desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_SRC_ALPHA:
				rt_blend_desc.DestBlend = D3D12_BLEND_SRC_ALPHA;
				rt_blend_desc.DestBlendAlpha = D3D12_BLEND_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
				rt_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
				rt_blend_desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_DST_ALPHA:
				rt_blend_desc.DestBlend = D3D12_BLEND_DEST_ALPHA;
				rt_blend_desc.DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
				rt_blend_desc.DestBlend = D3D12_BLEND_INV_DEST_ALPHA;
				rt_blend_desc.DestBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
				break;
			default:
				ri.Error( ERR_DROP, "create_pipeline: invalid dst blend state bits\n" );
				break;
		}
	}
	rt_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
	rt_blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt_blend_desc.LogicOp = D3D12_LOGIC_OP_COPY;
	rt_blend_desc.RenderTargetWriteMask = (def.shadow_phase == Vk_Shadow_Phase::shadow_edges_rendering) ? 0 : D3D12_COLOR_WRITE_ENABLE_ALL;

	//
	// Rasteriazation state.
	//
	D3D12_RASTERIZER_DESC rasterization_state = {};
	rasterization_state.FillMode = (def.state_bits & GLS_POLYMODE_LINE) ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;

	if (def.face_culling == CT_TWO_SIDED)
		rasterization_state.CullMode = D3D12_CULL_MODE_NONE;
	else if (def.face_culling == CT_FRONT_SIDED)
		rasterization_state.CullMode = (def.mirror ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK);
	else if (def.face_culling == CT_BACK_SIDED)
		rasterization_state.CullMode = (def.mirror ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_FRONT);
	else
		ri.Error(ERR_DROP, "create_pipeline: invalid face culling mode\n");

	rasterization_state.FrontCounterClockwise = FALSE; // Q3 defaults to clockwise vertex order
	rasterization_state.DepthBias = def.polygon_offset ? r_offsetUnits->integer : 0;
	rasterization_state.DepthBiasClamp = 0.0f;
	rasterization_state.SlopeScaledDepthBias = def.polygon_offset ? r_offsetFactor->value : 0.0f;
	rasterization_state.DepthClipEnable = TRUE;
	rasterization_state.MultisampleEnable = FALSE;
	rasterization_state.AntialiasedLineEnable = FALSE;
	rasterization_state.ForcedSampleCount = 0;
	rasterization_state.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	//
	// Depth/stencil state.
	//
	D3D12_DEPTH_STENCIL_DESC depth_stencil_state = {};
	depth_stencil_state.DepthEnable = (def.state_bits & GLS_DEPTHTEST_DISABLE) ? FALSE : TRUE;
	depth_stencil_state.DepthWriteMask = (def.state_bits & GLS_DEPTHMASK_TRUE) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	depth_stencil_state.DepthFunc = (def.state_bits & GLS_DEPTHFUNC_EQUAL) ? D3D12_COMPARISON_FUNC_EQUAL : D3D12_COMPARISON_FUNC_LESS_EQUAL;
	depth_stencil_state.StencilEnable = (def.shadow_phase != Vk_Shadow_Phase::disabled) ? TRUE : FALSE;
	depth_stencil_state.StencilReadMask = 255;
	depth_stencil_state.StencilWriteMask = 255;

	if (def.shadow_phase == Vk_Shadow_Phase::shadow_edges_rendering) {
		depth_stencil_state.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilPassOp = (def.face_culling == CT_FRONT_SIDED) ? D3D12_STENCIL_OP_INCR_SAT : D3D12_STENCIL_OP_DECR_SAT;
		depth_stencil_state.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;

		depth_stencil_state.BackFace = depth_stencil_state.FrontFace;
	} else if (def.shadow_phase == Vk_Shadow_Phase::fullscreen_quad_rendering) {
		depth_stencil_state.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		depth_stencil_state.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;

		depth_stencil_state.BackFace = depth_stencil_state.FrontFace;
	} else {
		depth_stencil_state.FrontFace = {};
		depth_stencil_state.BackFace = {};
	}

	//
	// Create pipeline state.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
	pipeline_desc.pRootSignature = dx.root_signature;
	pipeline_desc.VS = vs_bytecode;
	pipeline_desc.PS = ps_bytecode;
	pipeline_desc.BlendState = blend_state;
	pipeline_desc.SampleMask = UINT_MAX;
	pipeline_desc.RasterizerState = rasterization_state;
	pipeline_desc.DepthStencilState = depth_stencil_state;
	pipeline_desc.InputLayout = { input_element_desc, _countof(input_element_desc) };
	pipeline_desc.PrimitiveTopologyType = def.line_primitives ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipeline_desc.NumRenderTargets = 1;
	pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pipeline_desc.DSVFormat = get_depth_format();
	pipeline_desc.SampleDesc.Count = 1;
	pipeline_desc.SampleDesc.Quality = 0;

	ID3D12PipelineState* pipeline_state;
	DX_CHECK(dx.device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline_state)));
	return pipeline_state;
}

struct Timer {
	using Clock = std::chrono::high_resolution_clock;
	using Second = std::chrono::duration<double, std::ratio<1>>;

	Clock::time_point start = Clock::now();
	double elapsed_seconds() const {
		const auto duration = Clock::now() - start;
		double seconds = std::chrono::duration_cast<Second>(duration).count();
		return seconds;
	}
};

void dx_create_sampler_descriptor(const Vk_Sampler_Def& def, Dx_Sampler_Index sampler_index)
{
	uint32_t min, mag, mip;

	if (def.gl_mag_filter == GL_NEAREST) {
		mag = 0;
	} else if (def.gl_mag_filter == GL_LINEAR) {
		mag = 1;
	} else {
		ri.Error(ERR_FATAL, "create_sampler_descriptor: invalid gl_mag_filter");
	}

	bool max_lod_0_25 = false; // used to emulate OpenGL's GL_LINEAR/GL_NEAREST minification filter
	if (def.gl_min_filter == GL_NEAREST) {
		min = 0;
		mip = 0;
		max_lod_0_25 = true;
	} else if (def.gl_min_filter == GL_LINEAR) {
		min = 1;
		mip = 0;
		max_lod_0_25 = true;
	} else if (def.gl_min_filter == GL_NEAREST_MIPMAP_NEAREST) {
		min = 0;
		mip = 0;
	} else if (def.gl_min_filter == GL_LINEAR_MIPMAP_NEAREST) {
		min = 1;
		mip = 0;
	} else if (def.gl_min_filter == GL_NEAREST_MIPMAP_LINEAR) {
		min = 0;
		mip = 1;
	} else if (def.gl_min_filter == GL_LINEAR_MIPMAP_LINEAR) {
		min = 1;
		mip = 1;
	} else {
		ri.Error(ERR_FATAL, "vk_find_sampler: invalid gl_min_filter");
	}

	D3D12_TEXTURE_ADDRESS_MODE address_mode = def.repeat_texture ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

	D3D12_SAMPLER_DESC sampler_desc;
	sampler_desc.Filter = D3D12_ENCODE_BASIC_FILTER(min, mag, mip, 0);
	sampler_desc.AddressU = address_mode;
	sampler_desc.AddressV = address_mode;
	sampler_desc.AddressW = address_mode;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 1;
	sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler_desc.BorderColor[0] = 0.0f;
	sampler_desc.BorderColor[1] = 0.0f;
	sampler_desc.BorderColor[2] = 0.0f;
	sampler_desc.BorderColor[3] = 0.0f;
	sampler_desc.MinLOD = 0.0f;
	sampler_desc.MaxLOD = max_lod_0_25 ? 0.25f : 12.0f;

	D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle = dx.sampler_heap->GetCPUDescriptorHandleForHeapStart();
	sampler_handle.ptr += dx.sampler_descriptor_size * sampler_index;

	dx.device->CreateSampler(&sampler_desc, sampler_handle);
}

ID3D12PipelineState* dx_find_pipeline(const Vk_Pipeline_Def& def) {
	for (int i = 0; i < dx_world.num_pipeline_states; i++) {
		const auto& cur_def = dx_world.pipeline_defs[i];

		if (cur_def.shader_type == def.shader_type &&
			cur_def.state_bits == def.state_bits &&
			cur_def.face_culling == def.face_culling &&
			cur_def.polygon_offset == def.polygon_offset &&
			cur_def.clipping_plane == def.clipping_plane &&
			cur_def.mirror == def.mirror &&
			cur_def.line_primitives == def.line_primitives &&
			cur_def.shadow_phase == def.shadow_phase)
		{
			return dx_world.pipeline_states[i];
		}
	}

	if (dx_world.num_pipeline_states >= MAX_VK_PIPELINES) {
		ri.Error(ERR_DROP, "dx_find_pipeline: MAX_VK_PIPELINES hit\n");
	}

	Timer t;
	ID3D12PipelineState* pipeline_state = create_pipeline(def);
	dx_world.pipeline_create_time += t.elapsed_seconds();

	dx_world.pipeline_defs[dx_world.num_pipeline_states] = def;
	dx_world.pipeline_states[dx_world.num_pipeline_states] = pipeline_state;
	dx_world.num_pipeline_states++;
	return pipeline_state;
}

static void get_mvp_transform(float* mvp) {
	if (backEnd.projection2D) {
		float mvp0 = 2.0f / glConfig.vidWidth;
		float mvp5 = 2.0f / glConfig.vidHeight;

		mvp[0]  =  mvp0; mvp[1]  =  0.0f; mvp[2]  = 0.0f; mvp[3]  = 0.0f;
		mvp[4]  =  0.0f; mvp[5]  = -mvp5; mvp[6]  = 0.0f; mvp[7]  = 0.0f;
		mvp[8]  =  0.0f; mvp[9]  = 0.0f; mvp[10] = 1.0f; mvp[11] = 0.0f;
		mvp[12] = -1.0f; mvp[13] = 1.0f; mvp[14] = 0.0f; mvp[15] = 1.0f;

	} else {
		const float* p = backEnd.viewParms.projectionMatrix;

		// update q3's proj matrix (opengl) to d3d conventions: z - [0, 1] instead of [-1, 1]
		float zNear	= r_znear->value;
		float zFar = backEnd.viewParms.zFar;
		float P10 = -zFar / (zFar - zNear);
		float P14 = -zFar*zNear / (zFar - zNear);

		float proj[16] = {
			p[0],  p[1],  p[2], p[3],
			p[4],  p[5],  p[6], p[7],
			p[8],  p[9],  P10,  p[11],
			p[12], p[13], P14,  p[15]
		};

		myGlMultMatrix(vk_world.modelview_transform, proj, mvp);
	}
}

void dx_bind_geometry() {
	if (!dx.active) 
		return;

	// xyz stream
	{
		if ((dx.xyz_elements + tess.numVertexes) * sizeof(vec4_t) > XYZ_SIZE)
			ri.Error(ERR_DROP, "dx_bind_geometry: vertex buffer overflow (xyz)\n");

		byte* dst = dx.vertex_buffer_ptr + XYZ_OFFSET + dx.xyz_elements * sizeof(vec4_t);
		Com_Memcpy(dst, tess.xyz, tess.numVertexes * sizeof(vec4_t));

		uint32_t xyz_offset = XYZ_OFFSET + dx.xyz_elements * sizeof(vec4_t);

		D3D12_VERTEX_BUFFER_VIEW xyz_view;
		xyz_view.BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + xyz_offset;
		xyz_view.SizeInBytes = static_cast<UINT>(tess.numVertexes * sizeof(vec4_t));
		xyz_view.StrideInBytes = static_cast<UINT>(sizeof(vec4_t));
		dx.command_list->IASetVertexBuffers(0, 1, &xyz_view);

		dx.xyz_elements += tess.numVertexes;
	}

	// indexes stream
	{
		std::size_t indexes_size = tess.numIndexes * sizeof(uint32_t);        

		if (dx.index_buffer_offset + indexes_size > INDEX_BUFFER_SIZE)
			ri.Error(ERR_DROP, "dx_bind_geometry: index buffer overflow\n");

		byte* dst = dx.index_buffer_ptr + dx.index_buffer_offset;
		Com_Memcpy(dst, tess.indexes, indexes_size);

		D3D12_INDEX_BUFFER_VIEW index_view;
		index_view.BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + VERTEX_BUFFER_SIZE + dx.index_buffer_offset;
		index_view.SizeInBytes = static_cast<UINT>(indexes_size);
		index_view.Format = DXGI_FORMAT_R32_UINT;
		dx.command_list->IASetIndexBuffer(&index_view);

		dx.index_buffer_offset += static_cast<int>(indexes_size);
	}

	//
	// Specify push constants.
	//
	float root_constants[16 + 12 + 4]; // mvp transform + eye transform + clipping plane in eye space

	get_mvp_transform(root_constants);
	int root_constant_count = 16;

	if (backEnd.viewParms.isPortal) {
		// Eye space transform.
		// NOTE: backEnd.or.modelMatrix incorporates s_flipMatrix, so it should be taken into account 
		// when computing clipping plane too.
		float* eye_xform = root_constants + 16;
		for (int i = 0; i < 12; i++) {
			eye_xform[i] = backEnd.or.modelMatrix[(i%4)*4 + i/4 ];
		}

		// Clipping plane in eye coordinates.
		float world_plane[4];
		world_plane[0] = backEnd.viewParms.portalPlane.normal[0];
		world_plane[1] = backEnd.viewParms.portalPlane.normal[1];
		world_plane[2] = backEnd.viewParms.portalPlane.normal[2];
		world_plane[3] = backEnd.viewParms.portalPlane.dist;

		float eye_plane[4];
		eye_plane[0] = DotProduct (backEnd.viewParms.or.axis[0], world_plane);
		eye_plane[1] = DotProduct (backEnd.viewParms.or.axis[1], world_plane);
		eye_plane[2] = DotProduct (backEnd.viewParms.or.axis[2], world_plane);
		eye_plane[3] = DotProduct (world_plane, backEnd.viewParms.or.origin) - world_plane[3];

		// Apply s_flipMatrix to be in the same coordinate system as eye_xfrom.
		root_constants[28] = -eye_plane[1];
		root_constants[29] =  eye_plane[2];
		root_constants[30] = -eye_plane[0];
		root_constants[31] =  eye_plane[3];

		root_constant_count += 16;
	}
	dx.command_list->SetGraphicsRoot32BitConstants(0, root_constant_count, root_constants, 0);
}

static D3D12_RECT get_viewport_rect() {
	D3D12_RECT r;
	if (backEnd.projection2D) {
		r.left = 0.0f;
		r.top = 0.0f;
		r.right = glConfig.vidWidth;
		r.bottom = glConfig.vidHeight;
	} else {
		r.left = backEnd.viewParms.viewportX;
		r.top = glConfig.vidHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight);
		r.right = r.left + backEnd.viewParms.viewportWidth;
		r.bottom = r.top + backEnd.viewParms.viewportHeight;
	}
	return r;
}

static D3D12_VIEWPORT get_viewport(Vk_Depth_Range depth_range) {
	D3D12_RECT r = get_viewport_rect();

	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = (float)r.left;
	viewport.TopLeftY = (float)r.top;
	viewport.Width = (float)(r.right - r.left);
	viewport.Height = (float)(r.bottom - r.top);

	if (depth_range == Vk_Depth_Range::force_zero) {
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 0.0f;
	} else if (depth_range == Vk_Depth_Range::force_one) {
		viewport.MinDepth = 1.0f;
		viewport.MaxDepth = 1.0f;
	} else if (depth_range == Vk_Depth_Range::weapon) {
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 0.3f;
	} else {
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
	}
	return viewport;
}

static D3D12_RECT get_scissor_rect() {
	D3D12_RECT r = get_viewport_rect();

	if (r.left < 0)
		r.left = 0;
	if (r.top < 0)
		r.top = 0;

	if (r.right > glConfig.vidWidth)
		r.right = glConfig.vidWidth;
	if (r.bottom > glConfig.vidHeight)
		r.bottom = glConfig.vidHeight;

	return r;
}

void dx_shade_geometry(ID3D12PipelineState* pipeline_state, bool multitexture, Vk_Depth_Range depth_range, bool indexed) {
	// color
	{
		if ((dx.color_st_elements + tess.numVertexes) * sizeof(color4ub_t) > COLOR_SIZE)
			ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (color)\n");

		byte* dst = dx.vertex_buffer_ptr + COLOR_OFFSET + dx.color_st_elements * sizeof(color4ub_t);
		Com_Memcpy(dst, tess.svars.colors, tess.numVertexes * sizeof(color4ub_t));
	}
	// st0
	{
		if ((dx.color_st_elements + tess.numVertexes) * sizeof(vec2_t) > ST0_SIZE)
			ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (st0)\n");

		byte* dst = dx.vertex_buffer_ptr + ST0_OFFSET + dx.color_st_elements * sizeof(vec2_t);
		Com_Memcpy(dst, tess.svars.texcoords[0], tess.numVertexes * sizeof(vec2_t));
	}
	// st1
	if (multitexture) {
		if ((dx.color_st_elements + tess.numVertexes) * sizeof(vec2_t) > ST1_SIZE)
			ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (st1)\n");

		byte* dst = dx.vertex_buffer_ptr + ST1_OFFSET + dx.color_st_elements * sizeof(vec2_t);
		Com_Memcpy(dst, tess.svars.texcoords[1], tess.numVertexes * sizeof(vec2_t));
	}

	// configure vertex data stream
	D3D12_VERTEX_BUFFER_VIEW color_st_views[3];
	color_st_views[0].BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + COLOR_OFFSET + dx.color_st_elements * sizeof(color4ub_t);
	color_st_views[0].SizeInBytes = static_cast<UINT>(tess.numVertexes * sizeof(color4ub_t));
	color_st_views[0].StrideInBytes = static_cast<UINT>(sizeof(color4ub_t));

	color_st_views[1].BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + ST0_OFFSET + dx.color_st_elements * sizeof(vec2_t);
	color_st_views[1].SizeInBytes = static_cast<UINT>(tess.numVertexes * sizeof(vec2_t));
	color_st_views[1].StrideInBytes = static_cast<UINT>(sizeof(vec2_t));

	color_st_views[2].BufferLocation = dx.geometry_buffer->GetGPUVirtualAddress() + ST1_OFFSET + dx.color_st_elements * sizeof(vec2_t);
	color_st_views[2].SizeInBytes = static_cast<UINT>(tess.numVertexes * sizeof(vec2_t));
	color_st_views[2].StrideInBytes = static_cast<UINT>(sizeof(vec2_t));

	dx.command_list->IASetVertexBuffers(1, multitexture ? 3 : 2, color_st_views);

	dx.color_st_elements += tess.numVertexes;

	//
	// Set descriptor tables.
	//
	{
		D3D12_GPU_DESCRIPTOR_HANDLE srv_handle = dx.srv_heap->GetGPUDescriptorHandleForHeapStart();
		srv_handle.ptr += dx.srv_descriptor_size * dx_world.current_image_indices[0];
		dx.command_list->SetGraphicsRootDescriptorTable(1, srv_handle);

		D3D12_GPU_DESCRIPTOR_HANDLE sampler_handle = dx.sampler_heap->GetGPUDescriptorHandleForHeapStart();
		const int sampler_index = dx_world.images[dx_world.current_image_indices[0]].sampler_index;
		sampler_handle.ptr += dx.sampler_descriptor_size * sampler_index;
		dx.command_list->SetGraphicsRootDescriptorTable(2, sampler_handle);
	}

	if (multitexture) {
		D3D12_GPU_DESCRIPTOR_HANDLE srv_handle = dx.srv_heap->GetGPUDescriptorHandleForHeapStart();
		srv_handle.ptr += dx.srv_descriptor_size * dx_world.current_image_indices[1];
		dx.command_list->SetGraphicsRootDescriptorTable(3, srv_handle);

		D3D12_GPU_DESCRIPTOR_HANDLE sampler_handle = dx.sampler_heap->GetGPUDescriptorHandleForHeapStart();
		const int sampler_index = dx_world.images[dx_world.current_image_indices[1]].sampler_index;
		sampler_handle.ptr += dx.sampler_descriptor_size * sampler_index;
		dx.command_list->SetGraphicsRootDescriptorTable(4, sampler_handle);
	}

	// bind pipeline
	dx.command_list->SetPipelineState(pipeline_state);

	// configure pipeline's dynamic state
	D3D12_RECT scissor_rect = get_scissor_rect();
	dx.command_list->RSSetScissorRects(1, &scissor_rect);

	D3D12_VIEWPORT viewport = get_viewport(depth_range);
	dx.command_list->RSSetViewports(1, &viewport);

	/*if (tess.shader->polygonOffset) {
		vkCmdSetDepthBias(vk.command_buffer, r_offsetUnits->value, 0.0f, r_offsetFactor->value);
	}*/

	// issue draw call
	if (indexed)
		dx.command_list->DrawIndexedInstanced(tess.numIndexes, 1, 0, 0, 0);
	else
		dx.command_list->DrawInstanced(tess.numVertexes, 1, 0, 0);

	vk_world.dirty_depth_attachment = true;
}

void dx_begin_frame() {
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	DX_CHECK(dx.command_allocator->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	DX_CHECK(dx.command_list->Reset(dx.command_allocator, nullptr));

	// Set necessary state.
	dx.command_list->SetGraphicsRootSignature(dx.root_signature);

	CD3DX12_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(glConfig.vidWidth), static_cast<float>(glConfig.vidHeight));
	dx.command_list->RSSetViewports(1, &viewport);

	CD3DX12_RECT scissorRect(0, 0, static_cast<LONG>(glConfig.vidWidth), static_cast<LONG>(glConfig.vidHeight));
	dx.command_list->RSSetScissorRects(1, &scissorRect);

	ID3D12DescriptorHeap* heaps[] = { dx.srv_heap, dx.sampler_heap };
	dx.command_list->SetDescriptorHeaps(_countof(heaps), heaps);

	// Indicate that the back buffer will be used as a render target.
	dx.command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dx.render_targets[dx.frame_index],
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(dx.dsv_heap->GetCPUDescriptorHandleForHeapStart());
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(dx.rtv_heap->GetCPUDescriptorHandleForHeapStart(), dx.frame_index, dx.rtv_descriptor_size);
	dx.command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	dx.command_list->ClearRenderTargetView(rtv_handle, clearColor, 0, nullptr);
	dx.command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	dx.command_list->ClearDepthStencilView(dx.dsv_heap->GetCPUDescriptorHandleForHeapStart(),
		D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	dx.xyz_elements = 0;
	dx.color_st_elements = 0;
	dx.index_buffer_offset = 0;
}

void dx_end_frame() {
	dx.command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dx.render_targets[dx.frame_index],
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	DX_CHECK(dx.command_list->Close());

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { dx.command_list };
	dx.command_queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	DX_CHECK(dx.swapchain->Present(1, 0));

	wait_for_previous_frame();
}
