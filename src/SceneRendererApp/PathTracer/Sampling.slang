import Rose.Core.MathUtils;

namespace RoseEngine {

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

float2 SampleTexel(Texture2D<float> image, float2 rnd, out float pdf, const uint maxIterations = 16)
{
    uint2 imageExtent;
    uint levelCount;
    image.GetDimensions(0, imageExtent.x, imageExtent.y, levelCount);

    pdf = 1;
    int2 coord = 0;
    uint2 extent;
    for (uint i = 1; i < min(maxIterations + 1, levelCount - 1); i++)
    {
		coord *= 2;

        const uint level = levelCount - 1 - i;
        extent = max(1u, imageExtent / (1 << level));

        const bool2 b = (coord + 1) < extent;

        float4 p = 0;
		p[0] = image.Load(int3(coord + int2(0, 0), (int)level));
        if (b.x) p[1] = image.Load(int3(coord + int2(1, 0), (int)level));
        if (b.y) p[2] = image.Load(int3(coord + int2(0, 1), (int)level));
		if (all(b)) p[3] = image.Load(int3(coord + int2(1, 1), (int)level));

        const float ps = dot(p, 1);
        p = ps > 0 ? p / ps : 0;

        const float px = p[1] + p[3];
        if (rnd.x < px)
        {
            coord.x++;
            rnd.x /= px;
            pdf *= px;
		}
        else
        {
            rnd.x = (rnd.x - px) / (1 - px);
            pdf *= 1 - px;
        }

        const float py = p[2] + p[3];
        if (rnd.y < py)
        {
            coord.y++;
            rnd.y /= py;
			pdf *= py;
        }
        else
        {
            rnd.y = (rnd.y - py) / (1 - py);
			pdf *= 1 - py;
        }
    }

    pdf *= extent.x * extent.y;

    return (float2(coord) + rnd) / float2(extent);
}
float SampleTexelPdf(Texture2D<float> image, const float2 uv, const uint maxIterations = 16)
{
    uint2 imageExtent;
    uint levelCount;
    image.GetDimensions(0, imageExtent.x, imageExtent.y, levelCount);

    float pdf = 1;
    uint2 extent;
    for (uint i = 1; i < min(maxIterations + 1, levelCount - 1); i++)
    {
        const uint level = levelCount - 1 - i;
        extent = max(1u, imageExtent / (1 << level));

        const int2 uvi = int2(float2(extent) * uv);
        const int2 coord = (uvi / 2) * 2;

        const bool2 b = (coord + 1) < extent;

        float4 p = 0;
        p[0] = image.Load(int3(coord + int2(0, 0), (int)level));
        if (b.x) p[1] = image.Load(int3(coord + int2(1, 0), (int)level));
        if (b.y) p[2] = image.Load(int3(coord + int2(0, 1), (int)level));
        if (all(b)) p[3] = image.Load(int3(coord + int2(1, 1), (int)level));

        const float ps = dot(p, 1);
        p = ps > 0 ? p / ps : 0;

        const float px = p[1] + p[3];
        if (uvi.x > coord.x)
        {
            pdf *= px;
        }
        else
        {
            pdf *= 1 - px;
        }

        const float py = p[2] + p[3];
        if (uvi.y > coord.y)
        {
            pdf *= py;
        }
        else
        {
            pdf *= 1 - py;
        }
    }

    pdf *= extent.x * extent.y;

    return pdf;
}
}