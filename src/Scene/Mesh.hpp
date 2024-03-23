#pragma once

#include <Core/Buffer.hpp>
#include <Core/Hash.hpp>

namespace RoseEngine {

class CommandContext;
class ShaderModule;

enum class MeshVertexAttributeType {
	ePosition,
	eNormal,
	eTangent,
	eBinormal,
	eColor,
	eTexcoord,
	ePointSize,
	eBlendIndex,
	eBlendWeight
};
struct MeshVertexAttributeLayout {
	uint32_t            stride = sizeof(float) * 3;
	vk::Format          format = vk::Format::eR32G32B32Sfloat;
	uint32_t            offset = 0;
	vk::VertexInputRate inputRate = vk::VertexInputRate::eVertex;
};
using MeshVertexAttribute        = std::pair<BufferView/*vertex buffer*/, MeshVertexAttributeLayout>;
using MeshVertexAttributeBinding = std::pair<uint32_t  /*binding index*/, MeshVertexAttributeLayout>;
using MeshVertexAttributes        = std::unordered_map<MeshVertexAttributeType, std::vector<MeshVertexAttribute>>;
using MeshVertexAttributeBindings = std::unordered_map<MeshVertexAttributeType, std::vector<MeshVertexAttributeBinding>>;

struct MeshLayout {
	MeshVertexAttributeBindings vertexAttributeBindings = {};
	std::vector<vk::VertexInputBindingDescription>   bindings = {};
	std::vector<vk::VertexInputAttributeDescription> attributes = {};
	vk::PrimitiveTopology       topology  = vk::PrimitiveTopology::eTriangleList;
	vk::IndexType               indexType = vk::IndexType::eUint32;
	bool                        hasIndices = true;
};

struct Mesh {
	MeshVertexAttributes  vertexAttributes = {};
	BufferView            indexBuffer = {};
	vk::IndexType         indexType = {};
	vk::PrimitiveTopology topology = {};
	vk::AabbPositionsKHR  aabb = {};
	uint64_t              lastUpdate = 0;

	MeshLayout GetLayout(const ShaderModule& vertexShader) const;
	void Bind(CommandContext& context, const MeshLayout& layout) const;
};

}

namespace std {
template<>
struct hash<RoseEngine::MeshVertexAttributeLayout> {
	inline size_t operator()(const RoseEngine::MeshVertexAttributeLayout& v) const {
		return RoseEngine::HashArgs(v.stride, v.format, v.offset, v.inputRate);
	}
};

template<>
struct hash<RoseEngine::MeshLayout> {
	inline size_t operator()(const RoseEngine::MeshLayout& v) const {
		size_t h = 0;
		for (const auto[type, attribs] : v.vertexAttributeBindings) {
			h = RoseEngine::HashArgs(h, type);
			for (const auto&[a,i] : attribs)
				h = RoseEngine::HashArgs(h, a, i);
		}
		h = RoseEngine::HashArgs(h, v.topology);
		if (v.hasIndices) h = RoseEngine::HashArgs(h, v.indexType);
		return h;
	}
};

inline std::string to_string(const RoseEngine::MeshVertexAttributeType& value) {
	switch (value) {
		case RoseEngine::MeshVertexAttributeType::ePosition:    return "Position";
		case RoseEngine::MeshVertexAttributeType::eNormal:      return "Normal";
		case RoseEngine::MeshVertexAttributeType::eTangent:     return "Tangent";
		case RoseEngine::MeshVertexAttributeType::eBinormal:    return "Binormal";
		case RoseEngine::MeshVertexAttributeType::eBlendIndex:  return "BlendIndex";
		case RoseEngine::MeshVertexAttributeType::eBlendWeight: return "BlendWeight";
		case RoseEngine::MeshVertexAttributeType::eColor:       return "Color";
		case RoseEngine::MeshVertexAttributeType::ePointSize:   return "PointSize";
		case RoseEngine::MeshVertexAttributeType::eTexcoord:    return "Texcoord";
		default: return "invalid ( " + vk::toHexString( static_cast<uint32_t>( value ) ) + " )";
	}
}
}