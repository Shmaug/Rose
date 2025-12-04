/******************************************************************************
 * GPUSorting
 *
 * SPDX-License-Identifier: MIT
 * Copyright Thomas Smith 2/13/2024
 * https://github.com/b0nes164/GPUSorting
 *
 ******************************************************************************/
#pragma once
#include "Tuner.h"

class GPUSortBase
{
protected:
    const char* k_sortName;
    const uint32_t k_radixPasses;
    const uint32_t k_radix;
    const uint32_t k_maxReadBack;
    const uint32_t k_maxDispatchDimension = 65535;

    const GPUSorting::GPUSortingConfig k_sortingConfig{};
    const GPUSorting::TuningParameters k_tuningParameters{};

    uint32_t m_numKeys = 0;
    uint32_t m_partitions = 0;

    winrt::com_ptr<ID3D12Device> m_device;
    GPUSorting::DeviceInfo m_devInfo{};
    std::vector<std::wstring> m_compileArguments;

    winrt::com_ptr<ID3D12GraphicsCommandList> m_cmdList;
    winrt::com_ptr<ID3D12CommandQueue> m_cmdQueue;
    winrt::com_ptr<ID3D12CommandAllocator> m_cmdAllocator;

    winrt::com_ptr<ID3D12QueryHeap> m_queryHeap;
    winrt::com_ptr<ID3D12Fence> m_fence;
    wil::unique_event_nothrow m_fenceEvent;
    uint64_t m_nextFenceValue;
    uint64_t m_timestampFrequency;

    winrt::com_ptr<ID3D12Resource> m_sortBuffer;
    winrt::com_ptr<ID3D12Resource> m_sortPayloadBuffer;
    winrt::com_ptr<ID3D12Resource> m_altBuffer;
    winrt::com_ptr<ID3D12Resource> m_altPayloadBuffer;
    winrt::com_ptr<ID3D12Resource> m_errorCountBuffer;
    winrt::com_ptr<ID3D12Resource> m_readBackBuffer;

    UtilityKernels::InitSortInput* m_initSortInput;
    UtilityKernels::ClearErrorCount* m_clearErrorCount;
    UtilityKernels::Validate* m_validate;
    //Pairs
    GPUSortBase(
        winrt::com_ptr<ID3D12Device> _device,
        GPUSorting::DeviceInfo _deviceInfo,
        GPUSorting::ORDER sortingOrder,
        GPUSorting::KEY_TYPE keyType,
        GPUSorting::PAYLOAD_TYPE payloadType,
        const char* sortName,
        uint32_t radixPasses,
        uint32_t radix,
        uint32_t maxReadBack,
        GPUSorting::TuningParameters tuningParams) :
        k_sortName(sortName),
        k_radixPasses(radixPasses),
        k_radix(radix),
        k_maxReadBack(maxReadBack),
        m_devInfo(_deviceInfo),
        k_sortingConfig({
            GPUSorting::MODE_PAIRS,
            sortingOrder,
            keyType,
            payloadType }),
        k_tuningParameters(tuningParams)
    {
    };

    GPUSortBase(
        winrt::com_ptr<ID3D12Device> _device,
        GPUSorting::DeviceInfo _deviceInfo,
        GPUSorting::ORDER sortingOrder,
        GPUSorting::KEY_TYPE keyType,
        GPUSorting::PAYLOAD_TYPE payloadType,
        const char* sortName,
        uint32_t radixPasses,
        uint32_t radix,
        uint32_t maxReadBack) :
        GPUSortBase(
            _device,
            _deviceInfo,
            sortingOrder,
            keyType,
            payloadType,
            sortName,
            radixPasses,
            radix,
            maxReadBack,
            Tuner::GetTuningParameters(
                _deviceInfo,
                GPUSorting::MODE_PAIRS))
    {
    };

