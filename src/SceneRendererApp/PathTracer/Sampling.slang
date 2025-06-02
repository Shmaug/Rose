import Rose.Core.MathUtils;

using namespace RoseEngine;

float2 SampleUniformTriangle(const float2 uv) {
    const float a = sqrt(uv.x);
    return float2(1 - a, a * uv.y);
}
float3 SampleUniformSphere(const float2 uv) {
	const float z = uv.y * 2 - 1;
    const float r = sqrt(max(1 - z * z, 0));
    const float phi = 2 * M_PI * uv.x;
	return float3(r * cos(phi), r * sin(phi), z);
}

float2 SampleConcentricDisc(const float2 uv) {
	// from pbrtv3, sampling.cpp line 113

    // Map uniform random numbers to $[-1,1]^2$
    const float2 uOffset = 2 * float2(uv.x,uv.y) - 1;

    // Handle degeneracy at the origin
    if (uOffset.x == 0 && uOffset.y == 0) return 0;

    // Apply concentric mapping to point
    float theta, r;
    if (abs(uOffset.x) > abs(uOffset.y)) {
        r = uOffset.x;
        theta = M_PI/4 * (uOffset.y / uOffset.x);
    } else {
        r = uOffset.y;
        theta = M_PI/2 - M_PI/4 * (uOffset.x / uOffset.y);
    }
    return r * float2(cos(theta), sin(theta));
}
float ConcentricDiscPdfA() {
    return 1.0 / M_PI;
}

float3 SampleCosHemisphere(const float2 uv) {
    const float2 xy = SampleConcentricDisc(uv);
	return float3(xy, sqrt(max(0, 1 - dot(xy,xy))));
}
float CosHemispherePdfW(const float cosTheta) {
	return max(cosTheta, 0.f) / M_PI;
}

float2 SampleTexel(Texture2D<float4> image, float2 rnd, out float pdf, const uint maxIterations = 10) {
    uint2 imageExtent;
    uint levelCount;
    image.GetDimensions(0, imageExtent.x, imageExtent.y, levelCount);

    pdf = 1;
    int2 coord = 0;
    uint2 lastExtent = 1;
    for (uint i = 1; i < min(maxIterations + 1, levelCount - 1); i++) {
        const uint level = levelCount - 1 - i;
        uint tmp;
        uint2 extent;
        image.GetDimensions(level, extent.x, extent.y, tmp);
        const float inv_h = 1 / (float)extent.y;

        coord *= int2(extent / lastExtent);

        float4 p = 0;
        if (extent.x - coord.x > 1) {
            const float sy = sin(M_PI * (coord.y + 0.5f) * inv_h);
            p[0] = luminance(image.Load(int3(coord + int2(0, 0), (int)level)).rgb) * sy;
            p[1] = luminance(image.Load(int3(coord + int2(1, 0), (int)level)).rgb) * sy;
        }
        if (extent.y - coord.y > 1) {
            const float sy = sin(M_PI * (coord.y + 1.5f) * inv_h);
            p[2] = luminance(image.Load(int3(coord + int2(0, 1), (int)level)).rgb) * sy;
            p[3] = luminance(image.Load(int3(coord + int2(1, 1), (int)level)).rgb) * sy;
        }
        const float sum = dot(p, 1);
        if (sum < 1e-6) continue;
        p /= sum;

        for (int j = 0; j < 4; j++) {
            if (j == 3 || rnd.x < p[j]) {
                coord += int2(j % 2, j / 2);
                pdf *= p[j];
                rnd.x /= p[j];
                break;
            }
            rnd.x -= p[j];
        }

        lastExtent = extent;
    }

    pdf *= lastExtent.x * lastExtent.y;

    return (float2(coord) + rnd) / float2(lastExtent);
}
float SampleTexelPdf(Texture2D<float4> image, const float2 uv, const uint maxIterations = 10) {
    uint2 imageExtent;
    uint levelCount;
    image.GetDimensions(0, imageExtent.x, imageExtent.y, levelCount);

    float pdf = 1;
    uint2 lastExtent = 1;
    for (uint i = 1; i < min(maxIterations + 1, levelCount - 1); i++) {
        const uint level = levelCount - 1 - i;
        uint tmp;
        uint2 size;
        image.GetDimensions(level, size.x, size.y, tmp);
        const float inv_h = 1 / (float)size.y;

        const int2 uvi = int2(float2(size) * uv);
        const int2 coord = (uvi / 2) * 2;
        const uint2 o = min(uvi - coord, 1);

        float4 p = 0;
        if (size.x - coord.x > 1) {
            const float sy = sin(M_PI * (coord.y + 0.5f) * inv_h);
            p[0] = luminance(image.Load(int3(coord + int2(0, 0), (int)level)).rgb) * sy;
            p[1] = luminance(image.Load(int3(coord + int2(1, 0), (int)level)).rgb) * sy;
        }
        if (size.y - coord.y > 1) {
            const float sy = sin(M_PI * (coord.y + 1.5f) * inv_h);
            p[2] = luminance(image.Load(int3(coord + int2(0, 1), (int)level)).rgb) * sy;
            p[3] = luminance(image.Load(int3(coord + int2(1, 1), (int)level)).rgb) * sy;
        }
        const float sum = dot(p, 1);
        if (sum < 1e-6) continue;
        p /= sum;

        pdf *= p[o.y * 2 + o.x];

        lastExtent = size;
    }

    pdf *= lastExtent.x * lastExtent.y;

    return pdf;
}
