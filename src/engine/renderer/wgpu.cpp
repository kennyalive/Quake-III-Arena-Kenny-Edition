#include "tr_local.h"
#include "../platform/win_local.h"

#include <vector>

static const int GEOMETRY_BUFFER_SIZE = 8 * 1024 * 1024;

static WGPUTextureFormat get_depth_format() {
	if (r_stencilbits->integer > 0) {
		glConfig.stencilBits = 8;
		return WGPUTextureFormat_Depth24PlusStencil8;
	} else {
		glConfig.stencilBits = 0;
		return WGPUTextureFormat_Depth32Float;
	}
}

//
// Initialization
//
struct Wgpu_Request_Result {
	bool success = false;
	char message[MAX_STRING_CHARS] = {};
};

static void set_request_result(Wgpu_Request_Result& result, bool success, WGPUStringView message) {
	result.success = success;
	if (message.data != nullptr) {
		size_t length = message.length == WGPU_STRLEN ? strlen(message.data) : message.length;
		length = std::min(length, sizeof(result.message) - 1);
		Com_Memcpy(result.message, message.data, length);
		result.message[length] = '\0';
	}
}

static void wait_for_request(WGPUFuture future, const char* operation, Wgpu_Request_Result& result) {
	WGPUFutureWaitInfo wait_info = WGPU_FUTURE_WAIT_INFO_INIT;
	wait_info.future = future;
	WGPUWaitStatus status = wgpuInstanceWaitAny(wgpu.instance, 1, &wait_info, UINT64_MAX);
	if (status != WGPUWaitStatus_Success || !result.success) {
		char message[MAX_STRING_CHARS + 128];
		Com_sprintf(message, sizeof(message), "%s failed (wait status %d): %s", operation, (int)status, result.message);
		wgpu_shutdown();
		ri.Error(ERR_FATAL, "WebGPU: %s", message);
	}
}

static void log_device_error(const char* event, int status, WGPUStringView message) {
	const char* data = message.data != nullptr ? message.data : "";
	size_t length = message.length == WGPU_STRLEN ? strlen(data) : message.length;
	char text[MAX_STRING_CHARS];
	snprintf(text, sizeof(text), "WebGPU %s (%d): %.*s\n", event, status,
		(int)std::min(length, sizeof(text) - 1), data);

	// These callbacks can run on Dawn threads, so avoid calling into the engine
#ifdef _WIN32
	OutputDebugStringA(text);
#endif
	fputs(text, stderr);
}

static void end_render_pass() {
	if (wgpu.render_pass != nullptr) {
		wgpuRenderPassEncoderEnd(wgpu.render_pass);
		wgpuRenderPassEncoderRelease(wgpu.render_pass);
		wgpu.render_pass = nullptr;
	}
}

static void release_frame() {
	end_render_pass();
	if (wgpu.command_encoder != nullptr) {
		wgpuCommandEncoderRelease(wgpu.command_encoder);
		wgpu.command_encoder = nullptr;
	}
	if (wgpu.frame_view != nullptr) {
		wgpuTextureViewRelease(wgpu.frame_view);
		wgpu.frame_view = nullptr;
	}
	if (wgpu.frame_texture != nullptr) {
		wgpuTextureRelease(wgpu.frame_texture);
		wgpu.frame_texture = nullptr;
	}
}

WGPURenderPipeline create_pipeline(const Vk_Pipeline_Def& def);

