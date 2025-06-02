#pragma once

import Rose.Core.MathUtils;
#include "SceneTypes.h"

__exported import Transform;

namespace RoseEngine {

struct Scene {
    static const uint kMaxVertexBuffers = 1024;
    static const uint kMaxImages = 1024;

	StructuredBuffer<InstanceHeader> instances;
	StructuredBuffer<Transform>      transforms;
	StructuredBuffer<Transform>      inverseTransforms;
	StructuredBuffer<MeshHeader>     meshes;
	StructuredBuffer<Material>       materials;
    StructuredBuffer<uint>           emissiveInstances;
	RaytracingAccelerationStructure  accelerationStructure;
	SamplerState                     sampler;
	ByteAddressBuffer                meshBuffers[kMaxVertexBuffers];
    Texture2D<float4>                images[kMaxImages];

    float3 backgroundColor;
    uint   backgroundImage;
    uint   instanceCount;
    uint   meshBufferCount;
    uint   materialCount;
    uint   imageCount;
    uint   emissiveInstanceCount;
    float  backgroundSampleProbability;

    float4 SampleImage(const uint imageIndex, const float2 uv, const float uvScreenSize = 0) {
        Texture2D tex = images[NonUniformResourceIndex(imageIndex)];
        float lod = 0;
        if (uvScreenSize > 0) {
            float w, h;
            tex.GetDimensions(w, h);
            lod = log2(max(uvScreenSize * max(w, h), 1e-6f));
        }
        return tex.SampleLevel(sampler, uv, lod);
    }
    float4 SampleImageUniform(const uint imageIndex, const float2 uv, const float uvScreenSize = 0) {
        Texture2D tex = images[imageIndex];
        float lod = 0;
        if (uvScreenSize > 0) {
            float w, h;
            tex.GetDimensions(w, h);
            lod = log2(max(uvScreenSize * max(w, h), 1e-6f));
        }
        return tex.SampleLevel(sampler, uv, lod);
    }

	uint3 LoadTriangleIndices(const ByteAddressBuffer indices, const uint offset, const uint indexStride, const uint primitiveIndex) {
		// https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl
		const int offsetBytes = (int)(offset + primitiveIndex * 3 * indexStride);
		uint3 tri;
		if (indexStride == 2) {
			const int dwordAlignedOffset = offsetBytes & ~3;
			const uint2 four16BitIndices = indices.Load2(dwordAlignedOffset);
			if (dwordAlignedOffset == offsetBytes) {
				tri.x = four16BitIndices.x & 0xffff;
				tri.y = (four16BitIndices.x >> 16) & 0xffff;
				tri.z = four16BitIndices.y & 0xffff;
			} else {
				tri.x = (four16BitIndices.x >> 16) & 0xffff;
				tri.y = four16BitIndices.y & 0xffff;
				tri.z = (four16BitIndices.y >> 16) & 0xffff;
			}
		} else
			tri = indices.Load3(offsetBytes);
		return tri;
	}

	uint3 LoadTriangleIndices(const VertexAttribute attrib, const uint primitiveIndex) {
		return LoadTriangleIndices(meshBuffers[NonUniformResourceIndex(attrib.bufferIndex)], attrib.bufferOffset, attrib.stride, primitiveIndex);
	}
    uint3 LoadTriangleIndicesUniform(const VertexAttribute attrib, const uint primitiveIndex) {
        return LoadTriangleIndices(meshBuffers[attrib.bufferIndex], attrib.bufferOffset, attrib.stride, primitiveIndex);
	}

    void LoadTriangleAttribute<T>(const VertexAttribute attrib, const uint3 tri, out T v0, out T v1, out T v2) {
        ByteAddressBuffer buf = meshBuffers[NonUniformResourceIndex(attrib.bufferIndex)];
		v0 = buf.Load<T>(attrib.bufferOffset + attrib.stride * tri[0]);
		v1 = buf.Load<T>(attrib.bufferOffset + attrib.stride * tri[1]);
        v2 = buf.Load<T>(attrib.bufferOffset + attrib.stride * tri[2]);
    }
    void LoadTriangleAttributeUniform<T>(const VertexAttribute attrib, const uint3 tri, out T v0, out T v1, out T v2) {
        ByteAddressBuffer buf = meshBuffers[attrib.bufferIndex];
        v0 = buf.Load<T>(attrib.bufferOffset + attrib.stride * tri[0]);
        v1 = buf.Load<T>(attrib.bufferOffset + attrib.stride * tri[1]);
        v2 = buf.Load<T>(attrib.bufferOffset + attrib.stride * tri[2]);
    }

