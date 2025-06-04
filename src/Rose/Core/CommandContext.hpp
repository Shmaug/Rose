#pragma once

#include "Buffer.hpp"
#include "Image.hpp"
#include "AccelerationStructure.hpp"
#include "Pipeline.hpp"
#include "ParameterMap.hpp"

namespace RoseEngine {

// represents a uniform or push constant
class ConstantParameter : public std::vector<std::byte> {
public:
	template<typename T> requires(std::is_trivially_copyable_v<T>)
	inline ConstantParameter(const T& value) {
		resize(sizeof(value));
		*reinterpret_cast<T*>(data()) = value;
	}

	template<std::ranges::contiguous_range R> requires(!std::is_trivially_copyable_v<R>)
	inline ConstantParameter(const R& value) {
		resize(value.size() * sizeof(std::ranges::range_value_t<R>));
		std::memcpy( data(), value.data(), size() );
	}

	ConstantParameter() = default;
	ConstantParameter(ConstantParameter&&) = default;
	ConstantParameter(const ConstantParameter&) = default;
	ConstantParameter& operator=(ConstantParameter&&) = default;
	ConstantParameter& operator=(const ConstantParameter&) = default;

	template<typename T>
	inline T& get() {
		if (empty())
			resize(sizeof(T));
		return *reinterpret_cast<T*>(data());
	}

	template<typename T>
	inline const T& get() const {
		return *reinterpret_cast<const T*>(data());
	}

	template<typename T> requires(std::is_trivially_copyable_v<T>)
	inline T& operator=(const T& value) {
		resize(sizeof(value));
		return *reinterpret_cast<T*>(data()) = value;
	}

	template<std::ranges::contiguous_range R> requires(!std::is_trivially_copyable_v<R>)
	inline ConstantParameter& operator=(const R& value) {
		resize(value.size() * sizeof(std::ranges::range_value_t<R>));
		std::memcpy( data(), value.data(), size() );
		return *this;
	}
};

using BufferParameter = BufferView;

using TexelBufferParameter = TexelBufferView;

struct ImageParameter {
	ImageView              image = {};
	vk::ImageLayout        imageLayout = {};
	ref<vk::raii::Sampler> sampler = {};
};

using AccelerationStructureParameter = ref<AccelerationStructure>;

using ShaderParameter = ParameterMap<
	std::monostate,
	ConstantParameter,
	BufferParameter,
	TexelBufferParameter,
	ImageParameter,
	AccelerationStructureParameter >;

template<typename T>
concept shader_parameter = one_of<T, ShaderParameter, ConstantParameter, BufferParameter, TexelBufferParameter, ImageParameter, AccelerationStructureParameter>;

using DescriptorSets = std::vector<vk::raii::DescriptorSet>;

class CommandContext {
private:
	vk::raii::CommandPool mCommandPool = nullptr;
	std::list<vk::raii::DescriptorPool> mCachedDescriptorPools;

	vk::raii::CommandBuffer mCommandBuffer = nullptr;
	ref<Device> mDevice = {};
	uint32_t mQueueFamily = {};

	std::vector<vk::BufferMemoryBarrier2> mBufferBarrierQueue = {};
	std::vector<vk::ImageMemoryBarrier2>  mImageBarrierQueue = {};

	uint64_t mLastSubmit = 0;

	struct CachedData {
		std::unordered_map<vk::PipelineLayout, std::vector<ref<DescriptorSets>>> mDescriptorSets = {};
		std::unordered_map<vk::PipelineLayout, std::vector<ref<DescriptorSets>>> mNewDescriptorSets = {};

		struct CachedBuffers {
			BufferView hostBuffer;
			BufferView buffer;

			inline size_t size() const { return hostBuffer ? hostBuffer.size() : buffer.size(); }
		};
		std::unordered_map<vk::BufferUsageFlags, std::vector<CachedBuffers>> mBuffers = {};
		std::unordered_map<vk::BufferUsageFlags, std::vector<CachedBuffers>> mNewBuffers = {};


