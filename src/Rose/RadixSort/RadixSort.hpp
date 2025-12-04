#pragma once

#include <Rose/Core/CommandContext.hpp>
#include <Rose/Core/TransientResourceCache.hpp>
#include "RadixSort.h"

namespace RoseEngine {

class RadixSort {
private:
	// payload size -> (histogramPipeline, sortPipeline)
	std::unordered_map<uint32_t, std::pair<ref<Pipeline>, ref<Pipeline>>> pipelines;
	ref<Pipeline> histogramPipeline;
	ref<Pipeline> sortPipeline;

	uint32_t numBlocksPerWorkgroup = 32;

public:
	template<typename KeyType>
	inline void operator()(CommandContext& context, const BufferRange<KeyType>& keys) {
		const uint32_t keySize = sizeof(KeyType)/sizeof(uint32_t);

		auto&[histogramPipeline, sortPipeline] = pipelines[keySize];
		if (!histogramPipeline) {
			ShaderDefines defs {
				{ "SUBGROUP_SIZE", "32" },
				{ "KEY_SIZE", std::to_string(keySize) },
			};
			auto shaderFile = FindShaderPath("RadixSort.cs.slang");
			histogramPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "multi_radixsort_histograms", "sm_6_7", defs));
			sortPipeline      = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "multi_radixsort",            "sm_6_7", defs));
		}

		uint32_t numElements = (uint32_t)keys.size();
        uint32_t numThreads = numElements / numBlocksPerWorkgroup;
        uint32_t remainder  = numElements % numBlocksPerWorkgroup;
        numThreads += remainder > 0 ? 1 : 0;
		uint32_t numWorkgroups = (numThreads + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

		auto keys_tmp        = context.GetTransientBuffer<KeyType>(numElements, vk::BufferUsageFlagBits::eStorageBuffer);
		auto histogramBuffer = context.GetTransientBuffer<uint32_t>(numWorkgroups * RADIX_SORT_BINS, vk::BufferUsageFlagBits::eStorageBuffer);

		auto descriptorSets = context.GetDescriptorSets(*sortPipeline->Layout());
		{
			ShaderParameter params;
			params["g_keys"][0] = (BufferParameter)keys;
			params["g_keys"][1] = (BufferParameter)keys_tmp;
			params["g_histograms"] = (BufferParameter)histogramBuffer;
			context.UpdateDescriptorSets(*descriptorSets, params, *sortPipeline->Layout());
		}

		RadixSortPushConstants pushConstants;
		pushConstants.g_pass_index = 0;
		pushConstants.g_num_elements = numElements;
		pushConstants.g_num_workgroups = numWorkgroups;
		pushConstants.g_num_blocks_per_workgroup = numBlocksPerWorkgroup;

		vk::BufferMemoryBarrier2 barriers[] {
			histogramBuffer.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			}),
			keys.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			}),
			keys_tmp.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			})
		};
		vk::DependencyInfo depInfo { .dependencyFlags = vk::DependencyFlagBits::eByRegion };
		depInfo.setBufferMemoryBarriers(barriers);

		for (pushConstants.g_pass_index = 0; pushConstants.g_pass_index < sizeof(uint32_t); pushConstants.g_pass_index++) {

			context->pipelineBarrier2(depInfo);

			context->bindPipeline(vk::PipelineBindPoint::eCompute, **histogramPipeline);
			context.BindDescriptors(*histogramPipeline->Layout(), *descriptorSets);
			context->pushConstants<RadixSortPushConstants>(**histogramPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
			context->dispatch(numWorkgroups, 1, 1);

			context->pipelineBarrier2(depInfo);

			context->bindPipeline(vk::PipelineBindPoint::eCompute, **sortPipeline);
			context.BindDescriptors(*sortPipeline->Layout(), *descriptorSets);
			context->pushConstants<RadixSortPushConstants>(**sortPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
			context->dispatch(numWorkgroups, 1, 1);
		}

		context.AddBarrier(keys.SetState(Buffer::ResourceState{
			.stage = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
			.queueFamily = context.QueueFamily()
		}));
	}
};

}
