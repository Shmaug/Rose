#include <iostream>
#include "CommandContext.hpp"

namespace RoseEngine {

void CommandContext::Begin() {
	if (!*mCommandPool) {
		mCommandPool = (*mDevice)->createCommandPool(vk::CommandPoolCreateInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = mQueueFamily });
	}

	if (!*mCommandBuffer) {
		auto commandBuffers = (*mDevice)->allocateCommandBuffers(vk::CommandBufferAllocateInfo{
			.commandPool = *mCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1 });
		mCommandBuffer = std::move(commandBuffers[0]);
	}

	if (mLastSubmit > 0)
		mDevice->Wait(mLastSubmit);

	mCommandBuffer.reset();
	mCommandBuffer.begin(vk::CommandBufferBeginInfo{});

	if (!mCache.mNewBuffers.empty()) {
		for (auto& [usage, bufs] : mCache.mNewBuffers) {
			for (auto& b : bufs) {
				// if we aren't the only owner, release it
				if (b.buffer.mBuffer     && b.buffer.mBuffer.use_count() > 1) b.buffer = {};
				if (b.hostBuffer.mBuffer && b.hostBuffer.mBuffer.use_count() > 1) b.hostBuffer = {};
				if (b.buffer || b.hostBuffer)
					mCache.mBuffers[usage].emplace_back(std::move(b));
			}
		}
		mCache.mNewBuffers.clear();
		for (auto& [usage, bufs] : mCache.mBuffers)
			std::ranges::sort(bufs, {}, &CachedData::CachedBuffers::size);
	}

	if (!mCache.mNewImages.empty()) {
		for (auto&[info, images] : mCache.mNewImages) {
			for (auto& image : images) {
				if (image && image.use_count() == 1)
					mCache.mImages[info].emplace_back(std::move(image));
			}
			images.clear();
		}
	}

	if (!mCache.mNewDescriptorSets.empty()) {
		for (auto&[layout, sets] : mCache.mNewDescriptorSets)
			for (auto& s : sets) {
				if (s && s.use_count() == 1)
					mCache.mDescriptorSets[layout].emplace_back(std::move(s));
			}
		mCache.mNewDescriptorSets.clear();
	}
}

void CommandContext::PushDebugLabel(const std::string& name, const float4 color) const {
	if (!mDevice->DebugUtilsEnabled()) return;
	mCommandBuffer.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
		.pLabelName = name.c_str(),
		.color = { { color.x, color.y, color.z, color.w } }	});
}
void CommandContext::PopDebugLabel() const {
	if (!mDevice->DebugUtilsEnabled()) return;
	mCommandBuffer.endDebugUtilsLabelEXT();
}

uint64_t CommandContext::Submit(
	const uint32_t queueIndex,
	const vk::ArrayProxy<const vk::Semaphore>&          signalSemaphores,
	const vk::ArrayProxy<const uint64_t>&               signalValues,
	const vk::ArrayProxy<const vk::Semaphore>&          waitSemaphores,
	const vk::ArrayProxy<const vk::PipelineStageFlags>& waitStages,
	const vk::ArrayProxy<const uint64_t>&               waitValues) {

	mCommandBuffer.end();

	vk::StructureChain<vk::SubmitInfo, vk::TimelineSemaphoreSubmitInfo> submitInfoChain = {};
	auto& submitInfo = submitInfoChain.get<vk::SubmitInfo>();
	auto& timelineSubmitInfo = submitInfoChain.get<vk::TimelineSemaphoreSubmitInfo>();

	submitInfo.setCommandBuffers(*mCommandBuffer);

	uint64_t signalValue = mDevice->IncrementTimelineSignal();

	std::vector<vk::Semaphore> semaphores(signalSemaphores.size() + 1);
	std::vector<uint64_t> values(signalValues.size() + 1);
	std::ranges::copy(signalSemaphores, semaphores.begin());
	std::ranges::copy(signalValues, values.begin());
	semaphores.back() = *mDevice->TimelineSemaphore();
	values.back() = signalValue;

	submitInfo.setSignalSemaphores(semaphores);
	timelineSubmitInfo.setSignalSemaphoreValues(values);

	submitInfo.setWaitSemaphores(waitSemaphores);
	submitInfo.setWaitDstStageMask(waitStages);
	timelineSubmitInfo.setWaitSemaphoreValues(waitValues);

	(*mDevice)->getQueue(mQueueFamily, queueIndex).submit( submitInfo );

	mLastSubmit = signalValue;

	return signalValue;
}

