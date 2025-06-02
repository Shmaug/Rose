import Rose.Scene.Scene;
import Rose.Core.MathUtils;
import Rose.Core.Random;
import EmissionSampling;
import BSDF.Lambertian;
#include <Rose/Core/Bitfield.h>

using namespace RoseEngine;

#ifndef USE_NEE
#define USE_NEE 1
#endif

static const float kConnectabilityThreshold   = 0.5;
static const float kDiffuseRoughnessThreshold = 0.25;
static const bool  kNEE = USE_NEE;

ParameterBlock<Scene> scene;
RWTexture2D<float4> renderTarget;
Texture2D<uint4>    visibility;

uniform Transform worldToCamera;
uniform Transform cameraToWorld;
uniform Transform projection;
uniform Transform inverseProjection;
uniform uint2     imageSize;
uniform uint      seed;
uniform uint      maxBounces;
uniform uint      maxDiffuseBounces;

struct PackedHit {
    uint   instanceIndex;
    uint   primitiveIndex;
    float2 barycentrics;
}

// applies normal map to vertex
bool LoadMaterial(const InstanceHeader instance, inout SceneVertex vertex, out Material material, const float uvScreenSize = 0) {
    material = scene.materials[instance.materialIndex];

    if (material.baseColorImage < scene.imageCount) {
        float4 rgba = scene.SampleImage(material.baseColorImage, vertex.texcoord, uvScreenSize);
        if (rgba.a < material.GetAlphaCutoff() && material.HasFlag(MaterialFlags::eAlphaCutoff))
            return false;
        material.SetBaseColor(material.GetBaseColor() * rgba.rgb);
    }

    if (material.emissionImage < scene.imageCount)
        material.SetEmission(material.GetEmission() * scene.SampleImage(material.emissionImage, vertex.texcoord, uvScreenSize).rgb);

	if (material.bumpMap < scene.imageCount) {
        float3 bump = scene.SampleImage(material.bumpMap, vertex.texcoord, uvScreenSize).rgb;
        bump = float3(bump.x*2-1, bump.y*2-1, bump.z);
        vertex.shadingNormal = normalize(
            bump.x * vertex.tangent.xyz +
            bump.y * vertex.bitangent +
            bump.z * vertex.shadingNormal);
        vertex.tangent.xyz = normalize(vertex.tangent.xyz - vertex.shadingNormal * dot(vertex.tangent.xyz, vertex.shadingNormal));
    }
    return true;
}

bool LoadHit(const PackedHit hit, out SceneVertex vertex, out Material material, const float uvScreenSize = 0) {
    if (hit.instanceIndex >= scene.instanceCount) {
        vertex = {};
        material = {};
        return false;
    }

    const InstanceHeader instance = scene.instances[hit.instanceIndex];

    // Load vertex

    const MeshHeader mesh = scene.meshes[instance.meshIndex];
    vertex = LoadSceneVertex(scene, hit.instanceIndex, instance, mesh, hit.primitiveIndex, hit.barycentrics);

    return LoadMaterial(instance, vertex, material, uvScreenSize);
}

struct PathState {
    float3 throughput;
    uint   packedVertexIndices;
    float3 radiance;
    float  misBsdfPdf;
    float3 rayOrigin;
    float3 rayDirection;
    RandomSampler rng;

    property uint vertexIndex {
        get { return BF_GET(packedVertexIndices, 0, 16); }
        set { BF_SET(packedVertexIndices, newValue, 0, 16); }
    }

    property uint diffuseBounces {
        get { return BF_GET(packedVertexIndices, 16, 16); }
        set { BF_SET(packedVertexIndices, newValue, 16, 16); }
    }
};

bool SampleDirectLight(inout PathState path, const SceneVertex vertex, const Material material, const float3 localDirIn, inout LambertianBSDF bsdf, out float3 L) {
    float pdf;
    SceneVertex lightVertex = scene.SampleEmission(path.rng.NextFloat(), pdf);
    if (!(pdf > 0))
        return false;

    float G = 1;
    float3 dirOut;

    RayDesc ray;
    ray.TMin = 0;

    if (lightVertex.isBackground) {
        dirOut = lightVertex.position;
        L = scene.EvalBackground(dirOut);
        ray.Direction = dirOut;
        ray.Origin = OffsetRayOrigin(vertex.position, vertex.faceNormal, ray.Direction);
        ray.TMax = FLT_MAX;
    } else {
        Material lightMaterial;
        if (!LoadMaterial(scene.instances[lightVertex.instanceIndex], lightVertex, lightMaterial))
			return false;

        L = lightMaterial.GetEmission();

        dirOut = lightVertex.position - vertex.position;
        const float invdist2 = 1 / dot(dirOut, dirOut);
        dirOut *= sqrt(invdist2);

        const float cosLight = -dot(dirOut, lightVertex.faceNormal);
        if (cosLight <= 0)
            return false;

        G = cosLight * invdist2;

        ray.Origin    = OffsetRayOrigin(vertex.position,      vertex.faceNormal,       dirOut);
        ray.Direction = OffsetRayOrigin(lightVertex.position, lightVertex.faceNormal, -dirOut) - ray.Origin;
        ray.TMax = length(ray.Direction);
        ray.Direction /= ray.TMax;
	}
    if (!any(L > 0))
        return false;

    L /= pdf;

    const BSDFEval f = bsdf.Eval(localDirIn, vertex.ToLocal(dirOut));
    L *= f.f * G;

    if (!any(L > 0))
        return false;

    // balance heuristic
    L /= 1 + (f.fwdPdf * G) / pdf;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rq;
    rq.TraceRayInline(scene.accelerationStructure, 0, 1, ray);
    rq.Proceed();
    return rq.CommittedStatus() == COMMITTED_NOTHING;
    return true;
}

