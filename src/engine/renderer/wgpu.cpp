#include "tr_local.h"

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
	// Adapter
	//
	{
		WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
		options.backendType = WGPUBackendType_Vulkan;
		options.powerPreference = WGPUPowerPreference_HighPerformance;

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

	wgpu.active = true;
}

void wgpu_shutdown() {
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