void CommandContext::AllocateDescriptorPool() {
	std::vector<vk::DescriptorPoolSize> poolSizes {
		vk::DescriptorPoolSize{ vk::DescriptorType::eSampler,              std::min(16384u, mDevice->Limits().maxDescriptorSetSamplers) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, std::min(16384u, mDevice->Limits().maxDescriptorSetSampledImages) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eInputAttachment,      std::min(16384u, mDevice->Limits().maxDescriptorSetInputAttachments) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eSampledImage,         std::min(16384u, mDevice->Limits().maxDescriptorSetSampledImages) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageImage,         std::min(16384u, mDevice->Limits().maxDescriptorSetStorageImages) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer,        std::min(16384u, mDevice->Limits().maxDescriptorSetUniformBuffers) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBufferDynamic, std::min(16384u, mDevice->Limits().maxDescriptorSetUniformBuffersDynamic) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBuffer,        std::min(16384u, mDevice->Limits().maxDescriptorSetStorageBuffers) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBufferDynamic, std::min(16384u, mDevice->Limits().maxDescriptorSetStorageBuffersDynamic) }
	};
	mCachedDescriptorPools.push_front((*mDevice)->createDescriptorPool(
		vk::DescriptorPoolCreateInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = 8192 }
		.setPoolSizes(poolSizes)));
}

DescriptorSets CommandContext::AllocateDescriptorSets(const vk::ArrayProxy<const vk::DescriptorSetLayout>& layouts, const vk::ArrayProxy<const uint32_t>& variableSetCounts) {
	if (mCachedDescriptorPools.empty())
		AllocateDescriptorPool();

	vk::DescriptorSetVariableDescriptorCountAllocateInfo descriptorCounts {
		.descriptorSetCount = (uint32_t)variableSetCounts.size(),
		.pDescriptorCounts = variableSetCounts.data()
	};

	std::vector<vk::raii::DescriptorSet> sets;
	try {
		sets = (*mDevice)->allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ .pNext = variableSetCounts.empty() ? nullptr : &descriptorCounts, .descriptorPool = *mCachedDescriptorPools.front() }.setSetLayouts(layouts));
	} catch(vk::OutOfPoolMemoryError e) {
		AllocateDescriptorPool();
		sets = (*mDevice)->allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ .pNext = variableSetCounts.empty() ? nullptr : &descriptorCounts, .descriptorPool = *mCachedDescriptorPools.front() }.setSetLayouts(layouts));
	}

	return sets;
}

ref<DescriptorSets> CommandContext::GetDescriptorSets(const PipelineLayout& pipelineLayout) {
	if (pipelineLayout.GetDescriptorSetLayouts().empty())
		return nullptr;

	ref<DescriptorSets> descriptorSets = nullptr;

	auto it = mCache.mDescriptorSets.find(**pipelineLayout);
	if (it != mCache.mDescriptorSets.end() && it->second.size() > 0) {
		descriptorSets = it->second.back();
		it->second.pop_back();
	}

	if (!descriptorSets) {
		std::vector<vk::DescriptorSetLayout> setLayouts;
		for (const auto& l : pipelineLayout.GetDescriptorSetLayouts())
			setLayouts.emplace_back(**l);
		descriptorSets = make_ref<DescriptorSets>(std::move(AllocateDescriptorSets(setLayouts)));
	}

	mCache.mNewDescriptorSets[**pipelineLayout].emplace_back(descriptorSets);

	return descriptorSets;
}

