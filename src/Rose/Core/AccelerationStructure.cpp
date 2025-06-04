#pragma once

#include "AccelerationStructure.hpp"
#include "CommandContext.hpp"

namespace RoseEngine {


ref<AccelerationStructure> AccelerationStructure::Create(
	CommandContext& context,
	const vk::AccelerationStructureTypeKHR type,
	const vk::ArrayProxy<const vk::AccelerationStructureGeometryKHR>& geometries,
	const vk::ArrayProxy<const vk::AccelerationStructureBuildRangeInfoKHR>& buildRanges,
	const vk::BuildAccelerationStructureFlagBitsKHR buildFlags
) {
	vk::AccelerationStructureBuildGeometryInfoKHR buildGeometry {
		.type  = type,
		.flags = buildFlags,
		.mode  = vk::BuildAccelerationStructureModeKHR::eBuild };
	buildGeometry.setGeometries(geometries);

	vk::AccelerationStructureBuildSizesInfoKHR buildSizes;
	if (buildRanges.size() > 0 && buildRanges.front().primitiveCount > 0) {
		std::vector<uint32_t> counts((uint32_t)geometries.size());
		for (uint32_t i = 0; i < geometries.size(); i++)
			counts[i] = (buildRanges.data() + i)->primitiveCount;
		buildSizes = context.GetDevice()->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometry, counts);
	} else
		buildSizes.accelerationStructureSize = buildSizes.buildScratchSize = 4;

	auto scratchData = context.GetTransientBuffer(
		buildSizes.buildScratchSize,
		vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);

	AccelerationStructure* as = new AccelerationStructure();
	as->buffer = Buffer::Create(
		context.GetDevice(),
		buildSizes.accelerationStructureSize,
		vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	as->accelerationStructure = context.GetDevice()->createAccelerationStructureKHR(vk::AccelerationStructureCreateInfoKHR{
		.buffer = **as->buffer.mBuffer,
		.offset = as->buffer.mOffset,
		.size = as->buffer.size_bytes(),
		.type = type });

	buildGeometry.dstAccelerationStructure = **as;
	buildGeometry.scratchData = context.GetDevice()->getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = **scratchData.mBuffer }) + scratchData.mOffset;

	context->buildAccelerationStructuresKHR(buildGeometry, buildRanges.data());

	as->buffer.SetState(Buffer::ResourceState{
		.stage = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
		.access = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
		.queueFamily = context.QueueFamily() });

	return ref<AccelerationStructure>(as);
}

ref<AccelerationStructure> AccelerationStructure::Create(
	CommandContext& context,
	vk::ArrayProxy<vk::AccelerationStructureInstanceKHR>&& instances,
	const vk::BuildAccelerationStructureFlagBitsKHR buildFlags
) {
	BufferRange<vk::AccelerationStructureInstanceKHR> instanceBuf;

	if (instances.empty())
		instanceBuf = context.UploadData(std::vector<vk::AccelerationStructureInstanceKHR>(1), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress);
	else
		instanceBuf = context.UploadData(instances, vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	vk::AccelerationStructureGeometryInstancesDataKHR instanceGeometries{
		.data = context.GetDevice()->getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = **instanceBuf.mBuffer }) };

	vk::AccelerationStructureGeometryKHR geometry{
		.geometryType = vk::GeometryTypeKHR::eInstances,
		.geometry = instanceGeometries };

	vk::AccelerationStructureBuildRangeInfoKHR range{ .primitiveCount = (uint32_t)std::ranges::size(instances) };
	return Create(context, vk::AccelerationStructureTypeKHR::eTopLevel, geometry, range, buildFlags);
}

ref<AccelerationStructure> AccelerationStructure::Create(CommandContext& context, const float3 aabbMin, const float3 aabbMax, const bool opaque) {
	vk::AabbPositionsKHR aabb{
		.minX = aabbMin.x, .minY = aabbMin.y, .minZ = aabbMin.z,
		.maxX = aabbMax.x, .maxY = aabbMax.y, .maxZ = aabbMax.z };

	auto aabbBuf = context.UploadData(std::span<vk::AabbPositionsKHR>(&aabb, 1), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);

	vk::AccelerationStructureGeometryAabbsDataKHR aabbs{
		.data = context.GetDevice()->getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = **aabbBuf.mBuffer }) + aabbBuf.mOffset,
		.stride = sizeof(vk::AabbPositionsKHR) };

	vk::AccelerationStructureGeometryKHR geometry{
		.geometryType = vk::GeometryTypeKHR::eAabbs,
		.geometry = aabbs,
		.flags = opaque ? vk::GeometryFlagBitsKHR::eOpaque : vk::GeometryFlagBitsKHR{}};

	vk::AccelerationStructureBuildRangeInfoKHR range{ .primitiveCount = 1 };
	return Create(context, vk::AccelerationStructureTypeKHR::eBottomLevel, geometry, range);
}


}