#include "tr_local.h"
#include "../platform/win_local.h"

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

void wgpu_initialize() {
	//
	// Instance
	//
	WGPUInstanceFeatureName instance_feature = WGPUInstanceFeatureName_TimedWaitAny;
	WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
	instance_desc.requiredFeatureCount = 1;
	instance_desc.requiredFeatures = &instance_feature;
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
		WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
		device_desc.label = { "Quake III WebGPU device", WGPU_STRLEN };
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
	}

	wgpu.active = true;
}

void wgpu_shutdown() {
	release_frame();
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
	for (int i = 0; i < MAX_VK_IMAGES; i++) {
		wgpu_destroy_image(wgpu_world.images[i]);
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
	if (!wgpu.active || wgpu.command_encoder == nullptr) {
		return;
	}
	// WGPU_TODO: Clear depth/stencil when the depth attachment is implemented
	if (!clear_color) {
		return;
	}
	// WGPU_TODO: Restrict clears to the current viewport when geometry rendering is implemented
	end_render_pass();
	WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
	attachment.view = wgpu.frame_view;
	attachment.loadOp = WGPULoadOp_Clear;
	attachment.storeOp = WGPUStoreOp_Store;
	attachment.clearValue = { color[0], color[1], color[2], color[3] };

	WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
	pass_desc.colorAttachmentCount = 1;
	pass_desc.colorAttachments = &attachment;
	wgpu.render_pass = wgpuCommandEncoderBeginRenderPass(wgpu.command_encoder, &pass_desc);
}

void wgpu_bind_geometry() {
	// WGPU_TODO: Upload and bind the current tessellated geometry
}

void wgpu_shade_geometry(WGPURenderPipeline pipeline, bool multitexture, Vk_Depth_Range depth_range, bool indexed) {
	// WGPU_TODO: Bind the pipeline and resources and draw the current geometry
}

void wgpu_begin_frame() {
	if (!wgpu.active) {
		return;
	}
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
	wgpu_clear_attachments(false, true, black);
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
