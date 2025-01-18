#include <iostream>
#include <ranges>

#define VMA_IMPLEMENTATION
#include "Device.hpp"

#include "Instance.hpp"

namespace RoseEngine {

void ConfigureFeatures(Device& device, vk::PhysicalDeviceFeatures& features, auto& createInfo) {
	features.fillModeNonSolid = true;
	features.samplerAnisotropy = true;
	features.shaderImageGatherExtended = true;
	features.shaderStorageImageExtendedFormats = true;
	features.wideLines = true;
	features.largePoints = true;
	features.sampleRateShading = true;
	features.shaderInt16 = true;
	features.geometryShader = true;
	features.shaderStorageBufferArrayDynamicIndexing = true;
	features.shaderSampledImageArrayDynamicIndexing = true;
	features.shaderStorageImageArrayDynamicIndexing = true;

	const bool accelerationStructure = device.EnabledExtensions().contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

	vk::PhysicalDeviceVulkan12Features& vk12features = std::get<vk::PhysicalDeviceVulkan12Features>(createInfo);
	vk12features.shaderStorageBufferArrayNonUniformIndexing = true;
	vk12features.shaderSampledImageArrayNonUniformIndexing = true;
	vk12features.shaderStorageImageArrayNonUniformIndexing = true;
	vk12features.descriptorBindingPartiallyBound = true;
	vk12features.shaderFloat16 = true;
	vk12features.bufferDeviceAddress = accelerationStructure || device.EnabledExtensions().contains(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
	vk12features.timelineSemaphore = true;

	vk::PhysicalDeviceVulkan13Features& vk13features = std::get<vk::PhysicalDeviceVulkan13Features>(createInfo);
	vk13features.dynamicRendering = true;
	vk13features.synchronization2 = true;

	vk::PhysicalDevice16BitStorageFeatures& storageFeatures = std::get<vk::PhysicalDevice16BitStorageFeatures>(createInfo);
	storageFeatures.storageBuffer16BitAccess = true;

	if (device.EnabledExtensions().contains(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME))
		std::get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>(createInfo).shaderBufferFloat32AtomicAdd = true;
	else
		createInfo.unlink<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>();

	if (accelerationStructure)
		std::get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>(createInfo).accelerationStructure = accelerationStructure;
	else
		createInfo.unlink<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();

	if (device.EnabledExtensions().contains(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
		auto& rtfeatures = std::get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>(createInfo);
		rtfeatures.rayTracingPipeline = true;
		rtfeatures.rayTraversalPrimitiveCulling = rtfeatures.rayTracingPipeline;
	} else {
		createInfo.unlink<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
	}

	if (device.EnabledExtensions().contains(VK_KHR_RAY_QUERY_EXTENSION_NAME))
		std::get<vk::PhysicalDeviceRayQueryFeaturesKHR>(createInfo).rayQuery = device.EnabledExtensions().contains(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	else
		createInfo.unlink<vk::PhysicalDeviceRayQueryFeaturesKHR>();

	if (device.EnabledExtensions().contains(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME))
		std::get<vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR>(createInfo).fragmentShaderBarycentric = true;
	else
		createInfo.unlink<vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR>();
}

ref<Device> Device::Create(const Instance& instance, const vk::raii::PhysicalDevice& physicalDevice, const vk::ArrayProxy<const std::string>& deviceExtensions) {
	ref<Device> device = make_ref<Device>();

	device->mPhysicalDevice = physicalDevice;
	device->mInstance = **instance;

	for (const auto& e : deviceExtensions)
		device->mExtensions.emplace(e);

	ConfigureFeatures(*device, device->mFeatures, device->mCreateInfo);

	// Configure queues

	const auto queueFamilyProperties = device->mPhysicalDevice.getQueueFamilyProperties();
	std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
	float queuePriority = 1.0f;
	for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
		if (queueFamilyProperties[i].queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer)) {
			queueCreateInfos.emplace_back(vk::DeviceQueueCreateInfo{
				.queueFamilyIndex = i,
				.queueCount = 1,
				.pQueuePriorities = &queuePriority
			});
		}
	}

	// Create device

	std::vector<const char*> deviceExts;
	std::vector<const char*> validationLayers;
	for (const auto& s : device->mExtensions) deviceExts.emplace_back(s.c_str());
	for (const auto& s : instance.EnabledLayers()) validationLayers.emplace_back(s.c_str());

	vk::DeviceCreateInfo& createInfo = device->mCreateInfo.get<vk::DeviceCreateInfo>()
		.setQueueCreateInfos(queueCreateInfos)
		.setPEnabledLayerNames(validationLayers)
		.setPEnabledExtensionNames(deviceExts)
		.setPEnabledFeatures(&device->mFeatures);
	device->mDevice = device->mPhysicalDevice.createDevice(createInfo);

	device->mPipelineCache = device->mDevice.createPipelineCache({});

	// Create allocator

	VmaAllocatorCreateInfo allocatorInfo{
		.flags = 0,
		.physicalDevice = *device->mPhysicalDevice,
		.device   = *device->mDevice,
		.instance = **instance,
		.vulkanApiVersion = instance.VulkanVersion()
	};
	if (device->mExtensions.contains(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))                     allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	if (std::get<vk::PhysicalDeviceVulkan12Features>(device->mCreateInfo).bufferDeviceAddress) allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &device->mMemoryAllocator);

	// Create timeline semaphore

	device->mCurrentTimelineValue = 0;
	vk::StructureChain<vk::SemaphoreCreateInfo, vk::SemaphoreTypeCreateInfo> semaphoreInfo = {};
	semaphoreInfo.get<vk::SemaphoreTypeCreateInfo>()
		.setSemaphoreType(vk::SemaphoreType::eTimeline)
		.setInitialValue(device->IncrementTimelineSignal());
	device->mTimelineSemaphore = (*device)->createSemaphore(semaphoreInfo.get<vk::SemaphoreCreateInfo>());
	device->SetDebugName(*device->mTimelineSemaphore, "Device timeline");

	// Assign stuff

	device->mUseDebugUtils = instance.DebugMessengerEnabled();

	const auto& properties = device->mPhysicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
	device->SetDebugName(*device->mDevice, "[" + std::to_string(properties.get<vk::PhysicalDeviceProperties2>().properties.deviceID) + "]: " + properties.get<vk::PhysicalDeviceProperties2>().properties.deviceName.data());

	device->mLimits = properties.get<vk::PhysicalDeviceProperties2>().properties.limits;
	device->mAccelerationStructureProperties = properties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();

	return device;
}
Device::~Device() {
	if (mMemoryAllocator != nullptr) {
		vmaDestroyAllocator(mMemoryAllocator);
		mMemoryAllocator = nullptr;
	}
}

void Device::LoadPipelineCache(const std::filesystem::path& path) {
	std::vector<uint8_t> cacheData;
	vk::PipelineCacheCreateInfo cacheInfo = {};
	try {
		cacheData = ReadFile<std::vector<uint8_t>>(path);
		if (!cacheData.empty()) {
			cacheInfo.pInitialData = cacheData.data();
			cacheInfo.initialDataSize = cacheData.size();
			std::cout << "Read pipeline cache (" << std::fixed << std::showpoint << std::setprecision(2) << cacheData.size()/1024.f << "KiB)" << std::endl;
		}
	} catch (std::exception& e) {
		std::cerr << "Warning: Failed to read pipeline cache: " << e.what() << std::endl;
	}
	mPipelineCache = mDevice.createPipelineCache(cacheInfo);
}
void Device::StorePipelineCache(const std::filesystem::path& path) {
	try {
		const std::vector<uint8_t> cacheData = mPipelineCache.getData();
		if (!cacheData.empty())
			WriteFile(path, cacheData);
	} catch (std::exception& e) {
		std::cerr << "Warning: Failed to write pipeline cache: " << e.what() << std::endl;
	}
}

}