ref<Image> CommandContext::GetTransientImage(const ImageInfo& info) {
	ref<Image> image = {};
	if (auto it_ = mCache.mImages.find(info); it_ != mCache.mImages.end()) {
		auto& q = it_->second;
		if (!q.empty()) {
			image = std::move(q.back());
			q.pop_back();
		}
	}

	if (!image) image = Image::Create(GetDevice(), info);

	return mCache.mNewImages[info].emplace_back(image);
}


uint32_t align16(uint32_t s) {
	s = (s + 3)&(~3);
	if (s*4 == 12) {
		s += 4;
	}
	return s;
};

struct DescriptorSetWriter {
	union DescriptorInfo {
		vk::DescriptorBufferInfo buffer;
		vk::DescriptorImageInfo  image;
		vk::BufferView           texelBuffer;
		vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructure;
	};
	std::vector<DescriptorInfo> descriptorInfos;
	std::vector<vk::WriteDescriptorSet> writes;


	PairMap<std::vector<std::byte>, uint32_t, uint32_t> uniforms;
	std::vector<std::pair<uint32_t, std::span<const std::byte, std::dynamic_extent>>> pushConstants;

	std::vector<vk::DescriptorSet> descriptorSets;

	vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eComputeShader;

	vk::WriteDescriptorSet WriteDescriptor(const ShaderDescriptorBinding& binding, uint32_t arrayIndex, uint32_t bindingOffset) {
		return vk::WriteDescriptorSet{
			.dstSet = descriptorSets[binding.setIndex],
			.dstBinding = binding.bindingIndex + bindingOffset,
			.dstArrayElement = arrayIndex,
			.descriptorCount = 1,
			.descriptorType = binding.descriptorType };
	}
	void WriteBuffer(const ShaderDescriptorBinding& binding, uint32_t arrayIndex, uint32_t bindingOffset, const vk::DescriptorBufferInfo& data) {
		vk::WriteDescriptorSet& w = writes.emplace_back(WriteDescriptor(binding, arrayIndex, bindingOffset));
		DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});
		info.buffer = data;
		w.setBufferInfo(info.buffer);
	}
	void WriteTexelBuffer(const ShaderDescriptorBinding& binding, uint32_t arrayIndex, uint32_t bindingOffset, const TexelBufferView& data) {
		vk::WriteDescriptorSet& w = writes.emplace_back(WriteDescriptor(binding, arrayIndex, bindingOffset));
		DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});
		info.texelBuffer = *data;
		w.setTexelBufferView(info.texelBuffer);
	}
	void WriteImage(const ShaderDescriptorBinding& binding, uint32_t arrayIndex, uint32_t bindingOffset, const vk::DescriptorImageInfo& data) {
		vk::WriteDescriptorSet& w = writes.emplace_back(WriteDescriptor(binding, arrayIndex, bindingOffset));
		DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});
		info.image = data;
		w.setImageInfo(info.image);
	}
	void WriteAccelerationStructure(const ShaderDescriptorBinding& binding, uint32_t arrayIndex, uint32_t bindingOffset, const vk::WriteDescriptorSetAccelerationStructureKHR& data) {
		vk::WriteDescriptorSet& w = writes.emplace_back(WriteDescriptor(binding, arrayIndex, bindingOffset));
		DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});
		info.accelerationStructure = data;
		w.setPNext(&info);
	}

	void Write(CommandContext& context, const ShaderParameter& parameter, const ShaderParameterBinding& binding, uint32_t constantOffset = 0, uint32_t bindingOffset = 0) {
		for (const auto&[id, param] : parameter) {
			uint32_t arrayIndex = 0;

			bool isArrayElement = std::holds_alternative<size_t>(id);
			if (isArrayElement) {
				arrayIndex = std::get<size_t>(id);
				uint32_t arraySize = std::visit(
					overloads {
						[](const ShaderStructBinding& b) { return b.arraySize; },
						[](const ShaderDescriptorBinding& b) { return b.arraySize; },
						[](const ShaderConstantBinding& b) { return b.arraySize; },
						[](const ShaderVertexAttributeBinding& b) { return 1u; },
					},
					binding.raw_variant());
				if (arrayIndex >= arraySize) {
					std::cout << "Warning array index " << arrayIndex << " which is out of bounds for array size " << arraySize << std::endl;
				}
			} else if (binding.find(id) == binding.end()) {
				std::cout << "Error: No parameter " << id << " exists in pipeline." << std::endl;
				//PrintBinding(binding);
			}

			const ShaderParameterBinding& paramBinding = isArrayElement ? binding : binding.at(id);

			uint32_t offset = constantOffset;

			if (const auto* v = param.get_if<std::monostate>()) {
				if (const auto* binding = paramBinding.get_if<ShaderStructBinding>()) {
					if (isArrayElement) {
						if (arrayIndex >= binding->arraySize) {
							std::cout << "Warning: Array index out of bounds (" << arrayIndex << " >= " << binding->arraySize << ")" << std::endl;
							continue;
						} else {
							bindingOffset  += binding->descriptorStride * arrayIndex;
							constantOffset += binding->uniformStride * arrayIndex;
						}
					}
				}
			} else if (const auto* v = param.get_if<ConstantParameter>()) {
				if (const auto* constantBinding = paramBinding.get_if<ShaderConstantBinding>()) {
					// binding a constant to a variable inside a uniform buffer/push constant
					{
						uint32_t bindingSize = constantBinding->typeSize;
						if (!isArrayElement) bindingSize *= constantBinding->arraySize;
						if (v->size() > bindingSize)
							std::cout << "Warning: Binding constant parameter of size " << v->size() << " to binding of size " << bindingSize << std::endl;
					}

					offset += constantBinding->offset;
					offset += arrayIndex * align16(constantBinding->typeSize);

					if (constantBinding->pushConstant) {
						pushConstants.emplace_back(offset, *v);
					} else {
						auto& u = uniforms[{constantBinding->setIndex, constantBinding->bindingIndex + bindingOffset}];
						if (offset + v->size() > u.size())
							u.resize(offset + v->size());
						std::memcpy(u.data() + offset, v->data(), v->size());
					}
				} else if (const auto* descriptorBinding = paramBinding.get_if<ShaderDescriptorBinding>()) {
					// binding a constant to a uniform/storage buffer
					if (descriptorBinding->descriptorType == vk::DescriptorType::eUniformBuffer || descriptorBinding->descriptorType == vk::DescriptorType::eStorageBuffer) {
						auto buffer = context.UploadData(*v, descriptorBinding->descriptorType == vk::DescriptorType::eUniformBuffer ? vk::BufferUsageFlagBits::eUniformBuffer : vk::BufferUsageFlagBits::eStorageBuffer);
						context.AddBarrier(buffer, Buffer::ResourceState{
							.stage  = stage,
							.access = descriptorBinding->writable ? vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite : vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
							.queueFamily = context.QueueFamily() });
						WriteBuffer(*descriptorBinding, arrayIndex, bindingOffset, vk::DescriptorBufferInfo{
							.buffer = **buffer.mBuffer,
							.offset = buffer.mOffset,
							.range  = buffer.size() });
					} else
						std::cout << "Warning: Attempting to bind constant parameter to non-constant binding" << std::endl;
				} else
					std::cout << "Warning: Attempting to bind constant parameter to non-constant binding" << std::endl;
			} else {
				if (const auto* descriptorBinding = paramBinding.get_if<ShaderDescriptorBinding>()) {
					if (const auto* v = param.get_if<BufferParameter>()) {
						const auto& buffer = *v;
						if (buffer.empty()) continue;
						context.AddBarrier(*v, Buffer::ResourceState{
							.stage  = stage,
							.access = descriptorBinding->writable ? vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite : vk::AccessFlagBits2::eShaderRead,
							.queueFamily = context.QueueFamily() });
						WriteBuffer(*descriptorBinding, arrayIndex, bindingOffset, vk::DescriptorBufferInfo{
							.buffer = **buffer.mBuffer,
							.offset = buffer.mOffset,
							.range  = buffer.size() });
					} else if (const auto* v = param.get_if<TexelBufferParameter>()) {
						const auto& buffer = *v;
						if (buffer.GetBuffer().empty()) continue;
						context.AddBarrier(v->GetBuffer(), Buffer::ResourceState{
							.stage  = stage,
							.access = descriptorBinding->writable ? vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite : vk::AccessFlagBits2::eShaderRead,
							.queueFamily = context.QueueFamily() });
						WriteTexelBuffer(*descriptorBinding, arrayIndex, bindingOffset, buffer);
					} else if (const auto* v = param.get_if<ImageParameter>()) {
						const auto& [image, layout, sampler] = *v;
						if (!image && !sampler) continue;
						context.AddBarrier(image, Image::ResourceState{
							.layout = layout,
							.stage  = stage,
							.access = descriptorBinding->writable ? vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite : vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
							.queueFamily = context.QueueFamily() });
						WriteImage(*descriptorBinding, arrayIndex, bindingOffset, vk::DescriptorImageInfo{
							.sampler     = sampler ? **sampler : nullptr,
							.imageView   = image   ? *image    : nullptr,
							.imageLayout = layout });
					} else if (const auto* v = param.get_if<AccelerationStructureParameter>()) {
						const auto& as = *v;
						if (!as) continue;
						WriteAccelerationStructure(*descriptorBinding, arrayIndex, bindingOffset, vk::WriteDescriptorSetAccelerationStructureKHR{}.setAccelerationStructures(***as));
					}
				} else {
					std::cout << "Warning: Attempting to bind descriptor parameter to non-descriptor binding" << std::endl;
				}
			}

			Write(context, param, paramBinding, offset);
		}
	}
};