    ~GPUSortBase()
    {
    };

public:
    void TestSort(
        uint32_t testSize,
        uint32_t seed,
        bool shouldReadBack,
        bool shouldValidate)
    {
        UpdateSize(testSize);
        CreateTestInput(seed);
        PrepareSortCmdList();
        ExecuteCommandList();

        if (shouldValidate)
            ValidateOutput(true);

        if (shouldReadBack)
        {
            uint64_t readBackSize = m_numKeys < k_maxReadBack ? m_numKeys : k_maxReadBack;
            ReadbackPreBarrier(m_cmdList, m_sortBuffer);
            m_cmdList->CopyBufferRegion(m_readBackBuffer.get(), 0, m_sortBuffer.get(), 0, readBackSize * sizeof(uint32_t));
            ReadbackPostBarrier(m_cmdList, m_sortBuffer);
            ExecuteCommandList();
            std::vector<uint32_t> vecOut = ReadBackBuffer(m_readBackBuffer, (uint32_t)readBackSize);

            printf("---------------KEYS---------------\n");
            for (uint32_t i = 0; i < vecOut.size(); ++i)
                printf("%u %u \n", i, vecOut[i]);

            if (k_sortingConfig.sortingMode == GPUSorting::MODE_PAIRS)
            {
                ReadbackPreBarrier(m_cmdList, m_sortPayloadBuffer);
                m_cmdList->CopyBufferRegion(m_readBackBuffer.get(), 0, m_sortPayloadBuffer.get(), 0, (uint32_t)readBackSize * sizeof(uint32_t));
                ReadbackPostBarrier(m_cmdList, m_sortPayloadBuffer);
                ExecuteCommandList();
                vecOut = ReadBackBuffer(m_readBackBuffer, (uint32_t)readBackSize);

                printf("\n \n \n");
                printf("---------------PAYLOADS---------------\n");
                for (uint32_t i = 0; i < vecOut.size(); ++i)
                    printf("%u %u \n", i, vecOut[i]);
            }
        }
    }

    void BatchTiming(
        uint32_t inputSize,
        uint32_t batchSize,
        uint32_t seed,
        GPUSorting::ENTROPY_PRESET entropyPreset)
    {
        UpdateSize(inputSize);

        const float entLookup[5] = { 1.0f, .811f, .544f, .337f, .201f };
        printf("Beginning ");
        printf(k_sortName);
        PrintSortingConfig(k_sortingConfig);
        printf("batch timing test at:\n");
        printf("Size: %u\n", inputSize);
        printf("Entropy: %f bits\n", entLookup[entropyPreset]);
        printf("Test size: %u\n", batchSize);
        double totalTime = 0.0;
        for (uint32_t i = 0; i <= batchSize; ++i)
        {
            double t = TimeSort(i + seed, entropyPreset);
            if (i)
                totalTime += t;

            if ((i & 7) == 0)
                printf(".");
        }
        printf("\n");

        printf("Total time elapsed: %f\n", totalTime);
        printf("Estimated speed at %u 32-bit elements: %E keys/sec\n\n", inputSize, inputSize / totalTime * batchSize);
    }

    virtual bool TestAll()
    {
        printf("Beginning ");
        printf(k_sortName);
        PrintSortingConfig(k_sortingConfig);
        printf("test all. \n");

        uint32_t sortPayloadTestsPassed = 0;
        const uint32_t testEnd = k_tuningParameters.partitionSize * 2 + 1;
        for (uint32_t i = k_tuningParameters.partitionSize; i < testEnd; ++i)
        {
            sortPayloadTestsPassed += ValidateSort(i, i);

            if (!(i & 127))
                printf(".");
        }

        printf("\n");
        printf("%u / %u passed. \n", sortPayloadTestsPassed, k_tuningParameters.partitionSize + 1);

        printf("Beginning large size tests\n");
        sortPayloadTestsPassed += ValidateSort(1 << 21, 5);
        sortPayloadTestsPassed += ValidateSort(1 << 22, 7);
        sortPayloadTestsPassed += ValidateSort(1 << 23, 11);

        uint32_t testsExpected = k_tuningParameters.partitionSize + 1 + 3;
        if (sortPayloadTestsPassed == testsExpected)
        {
            printf("%u / %u  All tests passed. \n\n", testsExpected, testsExpected);
            return true;
        }
        else
        {
            printf("%u / %u  Test failed. \n\n", sortPayloadTestsPassed, testsExpected);
            return false;
        }

        return sortPayloadTestsPassed == testsExpected;
    }

protected:
    virtual void SetCompileArguments()
    {
        if (k_tuningParameters.shouldLockWavesTo32)
            m_compileArguments.push_back(L"-DLOCK_TO_W32");

        switch (k_tuningParameters.keysPerThread)
        {
        case 5:
            m_compileArguments.push_back(L"-DKEYS_PER_THREAD_5");
        case 7:
            m_compileArguments.push_back(L"-DKEYS_PER_THREAD_7");
            break;
        case 15:
            break;
        default:
#ifdef _DEBUG
            printf("KeysPerThread define missing!");
#endif
            break;
        }

        switch (k_tuningParameters.threadsPerThreadblock)
        {
        case 256:
            m_compileArguments.push_back(L"-DD_DIM_256");
            break;
        case 512:
            break;
        default:
#ifdef _DEBUG
            printf("ThreadsPerThread define missing!");
#endif
            break;
        }

        switch (k_tuningParameters.partitionSize)
        {
        case 1792:
            m_compileArguments.push_back(L"-DPART_SIZE_1792");
            break;
        case 2560:
            m_compileArguments.push_back(L"-DPART_SIZE_2560");
            break;
        case 3584:
            m_compileArguments.push_back(L"-DPART_SIZE_3584");
            break;
        case 3840:
            m_compileArguments.push_back(L"-DPART_SIZE_3840");
            break;
        case 7680:
            break;
        default:
#ifdef _DEBUG
            printf("PartitionSize define missing!");
#endif
            break;
        }

        switch (k_tuningParameters.totalSharedMemory)
        {
        case 4096:
            m_compileArguments.push_back(L"-DD_TOTAL_SMEM_4096");
            break;
        case 7936:
            break;
        default:
#ifdef _DEBUG
            printf("TotalSharedMemoryDefine define missing!");
#endif
            break;
        }

        if (k_sortingConfig.sortingOrder == GPUSorting::ORDER_ASCENDING)
            m_compileArguments.push_back(L"-DSHOULD_ASCEND");

        switch (k_sortingConfig.sortingKeyType)
        {
        case GPUSorting::KEY_UINT32:
            m_compileArguments.push_back(L"-DKEY_UINT");
            break;
        case GPUSorting::KEY_INT32:
            m_compileArguments.push_back(L"-DKEY_INT");
            break;
        case GPUSorting::KEY_FLOAT32:
            m_compileArguments.push_back(L"-DKEY_FLOAT");
            break;
        }

        if (k_sortingConfig.sortingMode == GPUSorting::MODE_PAIRS)
        {
            m_compileArguments.push_back(L"-DSORT_PAIRS");
            switch (k_sortingConfig.sortingPayloadType)
            {
            case GPUSorting::PAYLOAD_UINT32:
                m_compileArguments.push_back(L"-DPAYLOAD_UINT");
                break;
            case GPUSorting::PAYLOAD_INT32:
                m_compileArguments.push_back(L"-DPAYLOAD_INT");
                break;
            case GPUSorting::PAYLOAD_FLOAT32:
                m_compileArguments.push_back(L"-DPAYLOAD_FLOAT");
                break;
            }
        }

        if (m_devInfo.Supports16BitTypes)
        {
            m_compileArguments.push_back(L"-enable-16bit-types");
            m_compileArguments.push_back(L"-DENABLE_16_BIT");
        }

        m_compileArguments.push_back(L"-O3");
#ifdef _DEBUG
        m_compileArguments.push_back(L"-Zi");
#endif
    }

