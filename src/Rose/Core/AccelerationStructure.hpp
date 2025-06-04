#pragma once

#include <ranges>

#include "Buffer.hpp"
#include "MathTypes.hpp"

namespace RoseEngine {

class CommandContext;

class AccelerationStructure {
private:
	vk::raii::AccelerationStructureKHR accelerationStructure = nullptr;
	BufferView buffer = {};

	inline AccelerationStructure() {}

public:
	static ref<AccelerationStructure> Create(
		CommandContext& context,
		const vk::AccelerationStructureTypeKHR type,
		const vk::ArrayProxy<const vk::AccelerationStructureGeometryKHR>& geometries,
		const vk::ArrayProxy<const vk::AccelerationStructureBuildRangeInfoKHR>& buildRanges,
		const vk::BuildAccelerationStructureFlagBitsKHR buildFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
	);
	static ref<AccelerationStructure> Create(
		CommandContext& context,
		vk::ArrayProxy<vk::AccelerationStructureInstanceKHR>&& instances,
		const vk::BuildAccelerationStructureFlagBitsKHR buildFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
	);
	static ref<AccelerationStructure> Create(CommandContext& context, const float3 aabbMin, const float3 aabbMax, const bool opaque = true);

	inline ~AccelerationStructure() {
		accelerationStructure = nullptr;
		buffer = {};
	}

	inline       vk::raii::AccelerationStructureKHR& operator*()        { return accelerationStructure; }
	inline const vk::raii::AccelerationStructureKHR& operator*() const  { return accelerationStructure; }
	inline       vk::raii::AccelerationStructureKHR* operator->()       { return &accelerationStructure; }
	inline const vk::raii::AccelerationStructureKHR* operator->() const { return &accelerationStructure; }

	inline vk::DeviceAddress GetDeviceAddress(const Device& device) const {
		return device->getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{ .accelerationStructure = *accelerationStructure });
	}
};

}