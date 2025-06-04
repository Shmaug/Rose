#pragma once

#include <queue>

#include <Rose/Scene/Scene.hpp>

namespace RoseEngine {

class SceneEditor {
private:
	ref<Scene> scene = nullptr;
	weak_ref<SceneNode> selected = {};

	float magnifierZoom = 5.f;
	int2 magnifierCenter;

	ref<Pipeline> outlinePipeline = {};

	uint32_t operation = ImGuizmo::TRANSLATE | ImGuizmo::ROTATE;
	bool opLocal = false;
	bool opOriginWorld = false;

	struct ViewportPickerData {
		BufferRange<uint4> visibility = {};
		uint64_t timelineCounterValue = 0;
		std::vector<weak_ref<SceneNode>> nodes = {};
	};
	std::queue<ViewportPickerData> viewportPickerQueue = {};

	inline void SceneNodeTreeGui(SceneNode* n, const SceneNode* selected_ptr = nullptr) {
		bool open = true;

		ImGui::PushID(n);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_OpenOnDoubleClick;
		if (selected_ptr == n)      flags |= ImGuiTreeNodeFlags_Selected;
		if (std::ranges::empty(*n)) flags |= ImGuiTreeNodeFlags_Leaf;
		open = ImGui::TreeNodeEx(n->Name() == "" ? "<unnamed>" : n->Name().c_str(), flags);

		if (ImGui::IsItemClicked())
			selected = n->shared_from_this();

		if (ImGui::BeginPopupContextWindow()) {
			bool deleted = false;
			if (ImGui::Selectable("Delete")) {
				if (n->GetParent()) {
					n->GetParent()->RemoveChild(n);
					deleted = true;
					scene->SetDirty();
				}
			}
			ImGui::EndPopup();
			if (deleted) return;
		}

		if (open) {
			for (const ref<SceneNode>& c : *n)
				SceneNodeTreeGui(c.get(), selected_ptr);

			ImGui::TreePop();
		}

		ImGui::PopID();
	}

public:
	inline void SetScene(const ref<Scene>& s) { scene = s; }

	inline void SceneGraphWidget() {
		if (scene && scene->sceneRoot) {
			SceneNode* sel_p = selected.lock().get();
			for (const ref<SceneNode>& c : *scene->sceneRoot)
				SceneNodeTreeGui(c.get(), sel_p);
		}
	}

	inline void ToolsWidget() {
		const float w = ImGui::GetWindowContentRegionWidth();

		if (ImGui::IsKeyPressed(ImGuiKey_O, false) && !ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) opOriginWorld = !opOriginWorld;
		if (ImGui::Selectable("Object", opOriginWorld, 0, ImVec2(w/2,0))) opOriginWorld = true;
		ImGui::SameLine();
		if (ImGui::Selectable("AABB", !opOriginWorld, 0, ImVec2(w/2,0))) opOriginWorld = false;

		if (ImGui::IsKeyPressed(ImGuiKey_L, false)) opLocal = !opLocal;
		if (ImGui::Selectable("Local", opLocal, 0, ImVec2(w/2,0))) opLocal = true;
		ImGui::SameLine();
		if (ImGui::Selectable("Global", !opLocal, 0, ImVec2(w/2,0))) opLocal = false;

		if (ImGui::Selectable("Translate", (operation & ImGuizmo::TRANSLATE) != 0) || ImGui::IsKeyPressed(ImGuiKey_T, false)) operation ^= (uint32_t)ImGuizmo::TRANSLATE;
		if (ImGui::Selectable("Rotate",    (operation & ImGuizmo::ROTATE) != 0)    || ImGui::IsKeyPressed(ImGuiKey_R, false)) operation ^= (uint32_t)ImGuizmo::ROTATE;
		if (ImGui::Selectable("Scale",     (operation & ImGuizmo::SCALE) != 0)     || ImGui::IsKeyPressed(ImGuiKey_G, false)) operation ^= (uint32_t)ImGuizmo::SCALE;
	}

