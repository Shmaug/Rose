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

#ifndef DI_ONLY
#define DI_ONLY 0
#endif

static const float kConnectabilityThreshold   = 0.5;
static const float kDiffuseRoughnessThreshold = 0.25;
static const bool kNEE = (bool)USE_NEE;
static const bool kDIOnly = (bool)USE_NEE && (bool)DI_ONLY;

ParameterBlock<Scene> scene;
RWTexture2D<float4>   renderTarget;
Texture2D<uint4>      visibility;

uniform Transform worldToCamera;
uniform Transform cameraToWorld;
uniform Transform projection;
uniform Transform inverseProjection;
uniform uint2     imageSize;
uniform uint      seed;
uniform uint      maxBounces;
uniform uint      maxDiffuseBounces;

void GetPrimaryRay(const float2 pixel, out float3 origin, out float3 direction, const float depth = -1) {
    const float3 clip = float3(2 * pixel / float2(imageSize) - 1, depth);
    const float3 viewPos = inverseProjection.ProjectPoint(clip);
    direction = normalize(transpose(worldToCamera).TransformVector(viewPos));
    origin = cameraToWorld.TransformPoint(0);
}

struct PathState {
    float3 throughput;
    uint   packedVertexIndices;
    float3 radiance;
    float  misBsdfPdf;
    float3 rayOrigin;
	float  pad0;
    float3 rayDirection;
	float  pad1;
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
static const uint PathStateSize = sizeof(PathState);

// power hueristic (beta=2)
float evalMIS(float pdf1, float pdf2) {
    if (!(pdf1 > 0))
        return 0;
	// pdf1 / (pdf1 + pdf2)  =  1 / (1 + pdf2/pdf1)
	const float r = pdf2 / pdf1;
	return 1 / (1 + r*r);
}

bool SampleDirectLight(inout RandomSampler rng, const SceneVertex vertex, const Material material, const float3 localDirIn, const LambertianBSDF bsdf, out float3 L) {
    float pdf;
    SceneVertex lightVertex = scene.SampleEmission(rng.NextFloat(), pdf);
    if (!(pdf > 0)) {
        L = 0;
        return false;
    }

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
        if (!scene.LoadMaterial<Scene::ImageSampleFlags::eNonUniform>(scene.instances[lightVertex.instanceIndex], lightVertex, lightMaterial)) {
            L = 0;
			return false;
		}

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
    if (!any(L > 0)) {
        L = 0;
        return false;
    }

    L /= pdf;

    const BSDFEval f = bsdf.Eval(localDirIn, vertex.ToLocal(dirOut));
    L *= f.f * G;

    // balance heuristic
    if (!kDIOnly)
    	L *= evalMIS(pdf, f.fwdPdf * G);

    if (!any(L > 0))
        return false;

    return !scene.Occluded(ray);
}

void ShadeHit(inout PathState path, const PackedSceneHit hit) {
    SceneVertex vertex;
    Material material;
    if (!scene.UnpackSceneHit<Scene::ImageSampleFlags::eNonUniform, false>(hit, vertex, material)) {
        // evaluate background/environment emission
        const float3 Le = scene.EvalBackground(path.rayDirection);
        if (any(Le > 0)) {
            const float w = !kNEE || path.misBsdfPdf == 0 ? 1 : evalMIS(path.misBsdfPdf, scene.EvalBackgroundSamplePdf(path.rayDirection));
            path.radiance += path.throughput * Le * w;
        }
        path.throughput = 0;
        return;
    }

	// evaluate surface emission
    const float cosIn = -dot(path.rayDirection, vertex.faceNormal);
    if (cosIn > 0) {
        const float3 Le = material.GetEmission();
        if (any(Le > 0)) {
            const float3 d = path.rayOrigin - vertex.position;
            const float G = cosIn / dot(d, d);
            const float w = !kNEE || path.misBsdfPdf == 0 ? 1 : evalMIS(path.misBsdfPdf * G, scene.EvalSurfaceSamplePdf(vertex));
            path.radiance += path.throughput * Le * w;
        }
    }

	// path length limit
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
        if (SampleDirectLight(path.rng, vertex, material, localDirIn, bsdf, L)) {
            path.radiance += path.throughput * L;
        }
    }

    // bsdf sampling

    if (kDIOnly) {
        path.throughput = 0;
        return;
    }

    const BSDFSample s = bsdf.Sample(path.rng.NextFloat(), localDirIn);
    if (s.sampledLobe == kInvalidLobeIndex) {
        // bsdf sampling failed
        path.throughput = 0;
        return;
    }

	// diffuse bounce limit
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

[shader("compute")]
[numthreads(8,4,1)]
void main(uint3 index: SV_DispatchThreadID) {
    if (any(index.xy >= imageSize))
        return;

    PathState path;
    GetPrimaryRay(index.xy + .5, path.rayOrigin, path.rayDirection);
    path.rng = RandomSampler(seed, index.xy);
    path.throughput = 1;
    path.radiance = 0;
    path.diffuseBounces = 0;
    path.misBsdfPdf = 0;

    // load primary hit from vbuffer
    {
		path.vertexIndex = 1;
		// const PackedSceneHit hit = reinterpret<PackedSceneHit>(visibility[index.xy]);
		const PackedSceneHit hit = scene.TraceRay(RayDesc(path.rayOrigin, 0, path.rayDirection, FLT_MAX));
		ShadeHit(path, hit);
    }

    // trace rays for secondary hits
    while (!kDIOnly && any(path.throughput > 0)) {
        path.vertexIndex++;
        const PackedSceneHit hit = scene.TraceRay(RayDesc(path.rayOrigin, 0, path.rayDirection, FLT_MAX));
        ShadeHit(path, hit);
    }

    renderTarget[index.xy] = float4(path.radiance, 1);
}