size_t GetDescriptorCount(const ShaderParameterBinding& param) {
	size_t count = 0;
	if (const auto* b = param.get_if<ShaderStructBinding>())
		count = b->arraySize * b->descriptorStride;
	if (const auto* b = param.get_if<ShaderDescriptorBinding>())
		count = b->arraySize;
	if (const auto* b = param.get_if<ShaderConstantBinding>())
		if (!b->pushConstant)
			count = 1;
	for (const auto&[name, p] : param)
		count += GetDescriptorCount(p);
	return count;
}

void CommandContext::UpdateDescriptorSets(const DescriptorSets& descriptorSets, const ShaderParameter& rootParameter, const PipelineLayout& pipelineLayout) {
	if (pipelineLayout.GetDescriptorSetLayouts().empty())
		return;

	DescriptorSetWriter w = {};
	w.stage = pipelineLayout.PipelineStageMask();
	for (const auto& s : descriptorSets)
		w.descriptorSets.emplace_back(*s);
	w.descriptorInfos.reserve(GetDescriptorCount(pipelineLayout.RootBinding()));
	w.Write(*this, rootParameter, pipelineLayout.RootBinding());

	// upload uniforms and write uniform buffer descriptors

	for (const auto&[setBinding, data] : w.uniforms) {
		const auto [setIndex,bindingIndex] = setBinding;

		auto buffer = UploadData(data, vk::BufferUsageFlagBits::eUniformBuffer);

		AddBarrier(buffer, Buffer::ResourceState{
			.stage  = w.stage,
			.access = vk::AccessFlagBits2::eUniformRead,
			.queueFamily = QueueFamily() });

		w.WriteBuffer(
			ShaderDescriptorBinding{
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.setIndex = setIndex,
				.bindingIndex = bindingIndex },
			0, 0,
			vk::DescriptorBufferInfo{
				.buffer = **buffer.mBuffer,
				.offset = buffer.mOffset,
				.range  = buffer.size() });
	}

	if (!w.writes.empty())
		(*mDevice)->updateDescriptorSets(w.writes, {});
}

