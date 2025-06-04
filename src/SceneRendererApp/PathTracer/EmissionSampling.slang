import Rose.Scene.Scene;
import Rose.Core.MathUtils;
import Sampling;

using namespace RoseEngine;

extension RoseEngine::Scene {
	// uniformly samples a light instance and primitive index, then uniformly samples the primitive's area.
	// pdf is area measure except for background vertices
	SceneVertex SampleEmission(float4 rnd, out float pdf) {
		SceneVertex v = {};

		if (any(backgroundColor > 0)) {
			if (emissiveInstanceCount == 0 || rnd.w < backgroundSampleProbability) {
				// sample background light

				float3 dir;
				if (backgroundImage < imageCount) {
                    const float2 uv = SampleTexel(backgroundImportanceMap, rnd.xy, pdf);
                    dir = sphuv2xyz(uv);
                    // jacobian from sphuv2xyz
					pdf /= (2 * M_PI * M_PI * sqrt(1 - dir.y*dir.y));
				} else {
					dir = SampleUniformSphere(rnd.xy);
					pdf = 1 / (4 * M_PI);
				}

                if (emissiveInstanceCount > 0)
                    pdf *= backgroundSampleProbability;
                v = LoadBackgroundVertex(dir);
                return v;
			}

            // remap rnd.w from [mBackgroundSampleProbability,1] to [0,1]
            if (emissiveInstanceCount > 0)
				rnd.w = (rnd.w - backgroundSampleProbability) / (1 - backgroundSampleProbability);
		}

        if (emissiveInstanceCount == 0) {
			pdf = 0;
			return v;
		}

        // uniformly select an emissive instance
        const uint instanceIndex = emissiveInstances[min(uint(rnd.z * emissiveInstanceCount), emissiveInstanceCount-1)];
		if (instanceIndex >= instanceCount) {
			pdf = 0;
			return v;
        }
        pdf = 1 / (float)emissiveInstanceCount;
		if (any(backgroundColor > 0))
			pdf *= 1 - backgroundSampleProbability;

		const InstanceHeader instance = instances[instanceIndex];

		// uniformly select a triangle in the mesh
		const uint primitiveIndex = min(uint(rnd.w * instance.triangleCount), instance.triangleCount - 1);
		pdf *= 1 / (float)instance.triangleCount;

        const PackedSceneHit hit = PackedSceneHit(instanceIndex, primitiveIndex, SampleUniformTriangle(rnd.xy));
        v = LoadSceneVertex<false>(hit, instance, meshes[instance.meshIndex]);
		pdf *= 1 / v.faceArea;
		return v;
	}

    float EvalBackgroundSamplePdf(const float3 dir) {
		if (any(backgroundColor > 0)) {
			float pdf = emissiveInstanceCount > 0 ? backgroundSampleProbability : 1;
			if (backgroundImage < imageCount) {
				pdf *= SampleTexelPdf(backgroundImportanceMap, xyz2sphuv(dir));
				// jacobian from SphericalUVToCartesian
				pdf /= (2 * M_PI * M_PI * sqrt(1 - dir.y*dir.y));
			} else {
				pdf *= 1 / (4 * M_PI);
            }
            return pdf;
		} else {
			return 0;
		}
	}

	float EvalSurfaceSamplePdf(const SceneVertex v) {
        if (emissiveInstanceCount == 0) {
			return 0;
		}

        float pdf = 1;
        if (any(backgroundColor > 0))
			pdf *= 1 - backgroundSampleProbability;
        pdf *= 1 / (float)emissiveInstanceCount;

		const InstanceHeader instance = instances[v.instanceIndex];
		pdf *= 1 / (float)instance.triangleCount;
		pdf *= 1 / v.faceArea;
		return pdf;
	}
};