	inline void InspectorWidget(CommandContext& context) {
		bool changed = false;

		changed |= ImGui::ColorEdit3("Background color", &scene->backgroundColor.x, ImGuiColorEditFlags_Float|ImGuiColorEditFlags_HDR);

		auto n = selected.lock();
		if (!n) return;

		if (ImGui::CollapsingHeader("Selected node")) {
			ImGui::Text("Transform: %s", n->transform ? "true" : "false");
			if (n->transform.has_value()) {
				if (InspectorGui(n->transform.value()))
					changed = true;
			} else {
				Transform t = Transform::Identity();
				if (InspectorGui(t)) {
					n->transform = t;
					changed = true;
				}
			}
			if (n->material) {
				if (InspectorGui(*n->material))
					changed = true;
			}
		}

		if (changed)
			scene->SetDirty();
	}

	inline void PreRender(CommandContext& context, const Transform& worldToCamera, const Transform& projection) {
		// update selected node based on vbuffer pixel that was clicked on
		if (!viewportPickerQueue.empty()) {
			auto&[buf, value, nodes] = viewportPickerQueue.front();
			if (context.GetDevice().CurrentTimelineValue() >= value) {
				if (buf[0].x < nodes.size())
					selected = std::move(nodes[buf[0].x]);
				else
					selected.reset();
				viewportPickerQueue.pop();
			}
		}

		// draw gizmos for selected node
		if (auto n = selected.lock(); n) {
			Transform parentTransform = Transform::Identity();
			SceneNode* parent = n->GetParent().get();
			while (parent) {
				if (parent->transform.has_value())
					parentTransform = parent->transform.value() * parentTransform;
				parent = parent->GetParent().get();
			}

			if (!opOriginWorld && n->mesh) {
				const float3 aabbMin = float3(n->mesh->aabb.minX, n->mesh->aabb.minY, n->mesh->aabb.minZ);
				const float3 aabbMax = float3(n->mesh->aabb.maxX, n->mesh->aabb.maxY, n->mesh->aabb.maxZ);
				parentTransform = parentTransform * Transform::Translate((aabbMin + aabbMax) / 2.f);
			}

			Transform t = n->transform.has_value() ? (parentTransform * n->transform.value()) : parentTransform;

			if (ImGuizmo::Manipulate(
				&worldToCamera.transform[0][0],
				&projection.transform[0][0],
				(ImGuizmo::OPERATION)operation,
				opLocal ? ImGuizmo::MODE::LOCAL : ImGuizmo::MODE::WORLD,
				&t.transform[0][0],
				NULL,
				NULL)) {
				n->transform = inverse(parentTransform) * t;
				scene->SetDirty();
			}
		}
	}

