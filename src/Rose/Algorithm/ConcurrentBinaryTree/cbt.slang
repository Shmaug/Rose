#pragma once
/* cbt.glsl - public domain library for building binary trees in parallel by Jonathan Dupuy */

// data structures
struct CbtNode {
	uint id;    // heapID
	int depth;  // findMSB(heapID) := node depth
};

struct ConcurrentBinaryTree<let N: int> {
	// buffer binding (allows for simultaneous use of multiple CBTs)
	RWStructuredBuffer<uint> u_CbtBuffers[N];

	// manipulation
	void SplitNode_Fast(const int cbtID, in const CbtNode node);
	void SplitNode     (const int cbtID, in const CbtNode node);
	void MergeNode_Fast(const int cbtID, in const CbtNode node);
	void MergeNode     (const int cbtID, in const CbtNode node);

	// O(1) queries
	uint HeapRead(const int cbtID, in const CbtNode node);
	int MaxDepth(const int cbtID);
	uint NodeCount(const int cbtID);
	bool IsLeafNode(const int cbtID, in const CbtNode node);
	bool IsCeilNode(const int cbtID, in const CbtNode node);
	static bool IsRootNode(          in const CbtNode node);
	static bool IsNullNode(          in const CbtNode node);

	// O(depth) queries
	uint EncodeNode(const int cbtID, in const CbtNode node);
	CbtNode DecodeNode(const int cbtID, uint nodeID);

	// node constructors
	static CbtNode CreateNode           (uint id);
	static CbtNode CreateNode           (uint id, int depth);
	static CbtNode ParentNode           (const CbtNode node);
	static CbtNode ParentNode_Fast      (const CbtNode node);
	static CbtNode SiblingNode          (const CbtNode node);
	static CbtNode SiblingNode_Fast     (const CbtNode node);
	static CbtNode LeftSiblingNode      (const CbtNode node);
	static CbtNode LeftSiblingNode_Fast (const CbtNode node);
	static CbtNode RightSiblingNode     (const CbtNode node);
	static CbtNode RightSiblingNode_Fast(const CbtNode node);
	static CbtNode LeftChildNode        (const CbtNode node);
	static CbtNode LeftChildNode_Fast   (const CbtNode node);
	static CbtNode RightChildNode       (const CbtNode node);
	static CbtNode RightChildNode_Fast  (const CbtNode node);

	// -----------------------------------------------------------------------------
	// -----------------------------------------------------------------------------
	// -----------------------------------------------------------------------------

	/*******************************************************************************
	* GetBitValue -- Returns the value of a bit stored in a 32-bit word
	*
	*/
	uint _GetBitValue(uint bitField, uint bitID)
	{
		return ((bitField >> bitID) & 1u);
	}


	/*******************************************************************************
	* SetBitValue -- Sets the value of a bit stored in a 32-bit word
	*
	*/
	void
	_SetBitValue(const int cbtID, uint bufferID, uint bitID, uint bitValue)
	{
		const uint bitMask = ~(1u << bitID);

		InterlockedAnd(u_CbtBuffers[cbtID][bufferID], bitMask);
		InterlockedOr(u_CbtBuffers[cbtID][bufferID], bitValue << bitID);
	}


	/*******************************************************************************
	* BitFieldInsert -- Returns the bit field after insertion of some bit data in range
	* [bitOffset, bitOffset + bitCount - 1]
	*
	*/
	void
	_BitFieldInsert(
		const int cbtID,
		uint bufferID,
		uint bitOffset,
		uint bitCount,
		uint bitData
	) {
		uint bitMask = ~(~(0xFFFFFFFFu << bitCount) << bitOffset);

		InterlockedAnd(u_CbtBuffers[cbtID][bufferID], bitMask);
		InterlockedOr(u_CbtBuffers[cbtID][bufferID], bitData << bitOffset);
	}


	/*******************************************************************************
	* BitFieldExtract -- Extracts bits [bitOffset, bitOffset + bitCount - 1] from
	* a bit field, returning them in the least significant bits of the result.
	*
	*/
	uint _BitFieldExtract(uint bitField, uint bitOffset, uint bitCount)
	{
		uint bitMask = ~(0xFFFFFFFFu << bitCount);

		return (bitField >> bitOffset) & bitMask;
	}


	/*******************************************************************************
	* IsCeilNode -- Checks if a node is a ceil node, i.e., that can not split further
	*
	*/
	bool IsCeilNode(const int cbtID, in const CbtNode node)
	{
		return (node.depth == MaxDepth(cbtID));
	}


