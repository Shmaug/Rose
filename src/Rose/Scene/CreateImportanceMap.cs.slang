import Rose.Core.MathUtils;

Texture2D<float4> image;
RWTexture2D<float> importanceMap;
uniform uint2 dim;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID) {
    if (any(id.xy >= dim))
		return;

    const float f = RoseEngine::luminance(image[id.xy].rgb);
    const float j = sin(M_PI * (float(id.y) + 0.5) / float(dim.y));
    importanceMap[id.xy] = f * j;
}