void PushConstants(const CommandContext& context, const PipelineLayout& pipelineLayout, const ShaderParameter& parameter, const ShaderParameterBinding& binding, uint32_t constantOffset = 0) {
	for (const auto&[id, param] : parameter) {
		uint32_t arrayIndex = 0;
		bool isArrayElement = std::holds_alternative<size_t>(id);
		if (isArrayElement) {
			arrayIndex = std::get<size_t>(id);
			uint32_t arraySize = std::visit(
				overloads {
					[](const ShaderStructBinding& b) { return b.arraySize; },
					[](const ShaderDescriptorBinding& b) { return b.arraySize; },
					[](const ShaderConstantBinding& b) { return b.arraySize; },
					[](const ShaderVertexAttributeBinding& b) { return 1u; },
				},
				binding.raw_variant());
			if (arrayIndex >= arraySize) {
				std::cout << "Warning array index " << arrayIndex << " which is out of bounds for array size " << arraySize << std::endl;
			}
		}
		const ShaderParameterBinding& paramBinding = isArrayElement ? binding : binding.at(id);

		uint32_t offset = constantOffset;

		if (const auto* v = param.get_if<ConstantParameter>()) {
			if (const auto* constantBinding = paramBinding.get_if<ShaderConstantBinding>()) {
				if (!constantBinding->pushConstant)
					continue;
				{
					uint32_t bindingSize = constantBinding->typeSize;
					if (!isArrayElement) bindingSize *= constantBinding->arraySize;
					if (v->size() > bindingSize)
						std::cout << "Warning: Binding constant parameter of size " << v->size() << " to binding of size " << bindingSize << std::endl;
				}

				offset += constantBinding->offset;
				offset += arrayIndex * align16(constantBinding->typeSize);

				if (constantBinding->pushConstant)
					context->pushConstants<std::byte>(**pipelineLayout, pipelineLayout.ShaderStageMask(), offset, *v);
			}
		}

		if (!isArrayElement)
			PushConstants(context, pipelineLayout, param, paramBinding, offset);
	}
}

