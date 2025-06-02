import Rose.Scene.Scene;
import Rose.Core.MathUtils;
import Sampling;

using namespace RoseEngine;

struct SceneEmissionSampler {
    StructuredBuffer<uint> lightIds;
    uint lightCount;
    float backgroundSampleProbability;

	// uniformly samples a light instance and primitive index, then uniformly samples the primitive's area.
	// pdf is area measure except for background vertices
	SceneVertex Sample(const Scene scene, float4 rnd, out float pdf) {
		SceneVertex v = {};

		if (any(scene.backgroundColor > 0)) {
			if (lightCount == 0 || rnd.w < backgroundSampleProbability) {
				// sample background light

				float3 dir;
				if (scene.backgroundImage < scene.imageCount) {
					const float2 uv = SampleTexel(scene.images[scene.backgroundImage], rnd.xy, pdf);
                    dir = sphuv2xyz(uv);

                    float3x3 J = 0;
                    J[0] = __fwd_diff(sphuv2xyz)(diffPair(uv, float2(1, 0))).getDifferential();
                    J[1] = __fwd_diff(sphuv2xyz)(diffPair(uv, float2(0, 1))).getDifferential();
                    printf("detJ: %f    actual: %f\n", determinant(J), 2 * M_PI * M_PI * sqrt(1 - dir.y * dir.y));

                    // jacobian from sphuv2xyz
					pdf /= (2 * M_PI * M_PI * sqrt(1 - dir.y*dir.y));
				} else {
					dir = SampleUniformSphere(rnd.xy);
					pdf = 1 / (4 * M_PI);
				}

				if (lightCount > 0)
					pdf *= backgroundSampleProbability;
				v.position = dir;
				v.faceArea = -1;
				return v;
			}

			// remap rnd.w from [mBackgroundSampleProbability,1] to [0,1]
			if (lightCount > 0)
				rnd.w = (rnd.w - backgroundSampleProbability) / (1 - backgroundSampleProbability);
		}

		if (lightCount == 0) {
			pdf = 0;
			return v;
		}

		// uniformly select an instance containing an emissive material
		const uint instanceIndex = lightIds[min(uint(rnd.z * lightCount), lightCount-1)];
		if (instanceIndex >= scene.instanceCount) {
			pdf = 0;
			return v;
		}
		pdf = 1 / (float)lightCount;
		if (any(scene.backgroundColor > 0))
			pdf *= 1 - backgroundSampleProbability;

		const InstanceHeader instance = scene.instances[instanceIndex];

		// uniformly select a triangle in the mesh
		const uint primitiveIndex = min(uint(rnd.w * instance.triangleCount), instance.triangleCount - 1);
		pdf *= 1 / (float)instance.triangleCount;

        v = LoadSceneVertex(scene, instanceIndex, instance, scene.meshes[instance.meshIndex], primitiveIndex, SampleUniformTriangle(rnd.xy));
		pdf *= 1 / v.faceArea;
		return v;
	}

	float Pdf(const Scene scene, const SceneVertex v) {
		if (v.isBackground) {
			if (any(scene.backgroundColor > 0)) {
				float pdf = lightCount > 0 ? backgroundSampleProbability : 1;
				if (scene.backgroundImage < scene.imageCount) {
					pdf *= SampleTexelPdf(scene.images[scene.backgroundImage], xyz2sphuv(v.position));
					// jacobian from SphericalUVToCartesian
					pdf /= (2 * M_PI * M_PI * sqrt(1 - v.position.y*v.position.y));
				} else {
					pdf *= 1 / (4 * M_PI);
				}
			} else {
				return 0;
			}
		}

		if (lightCount == 0) {
			return 0;
		}

		float pdf = scene.backgroundImage < scene.imageCount ? 1 - backgroundSampleProbability : 1;
		pdf *= 1 / (float)lightCount;

		const InstanceHeader instance = scene.instances[v.instanceIndex];
		pdf *= 1 / (float)instance.triangleCount;
		pdf *= 1 / v.faceArea;
		return pdf;
	}
};