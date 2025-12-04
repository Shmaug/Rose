// Ported from VkRadixSort by Mirco Werner: https://github.com/MircoWerner/VkRadixSort

#include "RadixSort.h"

using namespace RoseEngine;

#ifndef SUBGROUP_SIZE
#define SUBGROUP_SIZE 32 // 32 NVIDIA; 64 AMD
#endif

// ---------- Key IO ---------- //

// [histogram_of_workgroup_0 | histogram_of_workgroup_1 | ... ]
RWStructuredBuffer<uint> g_histograms;// |g_histograms| = RADIX_SORT_BINS * #WORKGROUPS = RADIX_SORT_BINS * g_num_workgroups

[[vk::push_constant]]
ConstantBuffer<RadixSortPushConstants> pushConstants;

#ifndef KEY_SIZE
#define KEY_SIZE 1
#endif
typedef uint[KEY_SIZE] key_t;

RWStructuredBuffer<key_t> g_keys[2];

key_t load(uint idx) {
	uint buf = (pushConstants.g_pass_index&1);
    return g_keys[buf][idx];
}
void store(uint idx, key_t p) {
	uint buf = (pushConstants.g_pass_index&1)^1;
    g_keys[buf][idx] = p;
}

uint get_key_bits(key_t p) {
    return p[0] >> (8 * pushConstants.g_pass_index);
}


// ---------- histogram kernel ---------- //

groupshared uint histogram[RADIX_SORT_BINS];

[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void multi_radixsort_histograms(uint3 threadIndex: SV_DispatchThreadID, uint3 localThreadIndex: SV_GroupThreadID, uint3 workgroupIndex : SV_GroupID) {
    uint gID = threadIndex.x;
    uint lID = localThreadIndex.x;
    uint wID = workgroupIndex.x;

    // initialize histogram
    if (lID < RADIX_SORT_BINS) {
        histogram[lID] = 0U;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint index = 0; index < pushConstants.g_num_blocks_per_workgroup; index++) {
        uint elementId = wID * pushConstants.g_num_blocks_per_workgroup * WORKGROUP_SIZE + index * WORKGROUP_SIZE + lID;
        if (elementId < pushConstants.g_num_elements) {
            // determine the bin
            const uint bin = uint(get_key_bits(load(elementId))) & uint(RADIX_SORT_BINS - 1);
            // increment the histogram
            InterlockedAdd(histogram[bin], 1U);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (lID < RADIX_SORT_BINS) {
        g_histograms[RADIX_SORT_BINS * wID + lID] = histogram[lID];
    }
}

// ---------- main sort kernel ---------- //

groupshared uint[RADIX_SORT_BINS / SUBGROUP_SIZE] sums;// subgroup reductions
groupshared uint[RADIX_SORT_BINS] global_offsets;// global exclusive scan (prefix sum)

struct BinFlags {
    uint flags[WORKGROUP_SIZE / 32];
};
groupshared BinFlags[RADIX_SORT_BINS] bin_flags;

[WaveSize(SUBGROUP_SIZE)]
[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void multi_radixsort(uint3 threadIndex: SV_DispatchThreadID, uint3 localThreadIndex: SV_GroupThreadID, uint3 workgroupIndex : SV_GroupID) {
    uint gID = threadIndex.x;
    uint lID = localThreadIndex.x;
    uint wID = workgroupIndex.x;

    uint sID = lID / SUBGROUP_SIZE;
    uint lsID = WaveGetLaneIndex();

    uint local_histogram = 0;
    uint prefix_sum = 0;
    uint histogram_count = 0;

    if (lID < RADIX_SORT_BINS) {
        uint count = 0;
        for (uint j = 0; j < pushConstants.g_num_workgroups; j++) {
            const uint t = g_histograms[RADIX_SORT_BINS * j + lID];
            local_histogram = (j == wID) ? count : local_histogram;
            count += t;
        }
        histogram_count = count;
        const uint sum = WaveActiveSum(histogram_count);
        prefix_sum = WavePrefixSum(histogram_count);
        if (WaveIsFirstLane()) {
            // one thread inside the warp/subgroup enters this section
            sums[sID] = sum;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (lID < RADIX_SORT_BINS) {
        const uint sums_prefix_sum = WaveReadLaneAt(WavePrefixSum(sums[lsID]), sID);
        const uint global_histogram = sums_prefix_sum + prefix_sum;
        global_offsets[lID] = global_histogram + local_histogram;
    }

    //     ==== scatter keys according to global offsets =====
    const uint flags_bin = lID / 32;
    const uint flags_bit = 1 << (lID % 32);

    for (uint index = 0; index < pushConstants.g_num_blocks_per_workgroup; index++) {
        uint elementId = wID * pushConstants.g_num_blocks_per_workgroup * WORKGROUP_SIZE + index * WORKGROUP_SIZE + lID;

        // initialize bin flags
        if (lID < RADIX_SORT_BINS) {
            for (int i = 0; i < WORKGROUP_SIZE / 32; i++) {
                bin_flags[lID].flags[i] = 0U;// init all bin flags to 0
            }
        }
        GroupMemoryBarrierWithGroupSync();

        key_t element_in = {};
        uint binID = 0;
        uint binOffset = 0;
        if (elementId < pushConstants.g_num_elements) {
            element_in = load(elementId);
            binID = uint(get_key_bits(element_in)) & uint(RADIX_SORT_BINS - 1);
            // offset for group
            binOffset = global_offsets[binID];
            // add bit to flag
            InterlockedAdd(bin_flags[binID].flags[flags_bin], flags_bit);
        }
        GroupMemoryBarrierWithGroupSync();

        if (elementId < pushConstants.g_num_elements) {
            // calculate output index of element
            uint prefix = 0;
            uint count = 0;
            for (uint i = 0; i < WORKGROUP_SIZE / 32; i++) {
                const uint bits = bin_flags[binID].flags[i];
                const uint full_count = countbits(bits);
                const uint partial_count = countbits(bits & (flags_bit - 1));
                prefix += (i < flags_bin) ? full_count : 0U;
                prefix += (i == flags_bin) ? partial_count : 0U;
                count += full_count;
            }
            store(binOffset + prefix, element_in);
            if (prefix == count - 1) {
                InterlockedAdd(global_offsets[binID], count);
            }
        }

        GroupMemoryBarrierWithGroupSync();
    }
}