void wgpu_initialize() {
	//
	// Instance
	//
	WGPUInstanceFeatureName instance_features[] = {
		WGPUInstanceFeatureName_TimedWaitAny,
		WGPUInstanceFeatureName_ShaderSourceSPIRV
	};
	WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
	instance_desc.requiredFeatureCount = 2;
	instance_desc.requiredFeatures = instance_features;
	wgpu.instance = wgpuCreateInstance(&instance_desc);
	if (wgpu.instance == nullptr) {
		wgpu_shutdown();
		ri.Error(ERR_FATAL, "WebGPU: failed to create the Dawn instance");
		return;
	}

	//
	// Surface
	//
	{
		WGPUSurfaceSourceWindowsHWND window_desc = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
		window_desc.hinstance = GetModuleHandle(nullptr);
		window_desc.hwnd = g_wv.hWnd_webgpu;
		WGPUSurfaceDescriptor surface_desc = WGPU_SURFACE_DESCRIPTOR_INIT;
		surface_desc.nextInChain = &window_desc.chain;
		wgpu.surface = wgpuInstanceCreateSurface(wgpu.instance, &surface_desc);
		if (wgpu.surface == nullptr) {
			wgpu_shutdown();
			ri.Error(ERR_FATAL, "WebGPU: failed to create the window surface");
			return;
		}
	}

	//
	// Adapter
	//
	{
		WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
		options.backendType = WGPUBackendType_Vulkan;
		options.powerPreference = WGPUPowerPreference_HighPerformance;
		options.compatibleSurface = wgpu.surface;

		Wgpu_Request_Result result;
		WGPURequestAdapterCallbackInfo callback_info = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
		callback_info.mode = WGPUCallbackMode_WaitAnyOnly;
		callback_info.userdata1 = &result;
		callback_info.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void*)
		{
			wgpu.adapter = adapter;
			set_request_result(*(Wgpu_Request_Result*)userdata1, status == WGPURequestAdapterStatus_Success && adapter != nullptr, message);
		};
		WGPUFuture future = wgpuInstanceRequestAdapter(wgpu.instance, &options, callback_info);
		wait_for_request(future, "wgpuInstanceRequestAdapter (Vulkan)", result);
	}

	//
	// Device
	//
	{
		WGPULimits required_limits = WGPU_LIMITS_INIT;
		required_limits.maxImmediateSize = 64;
		WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
		device_desc.label = { "Quake III WebGPU device", WGPU_STRLEN };
		device_desc.requiredLimits = &required_limits;
		device_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
		device_desc.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*)
		{
			if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled) {
				log_device_error("device lost", (int)reason, message);
			}
		};
		device_desc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*)
		{
			log_device_error("uncaptured error", (int)type, message);
		};

		Wgpu_Request_Result result;
		WGPURequestDeviceCallbackInfo callback_info = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
		callback_info.mode = WGPUCallbackMode_WaitAnyOnly;
		callback_info.userdata1 = &result;
		callback_info.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void*)
		{
			wgpu.device = device;
			set_request_result(*(Wgpu_Request_Result*)userdata1, status == WGPURequestDeviceStatus_Success && device != nullptr, message);
		};
		WGPUFuture future = wgpuAdapterRequestDevice(wgpu.adapter, &device_desc, callback_info);
		wait_for_request(future, "wgpuAdapterRequestDevice", result);
	}

	//
	// Queue
	//
	wgpu.queue = wgpuDeviceGetQueue(wgpu.device);
	if (wgpu.queue == nullptr) {
		wgpu_shutdown();
		ri.Error(ERR_FATAL, "WebGPU: failed to get the device queue");
		return;
	}

	//
	// Surface configuration
	//
	{
		WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
		WGPUStatus status = wgpuSurfaceGetCapabilities(wgpu.surface, wgpu.adapter, &caps);
		if (status != WGPUStatus_Success || caps.formatCount == 0) {
			wgpuSurfaceCapabilitiesFreeMembers(caps);
			wgpu_shutdown();
			ri.Error(ERR_FATAL, "WebGPU: failed to query surface capabilities");
			return;
		}
		WGPUSurfaceConfiguration surface_config = WGPU_SURFACE_CONFIGURATION_INIT;
		surface_config.device = wgpu.device;
		surface_config.width = glConfig.vidWidth;
		surface_config.height = glConfig.vidHeight;
		surface_config.format = caps.formats[0];
		// Prefer a linear UNORM target, like the Vulkan and DX12 backends
		for (size_t i = 0; i < caps.formatCount; i++) {
			if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
				surface_config.format = caps.formats[i];
				break;
			}
		}
		surface_config.presentMode = WGPUPresentMode_Fifo;
		if (!r_vsync->integer) {
			for (size_t i = 0; i < caps.presentModeCount; i++) {
				if (caps.presentModes[i] == WGPUPresentMode_Mailbox) {
					surface_config.presentMode = WGPUPresentMode_Mailbox;
					break;
				}
				if (caps.presentModes[i] == WGPUPresentMode_Immediate) {
					surface_config.presentMode = WGPUPresentMode_Immediate;
				}
			}
		}
		wgpuSurfaceCapabilitiesFreeMembers(caps);
		wgpuSurfaceConfigure(wgpu.surface, &surface_config);
		wgpu.surface_format = surface_config.format;
	}

	//
	// Depth/stencil attachment
	//
	{
		WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
		desc.size = { (uint32_t)glConfig.vidWidth, (uint32_t)glConfig.vidHeight, 1 };
		desc.format = get_depth_format();
		desc.usage = WGPUTextureUsage_RenderAttachment;
		wgpu.depth_texture = wgpuDeviceCreateTexture(wgpu.device, &desc);
		wgpu.depth_view = wgpuTextureCreateView(wgpu.depth_texture, nullptr);
	}

	//
	// Geometry buffer
	//
	WGPUBufferDescriptor buffer_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
	buffer_desc.label = { "WebGPU geometry buffer", WGPU_STRLEN };
	buffer_desc.size = GEOMETRY_BUFFER_SIZE;
	buffer_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
	wgpu.geometry_buffer = wgpuDeviceCreateBuffer(wgpu.device, &buffer_desc);

	//
	// Pipeline layout
	//
	{
		WGPUBindGroupLayoutEntry entries[2] = { WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT };
		entries[0].binding = 0;
		entries[0].visibility = WGPUShaderStage_Fragment;
		entries[0].texture.sampleType = WGPUTextureSampleType_Float;
		entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
		entries[1].binding = 1;
		entries[1].visibility = WGPUShaderStage_Fragment;
		entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

		WGPUBindGroupLayoutDescriptor bind_group_desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
		bind_group_desc.entryCount = 2;
		bind_group_desc.entries = entries;
		wgpu.image_bind_group_layout = wgpuDeviceCreateBindGroupLayout(wgpu.device, &bind_group_desc);

		WGPUPipelineLayoutDescriptor layout_desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
		WGPUBindGroupLayout layouts[] = { wgpu.image_bind_group_layout, wgpu.image_bind_group_layout };
		layout_desc.bindGroupLayouts = layouts;
		layout_desc.immediateSize = 64;

		// Single texture
		layout_desc.bindGroupLayoutCount = 1;
		wgpu.pipeline_layouts[0] = wgpuDeviceCreatePipelineLayout(wgpu.device, &layout_desc);

		// Multitexture
		layout_desc.bindGroupLayoutCount = 2;
		wgpu.pipeline_layouts[1] = wgpuDeviceCreatePipelineLayout(wgpu.device, &layout_desc);
	}

	//
	// Shader modules
	//
	{
		auto create_shader_module = [](const unsigned char* bytes, size_t size) {
			std::vector<uint32_t> words(size / sizeof(uint32_t));
			Com_Memcpy(words.data(), bytes, size);
			WGPUShaderSourceSPIRV source = WGPU_SHADER_SOURCE_SPIRV_INIT;
			source.codeSize = (uint32_t)words.size();
			source.code = words.data();
			WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
			desc.nextInChain = &source.chain;
			return wgpuDeviceCreateShaderModule(wgpu.device, &desc);
		};

		extern unsigned char single_texture_vert_spv[];
		extern long long single_texture_vert_spv_size;
		wgpu.single_texture_vs = create_shader_module(single_texture_vert_spv, single_texture_vert_spv_size);

		extern unsigned char single_texture_frag_spv[];
		extern long long single_texture_frag_spv_size;
		wgpu.single_texture_fs = create_shader_module(single_texture_frag_spv, single_texture_frag_spv_size);

		extern unsigned char multi_texture_vert_spv[];
		extern long long multi_texture_vert_spv_size;
		wgpu.multi_texture_vs = create_shader_module(multi_texture_vert_spv, multi_texture_vert_spv_size);

		extern unsigned char multi_texture_mul_frag_spv[];
		extern long long multi_texture_mul_frag_spv_size;
		wgpu.multi_texture_mul_fs = create_shader_module(multi_texture_mul_frag_spv, multi_texture_mul_frag_spv_size);

		extern unsigned char multi_texture_add_frag_spv[];
		extern long long multi_texture_add_frag_spv_size;
		wgpu.multi_texture_add_fs = create_shader_module(multi_texture_add_frag_spv, multi_texture_add_frag_spv_size);

		// WGPU_TODO: Load the clipping variants after adapting their 128-byte transform block
	}

	//
	// Standard pipelines
	//
	{
		// skybox
		{
			Vk_Pipeline_Def def;
			def.shader_type = Vk_Shader_Type::single_texture;
			def.state_bits = 0;
			def.face_culling = CT_FRONT_SIDED;
			def.polygon_offset = false;
			def.clipping_plane = false;
			def.mirror = false;
			wgpu.skybox_pipeline = create_pipeline(def);
		}

		// fog and dlights
		{
			Vk_Pipeline_Def def;
			def.shader_type = Vk_Shader_Type::single_texture;
			def.clipping_plane = false;
			def.mirror = false;

			unsigned int fog_state_bits[2] = {
				GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL,
				GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA
			};
			unsigned int dlight_state_bits[2] = {
				GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL,
				GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL
			};
			bool polygon_offset[2] = {false, true};

			for (int i = 0; i < 2; i++) {
				unsigned fog_state = fog_state_bits[i];
				unsigned dlight_state = dlight_state_bits[i];

				for (int j = 0; j < 3; j++) {
					def.face_culling = j; // cullType_t value

					for (int k = 0; k < 2; k++) {
						def.polygon_offset = polygon_offset[k];

						def.state_bits = fog_state;
						wgpu.fog_pipelines[i][j][k] = create_pipeline(def);

						def.state_bits = dlight_state;
						wgpu.dlight_pipelines[i][j][k] = create_pipeline(def);
					}
				}
			}
		}

		// WGPU_TODO: Create the remaining standard pipelines as their rendering paths are implemented
		{
			Vk_Pipeline_Def def;
			def.state_bits = GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
			wgpu.images_debug_pipeline = create_pipeline(def);
		}
	}

	wgpu.active = true;
}

