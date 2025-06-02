import Rose.Core.MathUtils;
#include "Tonemapper.h"

using namespace RoseEngine;

#ifndef MODE
#define MODE 0
#endif
#ifndef GAMMA_CORRECT
#define GAMMA_CORRECT 1
#endif

RWTexture2D<float4> gImage;
RWByteAddressBuffer gMax;

[[vk::push_constant]]
cbuffer PushConstants {
    float gExposure;
};

float  sqr(float x) { return x * x; }
float3 sqr(float3 x) { return x * x; }

float3 Reinhard(const float3 c) {
	//return c / (1 + c);
	const float l = luminance(c);
	const float3 tc = c / (1 + c);
	return lerp(c/(1 + l), tc, tc);
}
float3 ReinhardExtended(const float3 c, const float3 max_c) {
	return c / (1 + c) * (1 + c / sqr(lerp(max_c, 1, float3(max_c == 0))));
}

float3 ReinhardLuminance(const float3 c) {
	const float l = luminance(c);
	const float l1 = l / (1 + l);
	return c * (l1 / l);
}
float3 ReinhardExtendedLuminance(const float3 c, const float max_l) {
	const float l = luminance(c);
	const float l1 = (l / (1 + l)) * (1 + l / sqr(max_l == 0 ? 1 : max_l));
	return c * (l1 / l);
}

float uncharted2_partial1(const float x) {
  	static const float A = 0.15;
  	static const float B = 0.50;
  	static const float C = 0.10;
  	static const float D = 0.20;
  	static const float E = 0.02;
  	static const float F = 0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}
float3 uncharted2_partial(const float3 x) {
  	static const float A = 0.15;
  	static const float B = 0.50;
  	static const float C = 0.10;
  	static const float D = 0.20;
  	static const float E = 0.02;
  	static const float F = 0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}
float3 Uncharted2(const float3 c, const float max_l) {
	return uncharted2_partial(c) / uncharted2_partial1(max_l == 0 ? 1 : max_l);
}

float3 Filmic(float3 c) {
	c = max(0, c - 0.004f);
	return (c * (6.2f * c + 0.5f)) / (c * (6.2f * c + 1.7f) + 0.06f);
}

float3 ACES(float3 v) {
	// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
	static const float3x3 aces_input_matrix = {
		{ 0.59719, 0.35458, 0.04823 },
		{ 0.07600, 0.90834, 0.01566 },
		{ 0.02840, 0.13383, 0.83777 }
	};
	// ODT_SAT => XYZ => D60_2_D65 => sRGB
	static const float3x3 aces_output_matrix = {
		{  1.60475, -0.53108, -0.07367 },
		{ -0.10208,  1.10813, -0.00605 },
		{ -0.00327, -0.07276,  1.07602 }
	};
    v = mul(aces_input_matrix, v);
    const float3 a = v * (v + 0.0245786f) - 0.000090537f;
    const float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    v = a / b;
    return saturate(mul(aces_output_matrix, v));
}

float3 ApproxACES(float3 v) {
    static const float a = 2.51f;
    static const float b = 0.03f;
    static const float c = 2.43f;
    static const float d = 0.59f;
    static const float e = 0.14f;
    v *= 0.6f;
    return saturate((v*(a*v+b))/(v*(c*v+d)+e));
}

#define gMaxQuantization 16384

[shader("compute")]
[numthreads(8, 8, 1)]
void MaxReduce(uint3 index : SV_DispatchThreadID, uint group_index : SV_GroupIndex) {
    uint2 resolution;
    gImage.GetDimensions(resolution.x, resolution.y);
    if (any(index.xy >= resolution)) return;

    float4 v = float4(gImage[index.xy].rgb, 0);
	v.w = luminance(v.rgb);

	if (any(v != v) || v.w <= 0) return;

	const uint4 vi = (uint4)clamp(v*gMaxQuantization, 0, float(0xFFFFFFFF));
	uint4 prev;
	gMax.InterlockedMax(0 , vi.x);
	gMax.InterlockedMax(4 , vi.y);
	gMax.InterlockedMax(8 , vi.z);
	gMax.InterlockedMax(12, vi.w);
	// addition overflow
	//if (vi.x > 0xFFFFFFFF - prev.x) gMax.InterlockedOr(40, (1 << 0));
	//if (vi.y > 0xFFFFFFFF - prev.y) gMax.InterlockedOr(40, (1 << 1));
	//if (vi.z > 0xFFFFFFFF - prev.z) gMax.InterlockedOr(40, (1 << 2));
	//if (vi.w > 0xFFFFFFFF - prev.w) gMax.InterlockedOr(40, (1 << 3));
}

[shader("compute")]
[numthreads(8,8,1)]
void Tonemap(uint3 index : SV_DispatchThreadID) {
    uint2 extent;
    gImage.GetDimensions(extent.x, extent.y);
	if (any(index.xy >= extent)) return;

    float3 radiance = gImage[index.xy].rgb;

    radiance *= gExposure;

    const float4 maxValue = gExposure * (gMax.Load<uint4>(0)/(float)gMaxQuantization);

    switch (MODE) {
	case TonemapperMode::eReinhard:
		radiance = Reinhard(radiance);
		break;
	case TonemapperMode::eReinhardExtended:
		radiance = ReinhardExtended(radiance, maxValue.rgb);
		break;
	case TonemapperMode::eReinhardLuminance:
		radiance = ReinhardLuminance(radiance);
		break;
	case TonemapperMode::eReinhardLuminanceExtended:
		radiance = ReinhardExtendedLuminance(radiance, maxValue.w);
		break;
	case TonemapperMode::eUncharted2:
		radiance = Uncharted2(radiance, maxValue.w);
		break;
	case TonemapperMode::eFilmic:
		radiance = Filmic(radiance);
		break;
	case TonemapperMode::eACES:
		radiance = ACES(radiance);
		break;
	case TonemapperMode::eACESApprox:
		radiance = ApproxACES(radiance);
		break;
	case TonemapperMode::eViridisR:
		radiance = viridis(saturate(radiance.r));
		break;
	case TonemapperMode::eViridisLengthRGB:
        radiance = viridis(saturate(length(radiance) / (maxValue.w == 0 ? 1 : maxValue.w)));
		break;
	}

    if (GAMMA_CORRECT) radiance = rgb2srgb(radiance);

    gImage[index.xy] = float4(radiance, 1);
}