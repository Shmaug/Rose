#include "ConcurrentBinaryTree.hpp"

#define CBT_IMPLEMENTATION
#include "cbt.h"

#define LEB_IMPLEMENTATION
#include "leb.h"

namespace RoseEngine {

ref<ConcurrentBinaryTree> ConcurrentBinaryTree::Create(CommandContext& context, uint32_t depth, uint32_t arraySize, bool square) {
	auto cbt = make_ref<ConcurrentBinaryTree>();
	cbt->squareMode = square;
	cbt->maxDepth = depth;
	cbt->numTrees = arraySize;
	cbt->trees.resize(cbt->numTrees);
	cbt->buffers.resize(cbt->numTrees);
	for (uint32_t i = 0; i < cbt->numTrees; i++) {
		cbt->trees[i] = cbt_Create(cbt->maxDepth);
		cbt->buffers[i] = context.UploadData(std::span{ cbt_GetHeap(cbt->trees[i]), (size_t)cbt_HeapByteSize(cbt->trees[i]) }, vk::BufferUsageFlagBits::eStorageBuffer);
		context.GetDevice().SetDebugName(**cbt->buffers[i].mBuffer, "CBT Buffer " + std::to_string(i));
	}

	ShaderDefines defs { { "CBT_HEAP_BUFFER_COUNT", std::to_string(arraySize) } };

	auto cbtSrc    = FindShaderPath("cbt/cbt.cs.slang");
	cbt->cbtReducePrepassPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), cbtSrc, "SumReducePrepass", "sm_6_7", defs));
	cbt->cbtReducePipeline        = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), cbtSrc, "SumReduce", "sm_6_7", defs));
	cbt->dispatchArgsPipeline     = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), cbtSrc, "WriteIndirectDispatchArgs", "sm_6_7", defs));
	cbt->drawArgsPipeline         = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), cbtSrc, "WriteIndirectDrawArgs", "sm_6_7", defs));
	return cbt;
}
ConcurrentBinaryTree::~ConcurrentBinaryTree() {
	for (uint32_t i = 0; i < trees.size(); i++)
		cbt_Release(trees[i]);
	trees.clear();
}

void ConcurrentBinaryTree::Build(CommandContext& context) {
	ShaderParameter params = {};
	params["cbt"] = GetShaderParameter();

	auto descriptorSets = context.GetDescriptorSets(*cbtReducePipeline->Layout());
	context.UpdateDescriptorSets(*descriptorSets, params, *cbtReducePipeline->Layout());

	int it = maxDepth;
	{
		for (uint32_t i = 0; i < trees.size(); i++)
			context.AddBarrier(buffers[i], Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily() });
		context.ExecuteBarriers();

		context->bindPipeline(vk::PipelineBindPoint::eCompute, ***cbtReducePrepassPipeline);
		context.BindDescriptors(*cbtReducePrepassPipeline->Layout(), *descriptorSets);
		context.ExecuteBarriers();
		params["u_PassID"] = it;
		for (uint32_t i = 0; i < trees.size(); i++) {
			params["u_CbtID"] = i;
			context.PushConstants(*cbtReducePrepassPipeline->Layout(), params);
			context->dispatch((((1 << it) >> 5) + 255) / 256, 1u, 1u);
		}
		it -= 5;
	}
	context->bindPipeline(vk::PipelineBindPoint::eCompute, ***cbtReducePipeline);
	context.BindDescriptors(*cbtReducePipeline->Layout(), *descriptorSets);
	while (--it >= 0) {
		for (uint32_t i = 0; i < trees.size(); i++)
			context.AddBarrier(buffers[i], Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily() });
		context.ExecuteBarriers();

		params["u_PassID"] = it;
		for (uint32_t i = 0; i < trees.size(); i++) {
			params["u_CbtID"] = i;
			context.PushConstants(*cbtReducePipeline->Layout(), params);
			context->dispatch(((1u << it) + 255) / 256, 1u, 1u);
		}
	}
}

}