		std::unordered_map<ImageInfo, std::vector<ref<Image>>> mImages = {};
		std::unordered_map<ImageInfo, std::vector<ref<Image>>> mNewImages = {};
	};
	CachedData mCache = {};

	void AllocateDescriptorPool();
	DescriptorSets AllocateDescriptorSets(const vk::ArrayProxy<const vk::DescriptorSetLayout>& layouts, const vk::ArrayProxy<const uint32_t>& variableSetCounts = {});

public:
	inline       vk::raii::CommandBuffer& operator*()        { return mCommandBuffer; }
	inline const vk::raii::CommandBuffer& operator*() const  { return mCommandBuffer; }
	inline       vk::raii::CommandBuffer* operator->()       { return &mCommandBuffer; }
	inline const vk::raii::CommandBuffer* operator->() const { return &mCommandBuffer; }

	inline static ref<CommandContext> Create(const ref<Device>& device, uint32_t queueFamily) {
		ref<CommandContext> context = make_ref<CommandContext>();
		context->mDevice = device;
		context->mQueueFamily = queueFamily;
		return context;
	}
	inline static ref<CommandContext> Create(const ref<Device>& device, const vk::QueueFlags flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer) {
		return Create(device, device->FindQueueFamily(flags));
	}

	inline Device& GetDevice() const { return *mDevice; }
	inline const ref<Device>& GetDeviceRef() const { return mDevice; }
	inline uint32_t QueueFamily() const { return mQueueFamily; }

	void Begin();

	// Signals the device's timeline semaphore upon completion. Returns the signal value.
	uint64_t Submit(
		const uint32_t queueIndex = 0,
		const vk::ArrayProxy<const vk::Semaphore>&          signalSemaphores = {},
		const vk::ArrayProxy<const uint64_t>&               signalValues = {},
		const vk::ArrayProxy<const vk::Semaphore>&          waitSemaphores = {},
		const vk::ArrayProxy<const vk::PipelineStageFlags>& waitStages = {},
		const vk::ArrayProxy<const uint64_t>&               waitValues = {});

	void UpdateDescriptorSets(const DescriptorSets& descriptorSets, const ShaderParameter& rootParameter, const PipelineLayout& pipelineLayout);
	ref<DescriptorSets> GetDescriptorSets(const PipelineLayout& pipelineLayout);
	void BindDescriptors(const PipelineLayout& pipelineLayout, const DescriptorSets& descriptorSets) const;
	void PushConstants  (const PipelineLayout& pipelineLayout, const ShaderParameter& rootParameter) const;
	void BindParameters (const PipelineLayout& pipelineLayout, const ShaderParameter& rootParameter);

	void PushDebugLabel(const std::string& name, const float4 color = float4(1,1,1,0)) const;
	void PopDebugLabel() const;

	#pragma region Barriers

	inline static const vk::AccessFlags2 gWriteAccesses =
			vk::AccessFlagBits2::eShaderWrite |
			vk::AccessFlagBits2::eColorAttachmentWrite |
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
			vk::AccessFlagBits2::eTransferWrite |
			vk::AccessFlagBits2::eHostWrite |
			vk::AccessFlagBits2::eMemoryWrite |
			vk::AccessFlagBits2::eAccelerationStructureWriteKHR;

	inline void ExecuteBarriers() {
		mCommandBuffer.pipelineBarrier2(vk::DependencyInfo {
			.dependencyFlags = vk::DependencyFlagBits::eByRegion,
			.bufferMemoryBarrierCount = (uint32_t)mBufferBarrierQueue.size(),
			.pBufferMemoryBarriers    = mBufferBarrierQueue.data(),
			.imageMemoryBarrierCount  = (uint32_t)mImageBarrierQueue.size(),
			.pImageMemoryBarriers     = mImageBarrierQueue.data(),
		});

		mBufferBarrierQueue.clear();
		mImageBarrierQueue.clear();
	}

