#pragma once

#include "Random.h"
import MathUtils;

namespace RoseEngine {

float  UnormToFloat(uint  x) { return asfloat(0x3f800000 | ((x) >> 9)) - 1; }
float2 UnormToFloat(uint2 x) { return asfloat(0x3f800000 | ((x) >> 9)) - 1; }
float3 UnormToFloat(uint3 x) { return asfloat(0x3f800000 | ((x) >> 9)) - 1; }
float4 UnormToFloat(uint4 x) { return asfloat(0x3f800000 | ((x) >> 9)) - 1; }

struct RandomSampler {
	uint4 mState;

	__init(const uint seed, const uint2 index) {
        mState = uint4(index, seed, 0);
        mState = pcg4d(mState);
	}

	[mutating]
	void Skip(const uint n = 1) {
		mState.w += n;
	}

	[mutating]
	uint4 Next() {
		mState.w++;
        return pcg4d(mState);
    }

    [mutating]
    float4 NextFloat() {
        return UnormToFloat(Next());
    }
};

}