    float3 EvalBackground(const float3 dir) {
        float3 c = backgroundColor;
        if (backgroundImage < imageCount) {
            c *= SampleImageUniform(backgroundImage, xyz2sphuv(dir)).rgb;
        }
        return c;
	}
};

struct SceneVertex : IDifferentiable {
	float3 position = 0;
	uint   instanceIndex = UINT32_MAX;
	float3 faceNormal = 0;
	uint   primitiveIndex = UINT32_MAX;

	float3 shadingNormal = 0;
	float  faceArea = 0;
	float4 tangent = 0;

	float2 texcoord = 0;

    // faceArea = -1 indicates background vertex
    property bool isBackground { get { return faceArea < 0; } }

	property float3 bitangent { get { return tangent.w * cross(tangent.xyz, shadingNormal); } }

	// tangent space to world
    float3 ToWorld(float3 v) { return v.x * tangent.xyz + v.y * bitangent + v.z * shadingNormal; }
	// world to tangent space
    float3 ToLocal(const float3 v) { return float3(dot(v, tangent.xyz), dot(v, bitangent), dot(v, shadingNormal)); }
};

[Differentiable]
SceneVertex LoadSceneVertex(const Scene scene, const uint instanceIndex, const InstanceHeader instance, const MeshHeader mesh, const uint primitiveIndex, const float2 bary) {
	const uint3 tri = scene.LoadTriangleIndices(mesh.triangles, primitiveIndex);

	float3 v0, v1, v2;
	float3 n0, n1, n2;
	float2 t0, t1, t2;
	bool hasNormals   = mesh.normals.bufferIndex < scene.meshBufferCount;
	bool hasTexcoords = mesh.texcoords.bufferIndex < scene.meshBufferCount;

	scene.LoadTriangleAttribute(mesh.positions, tri, v0, v1, v2);
	if (hasNormals)   scene.LoadTriangleAttribute(mesh.normals,   tri, n0, n1, n2);
	if (hasTexcoords) scene.LoadTriangleAttribute(mesh.texcoords, tri, t0, t1, t2);

	const float3 dPds = v1 - v0;
	const float3 dPdt = v2 - v0;

    SceneVertex v = {};
    v.instanceIndex = instanceIndex;
    v.primitiveIndex = primitiveIndex;
    v.position = v0 + dPds * bary.x + dPdt * bary.y;
	v.faceNormal = normalize(cross(dPds, dPdt));

	if (hasNormals) {
		v.shadingNormal = n0 + (n1 - n0) * bary.x + (n2 - n0) * bary.y;
		hasNormals = any(v.shadingNormal != 0) && all(v.shadingNormal == v.shadingNormal);
	}
	if (hasNormals) {
		v.shadingNormal = normalize(v.shadingNormal);

		// make face normal agree with shading normals
		if (dot(v.faceNormal, v.shadingNormal) < 0)
			v.faceNormal = -v.faceNormal;
	} else
		v.shadingNormal = v.faceNormal;

	float3 bitangent;
	if (hasTexcoords) {
		const float2 dTds = t1 - t0;
		const float2 dTdt = t2 - t0;
		v.texcoord = t0 + dTds * bary.x + dTdt * bary.y;

		// Slide 7: https://www.cs.utexas.edu/~fussell/courses/cs384g-spring2016/lectures/normal_mapping_tangent.pdf
		float2x2 inv_m = inverse(transpose(float2x2(dTds, dTdt)));
		float2x3 tb = mul(inv_m, float2x3(dPds, dPdt));
		v.tangent.xyz = normalize(tb[0]);
		bitangent = tb[1];
	} else {
		ONB(v.shadingNormal, v.tangent.xyz, bitangent);
	}
	v.tangent.w = 1;
	v.tangent.w = dot(v.bitangent, bitangent) > 0 ? 1 : -1;

	// Apply transform

    const Transform t = scene.transforms[instance.transformIndex];
    v.position = t.TransformPoint(v.position);

	const Transform nt = transpose(scene.inverseTransforms[instance.transformIndex]);
	v.faceNormal    = normalize(nt.TransformVector(v.faceNormal));
	v.shadingNormal = normalize(nt.TransformVector(v.shadingNormal));
	v.tangent.xyz   = normalize(nt.TransformVector(v.tangent.xyz));
    v.faceArea      = length(cross(nt.TransformVector(dPds), nt.TransformVector(dPdt))) / 2;

	return v;
}

}