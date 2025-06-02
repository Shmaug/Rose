#include <Rose/Core/Instance.hpp>
#include <Rose/Algorithm/PrefixSum/PrefixSum.hpp>

#include <iostream>
#include <vulkan/vulkan_hash.hpp>

int main(int argc, const char** argv) {
	std::span args = { argv, (size_t)argc };

	using namespace RoseEngine;

	ref<Instance> instance = Instance::Create({}, { "VK_LAYER_KHRONOS_validation" });
	ref<Device>   device   = Device::Create(*instance, (*instance)->enumeratePhysicalDevices()[0]);

	PrefixSumExclusive prefixSum;

	// run on gpu
	bool allPassed = true;

	ref<CommandContext> context = CommandContext::Create(device, vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer);

	for (uint32_t N : { 10, 100, 10000, 1000000 }) {
		std::vector<uint32_t> inputData(N);
		for (size_t i = 0; i < inputData.size(); i++) {
			inputData[i] = 1;
		}
		std::vector<uint32_t> sorted_cpu = inputData;
		{
			uint32_t sum = 0;
			for (uint32_t& x : sorted_cpu) {
				uint32_t x0 = x;
				x = sum;
				sum += x0;
			}
		}

		{
			auto dataCpu = Buffer::Create(*device, inputData, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst).cast<uint32_t>();
			auto dataGpu = Buffer::Create(*device, dataCpu.size_bytes(), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst).cast<uint32_t>();

			context->Begin();

			context->Copy(dataCpu, dataGpu);

			prefixSum(*context, dataGpu);

			context->Copy(dataGpu, dataCpu);

			context->Submit();
			device->Wait();

			std::ranges::copy(dataCpu, inputData.begin());
		}

		bool passed = true;
		for (size_t i = 0; i < inputData.size(); i++) {
			//std::cout << inputData[i] << ", ";
			if (inputData[i] != sorted_cpu[i]) {
				passed = false;
				allPassed = false;
				std::cout << "Mismatch at index " << i << ": " << inputData[i] << " != " << sorted_cpu[i] << std::endl;
				break;
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