	/*******************************************************************************
	* IsRootNode -- Checks if a node is a root node
	*
	*/
	static bool IsRootNode(in const CbtNode node)
	{
		return (node.id == 1u);
	}


	/*******************************************************************************
	* IsNullNode -- Checks if a node is a null node
	*
	*/
	static bool IsNullNode(in const CbtNode node)
	{
		return (node.id == 0u);
	}


	/*******************************************************************************
	* CreateNode -- Constructor for the Node data structure
	*
	*/
	static CbtNode CreateNode(uint id)
	{
		return CreateNode(id, firstbithigh(id));
	}

	static CbtNode CreateNode(uint id, int depth)
	{
		CbtNode node;

		node.id = id;
		node.depth = depth;

		return node;
	}


	/*******************************************************************************
	* ParentNode -- Computes the parent of the input node
	*
	*/
	static CbtNode ParentNode_Fast(in const CbtNode node)
	{
		return CreateNode(node.id >> 1, node.depth - 1);
	}

	static CbtNode ParentNode(in const CbtNode node)
	{
		return IsNullNode(node) ? node : ParentNode_Fast(node);
	}


	/*******************************************************************************
	* CeilNode -- Returns the associated ceil node, i.e., the deepest possible leaf
	*
	*/
	CbtNode _CeilNode_Fast(const int cbtID, in const CbtNode node)
	{
		int maxDepth = MaxDepth(cbtID);
		return CreateNode(node.id << (maxDepth - node.depth), maxDepth);
	}

	CbtNode _CeilNode(const int cbtID, in const CbtNode node)
	{
		return IsNullNode(node) ? node : _CeilNode_Fast(cbtID, node);
	}


	/*******************************************************************************
	* SiblingNode -- Computes the sibling of the input node
	*
	*/
	static CbtNode SiblingNode_Fast(in const CbtNode node)
	{
		return CreateNode(node.id ^ 1u, node.depth);
	}

	static CbtNode SiblingNode(in const CbtNode node)
	{
		return IsNullNode(node) ? node : SiblingNode_Fast(node);
	}


	/*******************************************************************************
	* RightSiblingNode -- Computes the right sibling of the input node
	*
	*/
	static CbtNode RightSiblingNode_Fast(in const CbtNode node)
	{
		return CreateNode(node.id | 1u, node.depth);
	}

	static CbtNode RightSiblingNode(in const CbtNode node)
	{
		return IsNullNode(node) ? node : RightSiblingNode_Fast(node);
	}


	/*******************************************************************************
	* LeftSiblingNode -- Computes the left sibling of the input node
	*
	*/
	static CbtNode LeftSiblingNode_Fast(in const CbtNode node)
	{
		return CreateNode(node.id & (~1u), node.depth);
	}

	static CbtNode LeftSiblingNode(in const CbtNode node)
	{
		return IsNullNode(node) ? node : LeftSiblingNode_Fast(node);
	}


	/*******************************************************************************
	* RightChildNode -- Computes the right child of the input node
	*
	*/
	static CbtNode RightChildNode_Fast(in const CbtNode node)
	{
		return CreateNode((node.id << 1) | 1u, node.depth + 1);
	}

	static CbtNode RightChildNode(in const CbtNode node)
	{
		return IsNullNode(node) ? node : RightChildNode_Fast(node);
	}


	/*******************************************************************************
	* LeftChildNode -- Computes the left child of the input node
	*
	*/
	static CbtNode LeftChildNode_Fast(in const CbtNode node)
	{
		return CreateNode(node.id << 1, node.depth + 1);
	}

	static CbtNode LeftChildNode(in const CbtNode node)
	{
		return IsNullNode(node) ? node : LeftChildNode_Fast(node);
	}


	/*******************************************************************************
	* HeapByteSize -- Computes the number of Bytes to allocate for the bitfield
	*
	* For a tree of max depth D, the number of Bytes is 2^(D-1).
	* Note that 2 bits are "wasted" in the sense that they only serve
	* to round the required number of bytes to a power of two.
	*
	*/
	uint _HeapByteSize(uint cbtMaxDepth)
	{
		return 1u << (cbtMaxDepth - 1);
	}


	/*******************************************************************************
	* HeapUint32Size -- Computes the number of uints to allocate for the bitfield
	*
	*/
	uint _HeapUint32Size(uint cbtMaxDepth)
	{
		return _HeapByteSize(cbtMaxDepth) >> 2;
	}


