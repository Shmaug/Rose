RWTexture2D<float4> renderTarget;
Texture2D<float4> prevRenderTarget;

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 index: SV_DispatchThreadID) {
    uint2 dim;
    renderTarget.GetDimensions(dim.x, dim.y);
    if (any(index.xy >= dim))
		return;

    const float4 c0 = renderTarget[index.xy];
    const float4 c1 = prevRenderTarget[index.xy];

    const float n = 1 + c1.w;

    renderTarget[index.xy] = float4(lerp(c1.rgb, c0.rgb, 1/n), n);
}