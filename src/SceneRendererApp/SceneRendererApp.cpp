#include <Rose/Core/WindowedApp.hpp>
#include <Rose/Scene/ViewportCamera.hpp>
#include "SceneRenderer.hpp"
#include "SceneEditor/SceneEditor.hpp"

#include <ImGuizmo.h>

using namespace RoseEngine;

int main(int argc, const char** argv) {
	WindowedApp app("GLTF Viewer", {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_RAY_QUERY_EXTENSION_NAME,
		VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME,
	});

	auto sceneRenderer = make_ref<SceneRenderer>();
	auto sceneEditor   = make_ref<SceneEditor>();

	ref<Scene> scene = make_ref<Scene>();
	sceneRenderer->SetScene(scene);
	sceneEditor->SetScene(scene);

	ViewportCamera camera = {};

	app.AddMenuItem("File", [&]() {
		if (ImGui::MenuItem("Open scene") || (ImGui::IsKeyDown(ImGuiKey_ModCtrl) && ImGui::IsKeyPressed(ImGuiKey_O), false)) {
			scene->LoadDialog(app.CurrentContext());
		}
	});
	app.AddWidget("Scene", [&]() {
		sceneEditor->InspectorWidget(app.CurrentContext());
	}, true);

	app.AddWidget("Renderer", [&]() {
		sceneRenderer->DrawGui(app.CurrentContext());
	}, true);

	app.AddWidget("Viewport", [&]() {
		camera.Update(app.dt);

		const float2 extentf = std::bit_cast<float2>(ImGui::GetWindowContentRegionMax()) - std::bit_cast<float2>(ImGui::GetWindowContentRegionMin());
		const uint2 extent = extentf;
		if (extent.x == 0 || extent.y == 0) return;

		CommandContext& context = app.CurrentContext();

		const Transform cameraToWorld = camera.GetCameraToWorld();
		const float aspect = extent.x / (float)extent.y;

		sceneRenderer->PreRender(context, extent, cameraToWorld, camera.GetProjection(aspect));
		sceneEditor->PreRender(context, inverse(cameraToWorld), camera.GetProjection(aspect, camera.nearZ*100000, false));


		const ImageView& renderTarget = sceneRenderer->GetAttachment(0);
		const ImageView& visibility   = sceneRenderer->GetAttachment(1);

		ImGui::Image(Gui::GetTextureID(renderTarget, vk::Filter::eNearest), std::bit_cast<ImVec2>(extentf));

		const float2 viewportMin = std::bit_cast<float2>(ImGui::GetItemRectMin());
		const float2 viewportMax = std::bit_cast<float2>(ImGui::GetItemRectMax());
		const float4 viewportRect = float4(viewportMin, viewportMax - viewportMin);

		// ImGuizmo viewport
		ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y);
		ImGuizmo::SetID(0);

		sceneRenderer->Render(context);

		sceneRenderer->PostRender(context);
		sceneEditor->PostRender(context, renderTarget, visibility);
	}, true, WindowedApp::WidgetFlagBits::eNoBorders);

	app.AddWidget("Scene graph", [&]() { sceneEditor->SceneGraphWidget(); }, true);
	app.AddWidget("Tools", [&]()       { sceneEditor->ToolsWidget(); }, true);

	app.Run();

	app.device->Wait();

	return EXIT_SUCCESS;
}