void ShadeHit(inout PathState path, const PackedHit hit) {
    SceneVertex vertex;
    Material material;
	if (!LoadHit(hit, vertex, material)) {
        // ray missed
        const float3 Le = scene.EvalBackground(path.rayDirection);
        if (any(Le > 0)) {
            vertex.position = path.rayDirection;
            vertex.faceArea = -1;
            const float w = kNEE && (path.misBsdfPdf > 0) ?
                1 / (1 + scene.EmissionPdf(vertex) / path.misBsdfPdf)
                : 1;
            path.radiance += path.throughput * Le * w;
        }
        path.throughput = 0;
        return;
    }

    const float cosIn = -dot(path.rayDirection, vertex.faceNormal);
    if (cosIn > 0) {
        const float3 Le = material.GetEmission();
        if (any(Le > 0)) {
            const float3 dt = path.rayOrigin - vertex.position;
            const float G = cosIn / dot(dt, dt);
            const float w = kNEE && (path.misBsdfPdf > 0) ?
                1 / (1 + scene.EmissionPdf(vertex) / (path.misBsdfPdf * G))
                : 1;
			path.radiance += path.throughput * Le * w;
        }
    }

    if (path.vertexIndex >= maxBounces+1) {
        path.throughput = 0;
        return;
	}

    const float3 localDirIn = vertex.ToLocal(-path.rayDirection);
    LambertianBSDF bsdf = LambertianBSDF(material, localDirIn);

    // connection strategies

    const bool connectable = bsdf.Connectable();
    if (kNEE && connectable) {
        float3 L;
        if (SampleDirectLight(path, vertex, material, localDirIn, bsdf, L)) {
            path.radiance += path.throughput * L;
        }
    }

    // bsdf sampling

    const BSDFSample s = bsdf.Sample(path.rng.NextFloat(), localDirIn);
    if (s.sampledLobe == kInvalidLobeIndex) {
        // bsdf sampling failed
        path.throughput = 0;
        return;
    }

    if (s.lobeRoughness > kDiffuseRoughnessThreshold) {
        path.diffuseBounces++;
        if (path.diffuseBounces > maxDiffuseBounces) {
            path.throughput = 0;
            return;
        }
    }

    const BSDFEval f = bsdf.Eval(localDirIn, s.dirOut);
    path.throughput *= f.fwdPdf > 0 ? f.f / f.fwdPdf : 0;
    path.misBsdfPdf = connectable ? f.fwdPdf : 0;

	path.rayDirection = vertex.ToWorld(s.dirOut);
	path.rayOrigin = OffsetRayOrigin(vertex.position, vertex.faceNormal, path.rayDirection);
}

PackedHit TraceRay(const float3 origin, const float3 direction) {
    RayDesc ray = {};
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0;
    ray.TMax = FLT_MAX;
    RayQuery<RAY_FLAG_NONE> rq;
    rq.TraceRayInline(scene.accelerationStructure, 0, 1, ray);
    while (rq.Proceed()) {
        // TODO: non opaque triangles
        PackedHit candidateHit = PackedHit(rq.CandidateInstanceID(), rq.CandidatePrimitiveIndex(), rq.CandidateTriangleBarycentrics());
    }
    if (rq.CommittedStatus() == COMMITTED_NOTHING) {
        return PackedHit(UINT32_MAX, UINT32_MAX, 0);
    } else {
        return PackedHit(rq.CommittedInstanceID(), rq.CommittedPrimitiveIndex(), rq.CommittedTriangleBarycentrics());
    }
}

[shader("compute")]
[numthreads(8,4,1)]
void main(uint3 index: SV_DispatchThreadID) {
    if (any(index.xy >= imageSize))
        return;

    PathState path;
    const float2 clip = 2 * (index.xy + .5) / float2(imageSize) - 1;
    path.rayDirection = normalize(cameraToWorld.TransformVector(inverseProjection.ProjectPoint(float3(clip.x, clip.y, -1))));
    path.rayOrigin = cameraToWorld.TransformPoint(0);
    path.rng = RandomSampler(seed, index.xy);
    path.throughput = 1;
    path.radiance = 0;
    path.diffuseBounces = 0;

    // shade primary hit
    path.vertexIndex = 1;
    // ShadeHit(path, TraceRay(path.rayOrigin, path.rayDirection));
    ShadeHit(path, reinterpret<PackedHit>(visibility[index.xy]));

    while (any(path.throughput > 0)) {
        const PackedHit hit = TraceRay(path.rayOrigin, path.rayDirection);
        path.vertexIndex++;
        ShadeHit(path, hit);
    }

    renderTarget[index.xy] = float4(path.radiance, 1);
}