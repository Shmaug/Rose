import Rose.Core.MathUtils;
import Rose.Scene.Scene;
import SceneRendererApp.PathTracer.Sampling;

namespace RoseEngine {

struct LambertianBSDF {
    float3 baseColor;

    __init(const Material material) {
        baseColor = material.GetBaseColor();
	}

    float3 Eval(const float3 dirIn, const float3 dirOut, out float fwdPdf, out float revPdf) {
        if (sign(dirOut.z * dirIn.z) < 0) {
            fwdPdf = 0;
            revPdf = 0;
            return 0;
        }
        fwdPdf = CosHemispherePdfW(max(0, dirOut.z * sign(dirIn.z)));
        revPdf = CosHemispherePdfW(max(0, dirIn.z * sign(dirOut.z)));
        return (baseColor / M_PI) * abs(dirOut.z);
    }

    float3 Sample(const float4 rnd, const float3 dirIn) {
        float3 dirOut = SampleCosHemisphere(rnd.xy);
        if (dirIn.z < 0)
            dirOut = -dirOut;
        return dirOut;
    }
};

}