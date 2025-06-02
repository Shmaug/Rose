import cbt;
import Rose.Core.Indirect;

#ifndef CBT_HEAP_BUFFER_COUNT
#define CBT_HEAP_BUFFER_COUNT 1
#endif

ParameterBlock<ConcurrentBinaryTree<CBT_HEAP_BUFFER_COUNT>> cbt;

[shader("compute")]
[numthreads(256, 1, 1)]
void SumReducePrepass(uint3 dispatchThreadID: SV_DispatchThreadID, uniform uint u_CbtID, uniform uint u_PassID)
{
    const int cbtID = u_CbtID;
    uint cnt = (1u << u_PassID);
    uint threadID = dispatchThreadID.x << 5;

    if (threadID < cnt) {
        uint nodeID = threadID + cnt;
        uint alignedBitOffset = cbt._NodeBitID(cbtID, cbt.CreateNode(nodeID, u_PassID));
        uint bitField = cbt.u_CbtBuffers[cbtID][alignedBitOffset >> 5u];
        uint bitData = 0u;

        // 2-bits
        bitField = (bitField & 0x55555555u) + ((bitField >> 1u) & 0x55555555u);
        bitData = bitField;
        cbt.u_CbtBuffers[cbtID][(alignedBitOffset - cnt) >> 5u] = bitData;

        // 3-bits
        bitField = (bitField & 0x33333333u) + ((bitField >>  2u) & 0x33333333u);
        bitData = ((bitField >> 0u) & (7u <<  0u))
                | ((bitField >> 1u) & (7u <<  3u))
                | ((bitField >> 2u) & (7u <<  6u))
                | ((bitField >> 3u) & (7u <<  9u))
                | ((bitField >> 4u) & (7u << 12u))
                | ((bitField >> 5u) & (7u << 15u))
                | ((bitField >> 6u) & (7u << 18u))
                | ((bitField >> 7u) & (7u << 21u));
        cbt._HeapWriteExplicit(cbtID, cbt.CreateNode(nodeID >> 2u, u_PassID - 2), 24, bitData);

        // 4-bits
        bitField = (bitField & 0x0F0F0F0Fu) + ((bitField >>  4u) & 0x0F0F0F0Fu);
        bitData = ((bitField >>  0u) & (15u <<  0u))
                | ((bitField >>  4u) & (15u <<  4u))
                | ((bitField >>  8u) & (15u <<  8u))
                | ((bitField >> 12u) & (15u << 12u));
        cbt._HeapWriteExplicit(cbtID, cbt.CreateNode(nodeID >> 3u, u_PassID - 3), 16, bitData);

        // 5-bits
        bitField = (bitField & 0x00FF00FFu) + ((bitField >>  8u) & 0x00FF00FFu);
        bitData = ((bitField >>  0u) & (31u << 0u))
                | ((bitField >> 11u) & (31u << 5u));
        cbt._HeapWriteExplicit(cbtID, cbt.CreateNode(nodeID >> 4u, u_PassID - 4), 10, bitData);

        // 6-bits
        bitField = (bitField & 0x0000FFFFu) + ((bitField >> 16u) & 0x0000FFFFu);
        bitData = bitField;
        cbt._HeapWriteExplicit(cbtID, cbt.CreateNode(nodeID >> 5u, u_PassID - 5),  6, bitData);
    }
}

[shader("compute")]
[numthreads(256, 1, 1)]
void SumReduce(uint3 dispatchThreadID: SV_DispatchThreadID, uniform uint u_CbtID, uniform uint u_PassID)
{
    const int cbtID = u_CbtID;
    uint cnt = (1u << u_PassID);
    uint threadID = dispatchThreadID.x;

    if (threadID < cnt) {
        uint nodeID = threadID + cnt;
        uint x0 = cbt.HeapRead(cbtID, cbt.CreateNode(nodeID << 1u, u_PassID + 1));
        uint x1 = cbt.HeapRead(cbtID, cbt.CreateNode(nodeID << 1u | 1u, u_PassID + 1));

        cbt._HeapWrite(cbtID, cbt.CreateNode(nodeID, u_PassID), x0 + x1);
    }
}

[shader("compute")]
[numthreads(32, 1, 1)]
void WriteIndirectDispatchArgs(uint3 dispatchThreadID: SV_DispatchThreadID, RWStructuredBuffer<uint4> output, uniform uint blockDim) {
    uint i = dispatchThreadID.x;
    if (i < CBT_HEAP_BUFFER_COUNT) {
		uint count = cbt.NodeCount(i);
		output[i] = uint4(
			max((count + blockDim-1) / blockDim, 1),
			1,
			1,
			1 );
	}
}

[shader("compute")]
[numthreads(32, 1, 1)]
void WriteIndirectDrawArgs(uint3 dispatchThreadID: SV_DispatchThreadID, RWStructuredBuffer<VkDrawIndirectCommand> output) {
    VkDrawIndirectCommand arg = {};
    arg.vertexCount = 3;
    arg.firstVertex = 0;
    arg.firstInstance = 0;
    uint i = dispatchThreadID.x;
    if (i < CBT_HEAP_BUFFER_COUNT) {
		arg.instanceCount = cbt.NodeCount(i);
		output[i] = arg;
	}
}