#include <Rose/Core/Instance.hpp>
#include <Rose/Algorithm/RadixSort/RadixSort.hpp>

#include <iostream>
#include <vulkan/vulkan_hash.hpp>

int main(int argc, const char** argv) {
	std::span args = { argv, (size_t)argc };

	using namespace RoseEngine;

	ref<Instance> instance = Instance::Create({}, { "VK_LAYER_KHRONOS_validation" });
	ref<Device>   device   = Device::Create(*instance, (*instance)->enumeratePhysicalDevices()[0]);

	RadixSort radixSort;

	// run on gpu
	bool allPassed = true;

	ref<CommandContext> context = CommandContext::Create(device, vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer);

	// sort without payload

	for (uint32_t N : { 10, 100, 1000, 10000, 1000000 }) {
		std::vector<uint32_t> inputData(N);
		for (size_t i = 0; i < inputData.size(); i++) {
			size_t s = 0;
			VULKAN_HPP_HASH_COMBINE(s, N);
			VULKAN_HPP_HASH_COMBINE(s, i);
			inputData[i] = (uint32_t)s;
		}
		std::vector<uint32_t> sorted_cpu = inputData;
		std::ranges::sort(sorted_cpu);

		{
			auto dataCpu = Buffer::Create(*device, inputData, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst).cast<uint32_t>();
			auto dataGpu = Buffer::Create(*device, dataCpu.size_bytes()).cast<uint32_t>();

			context->Begin();

			context->Copy(dataCpu, dataGpu);

			radixSort(*context, dataGpu);

			context->Copy(dataGpu, dataCpu);

			context->Submit();
			device->Wait();

			std::ranges::copy(dataCpu, inputData.begin());
		}

		bool passed = true;
		for (size_t i = 0; i < inputData.size(); i++) {
			if (inputData[i] != sorted_cpu[i]) {
				passed = false;
				allPassed = false;
			}
		}
		std::cout << "N = " << N << ": " << (passed ? "PASSED" : "FAILED") << std::endl ;
	}

	// sort with fused payload

	for (uint32_t N : { 10, 100, 1000, 10000, 1000000 }) {
		std::vector<uint2> inputData(N);
		for (size_t i = 0; i < inputData.size(); i++) {
			size_t s = 0;
			VULKAN_HPP_HASH_COMBINE(s, N);
			VULKAN_HPP_HASH_COMBINE(s, i);
			inputData[i] = uint2((uint32_t)s, (uint32_t)i);
		}
		std::vector<uint2> sorted_cpu = inputData;
		std::ranges::sort(sorted_cpu, {}, &uint2::x);

		{
			auto dataCpu = Buffer::Create(*device, inputData, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst).cast<uint2>();
			auto dataGpu = Buffer::Create(*device, dataCpu.size_bytes()).cast<uint2>();

			context->Begin();

			context->Copy(dataCpu, dataGpu);

			radixSort(*context, dataGpu);

			context->Copy(dataGpu, dataCpu);

			context->Submit();
			device->Wait();

			std::ranges::copy(dataCpu, inputData.begin());
		}

		bool passed = true;
		for (size_t i = 0; i < inputData.size(); i++) {
			if (inputData[i] != sorted_cpu[i]) {
				passed = false;
				allPassed = false;
			}
		}
		std::cout << "N = " << N << ": " << (passed ? "PASSED" : "FAILED") << std::endl ;
	}

	if (allPassed) {
		std::cout << "SUCCESS" << std::endl;
		return EXIT_SUCCESS;
	} else {
		std::cout << "FAILURE" << std::endl;
		return EXIT_FAILURE;
	}
}