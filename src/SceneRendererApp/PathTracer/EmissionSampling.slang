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
					const float2 uv = SampleTexel(images[backgroundImage], rnd.xy, pdf);
                    dir = sphuv2xyz(uv);
                    // jacobian from sphuv2xyz
					pdf /= (2 * M_PI * M_PI * sqrt(1 - dir.y*dir.y));
				} else {
					dir = SampleUniformSphere(rnd.xy);
					pdf = 1 / (4 * M_PI);
				}

                if (emissiveInstanceCount > 0)
					pdf *= backgroundSampleProbability;
				v.position = dir;
				v.faceArea = -1;
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

        // uniformly select an instance containing an emissive material
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

        v = LoadSceneVertex(this, instanceIndex, instance, meshes[instance.meshIndex], primitiveIndex, SampleUniformTriangle(rnd.xy));
		pdf *= 1 / v.faceArea;
		return v;
	}

	float EmissionPdf(const SceneVertex v) {
		if (v.isBackground) {
			if (any(backgroundColor > 0)) {
				float pdf = emissiveInstanceCount > 0 ? backgroundSampleProbability : 1;
				if (backgroundImage < imageCount) {
					pdf *= SampleTexelPdf(images[backgroundImage], xyz2sphuv(v.position));
					// jacobian from SphericalUVToCartesian
					pdf /= (2 * M_PI * M_PI * sqrt(1 - v.position.y*v.position.y));
				} else {
					pdf *= 1 / (4 * M_PI);
				}
			} else {
				return 0;
			}
		}

        if (emissiveInstanceCount == 0) {
			return 0;
		}

        float pdf = any(backgroundColor > 0) ? 1 - backgroundSampleProbability : 1;
        pdf *= 1 / (float)emissiveInstanceCount;

		const InstanceHeader instance = instances[v.instanceIndex];
		pdf *= 1 / (float)instance.triangleCount;
		pdf *= 1 / v.faceArea;
		return pdf;
	}
};