	inline void PostRender(CommandContext& context, const ImageView& renderTarget, const ImageView& vbuffer) {
		const auto& instanceNodes = scene->renderData.instanceNodes;

		if (auto n = selected.lock(); n && n->mesh && n->material) {
			uint32_t idx = -1;
			for (uint32_t i = 0; i < instanceNodes.size(); i++) {
				if (instanceNodes[i].lock() == n) {
					idx = i;
					break;
				}
			}
			if (idx != -1) {
				// outline selected object
				{
					if (!outlinePipeline || (ImGui::IsKeyDown(ImGuiKey_F5) && outlinePipeline->GetShader()->IsStale())) {
						if (outlinePipeline) context.GetDevice().Wait();
						outlinePipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), FindShaderPath("Outline.cs.slang")));
					}

					ShaderParameter params = {};
					params["color"]      = ImageParameter{renderTarget, vk::ImageLayout::eGeneral};
					params["visibility"] = ImageParameter{vbuffer, vk::ImageLayout::eShaderReadOnlyOptimal};
					params["highlightColor"] = float3(1, 0.9f, 0.2f);
					params["selected"] = idx;
					context.Dispatch(*outlinePipeline, renderTarget.Extent(), params);
				}
			}
		}

		float4 rect;
		ImGuizmo::GetRect(&rect.x);
		float2 cursorScreen = std::bit_cast<float2>(ImGui::GetIO().MousePos);
		int2 cursor = int2(cursorScreen - float2(rect));

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowFocused() && ImGui::IsWindowHovered() && !ImGuizmo::IsUsing()) {
			// copy selected pixel from visibility buffer into host-visible buffer
			if (cursor.x >= 0 && cursor.y >= 0 && cursor.x < int(rect.z) && cursor.y < int(rect.w)) {
				context.AddBarrier(vbuffer, Image::ResourceState{
					.layout = vk::ImageLayout::eTransferSrcOptimal,
					.stage  = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferRead,
					.queueFamily = context.QueueFamily() });
				context.ExecuteBarriers();

				BufferRange<uint4> buf = Buffer::Create(
					context.GetDevice(),
					sizeof(uint4),
					vk::BufferUsageFlagBits::eTransferDst,
					vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

				context->copyImageToBuffer(**vbuffer.GetImage(), vk::ImageLayout::eTransferSrcOptimal, **buf.mBuffer, vk::BufferImageCopy{
					.bufferOffset = 0,
					.bufferRowLength = 0,
					.bufferImageHeight = 0,
					.imageSubresource = vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
					.imageOffset = vk::Offset3D{ cursor.x, cursor.y, 0 },
					.imageExtent = vk::Extent3D{ 1, 1, 1 } });

				viewportPickerQueue.push({ buf, context.GetDevice().NextTimelineSignal(), instanceNodes });
			}
		}

		if (ImGui::IsKeyDown(ImGuiKey_Z)) {
			const int2 imageSize = int2(renderTarget.Extent());
			if (all(glm::greaterThanEqual(cursor, int2(0))) && all(glm::lessThan(cursor, imageSize)))
				magnifierCenter = cursor;

			if (ImGui::GetIO().MouseWheel != 0) {
				magnifierZoom *= (1 + ImGui::GetIO().MouseWheel / 8);
				magnifierZoom = max(magnifierZoom, 1.f);
			}
			const int32_t dstRadius = 100;
			const int32_t srcRadius = max(dstRadius/magnifierZoom, 1.f);

			// const float2 bb0 = cursorScreen - dstRadius;
			// const float2 bb1 = cursorScreen + dstRadius;
			// const float2 uv0 = (float2(cursor) - srcRadius) / imageSize;
			// const float2 uv1 = (float2(cursor) + srcRadius) / imageSize;
			// ImGui::GetWindowDrawList()->AddImage(
			// 	Gui::GetTextureID(renderTarget, vk::Filter::eNearest),
			// 	std::bit_cast<ImVec2>(bb0),
			// 	std::bit_cast<ImVec2>(bb1),
			// 	std::bit_cast<ImVec2>(uv0),
			// 	std::bit_cast<ImVec2>(uv1),
			// 	ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));

			const int2 srcMin = max(int2(0),   magnifierCenter - srcRadius);
			const int2 srcMax = min(imageSize, magnifierCenter + srcRadius);
			const int2 dstMin = max(int2(0),   magnifierCenter - dstRadius);
			const int2 dstMax = min(imageSize, magnifierCenter + dstRadius);
			if (all(glm::greaterThan(srcMax, srcMin)) && all(glm::greaterThan(dstMax, dstMin))) {
				context.Blit(
					renderTarget.GetImage(),
					renderTarget.GetImage(),
					{ vk::ImageBlit{
						.srcSubresource = renderTarget.GetSubresourceLayer(),
						.srcOffsets = std::array<vk::Offset3D, 2>{
							vk::Offset3D{ srcMin.x, srcMin.y, 0 },
							vk::Offset3D{ srcMax.x, srcMax.y, 0 } },
						.dstSubresource = renderTarget.GetSubresourceLayer(),
						.dstOffsets = std::array<vk::Offset3D, 2>{
							vk::Offset3D{ dstMin.x, dstMin.y, 0 },
							vk::Offset3D{ dstMax.x, dstMax.y, 0 } }
					} },
					vk::Filter::eNearest);
			}
		}
	}
};

}