void wgpu_shutdown() {
	release_frame();
	if (wgpu.depth_view != nullptr) {
		wgpuTextureViewRelease(wgpu.depth_view);
	}
	if (wgpu.depth_texture != nullptr) {
		wgpuTextureRelease(wgpu.depth_texture);
	}
	if (wgpu.geometry_buffer != nullptr) {
		wgpuBufferRelease(wgpu.geometry_buffer);
	}
	if (wgpu.skybox_pipeline != nullptr) {
		wgpuRenderPipelineRelease(wgpu.skybox_pipeline);
	}
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 3; j++) {
			for (int k = 0; k < 2; k++) {
				if (wgpu.fog_pipelines[i][j][k] != nullptr) {
					wgpuRenderPipelineRelease(wgpu.fog_pipelines[i][j][k]);
				}
				if (wgpu.dlight_pipelines[i][j][k] != nullptr) {
					wgpuRenderPipelineRelease(wgpu.dlight_pipelines[i][j][k]);
				}
			}
		}
	}
	if (wgpu.images_debug_pipeline != nullptr) {
		wgpuRenderPipelineRelease(wgpu.images_debug_pipeline);
	}
	for (int i = 0; i < 2; i++) {
		if (wgpu.pipeline_layouts[i] != nullptr) {
			wgpuPipelineLayoutRelease(wgpu.pipeline_layouts[i]);
		}
	}
	if (wgpu.single_texture_vs != nullptr) {
		wgpuShaderModuleRelease(wgpu.single_texture_vs);
	}
	if (wgpu.single_texture_fs != nullptr) {
		wgpuShaderModuleRelease(wgpu.single_texture_fs);
	}
	if (wgpu.multi_texture_vs != nullptr) {
		wgpuShaderModuleRelease(wgpu.multi_texture_vs);
	}
	if (wgpu.multi_texture_mul_fs != nullptr) {
		wgpuShaderModuleRelease(wgpu.multi_texture_mul_fs);
	}
	if (wgpu.multi_texture_add_fs != nullptr) {
		wgpuShaderModuleRelease(wgpu.multi_texture_add_fs);
	}
	if (wgpu.image_bind_group_layout != nullptr) {
		wgpuBindGroupLayoutRelease(wgpu.image_bind_group_layout);
	}
	if (wgpu.surface != nullptr) {
		wgpuSurfaceUnconfigure(wgpu.surface);
		wgpuSurfaceRelease(wgpu.surface);
	}
	if (wgpu.queue != nullptr) {
		wgpuQueueRelease(wgpu.queue);
	}
	if (wgpu.device != nullptr) {
		wgpuDeviceDestroy(wgpu.device);
		wgpuDeviceRelease(wgpu.device);
	}
	if (wgpu.adapter != nullptr) {
		wgpuAdapterRelease(wgpu.adapter);
	}
	if (wgpu.instance != nullptr) {
		wgpuInstanceRelease(wgpu.instance);
	}
	Com_Memset(&wgpu, 0, sizeof(wgpu));
}

