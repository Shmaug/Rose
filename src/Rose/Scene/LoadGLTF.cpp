#include <iostream>
#include <Rose/Core/MathUtils.h>

#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include "LoadGLTF.hpp"

namespace RoseEngine {

ref<SceneNode> LoadGLTF(CommandContext& context, const std::filesystem::path& filename) {
	std::cout << "Loading " << filename << std::endl;

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err, warn;
	if ((filename.extension() == ".glb" && !loader.LoadBinaryFromFile(&model, &err, &warn, filename.string())) ||
		(filename.extension() == ".gltf" && !loader.LoadASCIIFromFile(&model, &err, &warn, filename.string())) )
		throw std::runtime_error(filename.string() + ": " + err);
	if (!warn.empty()) std::cerr << filename.string() << ": " << warn << std::endl;

	Device& device = context.GetDevice();

	std::vector<BufferView>               buffersCpu(model.buffers.size());
	std::vector<BufferView>               buffers  (model.buffers.size());
	std::vector<ImageView>                images   (model.images.size());
	std::vector<std::vector<ref<Mesh>>>   meshes   (model.meshes.size());
	std::vector<ref<Material<ImageView>>> materials(model.materials.size());

	vk::BufferUsageFlags bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eTransferSrc;
	if (device.EnabledExtensions().contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) {
		bufferUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
		bufferUsage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
	}

	std::cout << "Loading buffers..." << std::endl;
	for (size_t i = 0; i < buffers.size(); i++) {
		buffersCpu[i] = Buffer::Create(
			context.GetDevice(),
			model.buffers[i].data.size(),
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent,
			VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		context.GetDevice().SetDebugName(**buffersCpu[i].mBuffer, filename.stem().string() + "/hostbuffer" + std::to_string(i));

		buffers[i] = Buffer::Create(
			context.GetDevice(),
			model.buffers[i].data.size(),
			bufferUsage,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT);
		context.GetDevice().SetDebugName(**buffers[i].mBuffer, filename.stem().string() + "/buffer" + std::to_string(i));

		std::memcpy(buffersCpu[i].data(), model.buffers[i].data.data(), model.buffers[i].data.size());
		context.Copy(buffersCpu[i], buffers[i]);
	};

	auto GetImage = [&](const uint32_t textureIndex, const bool srgb) -> ImageView {
		if (textureIndex >= model.textures.size()) return {};
		const uint32_t index = model.textures[textureIndex].source;
		if (index >= images.size()) return {};
		if (images[index]) return images[index];

		const tinygltf::Image& image = model.images[index];

		ImageInfo md = {};
		md.extent = uint3(image.width, image.height, 1);
		md.mipLevels = GetMaxMipLevels(md.extent);
		if (srgb) {
			static const std::array<vk::Format,4> formatMap { vk::Format::eR8Srgb, vk::Format::eR8G8Srgb, vk::Format::eR8G8B8Srgb, vk::Format::eR8G8B8A8Srgb };
			md.format = formatMap.at(image.component - 1);
		} else {
			static const std::unordered_map<int, std::array<vk::Format,4>> formatMap {
				{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE,  { vk::Format::eR8Unorm,   vk::Format::eR8G8Unorm,    vk::Format::eR8G8B8Unorm,     vk::Format::eR8G8B8A8Unorm } },
				{ TINYGLTF_COMPONENT_TYPE_BYTE,           { vk::Format::eR8Snorm,   vk::Format::eR8G8Snorm,    vk::Format::eR8G8B8Snorm,     vk::Format::eR8G8B8A8Snorm } },
				{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, { vk::Format::eR16Unorm,  vk::Format::eR16G16Unorm,  vk::Format::eR16G16B16Unorm,  vk::Format::eR16G16B16A16Unorm } },
				{ TINYGLTF_COMPONENT_TYPE_SHORT,          { vk::Format::eR16Snorm,  vk::Format::eR16G16Snorm,  vk::Format::eR16G16B16Snorm,  vk::Format::eR16G16B16A16Snorm } },
				{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,   { vk::Format::eR32Uint,   vk::Format::eR32G32Uint,   vk::Format::eR32G32B32Uint,   vk::Format::eR32G32B32A32Uint } },
				{ TINYGLTF_COMPONENT_TYPE_INT,            { vk::Format::eR32Sint,   vk::Format::eR32G32Sint,   vk::Format::eR32G32B32Sint,   vk::Format::eR32G32B32A32Sint } },
				{ TINYGLTF_COMPONENT_TYPE_FLOAT,          { vk::Format::eR32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR32G32B32Sfloat, vk::Format::eR32G32B32A32Sfloat } },
				{ TINYGLTF_COMPONENT_TYPE_DOUBLE,         { vk::Format::eR64Sfloat, vk::Format::eR64G64Sfloat, vk::Format::eR64G64B64Sfloat, vk::Format::eR64G64B64A64Sfloat } }
			};
			md.format = formatMap.at(image.pixel_type).at(image.component - 1);
		}
		md.queueFamilies = { context.QueueFamily() };

		ImageView img = ImageView::Create(Image::Create(device, md), vk::ImageSubresourceRange{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1 });
		device.SetDebugName(**img.mImage, filename.stem().string() + "/" + image.name);

		context.Copy(context.UploadData(image.image), img);
		context.GenerateMipMaps(img.mImage);

		img = ImageView::Create(img.mImage, vk::ImageSubresourceRange{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = md.mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 1 });

		images[index] = img;
		return img;
	};

	std::cout << "Loading materials..." << std::endl;
	std::ranges::transform(model.materials, materials.begin(), [&](const tinygltf::Material& material) {
		Material<ImageView> m;
		m.emissionImage     = GetImage(material.emissiveTexture.index, true);
		m.baseColorImage    = GetImage(material.pbrMetallicRoughness.baseColorTexture.index, true);
		m.metallicRoughness = GetImage(material.pbrMetallicRoughness.metallicRoughnessTexture.index, false);
		m.bumpMap           = GetImage(material.normalTexture.index, false);
		m.SetBaseColor((float3)double3(material.pbrMetallicRoughness.baseColorFactor[0], material.pbrMetallicRoughness.baseColorFactor[1], material.pbrMetallicRoughness.baseColorFactor[2]));
		m.SetAlphaCutoff((float)material.alphaCutoff);
		m.SetRoughness((float)material.pbrMetallicRoughness.roughnessFactor);
		m.SetMetallic ((float)material.pbrMetallicRoughness.metallicFactor);
		m.SetIor(1.5f);
		m.SetTransmission(0);
		m.SetClearcoat(0);
		m.SetSpecular(.5f);
		if      (material.alphaMode == "MASK")  m.SetFlags(MaterialFlags::eAlphaCutoff);
		else if (material.alphaMode == "BLEND") m.SetFlags(MaterialFlags::eAlphaBlend);
		if (material.doubleSided) m.SetFlags((MaterialFlags)(m.GetFlags() | (uint)MaterialFlags::eDoubleSided));

		float3 emission = (float3)double3(material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2]);

		if (material.extensions.contains("KHR_materials_ior"))          m.SetIor(          (float)material.extensions.at("KHR_materials_ior").Get("ior").GetNumberAsDouble() );
		if (material.extensions.contains("KHR_materials_transmission")) m.SetTransmission( (float)material.extensions.at("KHR_materials_transmission").Get("transmissionFactor").GetNumberAsDouble() );
		if (material.extensions.contains("KHR_materials_clearcoat"))    m.SetClearcoat(    (float)material.extensions.at("KHR_materials_clearcoat").Get("clearcoatFactor").GetNumberAsDouble() );

		if (material.extensions.find("KHR_materials_emissive_strength") != material.extensions.end())
			emission *= (float)material.extensions.at("KHR_materials_emissive_strength").Get("emissiveStrength").GetNumberAsDouble();
		m.SetEmission(emission);

		if (material.extensions.contains("KHR_materials_specular")) {
			const auto& v = material.extensions.at("KHR_materials_specular");
			if (v.Has("specularColorFactor")) {
				auto& a = v.Get("specularColorFactor");
				m.SetSpecular( luminance((float3)double3(a.Get(0).GetNumberAsDouble(), a.Get(1).GetNumberAsDouble(), a.Get(2).GetNumberAsDouble())) );
			} else if (v.Has("specularFactor")) {
				m.SetSpecular( (float)v.Get("specularFactor").GetNumberAsDouble() );
			}
		}

		return make_ref<Material<ImageView>>(m);
	});

	std::cout << "Loading meshes...";
	for (uint32_t i = 0; i < model.meshes.size(); i++) {
		std::cout << "\rLoading meshes " << (i+1) << "/" << model.meshes.size() << "     ";
		meshes[i].resize(model.meshes[i].primitives.size());
		for (uint32_t j = 0; j < model.meshes[i].primitives.size(); j++) {
			const tinygltf::Primitive& prim = model.meshes[i].primitives[j];
			const auto& indicesAccessor = model.accessors[prim.indices];
			const auto& indexBufferView = model.bufferViews[indicesAccessor.bufferView];
			const size_t indexStride = tinygltf::GetComponentSizeInBytes(indicesAccessor.componentType);

			Mesh mesh = {};
			mesh.indexBufferCpu = buffersCpu[indexBufferView.buffer].slice(indexBufferView.byteOffset + indicesAccessor.byteOffset, indicesAccessor.count * indexStride);
			mesh.indexBuffer    = buffers   [indexBufferView.buffer].slice(indexBufferView.byteOffset + indicesAccessor.byteOffset, indicesAccessor.count * indexStride);
			mesh.indexSize = indexStride;
			switch (prim.mode) {
				case TINYGLTF_MODE_POINTS: 			mesh.topology = vk::PrimitiveTopology::ePointList; break;
				case TINYGLTF_MODE_LINE: 			mesh.topology = vk::PrimitiveTopology::eLineList; break;
				case TINYGLTF_MODE_LINE_LOOP: 		mesh.topology = vk::PrimitiveTopology::eLineStrip; break;
				case TINYGLTF_MODE_LINE_STRIP: 		mesh.topology = vk::PrimitiveTopology::eLineStrip; break;
				case TINYGLTF_MODE_TRIANGLES: 		mesh.topology = vk::PrimitiveTopology::eTriangleList; break;
				case TINYGLTF_MODE_TRIANGLE_STRIP: 	mesh.topology = vk::PrimitiveTopology::eTriangleStrip; break;
				case TINYGLTF_MODE_TRIANGLE_FAN: 	mesh.topology = vk::PrimitiveTopology::eTriangleFan; break;
			}

			for (const auto&[attribName,attribIndex] : prim.attributes) {
				const tinygltf::Accessor& accessor = model.accessors[attribIndex];

				static const std::unordered_map<int, std::unordered_map<int, vk::Format>> formatMap {
					{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR8Uint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR8G8Uint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR8G8B8Uint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR8G8B8A8Uint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_BYTE, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR8Sint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR8G8Sint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR8G8B8Sint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR8G8B8A8Sint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR16Uint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR16G16Uint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR16G16B16Uint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR16G16B16A16Uint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_SHORT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR16Sint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR16G16Sint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR16G16B16Sint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR16G16B16A16Sint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR32Uint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR32G32Uint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR32G32B32Uint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR32G32B32A32Uint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_INT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR32Sint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR32G32Sint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR32G32B32Sint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR32G32B32A32Sint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_FLOAT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR32Sfloat },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR32G32Sfloat },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR32G32B32Sfloat },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR32G32B32A32Sfloat },
					} },
					{ TINYGLTF_COMPONENT_TYPE_DOUBLE, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR64Sfloat },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR64G64Sfloat },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR64G64B64Sfloat },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR64G64B64A64Sfloat },
					} }
				};
				vk::Format attributeFormat = formatMap.at(accessor.componentType).at(accessor.type);

				MeshVertexAttributeType attributeType;
				uint32_t typeIndex = 0;
				// parse typename & typeindex
				{
					std::string typeName;
					typeName.resize(attribName.size());
					std::ranges::transform(attribName, typeName.begin(), [&](char c) { return tolower(c); });
					size_t c = typeName.find_first_of("0123456789");
					if (c != std::string::npos) {
						typeIndex = stoi(typeName.substr(c));
						typeName = typeName.substr(0, c);
					}
					if (typeName.back() == '_') typeName.pop_back();
					static const std::unordered_map<std::string, MeshVertexAttributeType> semanticMap {
						{ "position", 	MeshVertexAttributeType::ePosition },
						{ "normal", 	MeshVertexAttributeType::eNormal },
						{ "tangent", 	MeshVertexAttributeType::eTangent },
						{ "bitangent", 	MeshVertexAttributeType::eBinormal },
						{ "texcoord", 	MeshVertexAttributeType::eTexcoord },
						{ "color", 		MeshVertexAttributeType::eColor },
						{ "psize", 		MeshVertexAttributeType::ePointSize },
						{ "pointsize", 	MeshVertexAttributeType::ePointSize },
						{ "joints",     MeshVertexAttributeType::eBlendIndex },
						{ "weights",    MeshVertexAttributeType::eBlendWeight }
					};
					attributeType = semanticMap.at(typeName);
				}

				if (attributeType == MeshVertexAttributeType::ePosition) {
					mesh.aabb.minX = (float)accessor.minValues[0];
					mesh.aabb.minY = (float)accessor.minValues[1];
					mesh.aabb.minZ = (float)accessor.minValues[2];
					mesh.aabb.maxX = (float)accessor.maxValues[0];
					mesh.aabb.maxY = (float)accessor.maxValues[1];
					mesh.aabb.maxZ = (float)accessor.maxValues[2];
				}

				for (int i = 0; i < 2; i++) {
					auto& attribs = i == 0 ? mesh.vertexAttributes[attributeType] : mesh.vertexAttributesCpu[attributeType];
					if (attribs.size() <= typeIndex) attribs.resize(typeIndex+1);
					const tinygltf::BufferView& b = model.bufferViews[accessor.bufferView];
					const uint32_t stride = accessor.ByteStride(b);
					attribs[typeIndex] = {
						(i == 0 ? buffers[b.buffer] : buffersCpu[b.buffer]).slice(b.byteOffset + accessor.byteOffset, stride*accessor.count),
						MeshVertexAttributeLayout{
							.stride = stride,
							.format = attributeFormat,
							.offset = 0,
							.inputRate = vk::VertexInputRate::eVertex} };
				}
			}

			meshes[i][j] = make_ref<Mesh>(std::move(mesh));
		}
	}
	std::cout << std::endl;

	ref<Mesh> sphereMesh = {};

	std::cout << "Loading scene nodes...";
	const ref<SceneNode> rootNode = SceneNode::Create(filename.stem().string());
	std::vector<ref<SceneNode>> nodes(model.nodes.size());
	for (size_t n = 0; n < model.nodes.size(); n++) {
		std::cout << "\rLoading scene nodes " << (n+1) << "/" << model.nodes.size() << "     ";

		const auto& node = model.nodes[n];
		const ref<SceneNode> dst = SceneNode::Create(node.name.empty() ? "node" : node.name);
		nodes[n] = dst;

		// parse transform

		Transform transform = Transform::Identity();
		if (!node.translation.empty() || !node.rotation.empty() || !node.scale.empty()) {
			if (node.translation.size() == 3) transform = Transform::Translate((float3)*reinterpret_cast<const double3*>(node.translation.data()));
			if (node.rotation.size() == 4)    transform.transform = transform.transform * glm::mat4_cast(glm::quat((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]));
			if (node.scale.size() == 1)       transform = transform * Transform::Scale(float3((float)node.scale[0]));
			dst->transform = transform;
		} else if (node.matrix.size() == 16) {
			transform.transform = (float4x4)*reinterpret_cast<const double4x4*>(node.matrix.data());
			dst->transform = transform;
		}

		// parse primitives

		if (node.mesh < model.meshes.size()) {
			const auto& prims = model.meshes[node.mesh].primitives;
			for (uint32_t i = 0; i < prims.size(); i++) {
				const auto& prim = prims[i];
				auto primNode = SceneNode::Create(model.meshes[node.mesh].name);
				primNode->mesh = meshes[node.mesh][i];
				primNode->material = materials[prim.material];
				primNode->SetParent(dst);
			}
		}

		auto light_it = node.extensions.find("KHR_lights_punctual");
		if (light_it != node.extensions.end() && light_it->second.Has("light")) {
			const tinygltf::Light& l = model.lights[light_it->second.Get("light").GetNumberAsInt()];
			if (l.type == "point") {
				auto lightNode = SceneNode::Create("PointLight");
				lightNode->SetParent(dst);
				lightNode->mesh = sphereMesh;

				double3 c = double3(l.color[0], l.color[1], l.color[2]) * l.intensity;
				double r = 1e-4;
				if (l.extras.Has("radius")) {
					r = l.extras.Get("radius").GetNumberAsDouble();
					c /= 4*M_PI*r*r;
				}
				lightNode->transform = Transform::Scale(float3((float)r));

				Material<ImageView> m = {};
				m.SetBaseColor(float3(0));
				m.SetEmission((float3)c);
				lightNode->material = make_ref<Material<ImageView>>(m);
			}
		}
	}
	std::cout << std::endl;

	// link children
	for (size_t i = 0; i < model.nodes.size(); i++)
		for (int c : model.nodes[i].children)
			nodes[c]->SetParent(nodes[i]);

	for (const auto& n : nodes)
		if (!n->GetParent())
			n->SetParent(rootNode);

	std::cout << "Loaded " << filename << std::endl;

	return rootNode;
}

}