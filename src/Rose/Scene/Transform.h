#pragma once

#include <Rose/Core/RoseEngine.h>

namespace RoseEngine {

#ifdef __cplusplus
inline auto mul(auto x, auto y) { return x * y; }
#endif

struct Transform {
	float4x4 transform;

	inline static Transform Identity() {
		Transform t = {};
		t.transform = float4x4(
			1,0,0,0,
			0,1,0,0,
			0,0,1,0,
			0,0,0,1 );
		return t;
	}

	#ifdef __cplusplus
	inline static Transform Translate(const float3 v) {
		Transform t = {};
		t.transform = glm::translate(v);
		return t;
	}
	inline static Transform Scale(const float3 v) {
		Transform t = {};
		t.transform = glm::scale(v);
		return t;
	}
	inline static Transform Rotate(const quat v) {
		Transform t = {};
		t.transform = float4x4(v);
		return t;
	}
	inline static Transform Perspective(const float fovY, const float aspect, const float nearZ) {
		Transform t = {};
		t.transform = glm::tweakedInfinitePerspective(fovY, aspect, nearZ);
		return t;
	}
	inline static Transform PerspectiveFovXY(const float fovX, const float fovY, const float nearZ) {
		Transform t = {};
		t.transform = glm::mat4(0.0f); // Initialize to all zeros

		// The projection matrix elements are based on the cotangent of half the FOV.
		// cot(a) = 1.0f / tan(a)
		const float f = 1.0f / tanf(fovY / 2.0f);
		const float g = 1.0f / tanf(fovX / 2.0f);

		// Manually construct the infinite projection matrix
		t.transform[0][0] = g;
		t.transform[1][1] = f;
		t.transform[2][2] = -1.0f; // Use -1 for a right-handed coordinate system
		t.transform[3][2] = -nearZ; // For tweaked/reversed Z, this might be nearZ
		t.transform[2][3] = -1.0f;

		// Based on your original code, you may also need to flip the Y-axis for
		// graphics APIs like Vulkan. If so, just uncomment the following line.
		// t.transform[1] = -t.transform[1];

		return t;
	}
	inline operator const float4x4&() const { return transform; }
	#endif

	inline float4 ProjectPointUnnormalized(const float4 v) CPP_CONST {
		return mul(transform, v);
	}
	inline float4 ProjectPointUnnormalized(const float3 v, const float w = 1.f) CPP_CONST {
		return ProjectPointUnnormalized(float4(v, w));
	}
	inline float3 ProjectPoint(const float3 v) CPP_CONST {
		float4 h = ProjectPointUnnormalized(v);
		if (h.w != 0) h /= h.w;
		return float3(h.x, h.y, h.z);
	}
	inline float3 TransformPoint(const float3 v) CPP_CONST {
		float4 h = ProjectPointUnnormalized(v);
		return float3(h.x, h.y, h.z);
	}
	inline float3 TransformVector(const float3 v) CPP_CONST {
		float4 h = ProjectPointUnnormalized(v, 0.f);
		return float3(h.x, h.y, h.z);
	}
};

inline Transform operator*(const Transform lhs, const Transform rhs) {
	Transform r = {};
	r.transform = mul(lhs.transform, rhs.transform);
	return r;
}

inline Transform transpose(const Transform t) {
	Transform r = {};
	r.transform = transpose(t.transform);
	return r;
}
#ifdef __cplusplus
inline Transform inverse(const Transform t) {
	Transform r = {};
	r.transform = inverse(t.transform);
	return r;
}
#endif

}
