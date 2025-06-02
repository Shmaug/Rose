#pragma once

#include <Rose/Core/CommandContext.hpp>
#include <Rose/Core/TransientResourceCache.hpp>

struct cbt_Tree;

namespace RoseEngine {

class ConcurrentBinaryTree {
private:
	std::vector<cbt_Tree*>  trees = {};
	std::vector<BufferView> buffers = {};

	ref<Pipeline> cbtReducePrepassPipeline = {};
	ref<Pipeline> cbtReducePipeline = {};
	ref<Pipeline> dispatchArgsPipeline = {};
	ref<Pipeline> drawArgsPipeline = {};

	uint32_t numTrees = 1;
	uint32_t maxDepth = 6;
	bool     squareMode = false;

public:
	static ref<ConcurrentBinaryTree> Create(CommandContext& context, uint32_t depth = 6, uint32_t arraySize=1, bool square = true);
	~ConcurrentBinaryTree();

	inline const BufferView& GetBuffer(uint32_t i) { return buffers[i]; }

	inline uint32_t ArraySize() const { return numTrees; }
	inline uint32_t MaxDepth() const { return maxDepth; }
	inline bool     Square() const { return squareMode; }

	void Build(CommandContext& context);

	inline ShaderParameter GetShaderParameter() const {
		ShaderParameter params = {};
		for (uint32_t i = 0; i < buffers.size(); i++)
			params["u_CbtBuffers"][i] = buffers[i];
		return params;
	}

	inline void WriteIndirectDispatchArgs(CommandContext& context, const BufferView& buf, const uint32_t workgroupDim) const {
		ShaderParameter params = GetShaderParameter();
		params["output"] = buf;
		params["blockDim"] = workgroupDim;
		context.Dispatch(*dispatchArgsPipeline, numTrees, params);
	}
	inline void WriteIndirectDrawArgs(CommandContext& context, const BufferView& buf) const {
		ShaderParameter params = GetShaderParameter();
		params["output"] = buf;
		context.Dispatch(*drawArgsPipeline, numTrees, params);
	}
};

}