    virtual void InitUtilityComputeShaders()
    {
        const std::filesystem::path path = "Shaders/Utility.hlsl";
        m_initSortInput = new UtilityKernels::InitSortInput(m_device, m_devInfo, m_compileArguments, path);
        m_clearErrorCount = new UtilityKernels::ClearErrorCount(m_device, m_devInfo, m_compileArguments, path);
        m_validate = new UtilityKernels::Validate(m_device, m_devInfo, m_compileArguments, path);
    }

    virtual void InitComputeShaders() = 0;

    virtual void UpdateSize(uint32_t size) = 0;

    virtual void DisposeBuffers() = 0;

    virtual void InitStaticBuffers() = 0;

    virtual void InitBuffers(
        const uint32_t numKeys,
        const uint32_t threadBlocks) = 0;

    void Initialize()
    {
        InitUtilityComputeShaders();
        InitComputeShaders();

        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        winrt::check_hresult(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(m_cmdQueue.put())));
        winrt::check_hresult(m_device->CreateCommandAllocator(desc.Type, IID_PPV_ARGS(m_cmdAllocator.put())));
        winrt::check_hresult(m_device->CreateCommandList(0, desc.Type, m_cmdAllocator.get(), nullptr, IID_PPV_ARGS(m_cmdList.put())));
        winrt::check_hresult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.put())));
        m_fenceEvent.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
        m_nextFenceValue = 1;

        D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
        queryHeapDesc.Count = 2;
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        winrt::check_hresult(m_device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(m_queryHeap.put())));
        winrt::check_hresult(m_cmdQueue->GetTimestampFrequency(&m_timestampFrequency));

        InitStaticBuffers();
    }

    void ExecuteCommandList()
    {
        winrt::check_hresult(m_cmdList->Close());
        ID3D12CommandList* commandLists[] = { m_cmdList.get() };
        m_cmdQueue->ExecuteCommandLists(1, commandLists);
        winrt::check_hresult(m_cmdQueue->Signal(m_fence.get(), m_nextFenceValue));
        winrt::check_hresult(m_fence->SetEventOnCompletion(m_nextFenceValue, m_fenceEvent.get()));
        ++m_nextFenceValue;
        winrt::check_hresult(m_fenceEvent.wait());
        winrt::check_hresult(m_cmdAllocator->Reset());
        winrt::check_hresult(m_cmdList->Reset(m_cmdAllocator.get(), nullptr));
    }
};
