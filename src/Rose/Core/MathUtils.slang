#pragma once

static const uint UINT32_MAX = 0xFFFFFFFF;

static const float FLT_MAX             = 3.402823466e+38F;         // max value
static const float FLT_MIN             = 1.175494351e-38F;         // min normalized positive value
static const float M_E                 = 2.71828182845904523536;   // e
static const float M_LOG2E             = 1.44269504088896340736;   // log2(e)
static const float M_LOG10E            = 0.434294481903251827651;  // log10(e)
static const float M_LN2               = 0.693147180559945309417;  // ln(2)
static const float M_LN10              = 2.30258509299404568402;   // ln(10)
static const float M_PI                = 3.14159265358979323846;   // pi
static const float M_PI_2              = 1.57079632679489661923;   // pi/2
static const float M_PI_4              = 0.785398163397448309616;  // pi/4
static const float M_1_PI              = 0.318309886183790671538;  // 1/pi
static const float M_2_PI              = 0.636619772367581343076;  // 2/pi
static const float M_2_SQRTPI          = 1.12837916709551257390;   // 2/sqrt(pi)
static const float M_SQRT2             = 1.41421356237309504880;   // sqrt(2)
static const float M_SQRT1_2           = 0.707106781186547524401;  // 1/sqrt(2)
static const float M_2PI               = 6.28318530717958647693;   // 2pi
static const float M_4PI               = 12.5663706143591729539;   // 4pi
static const float M_4_PI              = 1.27323954473516268615;   // 4/pi
static const float M_1_2PI             = 0.159154943091895335769;  // 1/2pi
static const float M_1_4PI             = 0.079577471545947667884;  // 1/4pi
static const float M_SQRT3             = 1.732050807568877293527;  // sqrt(3)
static const float M_SQRTPI            = 1.77245385090551602730;   // sqrt(pi)
static const float M_1_SQRT2           = 0.707106781186547524401;  // 1/sqrt(2)

[Differentiable]
float2x2 inverse(float2x2 m, float det) {
	float invdet = 1 / det;
    float2x2 r;
    r[0][0]  = m[1][1] * invdet;
    r[1][1]  = m[0][0] * invdet;
    r[0][1] = -m[0][1] * invdet;
    r[1][0] = -m[1][0] * invdet;
    return r;
}

[Differentiable]
float2x2 inverse(float2x2 m) {
    return inverse(m, determinant(m));
}

[Differentiable]
float3x3 inverse(float3x3 m, float det) {
	float invdet = 1 / det;
    float3x3 r;
    r[0][0] = (m[1][1] * m[2][2] - m[2][1] * m[1][2]) * invdet;
    r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invdet;
    r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invdet;
    r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * invdet;
    r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invdet;
    r[1][2] = (m[1][0] * m[0][2] - m[0][0] * m[1][2]) * invdet;
    r[2][0] = (m[1][0] * m[2][1] - m[2][0] * m[1][1]) * invdet;
    r[2][1] = (m[2][0] * m[0][1] - m[0][0] * m[2][1]) * invdet;
    r[2][2] = (m[0][0] * m[1][1] - m[1][0] * m[0][1]) * invdet;
    return r;
}

[Differentiable]
float3x3 inverse(float3x3 m) {
    return inverse(m, determinant(m));
}

float3 OffsetRayOrigin(const float3 P, const float3 Ng) {
    // https://research.nvidia.com/publication/2019-03_fast-and-robust-method-avoiding-self-intersection
    float int_scale = 256.0;
    int3 of_i = int3(int_scale * Ng);
    float origin = 1.0 / 32.0;
    float float_scale = 1.0 / 65536.0;
    return float3(abs(P.x) < origin ? P.x + float_scale * Ng.x : asfloat(asint(P.x) + ((P.x < 0.0) ? -of_i.x : of_i.x)),
                  abs(P.y) < origin ? P.y + float_scale * Ng.y : asfloat(asint(P.y) + ((P.y < 0.0) ? -of_i.y : of_i.y)),
                  abs(P.z) < origin ? P.z + float_scale * Ng.z : asfloat(asint(P.z) + ((P.z < 0.0) ? -of_i.z : of_i.z)));
}

float3 OffsetRayOrigin(const float3 P, const float3 Ng, const float3 dir) {
    return OffsetRayOrigin(P, dot(Ng, dir) < 0 ? -Ng : Ng);
}

#include "MathUtils.h"