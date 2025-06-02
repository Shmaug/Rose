#pragma once

#include <Rose/Core/Gui.hpp>
#include <Rose/Scene/Transform.hpp>

namespace RoseEngine {

struct ViewportCamera {
	float3 position = float3(0, 2, 2);
	float2 angles = float2(-float(M_PI)/4,0);
	float  fovY  = 50.f; // in degrees
	float  nearZ = 0.01f;

	float moveSpeed = 1.f;

	inline quat GetRotation() const {
		quat rx = glm::angleAxis(angles.x, float3(1,0,0));
		quat ry = glm::angleAxis(angles.y, float3(0,1,0));
		return ry * rx;
	}

	inline Transform GetCameraToWorld() const {
		return Transform::Translate(position) * Transform::Rotate(GetRotation());
	}

	// aspect = width / height
	inline Transform GetProjection(float aspect, bool vflip = true) const {
		Transform p = Transform::Perspective(glm::radians(fovY), aspect, nearZ);
		if (vflip) p.transform[1] = -p.transform[1];
		return p;
	}

	// aspect = width / height
	inline Transform GetProjection(float aspect, float farZ, bool vflip = true) const {
		Transform p = Transform::Perspective(glm::radians(fovY), aspect, nearZ, farZ);
		if (vflip) p.transform[1] = -p.transform[1];
		return p;
	}

	inline void DrawInspectorGui() {
		ImGui::PushID("Camera");
		ImGui::DragFloat3("Position", &position.x);
		ImGui::DragFloat2("Angle", &angles.x);
		Gui::ScalarField("Vertical field of view", &fovY);
		Gui::ScalarField("Near Z", &nearZ);
		ImGui::PopID();
	}

	inline void Update(double dt) {
		float3 move = float3(0.f);

		if (ImGui::IsWindowHovered()) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				angles += -float2(ImGui::GetIO().MouseDelta.y, ImGui::GetIO().MouseDelta.x) * float(M_PI) / 1920.f;
				angles.x = clamp(angles.x, -float(M_PI/2), float(M_PI/2));
			}

			if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
				move += float3(-ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y, 0) / 10.f;
			}
		}

		if (ImGui::IsWindowFocused()) {
			if (ImGui::GetIO().MouseWheel != 0) {
				moveSpeed *= (1 + ImGui::GetIO().MouseWheel / 8);
				moveSpeed = std::max(moveSpeed, .05f);
			}

			float3 m = float3(0,0,0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_W)) m += float3( 0, 0,-1);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_S)) m += float3( 0, 0, 1);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_D)) m += float3( 1, 0, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_A)) m += float3(-1, 0, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_Q)) m += float3( 0,-1, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_E)) m += float3( 0, 1, 0);
			if (m != float3(0,0,0)) move += normalize(m);
		}
		if (move != float3(0,0,0)) {
			if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
				move *= 3.f;
			position += GetRotation() * move * moveSpeed * float(dt);
		}
	}
};

}