	/*******************************************************************************
	* NodeBitID -- Returns the bit index that stores data associated with a given node
	*
	* For a LEB of max depth D and given an index in [0, 2^(D+1) - 1], this
	* functions is used to emulate the behaviour of a lookup in an array, i.e.,
	* uint32_t[nodeID]. It provides the first bit in memory that stores
	* information associated with the element of index nodeID.
	*
	* For data located at level d, the bit offset is 2^d x (3 - d + D)
	* We then offset this quantity by the index by (nodeID - 2^d) x (D + 1 - d)
	* Note that the null index (nodeID = 0) is also supported.
	*
	*/
	uint _NodeBitID(const int cbtID, in const CbtNode node)
	{
		uint tmp1 = 2u << node.depth;
		uint tmp2 = uint(1 + MaxDepth(cbtID) - node.depth);

		return tmp1 + node.id * tmp2;
	}


	/*******************************************************************************
	* NodeBitID_BitField -- Computes the bitfield bit location associated to a node
	*
	* Here, the node is converted into a final node and its bit offset is
	* returned, which is finalNodeID + 2^{D + 1}
	*/
	uint _NodeBitID_BitField(const int cbtID, in const CbtNode node)
	{
		return _NodeBitID(cbtID, _CeilNode(cbtID, node));
	}


	/*******************************************************************************
	* DataBitSize -- Returns the number of bits associated with a given node
	*
	*/
	int _NodeBitSize(const int cbtID, in const CbtNode node)
	{
		return MaxDepth(cbtID) - node.depth + 1;
	}


	/*******************************************************************************
	* HeapArgs
	*
	* The LEB heap data structure uses an array of 32-bit words to store its data.
	* Whenever we need to access a certain bit range, we need to query two such
	* words (because sometimes the requested bit range overlaps two 32-bit words).
	* The HeapArg data structure provides arguments for reading from and/or
	* writing to the two 32-bit words that bound the queries range.
	*
	*/
	struct _HeapArgs {
		uint heapIndexLSB, heapIndexMSB;
		uint bitOffsetLSB;
		uint bitCountLSB, bitCountMSB;
	};

	_HeapArgs
	_CreateHeapArgs(const int cbtID, in const CbtNode node, int bitCount)
	{
		uint alignedBitOffset = _NodeBitID(cbtID, node);
		uint maxHeapIndex = _HeapUint32Size(MaxDepth(cbtID)) - 1u;
		uint heapIndexLSB = (alignedBitOffset >> 5u);
		uint heapIndexMSB = min(heapIndexLSB + 1, maxHeapIndex);
		_HeapArgs args;

		args.bitOffsetLSB = alignedBitOffset & 31u;
		args.bitCountLSB = min(32u - args.bitOffsetLSB, bitCount);
		args.bitCountMSB = bitCount - args.bitCountLSB;
		args.heapIndexLSB = heapIndexLSB;
		args.heapIndexMSB = heapIndexMSB;

		return args;
	}


	/*******************************************************************************
	* HeapWrite -- Sets bitCount bits located at nodeID to bitData
	*
	* Note that this procedure writes to at most two uint32 elements.
	* Two elements are relevant whenever the specified interval overflows 32-bit
	* words.
	*
	*/
	void
	_HeapWriteExplicit(
		const int cbtID,
		in const CbtNode node,
		int bitCount,
		uint bitData
	) {
		_HeapArgs args = _CreateHeapArgs(cbtID, node, bitCount);

		_BitFieldInsert(cbtID,
							args.heapIndexLSB,
							args.bitOffsetLSB,
							args.bitCountLSB,
							bitData);
		_BitFieldInsert(cbtID,
							args.heapIndexMSB,
							0u,
							args.bitCountMSB,
							bitData >> args.bitCountLSB);
	}

	void _HeapWrite(const int cbtID, in const CbtNode node, uint bitData)
	{
		_HeapWriteExplicit(cbtID, node, _NodeBitSize(cbtID, node), bitData);
	}