void wgpu_release_resources() {
	for (int i = 0; i < wgpu_world.num_pipelines; i++) {
		wgpuRenderPipelineRelease(wgpu_world.pipelines[i]);
	}
	for (int i = 0; i < MAX_VK_IMAGES; i++) {
		wgpu_destroy_image(wgpu_world.images[i]);
	}
	for (int i = 0; i < wgpu_world.num_samplers; i++) {
		wgpuSamplerRelease(wgpu_world.samplers[i]);
	}
	Com_Memset(&wgpu_world, 0, sizeof(wgpu_world));
}

void wgpu_wait_device_idle() {
	// WGPU_TODO: Wait for submitted WebGPU work to complete
}

//
// Resources allocation
//
Wgpu_Image wgpu_create_image(int width, int height, WGPUTextureFormat format, int mip_levels, bool repeat_texture) {
	if (format != WGPUTextureFormat_RGBA8Unorm) {
		ri.Error(ERR_FATAL, "wgpu_create_image: only RGBA8 is supported");
		return Wgpu_Image{};
	}

	Wgpu_Image image;

	// Create texture
	WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
	desc.dimension = WGPUTextureDimension_2D;
	desc.size = { (uint32_t)width, (uint32_t)height, 1 };
	desc.format = format;
	desc.mipLevelCount = mip_levels;
	desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
	image.texture = wgpuDeviceCreateTexture(wgpu.device, &desc);

	// Create a view of all mip levels
	image.view = wgpuTextureCreateView(image.texture, nullptr);
	wgpu_update_bind_group(image, mip_levels > 1, repeat_texture);
	return image;
}

void wgpu_destroy_image(Wgpu_Image& image) {
	if (image.bind_group != nullptr) {
		wgpuBindGroupRelease(image.bind_group);
	}
	if (image.view != nullptr) {
		wgpuTextureViewRelease(image.view);
	}
	if (image.texture != nullptr) {
		wgpuTextureRelease(image.texture);
	}
	image = Wgpu_Image{};
}

void wgpu_upload_image_data(WGPUTexture texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel) {
	if (bytes_per_pixel != 4) {
		ri.Error(ERR_FATAL, "wgpu_upload_image_data: only RGBA8 is supported");
		return;
	}
	for (int i = 0; i < mip_levels; i++) {
		WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
		destination.texture = texture;
		destination.mipLevel = i;
		destination.aspect = WGPUTextureAspect_All;

        // Queue writes accept tightly packed rows. Dawn handles staging and alignment
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
		layout.bytesPerRow = width * bytes_per_pixel;
		layout.rowsPerImage = height;
		WGPUExtent3D size = { (uint32_t)width, (uint32_t)height, 1 };
		size_t data_size = (size_t)layout.bytesPerRow * height;
		wgpuQueueWriteTexture(wgpu.queue, &destination, pixels, data_size, &layout, &size);

		pixels += data_size;

		width >>= 1;
		if (width < 1) width = 1;

		height >>= 1;
		if (height < 1) height = 1;
	}
}

void wgpu_update_bind_group(Wgpu_Image& image, bool mipmap, bool repeat_texture) {
	Vk_Sampler_Def sampler_def;
	sampler_def.repeat_texture = repeat_texture;
	if (mipmap) {
		sampler_def.gl_mag_filter = gl_filter_max;
		sampler_def.gl_min_filter = gl_filter_min;
	}
	else {
		sampler_def.gl_mag_filter = GL_LINEAR;
		sampler_def.gl_min_filter = GL_LINEAR;
	}

	WGPUBindGroupEntry entries[2] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
	entries[0].binding = 0;
	entries[0].textureView = image.view;

	entries[1].binding = 1;
	entries[1].sampler = wgpu_find_sampler(sampler_def);

	WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
	desc.layout = wgpu.image_bind_group_layout;
	desc.entryCount = 2;
	desc.entries = entries;

	WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(wgpu.device, &desc);
	if (image.bind_group != nullptr) {
		wgpuBindGroupRelease(image.bind_group);
	}
	image.bind_group = bind_group;
}

