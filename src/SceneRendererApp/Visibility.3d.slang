import Rose.Scene.Scene;

using namespace RoseEngine;

struct GBuffer {
    float4 color: SV_Target0;
    uint4  visibility: SV_Target1;
};

uniform Transform worldToCamera;
uniform Transform projection;

ParameterBlock<Scene> scene;

#ifndef HAS_TEXCOORD
#define HAS_TEXCOORD 0
#endif
#ifndef USE_ALPHA_CUTOFF
#define USE_ALPHA_CUTOFF 0
#endif

struct v2f {
    float4 pos: SV_Position;
	nointerpolation uint instanceId: TEXCOORD0;
#if HAS_TEXCOORD
    float2 uv: TEXCOORD1;
#endif
};

[shader("vertex")]
v2f vertexMain(
    float3 pos: POSITION,
#if HAS_TEXCOORD
    float2 uv: TEXCOORD0,
#endif
    uint instanceId: SV_InstanceID) {
    v2f o = {};
    o.pos = (projection * (worldToCamera * scene.transforms[scene.instances[instanceId].transformIndex])).ProjectPointUnnormalized(pos);
	o.instanceId = instanceId;
#if HAS_TEXCOORD
    o.uv = uv;
#endif

    return o;
}

[shader("fragment")]
GBuffer fragmentMain(v2f i, uint primId: SV_PrimitiveID, float3 bary: SV_Barycentrics) {
#if HAS_TEXCOORD
#if USE_ALPHA_CUTOFF
    const Material m = scene.materials[scene.instances[i.instanceId].materialIndex];
    if (m.baseColorImage < scene.imageCount && m.HasFlag(MaterialFlags::eAlphaCutoff)) {
        if (scene.images[m.baseColorImage].Sample(scene.sampler, i.uv).a < m.GetAlphaCutoff())
            discard;
	}
#endif
#endif
    GBuffer r = {};
    r.color      = float4(0, 0, 0, 1);
    r.visibility = uint4(i.instanceId, primId, asuint(bary.yz));
    return r;
}