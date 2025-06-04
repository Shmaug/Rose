#pragma once

import Rose.Core.MathUtils;
#include "SceneTypes.h"

__exported import Transform;

namespace RoseEngine {

struct PackedSceneHit {
	uint   instanceIndex;
	uint   primitiveIndex;
	float2 barycentrics;
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

struct Scene {
    static const uint kMaxVertexBuffers = 1024;
    static const uint kMaxImages = 4096;

	StructuredBuffer<InstanceHeader> instances;
	StructuredBuffer<Transform>      transforms;
	StructuredBuffer<Transform>      inverseTransforms;
	StructuredBuffer<MeshHeader>     meshes;
	StructuredBuffer<Material>       materials;
    StructuredBuffer<uint>           emissiveInstances;
	RaytracingAccelerationStructure  accelerationStructure;
    Texture2D<float>                 backgroundImportanceMap;
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

    enum ImageSampleFlags {
		eNonUniform = 0,
		eUniform    = 1, // imageIndex is uniform
		eFragShader = 2, // in a frag shader, can use ddx(uv) and ddy(uv)
    };

    float4 SampleImage<let kSampleFlags : ImageSampleFlags>(const uint imageIndex, const float2 uv, const float uvScreenSize = 0) {
        if (kSampleFlags == ImageSampleFlags::eFragShader) {
			return images[imageIndex].Sample(sampler, uv);
        } else {
			Texture2D tex = kSampleFlags == ImageSampleFlags::eUniform ? images[imageIndex] : images[NonUniformResourceIndex(imageIndex)];
			float lod = 0;
			if (uvScreenSize > 0) {
				float w, h;
				tex.GetDimensions(w, h);
				lod = log2(max(uvScreenSize * max(w, h), 1e-6f));
			}
			return tex.SampleLevel(sampler, uv, lod);
		}
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

    uint3 LoadTriangleIndices<let bMeshUniform : bool>(const VertexAttribute attrib, const uint primitiveIndex) {
        ByteAddressBuffer buf = bMeshUniform ? meshBuffers[attrib.bufferIndex] : meshBuffers[NonUniformResourceIndex(attrib.bufferIndex)];
        return LoadTriangleIndices(buf, attrib.bufferOffset, attrib.stride, primitiveIndex);
    }

    void LoadTriangleAttribute<let bMeshUniform : bool, T>(const VertexAttribute attrib, const uint3 tri, out T v0, out T v1, out T v2) {
        ByteAddressBuffer buf = bMeshUniform ? meshBuffers[attrib.bufferIndex] : meshBuffers[NonUniformResourceIndex(attrib.bufferIndex)];
		v0 = buf.Load<T>(attrib.bufferOffset + attrib.stride * tri[0]);
		v1 = buf.Load<T>(attrib.bufferOffset + attrib.stride * tri[1]);
        v2 = buf.Load<T>(attrib.bufferOffset + attrib.stride * tri[2]);
    }

	[Differentiable]
	SceneVertex LoadBackgroundVertex(const float3 dir) {
		SceneVertex v = {};
		v.position = dir;
		v.faceArea = -1;
		return v;
	}

	[Differentiable]
	SceneVertex LoadSceneVertex<let bMeshUniform : bool>(const PackedSceneHit hit, const InstanceHeader instance, const MeshHeader mesh) {
		const uint3 tri = LoadTriangleIndices<bMeshUniform>(mesh.triangles, hit.primitiveIndex);

		float3 v0, v1, v2;
		float3 n0, n1, n2;
		float2 t0, t1, t2;
		bool hasNormals   = mesh.normals.bufferIndex   < meshBufferCount;
		bool hasTexcoords = mesh.texcoords.bufferIndex < meshBufferCount;

		LoadTriangleAttribute<bMeshUniform>(mesh.positions, tri, v0, v1, v2);
		if (hasNormals)   LoadTriangleAttribute<bMeshUniform>(mesh.normals,   tri, n0, n1, n2);
		if (hasTexcoords) LoadTriangleAttribute<bMeshUniform>(mesh.texcoords, tri, t0, t1, t2);

		const float3 dPds = v1 - v0;
		const float3 dPdt = v2 - v0;

		SceneVertex v = {};
		v.instanceIndex  = hit.instanceIndex;
		v.primitiveIndex = hit.primitiveIndex;
		v.position       = v0 + dPds * hit.barycentrics.x + dPdt * hit.barycentrics.y;
		v.faceNormal     = normalize(cross(dPds, dPdt));

		// load shading normal
		if (hasNormals) {
			v.shadingNormal = n0 + (n1 - n0) * hit.barycentrics.x + (n2 - n0) * hit.barycentrics.y;
			hasNormals = any(v.shadingNormal != 0) && all(v.shadingNormal == v.shadingNormal);
		}
		if (hasNormals) {
			v.shadingNormal = normalize(v.shadingNormal);

			// make face normal agree with shading normals
			if (dot(v.faceNormal, v.shadingNormal) < 0) {
				v.faceNormal = -v.faceNormal;
			}
		} else {
			v.shadingNormal = v.faceNormal;
		}

        // load texcoord and tangent
		float3 bitangent;
		if (hasTexcoords) {
			const float2 dTds = t1 - t0;
			const float2 dTdt = t2 - t0;
			v.texcoord = t0 + dTds * hit.barycentrics.x + dTdt * hit.barycentrics.y;

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

		// Apply instance transform

		const Transform t = transforms[instance.transformIndex];
		v.position = t.TransformPoint(v.position);

		const Transform nt = transpose(inverseTransforms[instance.transformIndex]);
		v.faceNormal    = normalize(nt.TransformVector(v.faceNormal));
		v.shadingNormal = normalize(nt.TransformVector(v.shadingNormal));
		v.tangent.xyz   = normalize(nt.TransformVector(v.tangent.xyz));
		v.faceArea      = length(cross(nt.TransformVector(dPds), nt.TransformVector(dPdt))) / 2;

		return v;
	}

    // Load material, sample textures, apply bump mapping to vertex
    // returns false if alpha test fails
    bool LoadMaterial<let kSampleFlags : ImageSampleFlags>(const InstanceHeader instance, inout SceneVertex vertex, out Material material, const float uvScreenSize = 0) {
		material = materials[instance.materialIndex];

		if (material.baseColorImage < imageCount) {
            float4 rgba = SampleImage<kSampleFlags>(material.baseColorImage, vertex.texcoord, uvScreenSize);
            if (material.HasFlag(MaterialFlags::eAlphaCutoff) && rgba.a < material.GetAlphaCutoff())
				return false;
			material.SetBaseColor(material.GetBaseColor() * rgba.rgb);
		}

		if (material.emissionImage < imageCount)
			material.SetEmission(material.GetEmission() * SampleImage<kSampleFlags>(material.emissionImage, vertex.texcoord, uvScreenSize).rgb);

		if (material.bumpMap < imageCount) {
			float3 bump = SampleImage<kSampleFlags>(material.bumpMap, vertex.texcoord, uvScreenSize).rgb;
			bump = float3(bump.x*2-1, bump.y*2-1, bump.z);
			vertex.shadingNormal = normalize(
				bump.x * vertex.tangent.xyz +
				bump.y * vertex.bitangent +
				bump.z * vertex.shadingNormal);
			vertex.tangent.xyz = normalize(vertex.tangent.xyz - vertex.shadingNormal * dot(vertex.tangent.xyz, vertex.shadingNormal));
		}
		return true;
	}

    // Load a vertex and material
    bool UnpackSceneHit<let kSampleFlags : ImageSampleFlags, let bMeshUniform : bool>(const PackedSceneHit hit, out SceneVertex vertex, out Material material, const float uvScreenSize = 0) {
		if (hit.instanceIndex >= instanceCount) {
			// invalid hit (background vertex?)
			vertex = {};
			material = {};
			return false;
		}
		const InstanceHeader instance = instances[hit.instanceIndex];
		const MeshHeader     mesh     = meshes[instance.meshIndex];
		vertex = LoadSceneVertex<bMeshUniform>(hit, instance, mesh);
		return LoadMaterial<kSampleFlags>(instance, vertex, material, uvScreenSize);
	}

    float3 EvalBackground(const float3 dir) {
        float3 c = backgroundColor;
        if (backgroundImage < imageCount) {
            c *= SampleImage<Scene::ImageSampleFlags::eUniform>(backgroundImage, xyz2sphuv(dir)).rgb;
        }
        return c;
	}


    bool Occluded(const RayDesc ray, const uint instanceMask = UINT32_MAX) {
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rq;
        rq.TraceRayInline(accelerationStructure, RAY_FLAG_NONE, instanceMask, ray);
        rq.Proceed();
        return rq.CommittedStatus() != COMMITTED_NOTHING;
	}

    PackedSceneHit TraceRay(const RayDesc ray, const uint instanceMask = UINT32_MAX) {
        RayQuery<RAY_FLAG_NONE> rq;
        rq.TraceRayInline(accelerationStructure, 0, instanceMask, ray);
		while (rq.Proceed()) {
            const PackedSceneHit candidateHit = PackedSceneHit(rq.CandidateInstanceID(), rq.CandidatePrimitiveIndex(), rq.CandidateTriangleBarycentrics());
            if (rq.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
				// alpha testing done in LoadMaterial
                SceneVertex vertex;
                Material material;
                if (UnpackSceneHit<ImageSampleFlags::eNonUniform, false>(candidateHit, vertex, material))
                	rq.CommitNonOpaqueTriangleHit();
			}
		}
		if (rq.CommittedStatus() == COMMITTED_NOTHING) {
			return PackedSceneHit(UINT32_MAX, UINT32_MAX, 0);
		} else {
			return PackedSceneHit(rq.CommittedInstanceID(), rq.CommittedPrimitiveIndex(), rq.CommittedTriangleBarycentrics());
		}
	}
};

}