	inline void AddBarrier(const vk::BufferMemoryBarrier2& barrier) { mBufferBarrierQueue.emplace_back(barrier); }
	inline void AddBarrier(const vk::ImageMemoryBarrier2& barrier)  { mImageBarrierQueue.emplace_back(barrier); }

	template<typename T>
	inline void AddBarrier(const BufferRange<T>& buffer, const Buffer::ResourceState& newState) {
		const auto& oldState = buffer.GetState();
		auto b = buffer.SetState(newState);
		if (oldState.access == vk::AccessFlagBits2::eNone || newState.access == vk::AccessFlagBits2::eNone)
			return;
		//if (((oldState.access & gWriteAccesses) == 0) && (newState.access & gWriteAccesses) == 0)
		//	return; // remove read-read buffer barriers

		if (b.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED && b.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED)
			b.dstQueueFamilyIndex = b.srcQueueFamilyIndex;
		else if (b.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED && b.dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED)
			b.srcQueueFamilyIndex = b.dstQueueFamilyIndex;

		AddBarrier(b);
	}
	inline void AddBarrier(const ref<Image>& img, const vk::ImageSubresourceRange& subresource, const Image::ResourceState& newState) {
		auto barriers = img->SetSubresourceState(subresource, newState);
		for (const auto& barrier : barriers)
			AddBarrier(barrier);
	}
	inline void AddBarrier(const ImageView& img, const Image::ResourceState& newState) {
		auto barriers = img.SetState(newState);
		for (const auto& barrier : barriers)
			AddBarrier(barrier);
	}

	#pragma endregion

	#pragma region Resource manipulation

	template<typename T> requires(sizeof(T) == sizeof(uint32_t))
	inline void Fill(const BufferRange<T>& buffer, const T data, const vk::DeviceSize offset = 0, const vk::DeviceSize size = VK_WHOLE_SIZE) {
		AddBarrier(buffer, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eTransfer,
			.access = vk::AccessFlagBits2::eTransferWrite,
			.queueFamily = mQueueFamily });
		ExecuteBarriers();
		mCommandBuffer.fillBuffer(**buffer.mBuffer, buffer.mOffset + offset, std::min(size, buffer.size_bytes()), std::bit_cast<uint32_t>(data));
	}

