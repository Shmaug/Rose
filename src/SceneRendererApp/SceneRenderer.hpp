#pragma once

#include <stack>

#include <Rose/Core/PipelineCache.hpp>
#include <Rose/Scene/Scene.hpp>
#include "Tonemapper/Tonemapper.hpp"

namespace RoseEngine {

ref<SceneNode> LoadGLTF(CommandContext& context, const std::filesystem::path& filename);

class SceneRenderer {
public:
	using AttachmentInfo = std::tuple<std::string, vk::Format, vk::ClearValue>;
	inline static const std::array<AttachmentInfo, 3> kRenderAttachments = {
		AttachmentInfo{ "renderTarget", vk::Format::eR16G16B16A16Sfloat, vk::ClearValue{vk::ClearColorValue(0.f,0.f,0.f,1.f)} },
		AttachmentInfo{ "visibility",   vk::Format::eR32G32B32A32Uint,   vk::ClearValue{vk::ClearColorValue(~0u,~0u,~0u,~0u)} },
		AttachmentInfo{ "depthBuffer",  vk::Format::eD32Sfloat,          vk::ClearValue{vk::ClearDepthStencilValue{1.f, 0}} },
	};

private:
	TupleMap<ref<Pipeline>, MeshLayout, MaterialFlags, bool> cachedPipelines = {};
	ref<vk::raii::Sampler> cachedSampler = nullptr;
	ref<const ShaderModule> vertexShader, vertexShaderTextured, fragmentShader, fragmentShaderTextured, fragmentShaderTexturedAlphaCutoff;

	std::vector<ImageView> attachments;
	ref<DescriptorSets> descriptorSets = {};
	struct ViewportParams {
		Transform cameraToWorld;
		Transform worldToCamera;
		Transform projection;
	};
	ViewportParams viewData;

	ref<Scene> scene = nullptr;

	PipelineCache pathTracer = PipelineCache(FindShaderPath("PathTracer/PathTracer.cs.slang"), "main", PipelineLayoutInfo{
		.descriptorBindingFlags = {
			{ "scene.meshBuffers", vk::DescriptorBindingFlagBits::ePartiallyBound },
			{ "scene.images",      vk::DescriptorBindingFlagBits::ePartiallyBound } },
		.immutableSamplers = { { "scene.sampler", { vk::SamplerCreateInfo{
			.magFilter  = vk::Filter::eLinear,
			.minFilter  = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.minLod = 0,
			.maxLod = 12 } } } } });
	uint32_t maxBounces = 10;
	uint32_t maxDiffuseBounces = 3;

	PipelineCache accumulation = PipelineCache(FindShaderPath("Accumulation.cs.slang"));
	ImageView prevRenderTarget;
	Transform prevCameraToWorld;
	std::chrono::high_resolution_clock::time_point prevSceneVersion;
	bool enableAccumulation = true;
	bool useFixedSeed = false;
	uint32_t fixedSeed = 1u;

	Tonemapper tonemapper;

