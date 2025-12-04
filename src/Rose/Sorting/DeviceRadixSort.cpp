
#include "DeviceRadixSort.h"
#include "Tuner.h"
#include "GPUSorting.h"

namespace RoseEngine {

const uint32_t k_isNotPartialBitFlag = 0;
const uint32_t k_isPartialBitFlag = 1;
const uint32_t k_maxDim = 65535;
const uint32_t k_radix = 256; // 8 bits per pass
const uint32_t k_radixPasses = 4; // 32-bit keys
const uint32_t k_maxReadback = 1 << 13; // 32-bit keys

static inline uint32_t divRoundUp(uint32_t x, uint32_t y)
{
	return (x + y - 1) / y;
}

GPUSorting::DeviceInfo GetDeviceInfo(const RoseEngine::Device& device)
{
	GPUSorting::DeviceInfo devInfo = {};
	const vk::raii::PhysicalDevice& physicalDevice = device.PhysicalDevice();

	// --- Query for features and properties using a pNext chain ---

	// FIX: Add PhysicalDevice16BitStorageFeatures to the chain to query for shaderInt16
	vk::PhysicalDevice16BitStorageFeatures features16bit_storage = {};
	vk::PhysicalDeviceShaderFloat16Int8Features features16bit_float_int8 = {};
	features16bit_storage.pNext = &features16bit_float_int8; // Chain them

	// FIX: The RAII-style getFeatures2 returns a StructureChain object.
	auto featuresChain = physicalDevice.getFeatures2<
		vk::PhysicalDeviceFeatures2,
		vk::PhysicalDevice16BitStorageFeatures,
		vk::PhysicalDeviceShaderFloat16Int8Features
		>();

	// Get the specific feature structs from the chain
	vk::PhysicalDeviceFeatures2& features2 = featuresChain.get<vk::PhysicalDeviceFeatures2>();
	vk::PhysicalDevice16BitStorageFeatures& features16bit = featuresChain.get<vk::PhysicalDevice16BitStorageFeatures>();
	vk::PhysicalDeviceShaderFloat16Int8Features& features_float_int8 = featuresChain.get<vk::PhysicalDeviceShaderFloat16Int8Features>();


	// FIX: The RAII-style getProperties2 also returns a StructureChain.
	auto propertiesChain = physicalDevice.getProperties2<
		vk::PhysicalDeviceProperties2,
		vk::PhysicalDeviceSubgroupProperties
		>();

	// Get the specific property structs from the chain
	vk::PhysicalDeviceProperties2& properties2 = propertiesChain.get<vk::PhysicalDeviceProperties2>();
	vk::PhysicalDeviceSubgroupProperties& subgroupProperties = propertiesChain.get<vk::PhysicalDeviceSubgroupProperties>();
	vk::PhysicalDeviceProperties props = properties2.properties;


	// --- Populate DeviceInfo from queried Vulkan structures ---

	// FIX: Assign char array directly to std::string.
	devInfo.Description = props.deviceName.data();
	devInfo.deviceId = props.deviceID;
	devInfo.vendorId = props.vendorID;

	bool isSoftwareDevice = (props.deviceType == vk::PhysicalDeviceType::eCpu);

	vk::PhysicalDeviceMemoryProperties memProps = physicalDevice.getMemoryProperties();
	for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
	{
		if (memProps.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
		{
			devInfo.dedicatedVideoMemory += memProps.memoryHeaps[i].size;
		}
		else
	{
			devInfo.sharedSystemMemory += memProps.memoryHeaps[i].size;
		}
	}

	uint32_t major = VK_API_VERSION_MAJOR(props.apiVersion);
	uint32_t minor = VK_API_VERSION_MINOR(props.apiVersion);
	// FIX: Use std::string for the assignment.
	devInfo.SupportedShaderModel = "Vulkan " + std::to_string(major) + "." + std::to_string(minor);
	bool hasModernAPIVersion = props.apiVersion >= VK_API_VERSION_1_1;

	devInfo.SIMDWidth = subgroupProperties.subgroupSize;
	devInfo.SIMDMaxWidth = subgroupProperties.subgroupSize;
	devInfo.SIMDLaneCount = subgroupProperties.subgroupSize;

	devInfo.SupportsWaveIntrinsics =
		(subgroupProperties.supportedOperations & vk::SubgroupFeatureFlagBits::eBallot) &&
		(subgroupProperties.supportedStages & vk::ShaderStageFlagBits::eCompute);

	// FIX: Check the correct feature from the correct struct.
	devInfo.Supports16BitTypes = features_float_int8.shaderFloat16 && features16bit.storageBuffer16BitAccess;

	devInfo.SupportsDeviceRadixSort =
		devInfo.SIMDWidth >= 4 &&
		devInfo.SupportsWaveIntrinsics &&
		hasModernAPIVersion;

	devInfo.SupportsOneSweep = devInfo.SupportsDeviceRadixSort && !isSoftwareDevice;

	auto to_mb = [](uint64_t bytes) { return bytes / (1024 * 1024); };
	std::cout << "--- Vulkan Device Info ---" << std::endl;
	std::cout << "Device:                   " << devInfo.Description << std::endl;
	std::cout << "API Version:              " << devInfo.SupportedShaderModel << std::endl;
	std::cout << "Subgroup (Wave) Size:     " << devInfo.SIMDWidth << std::endl;
	std::cout << "Dedicated VRAM (MB):      " << to_mb(devInfo.dedicatedVideoMemory) << std::endl;
	std::cout << "Shared System RAM (MB):   " << to_mb(devInfo.sharedSystemMemory) << std::endl;
	std::cout << "Supports Subgroup Ops:    " << (devInfo.SupportsWaveIntrinsics ? "Yes" : "No") << std::endl;
	std::cout << "Supports 16-Bit Types:    " << (devInfo.Supports16BitTypes ? "Yes" : "No") << std::endl;
	std::cout << "Supports GPU Radix Sort:  " << (devInfo.SupportsDeviceRadixSort ? "Yes" : "No") << std::endl;
	std::cout << "Supports OneSweep Sort:   " << (devInfo.SupportsOneSweep ? "Yes" : "No") << std::endl << std::endl;

	return devInfo;
}

void AddBufferBarrier(
	CommandContext& context,
	const BufferRange<uint32_t>& buffer,
	vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
	vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess)
{
	vk::BufferMemoryBarrier2 barriers[] {
		buffer.SetState(Buffer::ResourceState{
			.stage = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
			.queueFamily = context.QueueFamily()
		}),
	};
	vk::DependencyInfo depInfo{ .dependencyFlags = vk::DependencyFlagBits::eByRegion };
	depInfo.setBufferMemoryBarriers(barriers);
	context->pipelineBarrier2(depInfo);
}

void DeviceRadixSort::operator()(CommandContext& context, const BufferRange<uint>& keys, const BufferRange<uint>& payloads) {
	uint32_t numKeys = (uint32_t)keys.size();
	auto&[initPipeline, upsweepPipeline, scanPipeline, downsweepPipeline] = pipelines;
	if (!initPipeline) {
		GPUSorting::DeviceInfo devInfo = GetDeviceInfo(context.GetDevice());
		m_tuning = Tuner::GetTuningParameters(devInfo, GPUSorting::MODE::MODE_PAIRS);
		// TODO set parameters using TuningParameters
		ShaderDefines defs {
			{ "LOCK_TO_W32",     std::to_string(m_tuning.shouldLockWavesTo32) },
			{ "KEYS_PER_THREAD", std::to_string(m_tuning.keysPerThread) },
			{ "D_DIM",           std::to_string(m_tuning.threadsPerThreadblock) },
			{ "D_TOTAL_SMEM",    std::to_string(m_tuning.totalSharedMemory) },
			{ "PART_SIZE",       std::to_string(m_tuning.partitionSize) },
			{ "KEY_UINT",        "true" },
			{ "PAYLOAD_UINT",    "true" },
			{ "SHOULD_ASCEND",   "true" },
			{ "SORT_PAIRS",      "true" },
		};
		auto shaderFile = FindShaderPath("DeviceRadixSort.slang");
		initPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "InitDeviceRadixSort", "sm_6_7", defs));
		upsweepPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "Upsweep", "sm_6_7", defs));
		scanPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "Scan", "sm_6_7", defs));
		downsweepPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "Downsweep", "sm_6_7", defs));
	}

	// TODO these values need to be obtained
	uint32_t threadBlocks = divRoundUp(numKeys, m_tuning.partitionSize);

	auto m_globalHistBuffer = context.GetTransientBuffer<uint32_t>(
		k_radix * k_radixPasses,
		vk::BufferUsageFlagBits::eStorageBuffer);
	// auto m_errorCountBuffer = context.GetTransientBuffer<uint32_t>(
	// 	1
	// 	vk::BufferUsageFlagBits::eStorageBuffer);
	// auto m_readBackBuffer = context.GetTransientBuffer<uint32_t>(
	// 	k_maxReadBack,
	// 	vk::BufferUsageFlagBits::eStorageBuffer);

	auto m_sortBuffer = keys;
	auto m_altBuffer = context.GetTransientBuffer<uint32_t>(
		numKeys,
		vk::BufferUsageFlagBits::eStorageBuffer);
	auto m_passHistBuffer = context.GetTransientBuffer<uint32_t>(
		k_radix * threadBlocks,
		vk::BufferUsageFlagBits::eStorageBuffer);
	auto m_sortPayloadBuffer = payloads;
	auto m_altPayloadBuffer = context.GetTransientBuffer<uint32_t>(
		numKeys,
		vk::BufferUsageFlagBits::eStorageBuffer);

	auto even_desc_set = context.GetDescriptorSets(*initPipeline->Layout());
	auto odd_desc_set = context.GetDescriptorSets(*initPipeline->Layout());
	{
		ShaderParameter params;
		params["b_sort"] = (BufferParameter)m_sortBuffer; // u0
		params["b_alt"] = (BufferParameter)m_altBuffer; // u1
		params["b_sortPayload"] = (BufferParameter)m_sortPayloadBuffer; // u2
		params["b_altPayload"] = (BufferParameter)m_altPayloadBuffer; // u3
		params["b_globalHist"] = (BufferParameter)m_globalHistBuffer; //u4
		params["b_passHist"] = (BufferParameter)m_passHistBuffer; // u5

		context.UpdateDescriptorSets(*even_desc_set, params, *initPipeline->Layout());
	}
	{
		ShaderParameter params;
		params["b_sort"] = (BufferParameter)m_altBuffer; // u0
		params["b_alt"] = (BufferParameter)m_sortBuffer; // u1
		params["b_sortPayload"] = (BufferParameter)m_altPayloadBuffer; // u2
		params["b_altPayload"] = (BufferParameter)m_sortPayloadBuffer; // u3
		params["b_globalHist"] = (BufferParameter)m_globalHistBuffer; //u4
		params["b_passHist"] = (BufferParameter)m_passHistBuffer; // u5

		context.UpdateDescriptorSets(*odd_desc_set, params, *initPipeline->Layout());
	}

	DeviceRadixSortPushConstants pushConstants;
	pushConstants.numKeys = numKeys;
	pushConstants.radixShift = 0;
	pushConstants.threadBlocks = threadBlocks;
	pushConstants.isPartial = k_isNotPartialBitFlag;

	// context->bindPipeline(vk::PipelineBindPoint::eCompute, **initPipeline);
	// context.BindDescriptors(*initPipeline->Layout(), *even_desc_set);
	// context->dispatch(1, 1, 1);
	context.Fill(m_globalHistBuffer.cast<uint32_t>(), 0u);
	AddBufferBarrier(context, m_globalHistBuffer,
				  vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
				  vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
	for (uint32_t pass = 0; pass < k_radixPasses; ++pass)
	{
		pushConstants.radixShift = pass * 8;
		auto& desc_set = (pass % 2 == 0) ? even_desc_set : odd_desc_set;

		// Upsweep kernel
		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, **upsweepPipeline);
			context.BindDescriptors(*upsweepPipeline->Layout(), *desc_set);
			const uint32_t fullBlocks = threadBlocks / k_maxDim;
			const uint32_t partialBlocks = threadBlocks - fullBlocks * k_maxDim;
			if (fullBlocks) {
				pushConstants.isPartial = k_isNotPartialBitFlag;
				context->pushConstants<DeviceRadixSortPushConstants>(**upsweepPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
				context->dispatch(k_maxDim, fullBlocks, 1);
			}
			if (partialBlocks) {
				pushConstants.isPartial = fullBlocks << 1 | k_isPartialBitFlag;
				context->pushConstants<DeviceRadixSortPushConstants>(**upsweepPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
				context->dispatch(partialBlocks, 1, 1);
			}
		}

		AddBufferBarrier(context, m_passHistBuffer,
				   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
				   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);


		// Scan Kernel
		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, **scanPipeline);
			context.BindDescriptors(*scanPipeline->Layout(), *desc_set);
			context->pushConstants<DeviceRadixSortPushConstants>(**scanPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
			context->dispatch(256, 1, 1);
		}
		AddBufferBarrier(context, m_passHistBuffer,
				   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
				   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
		// AddBufferBarrier(context, m_globalHistBuffer,
		// 		   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
		// 		   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);

		// Downsweep Kernel
		{
			context->bindPipeline(vk::PipelineBindPoint::eCompute, **downsweepPipeline);
			context.BindDescriptors(*downsweepPipeline->Layout(), *desc_set);
			const uint32_t fullBlocks = threadBlocks / k_maxDim;
			const uint32_t partialBlocks = threadBlocks - fullBlocks * k_maxDim;
			if (fullBlocks) {
				pushConstants.isPartial = k_isNotPartialBitFlag;
				context->pushConstants<DeviceRadixSortPushConstants>(**downsweepPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
				context->dispatch(k_maxDim, fullBlocks, 1);
			}
			if (partialBlocks) {
				pushConstants.isPartial = fullBlocks << 1 | k_isPartialBitFlag;
				context->pushConstants<DeviceRadixSortPushConstants>(**downsweepPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
				context->dispatch(partialBlocks, 1, 1);
			}
		}
		AddBufferBarrier(context, m_sortPayloadBuffer,
				   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
				   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
		// AddBufferBarrier(context, m_sortBuffer,
		// 		   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
		// 		   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
		// AddBufferBarrier(context, m_altPayloadBuffer,
		// 		   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
		// 		   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
		// AddBufferBarrier(context, m_altBuffer,
		// 		   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
		// 		   vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
	}

	// TODO figure out how to get the data out
	context.AddBarrier(keys.SetState(Buffer::ResourceState{
		.stage = vk::PipelineStageFlagBits2::eComputeShader,
		.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
		.queueFamily = context.QueueFamily()
	}));
}
}