WGPUSampler wgpu_find_sampler(const Vk_Sampler_Def& def) {
	for (int i = 0; i < wgpu_world.num_samplers; i++) {
		const auto& cur_def = wgpu_world.sampler_defs[i];

		if (cur_def.repeat_texture == def.repeat_texture &&
			cur_def.gl_mag_filter == def.gl_mag_filter &&
			cur_def.gl_min_filter == def.gl_min_filter)
		{
			return wgpu_world.samplers[i];
		}
	}
	if (wgpu_world.num_samplers >= MAX_VK_SAMPLERS) {
		ri.Error(ERR_DROP, "wgpu_find_sampler: MAX_VK_SAMPLERS hit\n");
		return nullptr;
	}

	// Create new sampler
	WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
	desc.addressModeU = def.repeat_texture ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
	desc.addressModeV = desc.addressModeU;
	desc.addressModeW = desc.addressModeU;

	if (def.gl_mag_filter == GL_NEAREST) {
		desc.magFilter = WGPUFilterMode_Nearest;
	} else if (def.gl_mag_filter == GL_LINEAR) {
		desc.magFilter = WGPUFilterMode_Linear;
	} else {
		ri.Error(ERR_FATAL, "wgpu_find_sampler: invalid gl_mag_filter");
		return nullptr;
	}

	desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
	desc.lodMaxClamp = 12.0f;
	switch (def.gl_min_filter) {
		case GL_NEAREST:
		case GL_LINEAR:
			desc.minFilter = def.gl_min_filter == GL_NEAREST ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
			desc.lodMaxClamp = 0.25f;
			break;
		case GL_NEAREST_MIPMAP_NEAREST:
		case GL_LINEAR_MIPMAP_NEAREST:
			desc.minFilter = def.gl_min_filter == GL_NEAREST_MIPMAP_NEAREST ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
			break;
		case GL_NEAREST_MIPMAP_LINEAR:
		case GL_LINEAR_MIPMAP_LINEAR:
			desc.minFilter = def.gl_min_filter == GL_NEAREST_MIPMAP_LINEAR ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
			desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
			break;
		default:
			ri.Error(ERR_FATAL, "wgpu_find_sampler: invalid gl_min_filter");
			return nullptr;
	}

	WGPUSampler sampler = wgpuDeviceCreateSampler(wgpu.device, &desc);
	wgpu_world.sampler_defs[wgpu_world.num_samplers] = def;
	wgpu_world.samplers[wgpu_world.num_samplers] = sampler;
	wgpu_world.num_samplers++;
	return sampler;
}