	inline const auto& GetPipeline(Device& device, const Mesh& mesh, const Material<ImageView>& material) {
		if (!vertexShader || (ImGui::IsKeyPressed(ImGuiKey_F5, false) && vertexShader->IsStale())) {
			if (vertexShader) device.Wait();
			vertexShader                      = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "vertexMain");
			vertexShaderTextured              = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "vertexMain", "sm_6_7", ShaderDefines{ { "HAS_TEXCOORD", "1" } });
			fragmentShader                    = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "fragmentMain");
			fragmentShaderTextured            = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "fragmentMain", "sm_6_7", ShaderDefines{ { "HAS_TEXCOORD", "1" } });
			fragmentShaderTexturedAlphaCutoff = ShaderModule::Create(device, FindShaderPath("Visibility.3d.slang"), "fragmentMain", "sm_6_7", ShaderDefines{ { "HAS_TEXCOORD", "1" }, { "USE_ALPHA_CUTOFF", "1" } });
		}

		bool textured = mesh.vertexAttributes.contains(MeshVertexAttributeType::eTexcoord) && mesh.vertexAttributes.at(MeshVertexAttributeType::eTexcoord).size() > 0;
		auto vs = textured ? vertexShaderTextured : vertexShader;
		auto fs = textured ? (material.HasFlag(MaterialFlags::eAlphaCutoff) ? fragmentShaderTexturedAlphaCutoff : fragmentShaderTextured) : fragmentShader;
		const bool alphaBlend = material.HasFlag(MaterialFlags::eAlphaBlend);

		auto key = std::tuple{ mesh.GetLayout(*vs), (MaterialFlags)material.GetFlags(), textured };

		if (auto it = cachedPipelines.find(key); it != cachedPipelines.end()) {
			const auto& pipeline = it->second;
			if (pipeline->GetShader(vk::ShaderStageFlagBits::eVertex) != vs || pipeline->GetShader(vk::ShaderStageFlagBits::eFragment) != fs)
				cachedPipelines.erase(it);
			else
				return *it;
		}

		if (!cachedSampler) {
			cachedSampler = make_ref<vk::raii::Sampler>(*device, vk::SamplerCreateInfo{
				.magFilter  = vk::Filter::eLinear,
				.minFilter  = vk::Filter::eLinear,
				.mipmapMode = vk::SamplerMipmapMode::eLinear,
				.minLod = 0,
				.maxLod = 12 });
		}

		// get vertex buffer bindings from the mesh layout

		DynamicRenderingState renderState;
		for (const auto&[name, format, clearValue] : kRenderAttachments) {
			if (IsDepthStencil(format))
				renderState.depthFormat = format;
			else
				renderState.colorFormats.emplace_back(format);
		}

		GraphicsPipelineInfo pipelineInfo {
			.vertexInputState = VertexInputDescription{
				.bindings   = std::get<0>(key).bindings,
				.attributes = std::get<0>(key).attributes },
			.inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
				.topology = std::get<0>(key).topology },
			.rasterizationState = vk::PipelineRasterizationStateCreateInfo{
				.depthClampEnable = false,
				.rasterizerDiscardEnable = false,
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = material.HasFlag(MaterialFlags::eDoubleSided) ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.depthBiasEnable = false },
			.multisampleState = vk::PipelineMultisampleStateCreateInfo{},
			.depthStencilState = vk::PipelineDepthStencilStateCreateInfo{
				.depthTestEnable = true,
				.depthWriteEnable = true,
				.depthCompareOp = vk::CompareOp::eLess,
				.depthBoundsTestEnable = false,
				.stencilTestEnable = false },
			.viewports = { vk::Viewport{} },
			.scissors = { vk::Rect2D{} },
			.colorBlendState = ColorBlendState{
				.attachments = std::vector<vk::PipelineColorBlendAttachmentState>(2, vk::PipelineColorBlendAttachmentState {
						.blendEnable         = alphaBlend,
						.srcColorBlendFactor = alphaBlend ? vk::BlendFactor::eOneMinusDstAlpha : vk::BlendFactor::eZero,
						.dstColorBlendFactor = alphaBlend ? vk::BlendFactor::eDstAlpha         : vk::BlendFactor::eOne,
						.colorBlendOp        = alphaBlend ? vk::BlendOp::eAdd                  : vk::BlendOp::eAdd,
						.srcAlphaBlendFactor = alphaBlend ? vk::BlendFactor::eOne              : vk::BlendFactor::eZero,
						.dstAlphaBlendFactor = alphaBlend ? vk::BlendFactor::eZero             : vk::BlendFactor::eOne,
						.alphaBlendOp        = alphaBlend ? vk::BlendOp::eAdd                  : vk::BlendOp::eAdd,
						.colorWriteMask      = vk::ColorComponentFlags{vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags} }) },
			.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor },
			.dynamicRenderingState = renderState
		};
		PipelineLayoutInfo layoutInfo {
			.descriptorBindingFlags = {
				{ "scene.meshBuffers", vk::DescriptorBindingFlagBits::ePartiallyBound },
				{ "scene.images",      vk::DescriptorBindingFlagBits::ePartiallyBound } },
			.immutableSamplers      = { { "scene.sampler", { cachedSampler } } } };
		auto pipeline = Pipeline::CreateGraphics(device, { vs, fs }, pipelineInfo, layoutInfo);
		return *cachedPipelines.emplace(key, pipeline).first;
	}

