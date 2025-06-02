import Rose.Scene.Scene;
import Rose.Core.MathUtils;
import Rose.Core.Random;
import Sampling;
import BSDF.Lambertian;
import BSDF.Disney;
#include <Rose/Core/Bitfield.h>

using namespace RoseEngine;

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

    // Load material, sample images

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

bool SampleLight(inout RandomSampler rng, const SceneVertex vertex, out float3 le, out float pdf, out float3 dir) {
    le = scene.backgroundColor;
    if (scene.backgroundImage < scene.imageCount) {
        const float2 uv = SampleTexel(scene.images[scene.backgroundImage], rng.NextFloat().xy, pdf);
        dir = sphuv2xyz(uv);
        pdf /= (2 * M_PI * M_PI * sqrt(1 - dir.y * dir.y));
        le *= scene.SampleImageUniform(scene.backgroundImage, uv).rgb;
    } else {
        dir = SampleUniformSphere(rng.NextFloat().xy);
        pdf = 1 / (4 * M_PI);
    }

    RayDesc ray = {};
    ray.Origin = OffsetRayOrigin(vertex.position, vertex.faceNormal, dir);
    ray.Direction = dir;
    ray.TMin = 0;
    ray.TMax = 1e9;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rq;
    rq.TraceRayInline(scene.accelerationStructure, 0, 1, ray);
    rq.Proceed();
    return rq.CommittedStatus() == COMMITTED_NOTHING;
}

struct PathState {
    float3 throughput;
    uint   packedVertexIndices;
    float3 radiance;
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

void ShadeHit(inout PathState path, const PackedHit hit) {
    SceneVertex vertex;
    Material material;
    if (!LoadHit(hit, vertex, material))
    {
		// ray missed
        path.radiance += path.throughput * scene.EvalBackground(path.rayDirection);
        path.throughput = 0;
        return;
    }

    const float3 localDirIn = vertex.ToLocal(-path.rayDirection);
    if (localDirIn.z > 0) path.radiance += path.throughput * material.GetEmission();

    if (path.vertexIndex == maxBounces+1) {
        path.throughput = 0;
        return;
	}

    DisneyBSDF bsdf = DisneyBSDF(material, localDirIn);

    let s = bsdf.Sample(material, path.rng.NextFloat(), false);

    if (s.sampledLobe == 0) {
        // bsdf sampling failed
        path.throughput = 0;
        return;
    }

    if (s.lobeRoughness > 0.25) {
        path.diffuseBounces++;
        if (path.diffuseBounces > maxDiffuseBounces) {
            path.throughput = 0;
            return;
        }
    }

    float fwdPdf, revPdf;
    const float3 f = bsdf.Eval(material, s.localDirOut, fwdPdf, revPdf);

    path.throughput *= fwdPdf > 0 ? f / fwdPdf : 0;

	path.rayDirection = vertex.ToWorld(s.localDirOut);
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