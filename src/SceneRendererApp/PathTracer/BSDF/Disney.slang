import Rose.Core.MathUtils;
import Rose.Scene.Scene;
import SceneRendererApp.PathTracer.Microfacet;
import SceneRendererApp.PathTracer.Sampling;

using namespace RoseEngine;

inline float sqr(const float x) { return x * x; }
inline float pow5(const float x) { return sqr(sqr(x)) * x; }

struct BSDFEval {
    float3 f;
    float fwdPdf;
    float revPdf;
};
struct BSDFSample {
    float3 localDirOut;
    uint   sampledLobe;
    float  lobeRoughness;
    float  lobeEta;
};

enum EvalFlags {
	eNone        = 0,
	eAdjoint     = 1,
	eFresnelOnly = 2
};
static const EvalFlags kEvalFlags = EvalFlags::eNone;

struct DisneyBSDF {
    float diffuse_weight;
    float sheen_weight;
	float specular_weight;
	float clearcoat_weight;
	float glass_weight;

    float prob_norm;
	property float diffuse_prob   { get { return diffuse_weight * prob_norm; } }
	property float specular_prob  { get { return specular_weight * prob_norm; } }
	property float clearcoat_prob { get { return clearcoat_weight * prob_norm; } }
	property float glass_prob     { get { return glass_weight * prob_norm; } }

    float aspect;
	float min_alpha;
    float alpha;
	float alpha_x;
	float alpha_y;
    float alpha_c;
    float3 Ctint;
    float eta;

    float3 localDirIn;
    float3 localDirOut;
    float3 half_vector;
    float h_dot_in;
    float h_dot_out;

    property float n_dot_in  { get { return abs(localDirIn.z); } }
    property float n_dot_out { get { return abs(localDirOut.z); } }
    property float n_dot_h   { get { return abs(half_vector.z); } }

	enum LobeFlags {
		eDiffuse   = 1,
		eSpecular  = 2,
		eClearcoat = 4,
        eGlass     = 8,
        eAll       = 0xF
	};

    __init(const Material sd, const float3 localDirIn) {
        const float m = sd.GetMetallic();
        const float t = sd.GetTransmission();
        const float s = sd.GetSpecular();
        const float mt = (1 - t) * (1 - m);
        diffuse_weight   = lerp(0, 1 - s, mt);
		specular_weight  = lerp(1, s, mt);
		glass_weight     = lerp(t, 0, m);
		clearcoat_weight = sd.GetClearcoat();

        this.localDirIn = localDirIn;

		// only glass on the inside
        if (localDirIn.z < 0) {
            diffuse_weight   = 0;
            specular_weight  = 0;
            clearcoat_weight = 0;
            if (glass_weight > 0)
                glass_weight = 1;
        }

        const float w_sum = diffuse_weight + specular_weight + glass_weight + clearcoat_weight;
        prob_norm = w_sum > 0 ? 1 / w_sum : 0;

        aspect = 1;//sqrt(1 - sd.GetAnisotropic() * 0.9);
        min_alpha = 1e-4;
        alpha = sqr(sd.GetRoughness());
        alpha_x = max(min_alpha, alpha / aspect);
        alpha_y = max(min_alpha, alpha * aspect);
        alpha_c = lerp(0.1, 0.001, 0.5/*sd.GetClearcoatGloss()*/);
        const float3 baseColor = sd.GetBaseColor();
        Ctint = luminance(baseColor) > 1e-2 ? baseColor / luminance(baseColor) : 1;

        eta = localDirIn.z > 0 ? sd.GetIor() : 1 / sd.GetIor();
    }

	float3 EvalDiffuse(const Material sd, out float fwdPdf, out float revPdf) {
		const float Fd90 = float(0.5) + 2 * sd.GetRoughness() * h_dot_out * h_dot_out;
		const float schlick_n_dot_out = pow5(1 - n_dot_out);
		const float schlick_n_dot_in  = pow5(1 - n_dot_in);
		const float schlick_h_dot_out = pow5(1 - h_dot_out);
		const float base_diffuse = (1 + (Fd90 - 1) * schlick_n_dot_out) * (1 + (Fd90 - 1) * schlick_n_dot_in);

		// The subsurface model
		// Disney's hack to increase the response at grazing angle
		const float Fss90 = h_dot_out * h_dot_out * sd.GetRoughness();
		const float Fss = (1 + (Fss90 - 1) * schlick_n_dot_out) * (1 + (Fss90 - 1) * schlick_n_dot_in);
		// Lommel-Seeliger law (modified/rescaled)
		const float ss = float(1.25) * (Fss * (1 / (n_dot_out + n_dot_in) - float(0.5)) + float(0.5));

		fwdPdf = CosHemispherePdfW(n_dot_out);
		revPdf = CosHemispherePdfW(n_dot_in);

		return sd.GetBaseColor() * (lerp(base_diffuse, ss, 0.1/*sd.GetSubsurface()*/) / M_PI) * n_dot_out;
    }

