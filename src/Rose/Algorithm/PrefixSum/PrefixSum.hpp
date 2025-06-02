#pragma once

#include <Rose/Core/CommandContext.hpp>
#include <Rose/Core/TransientResourceCache.hpp>

#include "PrefixSum.h"

namespace RoseEngine {

class PrefixSumExclusive {
private:
	ref<Pipeline> groupScanPipeline;
	ref<Pipeline> finalizeGroupsPipeline;

public:
	inline void operator()(CommandContext& context, const BufferRange<uint32_t>& data) {
		if (!groupScanPipeline) {
			auto shaderFile = FindShaderPath("PrefixSum.cs.slang");
			groupScanPipeline      = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "groupScan"));
			finalizeGroupsPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "finalizeGroups"));
		}

		const uint32_t blockDim = groupScanPipeline->GetShader()->WorkgroupSize().x;

		PrefixSumPushConstants pushConstants;
		pushConstants.dataSize = (uint32_t)data.size();
		pushConstants.numGroups = (pushConstants.dataSize + blockDim-1) / blockDim;

		auto groupSums  = context.GetTransientBuffer(sizeof(uint32_t) * blockDim, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst).cast<uint32_t>();
		auto globalSums = context.GetTransientBuffer(sizeof(uint32_t) * 2,        vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst).cast<uint32_t>();

		context.Fill(globalSums, 0u);

		auto descriptorSets = context.GetDescriptorSets(*groupScanPipeline->Layout());
		{
			ShaderParameter params = {};
			params["data"]       = (BufferParameter)data;
			params["groupSums"]  = (BufferParameter)groupSums;
			params["globalSums"] = (BufferParameter)globalSums;
			context.UpdateDescriptorSets(*descriptorSets, params, *groupScanPipeline->Layout());
		}

		const uint32_t elementsPerIteration = blockDim * blockDim * 2;
		const uint32_t iterationsCount = (pushConstants.dataSize + elementsPerIteration-1) / elementsPerIteration;

		uint32_t remaining = pushConstants.dataSize;

		for (pushConstants.iteration = 0; pushConstants.iteration < iterationsCount; pushConstants.iteration++) {
			pushConstants.numGroups = std::max(1u, (std::min(remaining, elementsPerIteration) + blockDim * 2 - 1)/ (blockDim * 2));

			context.Fill(groupSums, 0u);
			context.ExecuteBarriers();

			context->bindPipeline(vk::PipelineBindPoint::eCompute, ***groupScanPipeline);
			context.BindDescriptors(*groupScanPipeline->Layout(), *descriptorSets);
			context->pushConstants<PrefixSumPushConstants>(***groupScanPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0, pushConstants);
			context->dispatch(pushConstants.numGroups, 1, 1);

			context.AddBarrier(data.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			}));
			context.AddBarrier(groupSums.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			}));
			context.ExecuteBarriers();

			if (pushConstants.numGroups > 1) {
				context->bindPipeline(vk::PipelineBindPoint::eCompute, ***finalizeGroupsPipeline);
				context.BindDescriptors(*finalizeGroupsPipeline->Layout(), *descriptorSets);
				context->pushConstants<PrefixSumPushConstants>(***finalizeGroupsPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0, pushConstants);
				context->dispatch((pushConstants.numGroups - 1) * 2, 1, 1);
			}

			remaining -= elementsPerIteration;
		}
	}
};

}