static WGPURenderPipeline create_pipeline(const Vk_Pipeline_Def& def) {
	// WGPU_TODO: Add stencil shadows, wireframe triangles and clipping
	if (def.shadow_phase != Vk_Shadow_Phase::disabled || def.clipping_plane ||
		((def.state_bits & GLS_POLYMODE_LINE) && !def.line_primitives)) {
		return nullptr;
	}

	//
	// Shader stages
	//
	WGPUShaderModule vs, fs;
	if (def.shader_type == Vk_Shader_Type::single_texture) {
		vs = wgpu.single_texture_vs;
		fs = wgpu.single_texture_fs;
	} else if (def.shader_type == Vk_Shader_Type::multi_texture_mul) {
		vs = wgpu.multi_texture_vs;
		fs = wgpu.multi_texture_mul_fs;
	} else {
		vs = wgpu.multi_texture_vs;
		fs = wgpu.multi_texture_add_fs;
	}

	WGPUConstantEntry alpha_test = WGPU_CONSTANT_ENTRY_INIT;
	alpha_test.key = { "0", WGPU_STRLEN };
	if ((def.state_bits & GLS_ATEST_BITS) == 0)
		alpha_test.value = 0;
	else if (def.state_bits & GLS_ATEST_GT_0)
		alpha_test.value = 1;
	else if (def.state_bits & GLS_ATEST_LT_80)
		alpha_test.value = 2;
	else if (def.state_bits & GLS_ATEST_GE_80)
		alpha_test.value = 3;
	else
		ri.Error(ERR_DROP, "create_pipeline: invalid alpha test state bits\n");

	//
	// Vertex input
	//
	WGPUVertexAttribute attributes[4] = {
		WGPU_VERTEX_ATTRIBUTE_INIT,
		WGPU_VERTEX_ATTRIBUTE_INIT,
		WGPU_VERTEX_ATTRIBUTE_INIT,
		WGPU_VERTEX_ATTRIBUTE_INIT
	};
	attributes[0].format = WGPUVertexFormat_Float32x3;
	attributes[1].format = WGPUVertexFormat_Unorm8x4;
	attributes[2].format = WGPUVertexFormat_Float32x2;
	attributes[3].format = WGPUVertexFormat_Float32x2;

	WGPUVertexBufferLayout buffers[4] = {
		WGPU_VERTEX_BUFFER_LAYOUT_INIT,
		WGPU_VERTEX_BUFFER_LAYOUT_INIT,
		WGPU_VERTEX_BUFFER_LAYOUT_INIT,
		WGPU_VERTEX_BUFFER_LAYOUT_INIT
	};
	for (int i = 0; i < 4; i++) {
		attributes[i].shaderLocation = i;
		buffers[i].stepMode = WGPUVertexStepMode_Vertex;
		buffers[i].attributeCount = 1;
		buffers[i].attributes = &attributes[i];
	}
	buffers[0].arrayStride = sizeof(vec4_t);
	buffers[1].arrayStride = sizeof(color4ub_t);
	buffers[2].arrayStride = sizeof(vec2_t);
	buffers[3].arrayStride = sizeof(vec2_t);

	//
	// Primitive assembly and rasterization
	//
	WGPUPrimitiveState primitive = WGPU_PRIMITIVE_STATE_INIT;
	primitive.topology = def.line_primitives ? WGPUPrimitiveTopology_LineList : WGPUPrimitiveTopology_TriangleList;
	primitive.frontFace = WGPUFrontFace_CW;
	if (def.face_culling == CT_TWO_SIDED)
		primitive.cullMode = WGPUCullMode_None;
	else if (def.face_culling == CT_FRONT_SIDED)
		primitive.cullMode = def.mirror ? WGPUCullMode_Front : WGPUCullMode_Back;
	else if (def.face_culling == CT_BACK_SIDED)
		primitive.cullMode = def.mirror ? WGPUCullMode_Back : WGPUCullMode_Front;
	else
		ri.Error(ERR_DROP, "create_pipeline: invalid face culling mode\n");

	//
	// Blend state
	//
	WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
	bool blend_enable = (def.state_bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) != 0;
	if (blend_enable) {
		switch (def.state_bits & GLS_SRCBLEND_BITS) {
			case GLS_SRCBLEND_ZERO:
				blend.color.srcFactor = WGPUBlendFactor_Zero;
				break;
			case GLS_SRCBLEND_ONE:
				blend.color.srcFactor = WGPUBlendFactor_One;
				break;
			case GLS_SRCBLEND_DST_COLOR:
				blend.color.srcFactor = WGPUBlendFactor_Dst;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
				blend.color.srcFactor = WGPUBlendFactor_OneMinusDst;
				break;
			case GLS_SRCBLEND_SRC_ALPHA:
				blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
				break;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
				blend.color.srcFactor = WGPUBlendFactor_OneMinusSrcAlpha;
				break;
			case GLS_SRCBLEND_DST_ALPHA:
				blend.color.srcFactor = WGPUBlendFactor_DstAlpha;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
				blend.color.srcFactor = WGPUBlendFactor_OneMinusDstAlpha;
				break;
			case GLS_SRCBLEND_ALPHA_SATURATE:
				blend.color.srcFactor = WGPUBlendFactor_SrcAlphaSaturated;
				break;
			default:
				ri.Error(ERR_DROP, "create_pipeline: invalid src blend state bits\n");
		}
		switch (def.state_bits & GLS_DSTBLEND_BITS) {
			case GLS_DSTBLEND_ZERO:
				blend.color.dstFactor = WGPUBlendFactor_Zero;
				break;
			case GLS_DSTBLEND_ONE:
				blend.color.dstFactor = WGPUBlendFactor_One;
				break;
			case GLS_DSTBLEND_SRC_COLOR:
				blend.color.dstFactor = WGPUBlendFactor_Src;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
				blend.color.dstFactor = WGPUBlendFactor_OneMinusSrc;
				break;
			case GLS_DSTBLEND_SRC_ALPHA:
				blend.color.dstFactor = WGPUBlendFactor_SrcAlpha;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
				blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
				break;
			case GLS_DSTBLEND_DST_ALPHA:
				blend.color.dstFactor = WGPUBlendFactor_DstAlpha;
				break;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
				blend.color.dstFactor = WGPUBlendFactor_OneMinusDstAlpha;
				break;
			default:
				ri.Error(ERR_DROP, "create_pipeline: invalid dst blend state bits\n");
		}
		blend.color.operation = WGPUBlendOperation_Add;
		blend.alpha = blend.color;
	}

	//
	// Depth/stencil state
	//
	WGPUDepthStencilState depth_stencil = WGPU_DEPTH_STENCIL_STATE_INIT;
	depth_stencil.format = get_depth_format();
	depth_stencil.depthWriteEnabled = (def.state_bits & GLS_DEPTHMASK_TRUE) ? WGPUOptionalBool_True : WGPUOptionalBool_False;
	if (def.state_bits & GLS_DEPTHTEST_DISABLE)
		depth_stencil.depthCompare = WGPUCompareFunction_Always;
	else
		depth_stencil.depthCompare = (def.state_bits & GLS_DEPTHFUNC_EQUAL) ? WGPUCompareFunction_Equal : WGPUCompareFunction_LessEqual;
	depth_stencil.depthBias = def.polygon_offset ? r_offsetUnits->integer : 0;
	depth_stencil.depthBiasSlopeScale = def.polygon_offset ? r_offsetFactor->value : 0.0f;

	//
	// Create pipeline
	//
	WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
	target.format = wgpu.surface_format;
	target.blend = blend_enable ? &blend : nullptr;

	WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
	fragment.module = fs;
	fragment.entryPoint = { "main", WGPU_STRLEN };
	fragment.targetCount = 1;
	fragment.targets = &target;
	if (def.state_bits & GLS_ATEST_BITS) {
		fragment.constantCount = 1;
		fragment.constants = &alpha_test;
	}

	WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
	desc.layout = wgpu.pipeline_layouts[def.shader_type == Vk_Shader_Type::single_texture ? 0 : 1];
	desc.vertex.module = vs;
	desc.vertex.entryPoint = { "main", WGPU_STRLEN };
	desc.vertex.bufferCount = def.shader_type == Vk_Shader_Type::single_texture ? 3 : 4;
	desc.vertex.buffers = buffers;
	desc.primitive = primitive;
	desc.depthStencil = &depth_stencil;
	desc.fragment = &fragment;
	return wgpuDeviceCreateRenderPipeline(wgpu.device, &desc);
}

