import Rose.Core.MathUtils;
import Rose.Scene.Scene;
import SceneRendererApp.PathTracer.Sampling;

namespace RoseEngine {

struct BSDFEval {
    float3 f;
    float fwdPdf;
    float revPdf;
};
struct BSDFSample {
    float3 dirOut;
    uint   sampledLobe;
    float  lobeRoughness;
    float  lobeEta;
};

static const uint kInvalidLobeIndex = 0;

struct LambertianBSDF {
    float3 baseColor;

    __init(const Material material, const float3 localDirIn) {
        baseColor = material.GetBaseColor();
	}

    bool Connectable() { return true; }

    BSDFEval Eval(const float3 dirIn, const float3 dirOut) {
        BSDFEval f = {};
        if (sign(dirOut.z * dirIn.z) < 0) {
            f.f = 0;
            f.fwdPdf = 0;
            f.revPdf = 0;
            return f;
        }
        f.f = (baseColor / M_PI) * abs(dirOut.z);
        f.fwdPdf = CosHemispherePdfW(max(0, dirOut.z * sign(dirIn.z)));
        f.revPdf = CosHemispherePdfW(max(0, dirIn.z * sign(dirOut.z)));
        return f;
    }

    BSDFSample Sample(const float4 rnd, const float3 dirIn) {
        BSDFSample s = {};
        s.dirOut = SampleCosHemisphere(rnd.xy) * sign(dirIn.z);
        s.sampledLobe = 1;
        s.lobeRoughness = 1;
        s.lobeEta = 0;
        return s;
    }
};

}