#pragma once

#include <stack>
#include <chrono>

#include <Rose/Core/PipelineCache.hpp>
#include "SceneNode.hpp"

namespace RoseEngine {

struct SceneRenderData {
	struct DrawBatch {
		const Pipeline* pipeline = nullptr;
		const Mesh*     mesh = nullptr;
		MeshLayout      meshLayout = {};
		std::vector<std::pair<uint32_t/*firstInstance*/, uint32_t/*instanceCount*/>> draws = {};
	};

	// 3 drawlists: alpha, cutout, opaque
	std::vector<std::vector<DrawBatch>> drawLists = {};

	ref<AccelerationStructure>          accelerationStructure = {};

	std::vector<weak_ref<SceneNode>>    instanceNodes = {};
	ShaderParameter                     sceneParameters = {};

	std::chrono::high_resolution_clock::time_point updateTime;
};

class Scene {
private:
	PipelineCache createImportanceMap = PipelineCache(FindShaderPath("CreateImportanceMap.cs.slang"));

	std::vector<vk::AccelerationStructureInstanceKHR> instances;
	std::vector<InstanceHeader>                       instanceHeaders;
	std::vector<Transform>                            transforms;

	std::vector<Material<uint32_t>> materials;
	std::unordered_map<const Material<ImageView>*, size_t> materialMap;
	std::unordered_map<ImageView, uint32_t> imageMap;

	std::vector<uint32_t> emissiveInstances;
	std::vector<MeshHeader> meshes;
	std::unordered_map<const Mesh*, size_t> meshMap;
	std::unordered_map<ref<Buffer>, uint32_t> meshBufferMap;

	bool dirty = false;

	// Batch render calls by: pipeline/mesh/material
	using RenderableSet =
		std::unordered_map<const Pipeline*,
			std::pair<
				MeshLayout,
				std::unordered_map<Mesh*,
					std::unordered_map<const Material<ImageView>*,
						std::vector<
							std::pair<SceneNode*, Transform> >>>>>;

	void PrepareRenderData(CommandContext& context, const RenderableSet& renderables);

public:
	ref<SceneNode>  sceneRoot = nullptr;
	SceneRenderData renderData = {};
	ImageView backgroundImage = {};
	ImageView backgroundImportanceMap = {};
	float3    backgroundColor = float3(0);
	float     backgroundSampleProbability = 0.5f;

	inline void SetDirty() { dirty = true; }

	void Load(CommandContext& context, const std::filesystem::path& p);
	void LoadDialog(CommandContext& context);

	inline void PreRender(CommandContext& context, auto getPipelineFn) {
		if (!dirty || !sceneRoot) return;

		// collect renderables and their transforms from the scene graph

		RenderableSet renderables;

		std::stack<std::pair<SceneNode*, Transform>> todo;
		todo.push({sceneRoot.get(), Transform::Identity()});
		while (!todo.empty()) {
			auto [n, t] = todo.top();
			todo.pop();

			if (n->mesh && n->material) {
				const auto& [key, cachedPipeline] = getPipelineFn(context.GetDevice(), *n->mesh, *n->material);
				auto&[meshLayout_, meshes] = renderables[cachedPipeline.get()];
				meshLayout_ = std::get<0>(key);
				meshes[n->mesh.get()][n->material.get()].emplace_back(std::pair{n, t});
			}

			for (const ref<SceneNode>& c : *n)
				todo.push({c.get(), c->transform.has_value() ? t * c->transform.value() : t});
		}

		PrepareRenderData(context, renderables);

		dirty = false;
	}
};

}