    float3 EvalSpecular(const Material sd, out float fwdPdf, out float revPdf) {
        const float spec_f0 = sqr(eta - 1) / sqr(eta + 1);
        const float3 spec_color = 1;//lerp(1, Ctint, sd.GetSpecularTint());
        const float3 Cspec0 = lerp(sd.GetSpecular() * spec_f0 * spec_color, sd.GetBaseColor(), sd.GetMetallic());

        const float3 F    = FresnelSchlick(Cspec0, h_dot_out);
        const float D     = GTR2(half_vector, alpha_x, alpha_y);
        const float G_in  = SmithMaskingGTR2(localDirIn , alpha_x, alpha_y);
        const float G_out = SmithMaskingGTR2(localDirOut, alpha_x, alpha_y);
        const float G = G_in * G_out;

        fwdPdf = D * G_in  / (4 * n_dot_in);
        revPdf = D * G_out / (4 * n_dot_out);
        if (bool(uint(kEvalFlags) & uint(EvalFlags::eFresnelOnly)))
			return F;
		else
       		return F * D * G / (4 * n_dot_in);
    }

    float3 EvalClearcoat(const Material sd, out float fwdPdf, out float revPdf) {
        const float Fc = FresnelSchlick(float(0.04), h_dot_out);
        // Generalized Trowbridge-Reitz distribution
        const float Dc = GTR1(n_dot_h, alpha_c);
        // SmithG with fixed alpha
        const float Gc_in  = SmithMaskingGTR1(localDirIn);
        const float Gc_out = SmithMaskingGTR1(localDirOut);
        const float Gc = Gc_in * Gc_out;

        fwdPdf = Dc * n_dot_h / (4 * h_dot_out);
        revPdf = Dc * n_dot_h / (4 * h_dot_in);
        return Fc * Dc * Gc / (4 * n_dot_in);
    }
    float3 EvalGlass(const Material sd, out float fwdPdf, out float revPdf) {
		const float F     = FresnelDielectric(h_dot_in, eta);
		const float Fo    = FresnelDielectric(h_dot_out, 1/eta);
		const float D     = GTR2(half_vector, alpha_x, alpha_y);
		const float G_in  = SmithMaskingGTR2(localDirIn , alpha_x, alpha_y);
		const float G_out = SmithMaskingGTR2(localDirOut, alpha_x, alpha_y);
		const float G = G_in * G_out;

        if (localDirIn.z * localDirOut.z < 0) {
			// refraction
            const float sqrt_denom = h_dot_in + eta * h_dot_out;
            const float eta_factor = bool(uint(kEvalFlags) & uint(EvalFlags::eAdjoint)) ? (1 / sqr(eta)) : 1;
            const float dh_dout = sqr(eta)   * abs(h_dot_out) / sqr(sqrt_denom);
            const float dh_in   = sqr(1/eta) * abs(h_dot_in)  / sqr(sqrt_denom);
            fwdPdf = (1 - F)  * D * G_in  * abs(dh_dout * h_dot_in  / n_dot_in);
            revPdf = (1 - Fo) * D * G_out * abs(dh_in   * h_dot_out / n_dot_out);
            if (bool(uint(kEvalFlags) & uint(EvalFlags::eFresnelOnly)))
                return sqrt(sd.GetBaseColor()) * ((sqr(eta) * eta_factor) * (1 - F));
			else
				return sqrt(sd.GetBaseColor()) * (((sqr(eta)*eta_factor) * (1 - F) * D * G * abs(h_dot_out * h_dot_in)) / (n_dot_in * sqr(sqrt_denom)));
        } else {
			// reflection
            fwdPdf = F  * D * G_in  / (4 * abs(n_dot_in));
            revPdf = Fo * D * G_out / (4 * abs(n_dot_out));
            if (bool(uint(kEvalFlags) & uint(EvalFlags::eFresnelOnly)))
                return sd.GetBaseColor() * F;
			else
				return sd.GetBaseColor() * ((F * D * G) / (4 * abs(n_dot_in)));
		}
    }