	template<typename Tx, typename Ty>
	inline void Copy(const BufferRange<Tx>& src, const BufferRange<Ty>& dst) {
		if (dst.size_bytes() < src.size_bytes())
			throw std::runtime_error("dst smaller than src: " + std::to_string(dst.size_bytes()) + " < " + std::to_string(src.size_bytes()));
		AddBarrier(src, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eTransfer,
			.access = vk::AccessFlagBits2::eTransferRead,
			.queueFamily = mQueueFamily });
		AddBarrier(dst, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eTransfer,
			.access = vk::AccessFlagBits2::eTransferWrite,
			.queueFamily = mQueueFamily });

		ExecuteBarriers();

		mCommandBuffer.copyBuffer(
			**src.mBuffer,
			**dst.mBuffer,
			vk::BufferCopy{
				.srcOffset = src.mOffset,
				.dstOffset = dst.mOffset,
				.size = src.size_bytes() });
	}
	template<typename T>
	inline void Copy(const BufferRange<T>& src, const ImageView& dst, const uint32_t dstLevel = 0) {
		AddBarrier(src, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eTransfer,
			.access = vk::AccessFlagBits2::eTransferRead,
			.queueFamily = mQueueFamily });
		AddBarrier(dst,
			Image::ResourceState{
				.layout = vk::ImageLayout::eTransferDstOptimal,
				.stage = vk::PipelineStageFlagBits2::eTransfer,
				.access = vk::AccessFlagBits2::eTransferWrite,
				.queueFamily = mQueueFamily });

		ExecuteBarriers();

		mCommandBuffer.copyBufferToImage(
			**src.mBuffer,
			**dst.mImage,
			vk::ImageLayout::eTransferDstOptimal,
			vk::BufferImageCopy{
				.bufferOffset = src.mOffset,
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource = dst.GetSubresourceLayer(dstLevel),
				.imageOffset = { 0, 0, 0 },
				.imageExtent = vk::Extent3D{dst.Extent().x, dst.Extent().y, dst.Extent().z} });
	}

	inline void Copy(const ref<Image>& src, const ref<Image>& dst, const vk::ArrayProxy<const vk::ImageCopy>& regions) {
		for (const vk::ImageCopy& region : regions) {
			const auto& s = region.srcSubresource;
			AddBarrier(src,
				vk::ImageSubresourceRange{
					.aspectMask     = region.srcSubresource.aspectMask,
					.baseMipLevel   = region.srcSubresource.mipLevel,
					.levelCount     = 1,
					.baseArrayLayer = region.srcSubresource.baseArrayLayer,
					.layerCount     = region.srcSubresource.layerCount },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferSrcOptimal,
					.stage = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferRead,
					.queueFamily = mQueueFamily });
			AddBarrier(dst,
				vk::ImageSubresourceRange{
					.aspectMask     = region.dstSubresource.aspectMask,
					.baseMipLevel   = region.dstSubresource.mipLevel,
					.levelCount     = 1,
					.baseArrayLayer = region.dstSubresource.baseArrayLayer,
					.layerCount     = region.dstSubresource.layerCount },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferDstOptimal,
					.stage = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferWrite,
					.queueFamily = mQueueFamily });
		}

		ExecuteBarriers();

		mCommandBuffer.copyImage(
			**src,
			vk::ImageLayout::eTransferSrcOptimal,
			**dst,
			vk::ImageLayout::eTransferDstOptimal,
			regions);
	}
	inline void Copy(const ImageView& src, const ImageView& dst, uint32_t srcMip = 0, uint32_t dstMip = 0) {
		Copy(src.mImage, dst.mImage, vk::ImageCopy{
			.srcSubresource = src.GetSubresourceLayer(srcMip),
			.srcOffset = vk::Offset3D{ 0, 0, 0 },
			.dstSubresource = dst.GetSubresourceLayer(dstMip),
			.dstOffset = vk::Offset3D{ 0, 0, 0 },
			.extent = vk::Extent3D{dst.Extent().x, dst.Extent().y, dst.Extent().z} });
	}

	// Get a device buffer
	template<typename T = std::byte>
	inline BufferRange<T> GetTransientBuffer(const size_t count, const vk::BufferUsageFlags usage) {
		const size_t size = sizeof(T) * count;

		BufferView hostBuffer = {};
		BufferView buffer = {};
		if (auto it_ = mCache.mBuffers.find(usage); it_ != mCache.mBuffers.end()) {
			auto& q = it_->second;
			if (!q.empty() && q.back().size() >= size) {
				// find smallest cached buffer that can fit size
				auto it = std::ranges::lower_bound(q, size, {}, &CachedData::CachedBuffers::size);
				if (it != q.end()) {
					hostBuffer = it->hostBuffer;
					buffer     = it->buffer;
					q.erase(it);
				}
			}
		}

		if (!buffer || buffer.size() < size) {
			buffer = Buffer::Create(
				*mDevice,
				size,
				usage,
				vk::MemoryPropertyFlagBits::eDeviceLocal,
				VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT);
			mDevice->SetDebugName(**buffer.mBuffer, "Transient buffer");
			hostBuffer = {};
		}

		mCache.mNewBuffers[usage].emplace_back(hostBuffer, buffer);

		return buffer.slice(0, size);
	}

	// Copies data to buffer.
	// If usage is specified, also creates a device buffer
	template<std::ranges::contiguous_range R>
	inline BufferView UploadData(R&& data, vk::BufferUsageFlags usage = (vk::BufferUsageFlags)0) {
		const size_t size = sizeof(std::ranges::range_value_t<R>) * std::ranges::size(data);

		usage |= vk::BufferUsageFlagBits::eTransferDst;

		BufferView hostBuffer = {};
		BufferView buffer = {};
		if (auto it_ = mCache.mBuffers.find(usage); it_ != mCache.mBuffers.end()) {
			auto& q = it_->second;
			if (!q.empty() && q.back().size() >= size) {
				// find smallest cached buffer that can fit size
				auto it = std::ranges::lower_bound(q, size, {}, &CachedData::CachedBuffers::size);
				if (it != q.end()) {
					hostBuffer = it->hostBuffer;
					buffer     = it->buffer;
					q.erase(it);
				}
			}
		}

		// copy data to host buffer, or create host buffer
		if (hostBuffer && hostBuffer.size() >= size) {
			std::memcpy(hostBuffer.data(), std::ranges::data(data), size);
		} else {
			hostBuffer = Buffer::Create(*mDevice, data);
			mDevice->SetDebugName(**hostBuffer.mBuffer, "Transient host buffer");
		}

		// copy to device if usage is specified
		if (usage != (vk::BufferUsageFlags)0) {
			if (!buffer) {
				buffer = Buffer::Create(
					*mDevice,
					size,
					usage,
					vk::MemoryPropertyFlagBits::eDeviceLocal,
					VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT);
				mDevice->SetDebugName(**buffer.mBuffer, "Transient buffer");
			}

			Copy(hostBuffer.slice(0, size), buffer);
		}

		mCache.mNewBuffers[usage].emplace_back(hostBuffer, buffer);

		return usage == (vk::BufferUsageFlags)0 ? hostBuffer.slice(0, size) : buffer.slice(0, size);
	}

	ref<Image> GetTransientImage(const ImageInfo& info);
	inline ref<Image> GetTransientImage(const uint3 extent, const vk::Format format, const vk::ImageUsageFlags usage, const uint32_t mipLevels = 1, const uint32_t arrayLayers = 1) {
		return GetTransientImage(ImageInfo {
			.format = format,
			.extent = extent,
			.mipLevels = mipLevels,
			.arrayLayers = arrayLayers,
			.usage = usage,
			.queueFamilies = { QueueFamily() }
		});
	}

	inline void Blit(const ref<Image>& src, const ref<Image>& dst, const vk::ArrayProxy<const vk::ImageBlit>& regions, const vk::Filter filter) {
		for (const vk::ImageBlit& region : regions) {
			AddBarrier(src,
				vk::ImageSubresourceRange{
					.aspectMask     = region.srcSubresource.aspectMask,
					.baseMipLevel   = region.srcSubresource.mipLevel,
					.levelCount     = 1,
					.baseArrayLayer = region.srcSubresource.baseArrayLayer,
					.layerCount     = region.srcSubresource.layerCount },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferSrcOptimal,
					.stage = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferRead,
					.queueFamily = mQueueFamily });
			AddBarrier(dst,
				vk::ImageSubresourceRange{
					.aspectMask     = region.dstSubresource.aspectMask,
					.baseMipLevel   = region.dstSubresource.mipLevel,
					.levelCount     = 1,
					.baseArrayLayer = region.dstSubresource.baseArrayLayer,
					.layerCount     = region.dstSubresource.layerCount },
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferDstOptimal,
					.stage = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferWrite,
					.queueFamily = mQueueFamily });
		}

		ExecuteBarriers();

		mCommandBuffer.blitImage(**src, vk::ImageLayout::eTransferSrcOptimal, **dst, vk::ImageLayout::eTransferDstOptimal, regions, filter);
	}
	inline void Blit(const ImageView& src, const ImageView& dst, const vk::Filter filter = vk::Filter::eLinear) {
		Blit(
			src.mImage,
			dst.mImage,
			vk::ImageBlit{
				.srcSubresource = src.GetSubresourceLayer(),
				.srcOffsets = std::array<vk::Offset3D, 2>{
					vk::Offset3D{ 0, 0, 0 },
					std::bit_cast<vk::Offset3D>(src.Extent()) },
				.dstSubresource = dst.GetSubresourceLayer(),
				.dstOffsets = std::array<vk::Offset3D, 2>{
					vk::Offset3D{ 0, 0, 0 },
					std::bit_cast<vk::Offset3D>(dst.Extent()) } },
			filter);
	}

	inline void ClearColor(const ref<Image>& img, const vk::ClearColorValue& clearValue, const vk::ArrayProxy<const vk::ImageSubresourceRange>& subresources) {
		for (const auto& subresource : subresources)
			AddBarrier(img,
				subresource,
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferDstOptimal,
					.stage  = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferWrite,
					.queueFamily = mQueueFamily });

		ExecuteBarriers();

		mCommandBuffer.clearColorImage(**img, vk::ImageLayout::eTransferDstOptimal, clearValue, subresources);
	}
	inline void ClearDepth(const ref<Image>& img, const vk::ClearDepthStencilValue& clearValue, const vk::ArrayProxy<const vk::ImageSubresourceRange>& subresources) {
		for (const auto& subresource : subresources)
			AddBarrier(img,
				subresource,
				Image::ResourceState{
					.layout = vk::ImageLayout::eTransferDstOptimal,
					.stage  = vk::PipelineStageFlagBits2::eTransfer,
					.access = vk::AccessFlagBits2::eTransferWrite,
					.queueFamily = mQueueFamily });

		ExecuteBarriers();

		mCommandBuffer.clearDepthStencilImage(**img, vk::ImageLayout::eTransferDstOptimal, clearValue, subresources);
	}

	inline void ClearColor(const ImageView& img, const vk::ClearColorValue& clearValue) {
		ClearColor(img.mImage, clearValue, img.mSubresource);
	}
	inline void ClearDepth(const ImageView& img, const vk::ClearDepthStencilValue& clearValue) {
		ClearDepth(img.mImage, clearValue, img.mSubresource);
	}

	inline void GenerateMipMaps(const ref<Image>& img, const vk::Filter filter = vk::Filter::eLinear, const vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor) {
		vk::ImageBlit blit {
			.srcSubresource = vk::ImageSubresourceLayers{
				.aspectMask = aspect,
				.baseArrayLayer = 0,
				.layerCount = img->Info().arrayLayers },
			.srcOffsets = std::array<vk::Offset3D, 2>{
				vk::Offset3D{ 0, 0, 0 },
				std::bit_cast<vk::Offset3D>(img->Info().extent) },
			.dstSubresource = blit.srcSubresource,
			.dstOffsets = std::array<vk::Offset3D, 2>{
				vk::Offset3D{ 0, 0, 0 },
				vk::Offset3D{ 0, 0, 0 } } };
		for (uint32_t i = 1; i < img->Info().mipLevels; i++) {
			blit.srcSubresource.mipLevel = i - 1;
			blit.dstSubresource.mipLevel = i;
			blit.dstOffsets[1].x = std::max(1, blit.srcOffsets[1].x / 2);
			blit.dstOffsets[1].y = std::max(1, blit.srcOffsets[1].y / 2);
			blit.dstOffsets[1].z = std::max(1, blit.srcOffsets[1].z / 2);

			Blit(img, img, blit, vk::Filter::eLinear);

			blit.srcOffsets[1] = blit.dstOffsets[1];
		}
	}

	#pragma endregion

	#pragma region Rasterization
	inline void BeginRendering(const vk::ArrayProxy<std::pair<ImageView, vk::ClearValue>>& attachments) {
		uint2 imageExtent;

		std::vector<vk::RenderingAttachmentInfo> attachmentInfos;
		vk::RenderingAttachmentInfo depthAttachmentInfo;
		bool hasDepthAttachment = false;

		attachmentInfos.reserve(attachments.size());
		for (const auto& [attachment, clearValue] : attachments) {
			imageExtent = attachment.Extent();
			if (IsDepthStencil(attachment.GetImage()->Info().format)) {
				AddBarrier(attachment, Image::ResourceState{
					.layout = vk::ImageLayout::eDepthAttachmentOptimal,
					.stage  = vk::PipelineStageFlagBits2::eLateFragmentTests,
					.access =  vk::AccessFlagBits2::eDepthStencilAttachmentRead|vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
					.queueFamily = QueueFamily()
				});

				depthAttachmentInfo = vk::RenderingAttachmentInfo {
					.imageView = *attachment,
					.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
					.resolveMode = vk::ResolveModeFlagBits::eNone,
					.resolveImageView = {},
					.resolveImageLayout = vk::ImageLayout::eUndefined,
					.loadOp  = vk::AttachmentLoadOp::eClear,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.clearValue = clearValue
				};

				hasDepthAttachment = true;
			} else {
				AddBarrier(attachment, Image::ResourceState{
					.layout = vk::ImageLayout::eColorAttachmentOptimal,
					.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
					.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
					.queueFamily = QueueFamily()
				});

				attachmentInfos.emplace_back(vk::RenderingAttachmentInfo {
					.imageView = *attachment,
					.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
					.resolveMode = vk::ResolveModeFlagBits::eNone,
					.resolveImageView = {},
					.resolveImageLayout = vk::ImageLayout::eUndefined,
					.loadOp  = vk::AttachmentLoadOp::eClear,
					.storeOp = vk::AttachmentStoreOp::eStore,
					.clearValue = clearValue
				});
			}
		}

		ExecuteBarriers();

		mCommandBuffer.beginRendering(vk::RenderingInfo {
			.renderArea = vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ imageExtent.x, imageExtent.y } },
			.layerCount = 1,
			.viewMask = 0,
			.colorAttachmentCount = (uint32_t)attachmentInfos.size(),
			.pColorAttachments = attachmentInfos.data(),
			.pDepthAttachment = hasDepthAttachment ? &depthAttachmentInfo : nullptr,
			.pStencilAttachment = nullptr
		});

		mCommandBuffer.setViewport(0, vk::Viewport{ 0, 0, (float)imageExtent.x, (float)imageExtent.y, 0, 1 });
		mCommandBuffer.setScissor(0, vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ imageExtent.x, imageExtent.y } } );
	}
	inline void EndRendering() const {
		mCommandBuffer.endRendering();
	}

	#pragma endregion

	#pragma region Dispatch

	void Dispatch(const Pipeline& pipeline, const uint3 threadCount, const ShaderParameter& rootParameter) {
		mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, **pipeline);
		BindParameters(*pipeline.Layout(), rootParameter);
		ExecuteBarriers();

		auto dim = GetDispatchDim(pipeline.GetShader()->WorkgroupSize(), threadCount);
		mCommandBuffer.dispatch(dim.x, dim.y, dim.z);
	}
	void Dispatch(const Pipeline& pipeline, const uint2    threadCount, const ShaderParameter& rootParameter) { Dispatch(pipeline, uint3(threadCount, 1)   , rootParameter); }
	void Dispatch(const Pipeline& pipeline, const uint32_t threadCount, const ShaderParameter& rootParameter) { Dispatch(pipeline, uint3(threadCount, 1, 1), rootParameter); }


	void Dispatch(const Pipeline& pipeline, const uint3 threadCount, const DescriptorSets& descriptorSets) {
		mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, **pipeline);

		BindDescriptors(*pipeline.Layout(), descriptorSets);

		auto dim = GetDispatchDim(pipeline.GetShader()->WorkgroupSize(), threadCount);
		mCommandBuffer.dispatch(dim.x, dim.y, dim.z);
	}
	void Dispatch(const Pipeline& pipeline, const uint2    threadCount, const DescriptorSets& descriptorSets) { Dispatch(pipeline, uint3(threadCount, 1)   , descriptorSets); }
	void Dispatch(const Pipeline& pipeline, const uint32_t threadCount, const DescriptorSets& descriptorSets) { Dispatch(pipeline, uint3(threadCount, 1, 1), descriptorSets); }

	#pragma endregion
};

}