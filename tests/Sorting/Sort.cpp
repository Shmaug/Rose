#include <Rose/Core/Instance.hpp>
#include <Rose/Sorting/DeviceRadixSort.h>

#include <iostream>
#include <vulkan/vulkan_hash.hpp>
#include <numeric>
#include <algorithm>

int main(int argc, const char** argv) {
    std::span args = { argv, (size_t)argc };

    using namespace RoseEngine;

    ref<Instance> instance = Instance::Create({}, { "VK_LAYER_KHRONOS_validation" });
    ref<Device>   device   = Device::Create(*instance, (*instance)->enumeratePhysicalDevices()[0]);

    DeviceRadixSort radixSort;

    // run on gpu
    bool allPassed = true;

    ref<CommandContext> context = CommandContext::Create(device, vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer);

    // sort with separate key and payload buffers
    for (uint32_t N : { 10, 100, 1000, 10000, 1000000 }) {
        std::vector<uint32_t> keys(N);
        std::vector<uint32_t> payloads(N);
        for (size_t i = 0; i < N; i++) {
            size_t s = 0;
            VULKAN_HPP_HASH_COMBINE(s, N);
            VULKAN_HPP_HASH_COMBINE(s, i);
            keys[i] = (uint32_t)s;
            payloads[i] = (uint32_t)i;
        }
        // CPU sort for reference
        std::vector<uint32_t> keys_cpu = keys;
        std::vector<uint32_t> payloads_cpu = payloads;
        std::vector<size_t> indices(N);
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) { return keys_cpu[a] < keys_cpu[b]; });
        std::vector<uint32_t> keys_sorted_cpu(N), payloads_sorted_cpu(N);
        for (size_t i = 0; i < N; i++) {
            keys_sorted_cpu[i] = keys_cpu[indices[i]];
            payloads_sorted_cpu[i] = payloads_cpu[indices[i]];
        }

        // GPU sort
        auto keysBuf = Buffer::Create(*device, keys, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst).cast<uint32_t>();
        auto payloadsBuf = Buffer::Create(*device, payloads, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst).cast<uint32_t>();

        context->Begin();
        radixSort(*context, keysBuf, payloadsBuf);
        context->Submit();
        device->Wait();

        std::vector<uint32_t> keys_gpu(N), payloads_gpu(N);
        std::ranges::copy(keysBuf, keys_gpu.begin());
        std::ranges::copy(payloadsBuf, payloads_gpu.begin());

        bool passed = true;
        for (size_t i = 0; i < N; i++) {
            if (keys_gpu[i] != keys_sorted_cpu[i] || payloads_gpu[i] != payloads_sorted_cpu[i]) {
                passed = false;
                allPassed = false;
            }
        }
        std::cout << "N = " << N << " (split buffers): " << (passed ? "PASSED" : "FAILED") << std::endl;
    }

    if (allPassed) {
        std::cout << "SUCCESS" << std::endl;
        return EXIT_SUCCESS;
    } else {
        std::cout << "FAILURE" << std::endl;
        return EXIT_FAILURE;
    }
}