WGPURenderPipeline wgpu_find_pipeline(const Vk_Pipeline_Def& def) {
	for (int i = 0; i < wgpu_world.num_pipelines; i++) {
		const auto& cur_def = wgpu_world.pipeline_defs[i];

		if (cur_def.shader_type == def.shader_type &&
			cur_def.state_bits == def.state_bits &&
			cur_def.face_culling == def.face_culling &&
			cur_def.polygon_offset == def.polygon_offset &&
			cur_def.clipping_plane == def.clipping_plane &&
			cur_def.mirror == def.mirror &&
			cur_def.line_primitives == def.line_primitives &&
			cur_def.shadow_phase == def.shadow_phase)
		{
			return wgpu_world.pipelines[i];
		}
	}
	if (wgpu_world.num_pipelines >= MAX_VK_PIPELINES) {
		ri.Error(ERR_DROP, "wgpu_find_pipeline: MAX_VK_PIPELINES hit\n");
		return nullptr;
	}

	WGPURenderPipeline pipeline = create_pipeline(def);
	if (pipeline == nullptr)
		return nullptr;

	wgpu_world.pipeline_defs[wgpu_world.num_pipelines] = def;
	wgpu_world.pipelines[wgpu_world.num_pipelines] = pipeline;
	wgpu_world.num_pipelines++;
	return pipeline;
}

//
// Rendering setup
//
void wgpu_clear_attachments(bool clear_depth_stencil, bool clear_color, vec4_t color) {
	if (!wgpu.active || wgpu.command_encoder == nullptr) {
		return;
	}
	if (!clear_color && !clear_depth_stencil) {
		return;
	}
	// WGPU_TODO: Restrict clears to the current viewport when geometry rendering is implemented
	end_render_pass();
	WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
	attachment.view = wgpu.frame_view;
	attachment.loadOp = clear_color ? WGPULoadOp_Clear : WGPULoadOp_Load;
	attachment.storeOp = WGPUStoreOp_Store;
	if (clear_color)
		attachment.clearValue = { color[0], color[1], color[2], color[3] };

	WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
	depth_attachment.view = wgpu.depth_view;
	depth_attachment.depthLoadOp = clear_depth_stencil ? WGPULoadOp_Clear : WGPULoadOp_Load;
	depth_attachment.depthStoreOp = WGPUStoreOp_Store;
	depth_attachment.depthClearValue = 1.0f;
	if (r_stencilbits->integer > 0) {
		depth_attachment.stencilLoadOp = clear_depth_stencil ? WGPULoadOp_Clear : WGPULoadOp_Load;
		depth_attachment.stencilStoreOp = WGPUStoreOp_Store;
		depth_attachment.stencilClearValue = 0;
	}

	WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
	pass_desc.colorAttachmentCount = 1;
	pass_desc.colorAttachments = &attachment;
	pass_desc.depthStencilAttachment = &depth_attachment;
	wgpu.render_pass = wgpuCommandEncoderBeginRenderPass(wgpu.command_encoder, &pass_desc);
}

static uint64_t upload_geometry(const void* data, size_t size) {
	uint64_t offset = wgpu.geometry_buffer_offset;
	if (offset + size > GEOMETRY_BUFFER_SIZE) {
		ri.Error(ERR_DROP, "WebGPU: geometry buffer overflow\n");
		return 0;
	}
	// Keep each draw's data until submission; queue writes to one offset would overwrite earlier draws
	wgpuQueueWriteBuffer(wgpu.queue, wgpu.geometry_buffer, offset, data, size);
	wgpu.geometry_buffer_offset += size;
	return offset;
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

		// Update Q3's OpenGL projection to WebGPU conventions: z in [0, 1] instead of [-1, 1]
		float zNear = r_znear->value;
		float zFar = backEnd.viewParms.zFar;
		float P10 = -zFar / (zFar - zNear);
		float P14 = -zFar*zNear / (zFar - zNear);

		float proj[16] = {
			p[0],  p[1],  p[2], p[3],
			p[4],  p[5],  p[6], p[7],
			p[8],  p[9],  P10,  p[11],
			p[12], p[13], P14,  p[15]
		};

		myGlMultMatrix(wgpu_world.modelview_transform, proj, mvp);
	}
}

static void set_viewport_and_scissor(Vk_Depth_Range depth_range) {
	int left, top, right, bottom;
	if (backEnd.projection2D) {
		left = 0;
		top = 0;
		right = glConfig.vidWidth;
		bottom = glConfig.vidHeight;
	} else {
		left = backEnd.viewParms.viewportX;
		top = glConfig.vidHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight);
		right = left + backEnd.viewParms.viewportWidth;
		bottom = top + backEnd.viewParms.viewportHeight;
	}

	float min_depth, max_depth;
	if (depth_range == Vk_Depth_Range::force_zero) {
		min_depth = 0.0f;
		max_depth = 0.0f;
	} else if (depth_range == Vk_Depth_Range::force_one) {
		min_depth = 1.0f;
		max_depth = 1.0f;
	} else if (depth_range == Vk_Depth_Range::weapon) {
		min_depth = 0.0f;
		max_depth = 0.3f;
	} else {
		min_depth = 0.0f;
		max_depth = 1.0f;
	}
	wgpuRenderPassEncoderSetViewport(wgpu.render_pass, (float)left, (float)top,
		(float)(right - left), (float)(bottom - top), min_depth, max_depth);

	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > glConfig.vidWidth)
		right = glConfig.vidWidth;
	if (bottom > glConfig.vidHeight)
		bottom = glConfig.vidHeight;
	wgpuRenderPassEncoderSetScissorRect(wgpu.render_pass, left, top, right - left, bottom - top);
}

