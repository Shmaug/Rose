RWTexture2D<float4> color;
Texture2D<uint4>    visibility;

uniform float3 highlightColor;
uniform uint   selected;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 index: SV_DispatchThreadID) {
    uint2 extent;
    uint count;
    visibility.GetDimensions(0, extent.x, extent.y, count);
    if (any(index.xy >= extent)) return;

    const bool v0 = visibility[index.xy].x == selected;

	float f = 0;

    const int r = 2;
    for (int x = -r; x <= r; x++) {
        for (int y = -r; y <= r; y++) {
            const int2 i = int2(index.xy) + int2(x, y);
            if (any(i < 0) || any(i >= extent) || (x == 0 && y == 0))
                continue;

            const bool vxy = visibility[i].x == selected;
            if (vxy != v0) {
                f += 1/float(x * x + y * y);
			}
        }
    }

    if (v0) {
        color[index.xy].rgb += highlightColor * 0.05;
	}

    if (f > 0) {
        color[index.xy].rgb = lerp(color[index.xy].rgb, highlightColor, saturate(f));
    }
}