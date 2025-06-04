#pragma once

#include <imgui/imgui.h>

#include "CommandContext.hpp"
#include "Pipeline.hpp"
#include "Hash.hpp"

namespace RoseEngine {

using PipelineInfo = std::variant<GraphicsPipelineInfo, ComputePipelineInfo>;

class PipelineCache {
public:
	struct CacheKey {
		ShaderDefines defines;
		PipelineInfo pipelineInfo;
		bool operator==(const CacheKey&) const = default;
	};
	struct CacheKeyHasher {
		inline size_t operator()(const CacheKey& key) const {
			return HashArgs(key.defines, HashVariant(key.pipelineInfo));
		}
	};
	struct ShaderEntryPoint {
		std::filesystem::path path;
		std::string           entry;
	};

	inline PipelineCache(std::filesystem::path path, const std::string& entry = "main", PipelineLayoutInfo layoutInfo_ = {}) {
		stages = { ShaderEntryPoint{ path, entry } };
		cachedShaders.resize(1);
		layoutInfo = layoutInfo_;
	}

	// create from range of ShaderEntryPoint
	inline PipelineCache(const std::vector<ShaderEntryPoint>& stages_, PipelineLayoutInfo layoutInfo_ = {}) {
		stages = stages_;
		layoutInfo = layoutInfo_;
		cachedShaders.resize(stages.size());
	}

	PipelineCache() = default;
	PipelineCache(const PipelineCache&) = default;
	PipelineCache(PipelineCache&&) = default;
	PipelineCache& operator=(const PipelineCache&) = default;
	PipelineCache& operator=(PipelineCache&&) = default;

	inline operator bool() const { return !stages.empty(); }

	inline void clear() { cachedPipelines.clear(); cachedShaders.clear(); }

	inline const PipelineLayoutInfo& GetLayoutInfo() const {
		return layoutInfo;
	}
	inline void SetLayoutInfo(const PipelineLayoutInfo& layoutInfo_) {
		layoutInfo = layoutInfo_;
		clear();
	}


	inline const ref<ShaderModule>& getShader(Device& device, const uint32_t index, const ShaderDefines& defines = {}) {
		ref<ShaderModule>& shader = cachedShaders[index][defines];

		// shader hot reload
		if (shader && ImGui::IsKeyPressed(ImGuiKey_F5, false) && shader->IsStale())
			shader = {};

		if (!shader) shader = ShaderModule::Create(device, stages[index].path, stages[index].entry, "sm_6_7", defines);

		return shader;
	}


	inline ref<Pipeline> get(Device& device, const ShaderDefines& defines = {}, const PipelineInfo& pipelineInfo = ComputePipelineInfo{}) {
		CacheKey key = { defines, pipelineInfo };

		if (auto it = cachedPipelines.find(key); it != cachedPipelines.end()) {
			// shader hot reload
			bool stale = false;
			if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
				for (const auto& shader : it->second->Shaders()) {
					if (shader->IsStale()) {
						stale = true;
						break;
					}
				}
			}
			if (stale) {
				device.Wait();
				cachedPipelines.erase(it);
			} else
				return it->second; // pipeline in cache
		}

		// pipeline not in cache. create & cache pipeline.

		std::vector<ref<const ShaderModule>> shaders(stages.size());
		for (uint32_t i = 0; i < stages.size(); i++) {
			shaders[i] = getShader(device, i, defines);
		}

		ref<Pipeline> p;
		if (shaders.size() == 1 && shaders[0]->Stage() == vk::ShaderStageFlagBits::eCompute) {
			p = Pipeline::CreateCompute(device, shaders[0], std::get<ComputePipelineInfo>(pipelineInfo), layoutInfo);
		} else {
			p = Pipeline::CreateGraphics(device, shaders, std::get<GraphicsPipelineInfo>(pipelineInfo), layoutInfo);
		}

		cachedPipelines.emplace(key, p);

		return p;
	}

	inline void operator()(CommandContext& context, const uint3 extent, const ShaderParameter& params, const ShaderDefines& defines = {}, const PipelineInfo& pipelineInfo = ComputePipelineInfo{}) {
        context.Dispatch(*get(context.GetDevice(), defines, pipelineInfo), extent, params);
	}

private:
	std::unordered_map<CacheKey, ref<Pipeline>, CacheKeyHasher> cachedPipelines;
	std::vector<std::unordered_map<ShaderDefines, ref<ShaderModule>>> cachedShaders;
	std::vector<ShaderEntryPoint> stages;
	PipelineLayoutInfo layoutInfo;
};

}