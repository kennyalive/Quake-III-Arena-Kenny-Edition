#include "tr_local.h"

#include <functional>

#include "D3d12.h"
#include "D3d12SDKLayers.h"
#include "DXGI1_4.h"
#include "wrl.h"
#include "d3dx12.h"
#include "D3Dcompiler.h"
#include <DirectXMath.h>

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

static void record_and_run_commands(ID3D12CommandAllocator* command_allocator, ID3D12CommandQueue* command_queue,
	std::function<void(ID3D12GraphicsCommandList*)> recorder) {

	ID3D12GraphicsCommandList* command_list;
	DX_CHECK(dx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
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
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = D3D_FRAME_COUNT;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		DX_CHECK(dx.device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&dx.rtv_heap)));

		dx.rtv_descriptor_size = dx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
		srv_heap_desc.NumDescriptors = MAX_DRAWIMAGES;
		srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		DX_CHECK(dx.device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&dx.srv_heap)));

		dx.srv_descriptor_size = dx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(dx.rtv_heap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < D3D_FRAME_COUNT; n++)
		{
			DX_CHECK(dx.swapchain->GetBuffer(n, IID_PPV_ARGS(&dx.render_targets[n])));
			dx.device->CreateRenderTargetView(dx.render_targets[n], nullptr, rtv_handle);
			rtv_handle.Offset(1, dx.rtv_descriptor_size);
		}
	}

	DX_CHECK(dx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&dx.command_allocator)));

	// Create the root signature.
    {
		CD3DX12_DESCRIPTOR_RANGE ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER root_parameters[1];
		root_parameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init(_countof(root_parameters), root_parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        DX_CHECK(D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        DX_CHECK(dx.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&dx.root_signature)));
    }

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		DX_CHECK(D3DCompileFromFile(L"d:/Quake-III-Arena-Kenny-Edition/src/engine/renderer/shaders/shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		DX_CHECK(D3DCompileFromFile(L"d:/Quake-III-Arena-Kenny-Edition/src/engine/renderer/shaders/shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = dx.root_signature;
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		DX_CHECK(dx.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&dx.pipeline_state)));
	}

	DX_CHECK(dx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, dx.command_allocator, dx.pipeline_state, IID_PPV_ARGS(&dx.command_list)));

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	DX_CHECK(dx.command_list->Close());

	// Create the vertex buffer.
	{
		struct Vertex
		{
			XMFLOAT3 position;
			XMFLOAT2 color;
		};

		float aspect_ratio = float(glConfig.vidWidth) / float(glConfig.vidHeight);

		// Define the geometry for a triangle.
		Vertex triangleVertices[] =
		{
			{ { 0.0f, 0.5f * aspect_ratio, 0.0f }, { 0.5f, 0.0f } },
			{ { 0.5f, -0.5f * aspect_ratio, 0.0f }, { 1.0f, 1.0f } },
			{ { -0.5f, -0.5f * aspect_ratio, 0.0f }, { 0.0f, 1.0f } }
		};

		const UINT vertexBufferSize = sizeof(triangleVertices);

		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		DX_CHECK(dx.device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&dx.vertex_buffer)));

		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
		DX_CHECK(dx.vertex_buffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		dx.vertex_buffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view.
		dx.vertex_buffer_view.BufferLocation = dx.vertex_buffer->GetGPUVirtualAddress();
		dx.vertex_buffer_view.StrideInBytes = sizeof(Vertex);
		dx.vertex_buffer_view.SizeInBytes = vertexBufferSize;
	}

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

	dx.active = true;
}

void dx_shutdown() {
	dx.swapchain.Reset();

	dx.command_allocator->Release();
	dx.command_allocator = nullptr;

	for (int i = 0; i < D3D_FRAME_COUNT; i++) {
		dx.render_targets[i]->Release();
	}

	dx.rtv_heap->Release();
	dx.rtv_heap = nullptr;

	dx.srv_heap->Release();
	dx.srv_heap = nullptr;

	dx.root_signature->Release();
	dx.root_signature = nullptr;

	dx.command_queue->Release();
	dx.command_queue = nullptr;

	dx.command_list->Release();
	dx.command_list = nullptr;

	dx.pipeline_state->Release();
	dx.pipeline_state = nullptr;

	dx.fence->Release();
	dx.fence = nullptr;

	::CloseHandle(dx.fence_event);
	dx.fence_event = NULL;

	dx.vertex_buffer->Release();
	dx.vertex_buffer = nullptr;

	dx.device->Release();
	dx.device = nullptr;
}

