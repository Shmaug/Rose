#define WORKGROUP_SIZE 256 // assert WORKGROUP_SIZE >= RADIX_SORT_BINS
#define RADIX_SORT_BINS 256

namespace RoseEngine {

struct RadixSortPushConstants {
	uint g_pass_index;
    uint g_num_elements;
    uint g_num_workgroups;
    uint g_num_blocks_per_workgroup;
};

}