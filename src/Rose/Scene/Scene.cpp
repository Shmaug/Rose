#pragma once

#include <portable-file-dialogs.h>

#include "Scene.hpp"
#include "LoadGLTF.hpp"

namespace RoseEngine {

void Scene::Load(CommandContext& context, const std::filesystem::path& p) {
	if (p.extension() == ".gltf" || p.extension() == ".glb") {
		const ref<SceneNode> s = LoadGLTF(context, p);
		if (!s) return;
		sceneRoot = s;
		SetDirty();
	} else {
		const PixelData d = LoadImageFile(context, p);
		if (!d.data) return;
		const ImageView img = ImageView::Create(
			Image::Create(context.GetDevice(), ImageInfo{
				.format = d.format,
				.extent = d.extent,
				.queueFamilies = { context.QueueFamily() } }));
		if (!img) return;
		context.Copy(d.data, img);
		context.GenerateMipMaps(img.mImage);
		backgroundImage = img;
		backgroundColor = float3(1);
		SetDirty();
	}
}

void Scene::LoadDialog(CommandContext& context) {
	auto f = pfd::open_file("Open scene", "", {
		//"All files (.*)", "*.*",
		"glTF Scenes (.gltf .glb)", "*.gltf *.glb",
		"Environment maps (.exr .hdr .dds .png .jpg)", "*.exr *.hdr *.dds *.png *.jpg",
	});
	for (const std::string& filepath : f.result()) {
		Load(context, filepath);
	}
}

void Scene::PrepareRenderData(CommandContext& context, const Scene::RenderableSet& renderables) {
	// create instances and draw calls from renderables
	const bool useAccelerationStructure = context.GetDevice().EnabledExtensions().contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

	for (auto& d : renderData.drawLists) d.clear();
	renderData.drawLists.resize(3);
	renderData.instanceNodes.clear();

	instances.clear();
	instanceHeaders.clear();
	transforms.clear();

	materials.clear();
	materialMap.clear();
	imageMap.clear();

	meshes.clear();
	meshMap.clear();
	meshBufferMap.clear();

	emissiveInstances.clear();

	for (const auto&[pipeline, meshes__] : renderables) {
		const auto& [meshLayout, meshes_] = meshes__;
		for (const auto&[mesh, materials_] : meshes_) {
			size_t meshId = meshes.size();
			if (auto it = meshMap.find(mesh); it != meshMap.end())
				meshId = it->second;
			else {
				meshMap.emplace(mesh, meshId);
				meshes.emplace_back(PackMesh(*mesh, meshBufferMap));
			}

			bool opaque = true;
			for (const auto&[material, nt_] : materials_) {
				if (material->HasFlag(MaterialFlags::eAlphaCutoff)) opaque = false;
			}
			if (useAccelerationStructure) mesh->UpdateBLAS(context, opaque);


			for (const auto&[material, nt_] : materials_) {
				size_t materialId = materials.size();
				if (auto it = materialMap.find(material); it != materialMap.end())
					materialId = it->second;
				else {
					materialMap.emplace(material, materialId);
					materials.emplace_back(PackMaterial(*material, imageMap));
				}

				const bool isEmissive = any(glm::greaterThan(material->GetEmission(), float3(0.f)));

				const size_t start = instanceHeaders.size();
				for (const auto&[n,t] : nt_) {
					size_t instanceId = instanceHeaders.size();
					instanceHeaders.emplace_back(InstanceHeader{
						.transformIndex = (uint32_t)transforms.size(),
						.materialIndex  = (uint32_t)materialId,
						.meshIndex = (uint32_t)meshId,
						.triangleCount = uint32_t(mesh->indexBuffer.size_bytes()/mesh->indexSize)/3 });
					transforms.emplace_back(t);
					renderData.instanceNodes.emplace_back(n->shared_from_this());

					if (isEmissive) emissiveInstances.emplace_back((uint32_t)instanceId);

					if (useAccelerationStructure) {
						vk::GeometryInstanceFlagsKHR flags = material->HasFlag(MaterialFlags::eDoubleSided) ? vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable : vk::GeometryInstanceFlagBitsKHR{};
						instances.emplace_back(vk::AccelerationStructureInstanceKHR{
							.transform = std::bit_cast<vk::TransformMatrixKHR>((float3x4)transpose(t.transform)),
							.instanceCustomIndex = (uint32_t)instanceId,
							.mask = 1,
							.flags = (VkGeometryInstanceFlagsKHR)flags,
							.accelerationStructureReference = mesh->blas->GetDeviceAddress(context.GetDevice())
						});
					}
				}

				auto& drawList = renderData.drawLists[material->HasFlag(MaterialFlags::eAlphaBlend) ? 2 : material->HasFlag(MaterialFlags::eAlphaCutoff) ? 1 : 0];
				auto& draws = drawList.emplace_back(SceneRenderData::DrawBatch{
					.pipeline   = pipeline,
					.mesh       = mesh,
					.meshLayout = meshLayout,
					.draws = {}
				}).draws;
				draws.emplace_back(std::pair{(uint32_t)start, (uint32_t)(instanceHeaders.size() - start)});
			}
		}
	}

	if (useAccelerationStructure) renderData.accelerationStructure = AccelerationStructure::Create(context, instances);

	renderData.sceneParameters["backgroundColor"] = backgroundColor;
	uint32_t backgroundImageIndex = -1;
	if (backgroundImage && any(glm::greaterThan(backgroundColor, float3(0.f)))) {
		auto it = imageMap.find(backgroundImage);
		if (it == imageMap.end()) {
			backgroundImageIndex = (uint32_t)imageMap.size();
			imageMap.emplace(backgroundImage, backgroundImageIndex);
		} else {
			backgroundImageIndex = it->second;
		}

		if (!backgroundImportanceMap || backgroundImportanceMap.Extent() != backgroundImage.Extent()) {
			backgroundImportanceMap = ImageView::Create(
				Image::Create(context.GetDevice(), ImageInfo{
					.format = vk::Format::eR16Sfloat,
					.extent = backgroundImage.Extent(),
					.mipLevels = GetMaxMipLevels(backgroundImage.Extent()),
					.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
					.queueFamilies = { context.QueueFamily() } }));
		}

		ShaderParameter params = {};
		params["image"]         = ImageParameter{ .image = backgroundImage,         .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
		params["importanceMap"] = ImageParameter{ .image = backgroundImportanceMap, .imageLayout = vk::ImageLayout::eGeneral };
		params["dim"] = (uint2)backgroundImage.Extent();
		createImportanceMap(context, backgroundImage.Extent(), params);
		context.GenerateMipMaps(backgroundImportanceMap.GetImage());
	} else {
		backgroundImportanceMap = {};
	}
	renderData.sceneParameters["backgroundImage"] = backgroundImageIndex;

	renderData.sceneParameters["instanceCount"]               = (uint32_t)instanceHeaders.size();
	renderData.sceneParameters["meshBufferCount"]             = (uint32_t)meshBufferMap.size();
	renderData.sceneParameters["materialCount"]               = (uint32_t)materials.size();
	renderData.sceneParameters["imageCount"]                  = (uint32_t)imageMap.size();
	renderData.sceneParameters["emissiveInstanceCount"]       = (uint32_t)emissiveInstances.size();
	renderData.sceneParameters["backgroundSampleProbability"] = backgroundSampleProbability;

	// avoid creating empty buffers
	if (materials.empty())         materials.push_back({});
	if (emissiveInstances.empty()) emissiveInstances.push_back({});
	if (instanceHeaders.empty())   instanceHeaders.push_back({});
	if (transforms.empty())        transforms.push_back({});
	if (materials.empty())         materials.push_back({});
	if (meshes.empty())            meshes.push_back({});
	if (emissiveInstances.empty()) emissiveInstances.push_back({});

	std::vector<Transform> invTransforms(transforms.size());
	std::ranges::transform(transforms, invTransforms.begin(), [](const Transform& t) { return inverse(t); });

	renderData.sceneParameters["instances"]         = (BufferView)context.UploadData(instanceHeaders,   vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eVertexBuffer);
	renderData.sceneParameters["transforms"]        = (BufferView)context.UploadData(transforms,        vk::BufferUsageFlagBits::eStorageBuffer);
	renderData.sceneParameters["inverseTransforms"] = (BufferView)context.UploadData(invTransforms,     vk::BufferUsageFlagBits::eStorageBuffer);
	renderData.sceneParameters["materials"]         = (BufferView)context.UploadData(materials,         vk::BufferUsageFlagBits::eStorageBuffer);
	renderData.sceneParameters["meshes"]            = (BufferView)context.UploadData(meshes,            vk::BufferUsageFlagBits::eStorageBuffer);
	renderData.sceneParameters["emissiveInstances"] = (BufferView)context.UploadData(emissiveInstances, vk::BufferUsageFlagBits::eStorageBuffer);
	renderData.sceneParameters["backgroundImportanceMap"] = ImageParameter{ .image = backgroundImportanceMap, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
	if (useAccelerationStructure) renderData.sceneParameters["accelerationStructure"] = renderData.accelerationStructure;
	for (const auto& [buf, idx] : meshBufferMap) renderData.sceneParameters["meshBuffers"][idx] = BufferView{buf, 0, buf->Size()};
	for (const auto& [img, idx] : imageMap)      renderData.sceneParameters["images"][idx] = ImageParameter{ .image = img, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

	renderData.updateTime = std::chrono::high_resolution_clock::now();
}

}