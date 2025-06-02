// https://developer.nvidia.com/gpugems/gpugems3/part-vi-gpu-computing/chapter-39-parallel-prefix-sum-scan-cuda

#include "PrefixSum.h"

using namespace RoseEngine;

[[vk::push_constant]]
ConstantBuffer<PrefixSumPushConstants> pushConstants;

RWByteAddressBuffer data;
RWByteAddressBuffer groupSums;
RWByteAddressBuffer globalSums;

#ifndef GROUP_SIZE
#define GROUP_SIZE 1024
#endif

groupshared uint localSums[2 * GROUP_SIZE]; ///< Temporary working buffer in shared memory for 2N elements.

[shader("compute")]
[numthreads(GROUP_SIZE, 1, 1)]
void groupScan(uint3 groupID: SV_GroupID, uint3 groupThreadID: SV_GroupThreadID) {
    const uint thid = groupThreadID.x; // Local thread ID in the range 0..N-1.
    const uint groupIdx = groupID.x;   // Group index where each group represents 2N elements.

    // Load data for group into shared memory. Each thread loads two elements.
    // Interleaved load at consecutive addresses can lead to 2x bank conflicts.
    // It's probably better to load one element into each half of the array as we do here.
    // We pad the data with zeros in shared memory if actual #elements is less than working set.

    const uint prevSum = globalSums.Load(((pushConstants.iteration&1) ^1) * sizeof(uint));
    const uint idx = 2 * GROUP_SIZE * GROUP_SIZE * pushConstants.iteration + groupIdx * (2 * GROUP_SIZE) + thid;
    localSums[thid] = (idx < pushConstants.dataSize ? data.Load(idx * 4) : 0);
    localSums[thid + GROUP_SIZE] = ((idx + GROUP_SIZE) < pushConstants.dataSize ? data.Load((idx + GROUP_SIZE) * 4) : 0);

    // Reducation phase.
    // We do log2(N)+1 iterations for d = 2^(N), 2^(N-1), .., 2, 1.
    uint offset = 1;
    for (uint d = GROUP_SIZE; d > 0; d >>= 1) {
        GroupMemoryBarrierWithGroupSync();

        if (thid < d) {
            uint ai = offset * (2 * thid + 1) - 1;
            uint bi = ai + offset;

            localSums[bi] += localSums[ai];
        }
        offset *= 2; // offset = 1, 2, ... N
    }

    GroupMemoryBarrierWithGroupSync();

    // Compute prefix sum over groups.
    // Since groups run out-of-order, we use atomics to add our group's sum to all relevent group sums.
    // This can get slow for large inputs, but for moderate sized inputs (tens to hundreds of groups) it's probably still very fast.
    // The alternative is to run an extra shader pass computing the prefix sum over the groups.
    if (thid >= groupIdx && thid < pushConstants.numGroups) {
        uint sum = localSums[2 * GROUP_SIZE - 1];
        groupSums.InterlockedAdd(thid * 4, sum);

        // One thread per group also adds the group sum to the total sum.
        if (thid == pushConstants.numGroups - 1) {
            globalSums.InterlockedAdd((pushConstants.iteration&1) * sizeof(uint), sum);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Zero out top element, this is required for down-sweep phase to work correctly.
    // Only one thread in each group does this.
    if (thid == 0)
        localSums[2 * GROUP_SIZE - 1] = 0;

    // Down-sweep phase.
    // We do log2(N)+1 iterations for d = 1, 2, 4, ..., N.
    for (uint d = 1; d <= GROUP_SIZE; d *= 2) {
        offset >>= 1; // offset = N, N/2, ..., 1

        GroupMemoryBarrierWithGroupSync();

        if (thid < d) {
            uint ai = offset * (2 * thid + 1) - 1;
            uint bi = ai + offset;

            uint tmp = localSums[ai];
            localSums[ai] = localSums[bi];
            localSums[bi] += tmp;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Write results to memory and add prev sum. Lower half first then upper half.
    if (idx < pushConstants.dataSize)
        data.Store(idx * 4, localSums[thid] + prevSum);
    if ((idx + GROUP_SIZE) < pushConstants.dataSize)
        data.Store((idx + GROUP_SIZE) * 4, localSums[thid + GROUP_SIZE] + prevSum);
}

[shader("compute")]
[numthreads(GROUP_SIZE, 1, 1)]
void finalizeGroups(uint3 groupID: SV_GroupID, uint3 groupThreadID: SV_GroupThreadID) {
    const uint thid = groupThreadID.x; // Local thread ID in the range 0..N-1.
    const uint groupIdx = groupID.x;   // Group index where each group represents N elements (skipping first 2N elements).

    uint sum = groupSums.Load((groupIdx >> 1) * 4);
    uint globalIdx = (groupIdx * GROUP_SIZE) + thid + 2 * GROUP_SIZE * (1 + GROUP_SIZE * pushConstants.iteration); // Skip first 2N elements.

    if (globalIdx < pushConstants.dataSize) {
        uint addr = globalIdx * 4;
        uint elem = data.Load(addr);
        data.Store(addr, elem + sum);
    }
}