public:
	inline void SetScene(const ref<Scene>& s) { scene = s; }

	inline void DrawGui(CommandContext& context) {
		if (ImGui::CollapsingHeader("Path tracer")) {
			Gui::ScalarField("Max bounces", vk::Format::eR32Uint, &maxBounces);
			Gui::ScalarField("Max diffuse bounces", vk::Format::eR32Uint, &maxDiffuseBounces);

			ImGui::Checkbox("Accumulation", &enableAccumulation);
			ImGui::Checkbox("Fix seed", &useFixedSeed);
			if (useFixedSeed) {
				ImGui::SameLine();
				Gui::ScalarField("", vk::Format::eR32Uint, &fixedSeed);
			}
		}
		if (ImGui::CollapsingHeader("Tonemapper")) {
			tonemapper.DrawGui(context);
		}
	}

	inline const ImageView& GetAttachment(const uint32_t index) const {
		return attachments[index];
	}

	inline void PreRender(CommandContext& context, const uint2 extent, const Transform& cameraToWorld, const Transform& projection) {
		if (attachments.empty() || (uint2)attachments[0].Extent() != extent) {
			context.GetDevice().Wait();
			attachments.clear();
			// create attachments
			for (const auto&[name, format, clearValue] : kRenderAttachments) {
				ImageView attachment;
				if (IsDepthStencil(format)) {
					attachment = ImageView::Create(
						Image::Create(context.GetDevice(), ImageInfo{
							.format = format,
							.extent = uint3(extent, 1),
							.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eDepthStencilAttachment,
							.queueFamilies = { context.QueueFamily() } }),
						vk::ImageSubresourceRange{
							.aspectMask = vk::ImageAspectFlagBits::eDepth,
							.baseMipLevel = 0,
							.levelCount = 1,
							.baseArrayLayer = 0,
							.layerCount = 1 });
				} else {
					attachment = ImageView::Create(
						Image::Create(context.GetDevice(), ImageInfo{
							.format = format,
							.extent = uint3(extent, 1),
							.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment,
							.queueFamilies = { context.QueueFamily() } }));
				}
				attachments.emplace_back(attachment);
			}

			prevRenderTarget = ImageView::Create(
				Image::Create(context.GetDevice(), ImageInfo{
					.format = attachments[0].GetImage()->Info().format,
					.extent = uint3(extent, 1),
					.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
					.queueFamilies = { context.QueueFamily() } }));
		}

		viewData.cameraToWorld = cameraToWorld;
		viewData.worldToCamera = inverse(cameraToWorld);
		viewData.projection = projection;

		if (scene && scene->sceneRoot) {
			scene->PreRender(context, [&](Device& device, const Mesh& mesh, const Material<ImageView>& material) { return GetPipeline(device, mesh, material); });

			ShaderParameter params = {};
			params["scene"]         = scene->renderData.sceneParameters;
			params["worldToCamera"] = viewData.worldToCamera;
			params["projection"]    = viewData.projection;

			// all pipelines should have the same descriptor set layouts
			descriptorSets = context.GetDescriptorSets(*cachedPipelines.begin()->second->Layout());
			context.UpdateDescriptorSets(*descriptorSets, params, *cachedPipelines.begin()->second->Layout());
		} else {
			descriptorSets = {};
		}
	}

	inline void Render(CommandContext& context) {
		context.BeginRendering({
			{ attachments[0], std::get<vk::ClearValue>(kRenderAttachments[0]) },
			{ attachments[1], std::get<vk::ClearValue>(kRenderAttachments[1]) },
			{ attachments[2], std::get<vk::ClearValue>(kRenderAttachments[2]) },
		});

		if (descriptorSets) {
			const Pipeline* p = nullptr;
			for (const auto& drawList : scene->renderData.drawLists) {
				for (const auto&[pipeline, mesh, meshLayout, draws] : drawList) {
					if (p != pipeline) {
						context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***pipeline);
						context.BindDescriptors(*pipeline->Layout(), *descriptorSets);
						p = pipeline;
					}

					mesh->Bind(context, meshLayout);

					const uint32_t indexCount = mesh->indexBuffer.size_bytes() / mesh->indexSize;
					for (const auto&[firstInstance, instanceCount] : draws) {
						context->drawIndexed(indexCount, instanceCount, 0, 0, firstInstance);
					}
				}
			}
		}

		context.EndRendering();
	}

	inline void PostRender(CommandContext& context) {
		if (!scene || !scene->sceneRoot) return;

		const ImageView& renderTarget = attachments[0];
		const ImageView& visibility   = attachments[1];

		{
			ShaderParameter params = {};
			params["scene"] = scene->renderData.sceneParameters;
			params["renderTarget"] = ImageParameter{ .image = renderTarget, .imageLayout = vk::ImageLayout::eGeneral };
			params["visibility"]   = ImageParameter{ .image = visibility, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
			params["worldToCamera"] = viewData.worldToCamera;
			params["cameraToWorld"] = viewData.cameraToWorld;
			params["projection"]    = viewData.projection;
			params["inverseProjection"] = inverse(viewData.projection);
			params["imageSize"] = uint2(renderTarget.Extent());
			params["seed"] = useFixedSeed ? fixedSeed : (uint32_t)context.GetDevice().NextTimelineSignal();
			params["maxBounces"] = maxBounces;
			params["maxDiffuseBounces"] = maxDiffuseBounces;
			pathTracer(context, renderTarget.Extent(), params);
		}

		if (enableAccumulation && prevCameraToWorld.transform == viewData.cameraToWorld.transform && prevSceneVersion >= scene->renderData.updateTime)
		{
			ShaderParameter params = {};
			params["renderTarget"]     = ImageParameter{ .image = renderTarget,     .imageLayout = vk::ImageLayout::eGeneral };
			params["prevRenderTarget"] = ImageParameter{ .image = prevRenderTarget, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
			accumulation(context, renderTarget.Extent(), params);
		}
		context.Copy(renderTarget, prevRenderTarget);
		prevCameraToWorld = viewData.cameraToWorld;
		prevSceneVersion = scene->renderData.updateTime;

		tonemapper.Render(context, renderTarget);
	}
};

}