void CommandContext::PushConstants(const PipelineLayout& pipelineLayout, const ShaderParameter& rootParameter) const {
	RoseEngine::PushConstants(*this, pipelineLayout, rootParameter, pipelineLayout.RootBinding());
}

void CommandContext::BindDescriptors(const PipelineLayout& pipelineLayout, const DescriptorSets& descriptorSets) const {
	std::vector<vk::DescriptorSet> vkDescriptorSets;
	for (const auto& ds : descriptorSets)
		vkDescriptorSets.emplace_back(*ds);
	mCommandBuffer.bindDescriptorSets(pipelineLayout.ShaderStageMask() & vk::ShaderStageFlagBits::eCompute ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics, **pipelineLayout, 0, vkDescriptorSets, {});
}

void CommandContext::BindParameters(const PipelineLayout& pipelineLayout, const ShaderParameter& rootParameter) {
	auto descriptorSets = GetDescriptorSets(pipelineLayout);
	UpdateDescriptorSets(*descriptorSets, rootParameter, pipelineLayout);

	std::vector<vk::DescriptorSet> vkDescriptorSets;
	for (const auto& ds : *descriptorSets)
		vkDescriptorSets.emplace_back(*ds);
	mCommandBuffer.bindDescriptorSets(pipelineLayout.ShaderStageMask() & vk::ShaderStageFlagBits::eCompute ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics, **pipelineLayout, 0, vkDescriptorSets, {});

	PushConstants(pipelineLayout, rootParameter);
}

}