	/*******************************************************************************
	* HeapRead -- Returns bitCount bits located at nodeID
	*
	* Note that this procedure writes to at most two uint32 elements.
	* Two elements are relevant whenever the specified interval overflows 32-bit
	* words.
	*
	*/
	uint
	_HeapReadExplicit(const int cbtID, in const CbtNode node, int bitCount)
	{
		_HeapArgs args = _CreateHeapArgs(cbtID, node, bitCount);
		uint lsb = _BitFieldExtract(u_CbtBuffers[cbtID][args.heapIndexLSB],
										args.bitOffsetLSB,
										args.bitCountLSB);
		uint msb = _BitFieldExtract(u_CbtBuffers[cbtID][args.heapIndexMSB],
										0u,
										args.bitCountMSB);

		return (lsb | (msb << args.bitCountLSB));
	}

	uint HeapRead(const int cbtID, in const CbtNode node)
	{
		return _HeapReadExplicit(cbtID, node, _NodeBitSize(cbtID, node));
	}


	/*******************************************************************************
	* HeapWrite_BitField -- Sets the bit associated to a leaf node to bitValue
	*
	* This is a dedicated routine to write directly to the bitfield.
	*
	*/
	void
	_HeapWrite_BitField(const int cbtID, in const CbtNode node, uint bitValue)
	{
		uint bitID = _NodeBitID_BitField(cbtID, node);

		_SetBitValue(cbtID, bitID >> 5u, bitID & 31u, bitValue);
	}


	/*******************************************************************************
	* HeapRead_BitField -- Returns the value of the bit associated to a leaf node
	*
	* This is a dedicated routine to read directly from the bitfield.
	*
	*/
	uint _HeapRead_BitField(const int cbtID, in const CbtNode node)
	{
		uint bitID = _NodeBitID_BitField(cbtID, node);

		return _GetBitValue(u_CbtBuffers[cbtID][bitID >> 5u], bitID & 31u);
	}


	/*******************************************************************************
	* IsLeafNode -- Checks if a node is a leaf node
	*
	*/
	bool IsLeafNode(const int cbtID, in const CbtNode node)
	{
		return (HeapRead(cbtID, node) == 1u);
	}


	/*******************************************************************************
	* Split -- Subdivides a node in two
	*
	*/
	void SplitNode_Fast(const int cbtID, in const CbtNode node)
	{
		_HeapWrite_BitField(cbtID, RightChildNode(node), 1u);
	}
	void SplitNode(const int cbtID, in const CbtNode node)
	{
		if (!IsCeilNode(cbtID, node))
			SplitNode_Fast(cbtID, node);
	}


	/*******************************************************************************
	* Merge -- Merges the node with its neighbour
	*
	*/
	void MergeNode_Fast(const int cbtID, in const CbtNode node)
	{
		_HeapWrite_BitField(cbtID, RightSiblingNode(node), 0u);
	}
	void MergeNode(const int cbtID, in const CbtNode node)
	{
		if (!IsRootNode(node))
			MergeNode_Fast(cbtID, node);
	}


	/*******************************************************************************
	* MaxDepth -- Returns the maximum depth
	*
	*/
	int MaxDepth(const int cbtID)
	{
		return firstbitlow(u_CbtBuffers[cbtID][0]);
	}


	/*******************************************************************************
	* NodeCount -- Returns the number of triangles in the LEB
	*
	*/
	uint NodeCount(const int cbtID)
	{
		return HeapRead(cbtID, CreateNode(1u, 0));
	}


	/*******************************************************************************
	* Decode the LEB Node associated to an index
	*
	*/
	CbtNode DecodeNode(const int cbtID, uint nodeID)
	{
		CbtNode node = CreateNode(1u, 0);

		while (HeapRead(cbtID, node) > 1u) {
			CbtNode leftChild = LeftChildNode_Fast(node);
			uint cmp = HeapRead(cbtID, leftChild);
			uint b = nodeID < cmp ? 0u : 1u;

			node = leftChild;
			node.id|= b;
			nodeID-= cmp * b;
		}

		return node;
	}


	/*******************************************************************************
	* EncodeNode -- Returns the bit index associated with the Node
	*
	* This does the inverse of the DecodeNode routine.
	*
	*/
	uint EncodeNode(const int cbtID, in const CbtNode node)
	{
		uint nodeID = 0u;
		CbtNode nodeIterator = node;

		while (nodeIterator.id > 1u) {
			CbtNode sibling = LeftSiblingNode_Fast(nodeIterator);
			uint nodeCount = HeapRead(cbtID, sibling);

			nodeID+= (nodeIterator.id & 1u) * nodeCount;
			nodeIterator = ParentNode(nodeIterator);
		}

		return nodeID;
    }
};