void wgpu_bind_geometry() {
	if (wgpu.render_pass == nullptr || tess.numVertexes == 0) {
		return;
	}

	size_t size = tess.numVertexes * sizeof(vec4_t);
	uint64_t offset = upload_geometry(tess.xyz, size);
	wgpuRenderPassEncoderSetVertexBuffer(wgpu.render_pass, 0, wgpu.geometry_buffer, offset, size);
	if (tess.numIndexes > 0) {
		size = tess.numIndexes * sizeof(glIndex_t);
		offset = upload_geometry(tess.indexes, size);
		wgpuRenderPassEncoderSetIndexBuffer(wgpu.render_pass, wgpu.geometry_buffer, WGPUIndexFormat_Uint32, offset, size);
	}

	float mvp[16];
	get_mvp_transform(mvp);
	wgpuRenderPassEncoderSetImmediates(wgpu.render_pass, 0, mvp, sizeof(mvp));
}

void wgpu_shade_geometry(WGPURenderPipeline pipeline, bool multitexture, Vk_Depth_Range depth_range, bool indexed) {
	if (wgpu.render_pass == nullptr || pipeline == nullptr || tess.numVertexes == 0) {
		return;
	}
	WGPUBindGroup bind_group = wgpu_world.images[wgpu_world.current_image_indices[0]].bind_group;
	if (bind_group == nullptr) {
		return;
	}

	size_t size = tess.numVertexes * sizeof(color4ub_t);
	uint64_t offset = upload_geometry(tess.svars.colors, size);
	wgpuRenderPassEncoderSetVertexBuffer(wgpu.render_pass, 1, wgpu.geometry_buffer, offset, size);
	size = tess.numVertexes * sizeof(vec2_t);
	offset = upload_geometry(tess.svars.texcoords[0], size);
	wgpuRenderPassEncoderSetVertexBuffer(wgpu.render_pass, 2, wgpu.geometry_buffer, offset, size);
	if (multitexture) {
		offset = upload_geometry(tess.svars.texcoords[1], size);
		wgpuRenderPassEncoderSetVertexBuffer(wgpu.render_pass, 3, wgpu.geometry_buffer, offset, size);
	}

	wgpuRenderPassEncoderSetPipeline(wgpu.render_pass, pipeline);
	wgpuRenderPassEncoderSetBindGroup(wgpu.render_pass, 0, bind_group, 0, nullptr);
	if (multitexture) {
		WGPUBindGroup bind_group1 = wgpu_world.images[wgpu_world.current_image_indices[1]].bind_group;
		wgpuRenderPassEncoderSetBindGroup(wgpu.render_pass, 1, bind_group1, 0, nullptr);
	}
	set_viewport_and_scissor(depth_range);
	if (indexed) {
		wgpuRenderPassEncoderDrawIndexed(wgpu.render_pass, tess.numIndexes, 1, 0, 0, 0);
	} else {
		wgpuRenderPassEncoderDraw(wgpu.render_pass, tess.numVertexes, 1, 0, 0);
	}
}

void wgpu_begin_frame() {
	if (!wgpu.active) {
		return;
	}
	wgpu.geometry_buffer_offset = 0;
	WGPUSurfaceTexture surface_texture = WGPU_SURFACE_TEXTURE_INIT;
	wgpuSurfaceGetCurrentTexture(wgpu.surface, &surface_texture);
	switch (surface_texture.status) {
		case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
		case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
			break;
		case WGPUSurfaceGetCurrentTextureStatus_Timeout:
			return;
		default:
			ri.Error(ERR_FATAL, "WebGPU: failed to acquire the surface texture (status %d)", (int)surface_texture.status);
			return;
	}
	wgpu.frame_texture = surface_texture.texture;
	wgpu.frame_view = wgpuTextureCreateView(wgpu.frame_texture, nullptr);
	wgpu.command_encoder = wgpuDeviceCreateCommandEncoder(wgpu.device, nullptr);

	vec4_t black = { 0, 0, 0, 1 };
	wgpu_clear_attachments(true, true, black);
}

void wgpu_end_frame() {
	if (!wgpu.active || wgpu.command_encoder == nullptr)
		return;

	end_render_pass();
	WGPUCommandBuffer commands = wgpuCommandEncoderFinish(wgpu.command_encoder, nullptr);
	wgpuQueueSubmit(wgpu.queue, 1, &commands);
	wgpuCommandBufferRelease(commands);
	WGPUStatus status = wgpuSurfacePresent(wgpu.surface);
	release_frame();
	if (status != WGPUStatus_Success) {
		ri.Error(ERR_FATAL, "WebGPU: failed to present the surface");
	}
}

void wgpu_update_gamma_buffer(float gamma_table[256]) {
	// WGPU_TODO: Upload the gamma table for the WebGPU shader gamma path
}