void dx_release_resources() {
	for (int i = 0; i < MAX_VK_IMAGES; i++) {
		if (dx_world.images[i].texture != nullptr) {
			dx_world.images[i].texture->Release();
		}
	}
	Com_Memset(&dx_world, 0, sizeof(dx_world));
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
		desc.MipLevels = 1; //  /* mip_levels */; !!!!!!!!!!!!!!!!!!!!!!
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
		srv_desc.Texture2D.MipLevels = 1; // !!!!!!!!!!!!!!!!!!!!!!1

		D3D12_CPU_DESCRIPTOR_HANDLE handle;
		handle.ptr = dx.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + image_index * dx.srv_descriptor_size;
		dx.device->CreateShaderResourceView(image.texture, &srv_desc, handle);
	}

	return image;
}

void dx_upload_image_data(ID3D12Resource* texture, int width, int height, bool mipmap, const uint8_t* pixels, int bytes_per_pixel) {
	const UINT64 upload_buffer_size = GetRequiredIntermediateSize(texture, 0, 1);

	ComPtr<ID3D12Resource> upload_texture;
	DX_CHECK(dx.device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(upload_buffer_size),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&upload_texture)));

	D3D12_SUBRESOURCE_DATA texture_data {};
	texture_data.pData = pixels;
	texture_data.RowPitch = width * bytes_per_pixel;
	texture_data.SlicePitch = texture_data.RowPitch * height;

	record_and_run_commands(dx.command_allocator, dx.command_queue,
		[&texture, &upload_texture, &texture_data](ID3D12GraphicsCommandList* command_list)
	{
		command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));

		UpdateSubresources(command_list, texture, upload_texture.Get(), 0, 0, 1, &texture_data);

		command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	});
}

void dx_begin_frame() {
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	DX_CHECK(dx.command_allocator->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	DX_CHECK(dx.command_list->Reset(dx.command_allocator, dx.pipeline_state));

	// Set necessary state.
	dx.command_list->SetGraphicsRootSignature(dx.root_signature);

	CD3DX12_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(glConfig.vidWidth), static_cast<float>(glConfig.vidHeight));
	dx.command_list->RSSetViewports(1, &viewport);

	CD3DX12_RECT scissorRect(0, 0, static_cast<LONG>(glConfig.vidWidth), static_cast<LONG>(glConfig.vidHeight));
	dx.command_list->RSSetScissorRects(1, &scissorRect);

	ID3D12DescriptorHeap* heaps[] = { dx.srv_heap };
	dx.command_list->SetDescriptorHeaps(_countof(heaps), heaps);

	int image_index = 35 + (Com_Milliseconds() / 1000) %  (tr.numImages - 40);
	D3D12_GPU_DESCRIPTOR_HANDLE handle = dx.srv_heap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += image_index * dx.srv_descriptor_size;
	dx.command_list->SetGraphicsRootDescriptorTable(0, handle);

	// Indicate that the back buffer will be used as a render target.
	dx.command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dx.render_targets[dx.frame_index],
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(dx.rtv_heap->GetCPUDescriptorHandleForHeapStart(), dx.frame_index, dx.rtv_descriptor_size);
	dx.command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	dx.command_list->ClearRenderTargetView(rtv_handle, clearColor, 0, nullptr);
	dx.command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	dx.command_list->IASetVertexBuffers(0, 1, &dx.vertex_buffer_view);
	dx.command_list->DrawInstanced(3, 1, 0, 0);

	// Indicate that the back buffer will now be used to present.
	dx.command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dx.render_targets[dx.frame_index],
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	DX_CHECK(dx.command_list->Close());
}

void dx_end_frame() {
	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { dx.command_list };
	dx.command_queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	DX_CHECK(dx.swapchain->Present(1, 0));

	wait_for_previous_frame();
}
