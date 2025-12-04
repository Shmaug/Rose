#pragma once

#include <Rose/Core/Gui.hpp>
#include <Rose/Scene/Transform.hpp>

namespace RoseEngine {

struct ViewportCamera {
    // --- Enums for Mode Switching ---
    enum class CameraMode { Euler, Unlocked };
    enum class ProjectionMode { FovY, FovXY }; // NEW

    // --- Core Properties ---
    float3 position = float3(0, 2, 2);
    CameraMode mode = CameraMode::Euler;
    ProjectionMode projectionMode = ProjectionMode::FovY; // NEW

    union {
        float2 eulerAngles;
        quat rotation;
    };

    // --- Projection Properties ---
    float fovY  = 50.f; // Vertical FOV in degrees
    float fovX  = 70.f; // NEW: Horizontal FOV in degrees
    float nearZ = 0.01f;

    float moveSpeed = 1.f;

    // --- Constructors ---

    /// @brief Default constructor for a simple Euler camera.
    ViewportCamera() {
        eulerAngles = float2(-float(M_PI) / 4.0f, 0.0f);
    }

    /// @brief NEW: Initializer for a "full" unlocked camera with independent FOV.
    ViewportCamera(float3 pos, quat rot, float fov_x_deg, float fov_y_deg, float near_z = 0.01f) {
        mode = CameraMode::Unlocked;
        projectionMode = ProjectionMode::FovXY;

        position = pos;
        rotation = rot; // Directly set the quaternion
        fovX = fov_x_deg;
        fovY = fov_y_deg;
        nearZ = near_z;
    }

    // --- Core Functions ---

    inline quat GetRotation() const {
        switch (mode) {
            case CameraMode::Euler: {
                quat rx = glm::angleAxis(eulerAngles.x, float3(1, 0, 0));
                quat ry = glm::angleAxis(eulerAngles.y, float3(0, 1, 0));
                return ry * rx;
            }
            case CameraMode::Unlocked:
                return rotation;
        }
        return quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    inline Transform GetCameraToWorld() const {
        return Transform::Translate(position) * Transform::Rotate(GetRotation());
    }

    // UPDATED: GetProjection now uses the projectionMode to decide which math to use.
    inline Transform GetProjection(float aspect) const {
        Transform p;
        switch (projectionMode) {
            case ProjectionMode::FovY:
                p = Transform::Perspective(glm::radians(fovY), aspect, nearZ);
                break;
            case ProjectionMode::FovXY:
                p = Transform::PerspectiveFovXY(glm::radians(fovX), glm::radians(fovY), nearZ);
                break;
        }
        // Invert Y for Vulkan/DX conventions
        p.transform[1] = -p.transform[1];
        return p;
    }

    // UPDATED: GUI now includes controls for projection mode.
    inline void DrawInspectorGui() {
        ImGui::PushID("Camera");
        ImGui::DragFloat3("Position", &position.x);

        // --- Rotation Mode ---
        ImGui::Separator();
        if (ImGui::RadioButton("Euler", mode == CameraMode::Euler)) {
            if (mode == CameraMode::Unlocked) {
                float3 euler = glm::eulerAngles(rotation);
                eulerAngles = float2(euler.x, euler.y);
            }
            mode = CameraMode::Euler;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Unlocked", mode == CameraMode::Unlocked)) {
            if (mode == CameraMode::Euler) {
                rotation = GetRotation();
            }
            mode = CameraMode::Unlocked;
        }
        switch (mode) {
            case CameraMode::Euler: ImGui::DragFloat2("Angles", &eulerAngles.x, 0.01f); break;
            case CameraMode::Unlocked: if (ImGui::InputFloat4("Quaternion", &rotation.x)) { rotation = glm::normalize(rotation); } break;
        }

        // --- Projection Mode ---
        ImGui::Separator();
        if (ImGui::RadioButton("FovY + Aspect", projectionMode == ProjectionMode::FovY)) {
            projectionMode = ProjectionMode::FovY;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("FovX + FovY", projectionMode == ProjectionMode::FovXY)) {
            projectionMode = ProjectionMode::FovXY;
        }
        switch (projectionMode) {
            case ProjectionMode::FovY:
                Gui::ScalarField("Vertical FOV", &fovY);
                break;
            case ProjectionMode::FovXY:
                Gui::ScalarField("Horizontal FOV", &fovX);
                Gui::ScalarField("Vertical FOV", &fovY);
                break;
        }
        Gui::ScalarField("Near Z", &nearZ);
        ImGui::PopID();
    }

	inline void Update(double dt) {
		if (ImGui::IsWindowHovered()) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				const float2 mouseDelta = -float2(ImGui::GetIO().MouseDelta.y, ImGui::GetIO().MouseDelta.x);
				const float sensitivity = float(M_PI) / 1920.f;
				// eulerAngles += -float2(ImGui::GetIO().MouseDelta.y, ImGui::GetIO().MouseDelta.x) * float(M_PI) / 1920.f;
				// eulerAngles.x = clamp(eulerAngles.x, -float(M_PI/2), float(M_PI/2));
				switch (mode) {
					case CameraMode::Euler:
						eulerAngles += mouseDelta * sensitivity;
						eulerAngles.x = glm::clamp(eulerAngles.x, -float(M_PI / 2.0f), float(M_PI / 2.0f));
						break;
					case CameraMode::Unlocked: {
						// Yaw is applied in world space around the global up-axis (0,1,0).
						quat yawDelta = glm::angleAxis(mouseDelta.y * sensitivity, float3(0, 1, 0));

						// Pitch is applied in local space around the camera's right-axis (1,0,0).
						quat pitchDelta = glm::angleAxis(mouseDelta.x * sensitivity, float3(1, 0, 0));

						// Combine the rotations: world-space yaw, then local-space pitch.
						rotation = yawDelta * rotation * pitchDelta;
						rotation = glm::normalize(rotation);
						break;
					}

				}
			}
		}

		if (ImGui::IsWindowFocused()) {
			// if (ImGui::GetIO().MouseWheel != 0) {
			// 	moveSpeed *= (1 + ImGui::GetIO().MouseWheel / 8);
			// 	moveSpeed = std::max(moveSpeed, .05f);
			// }

			float3 move = float3(0,0,0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_W)) move += float3( 0, 0,-1);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_S)) move += float3( 0, 0, 1);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_D)) move += float3( 1, 0, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_A)) move += float3(-1, 0, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_Q)) move += float3( 0,-1, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_E)) move += float3( 0, 1, 0);
			if (move != float3(0,0,0)) {
				move = GetRotation() * normalize(move);
				if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
					move *= 3.f;
				position += move * moveSpeed * float(dt);
			}
		}
	}

};

}
