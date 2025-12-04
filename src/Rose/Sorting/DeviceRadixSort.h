#pragma once
#include <Rose/Core/CommandContext.hpp>
#include <Rose/Core/TransientResourceCache.hpp>
#include "GPUSorting.h"

namespace RoseEngine {

struct DeviceRadixSortPushConstants {
    uint numKeys;
    uint radixShift;
    uint threadBlocks;
    uint isPartial;
};

class DeviceRadixSort {
private:
	// payload size -> (histogramPipeline, sortPipeline)
	std::tuple<ref<Pipeline>, ref<Pipeline>, ref<Pipeline>, ref<Pipeline>> pipelines;
	ref<Pipeline> initPipeline;
	ref<Pipeline> upsweepPipeline;
	ref<Pipeline> scanPipeline;
	ref<Pipeline> downsweepPipeline;
	GPUSorting::TuningParameters m_tuning;
public:
	void operator()(CommandContext& context, const BufferRange<uint>& keys, const BufferRange<uint>& payloads);
};
}