    [mutating]
    float3 Eval(const Material sd, const float3 localDirOut_, out float fwdPdf, out float revPdf, const LobeFlags lobes = LobeFlags::eAll) {
        localDirOut = localDirOut_;
        if (localDirIn.z * localDirOut.z < 0) {
            half_vector = normalize(localDirIn + localDirOut * eta);
        } else {
            half_vector = normalize(localDirIn + localDirOut);
        }
        // make half vector on the same side as localDirIn
        if (half_vector.z * localDirIn.z < 0) {
            half_vector = -half_vector;
        }
        h_dot_in  = dot(half_vector, localDirIn);
        h_dot_out = dot(half_vector, localDirOut);

        float3 f = 0;
        fwdPdf = 0;
        revPdf = 0;

        float lobePdfFwd, lobePdfRev;

        if (bool(uint(lobes) & uint(LobeFlags::eGlass)) && glass_weight > 0) {
            f += glass_weight * EvalGlass(sd, lobePdfFwd, lobePdfRev);
            fwdPdf += glass_prob * lobePdfFwd;
            revPdf += glass_prob * lobePdfRev;
        }

        if (localDirIn.z >= 0 && localDirOut.z >= 0) {
            // Diffuse
            if (bool(uint(lobes) & uint(LobeFlags::eDiffuse)) && diffuse_weight > 0) {
                f += diffuse_weight * EvalDiffuse(sd, lobePdfFwd, lobePdfRev);
                fwdPdf += diffuse_prob * lobePdfFwd;
                revPdf += diffuse_prob * lobePdfRev;
            }

            // Metallic
            if (bool(uint(lobes) & uint(LobeFlags::eSpecular)) && specular_weight > 0) {
                f += specular_weight * EvalSpecular(sd, lobePdfFwd, lobePdfRev);
                fwdPdf += specular_prob * lobePdfFwd;
                revPdf += specular_prob * lobePdfRev;
            }

            // Clearcoat
            if (bool(uint(lobes) & uint(LobeFlags::eClearcoat)) && clearcoat_weight > 0) {
                f += clearcoat_weight * EvalClearcoat(sd, lobePdfFwd, lobePdfRev);
                fwdPdf += clearcoat_prob * lobePdfFwd;
                revPdf += clearcoat_prob * lobePdfRev;
            }
        }

		return f;
    }

	[mutating]
    BSDFSample Sample(const Material sd, const float4 rnd, const bool adjoint) {
        BSDFSample r;
        if (rnd.z <= glass_prob + specular_prob) {
            // glass / specular

            half_vector = SampleVisibleNormals(localDirIn, alpha_x, alpha_y, rnd.xy);
            if (half_vector.z < 0) {
                half_vector = -half_vector;
            }

            h_dot_in = dot(half_vector, localDirIn);
            float F = FresnelDielectric(h_dot_in, eta);
            if (rnd.z <= glass_prob && rnd.z > F) {
                // Refraction
                float h_dot_out_sq = 1 - (1 - sqr(h_dot_in)) / sqr(eta);
                if (h_dot_out_sq <= 0) {
                    r.sampledLobe = 0;
                    r.localDirOut = 0;
                    return r;
                }
                // flip half_vector if needed
                if (h_dot_in < 0) {
                    half_vector = -half_vector;
                }
                h_dot_out = sqrt(h_dot_out_sq);
                localDirOut = -localDirIn / eta + (abs(h_dot_in) / eta - h_dot_out) * half_vector;
                r.lobeEta = eta;
            } else {
                // Reflection
                localDirOut = normalize(-localDirIn + 2 * h_dot_in * half_vector);
                h_dot_out = dot(localDirOut, half_vector);
                r.lobeEta = 0;
            }
            r.localDirOut = localDirOut;
			r.sampledLobe = (uint)LobeFlags::eGlass;
            r.lobeRoughness = sd.GetRoughness();
        } else if (rnd.z <= glass_prob + specular_prob + clearcoat_prob) {
            // clearcoat

            float alpha2 = sqr(alpha_c);
            float cos_h_elevation = sqrt(max(0, (1 - pow(alpha2, 1 - rnd.x)) / (1 - alpha2)));
            float sin_h_elevation = sqrt(max(1 - sqr(cos_h_elevation), 0));
            float h_azimuth = 2 * M_PI * rnd.y;
            half_vector = float3(
                sin_h_elevation * cos(h_azimuth),
                sin_h_elevation * sin(h_azimuth),
                cos_h_elevation);

            h_dot_in = dot(half_vector, localDirIn);
            localDirOut = normalize(-localDirIn + 2 * h_dot_in * half_vector);
            h_dot_out = dot(half_vector, localDirOut);

            r.localDirOut = localDirOut;
            r.lobeEta = 0;
            r.lobeRoughness = alpha_c;
            r.sampledLobe = (uint)LobeFlags::eClearcoat;
        } else {
            // diffuse
            if (diffuse_prob <= 0)
                return r;

            localDirOut = SampleCosHemisphere(rnd.xy);
            half_vector = normalize(localDirIn + localDirOut);
            h_dot_in  = dot(half_vector, localDirIn);
            h_dot_out = dot(half_vector, localDirOut);

            r.localDirOut = localDirOut;
            r.lobeEta = 0;
            r.lobeRoughness = 1;
            r.sampledLobe = (uint)LobeFlags::eDiffuse;
        }
        return r;
	}
}
