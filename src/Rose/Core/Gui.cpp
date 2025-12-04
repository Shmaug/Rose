#include "Gui.hpp"
#include "CommandContext.hpp"
#include "Window.hpp"

#include <imgui/imgui_impl_vulkan.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_spectrum.h>
#include <ImGuizmo.h>
#include <imnodes.h>
#include <implot.h>

#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif

namespace RoseEngine {

std::string getExecutableDir() {
	#if defined(_WIN32)
		// Windows Implementation
		char buffer[MAX_PATH];
		// GetModuleFileName IS null-terminated, so it works naturally
		GetModuleFileNameA(NULL, buffer, MAX_PATH);
		return std::filesystem::path(buffer).parent_path().string();
	#elif defined(__linux__)
		// Linux Implementation
		char buffer[PATH_MAX];
		ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
		assert(count != -1);
		// Explicitly create string with length 'count'
		return std::filesystem::path(std::string(buffer, count)).parent_path().string();
	#endif
}

weak_ref<Device> Gui::mDevice = {};
vk::raii::RenderPass Gui::mRenderPass = nullptr;
uint32_t Gui::mQueueFamily = 0;
std::unordered_map<vk::Image, vk::raii::Framebuffer> Gui::mFramebuffers = {};
std::shared_ptr<vk::raii::DescriptorPool> Gui::mImGuiDescriptorPool = {};
ImFont* Gui::mHeaderFont = nullptr;
std::unordered_set<ImageView> Gui::mFrameTextures = {};
std::unordered_map<std::pair<ImageView, vk::Filter>, std::pair<vk::raii::DescriptorSet, vk::raii::Sampler>, PairHash<ImageView, vk::Filter>> Gui::mTextureIDs = {};

void Gui::ProgressSpinner(const char* label, const float radius, const float thickness, bool center) {
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	ImDrawList* drawList = window->DrawList;
	const ImGuiStyle& style = ImGui::GetStyle();

	ImVec2 pos = window->DC.CursorPos;
	if (center)
    	pos.x += (ImGui::GetContentRegionAvail().x - 2*radius) * .5f;

	const ImRect bb(pos, ImVec2(pos.x + radius*2, pos.y + (radius + style.FramePadding.y)*2));
	ImGui::ItemSize(bb, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, window->GetID(label)))
		return;

	const float t = ImGui::GetCurrentContext()->Time;

	const int num_segments = drawList->_CalcCircleAutoSegmentCount(radius);

	const int start = abs(sin(t * 1.8f))*(num_segments-5);
	const float a_min = float(M_PI*2) * ((float)start) / (float)num_segments;
	const float a_max = float(M_PI*2) * ((float)num_segments-3) / (float)num_segments;

	const ImVec2 c = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

	drawList->PathClear();

	for (int i = 0; i < num_segments; i++) {
		const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
		drawList->PathLineTo(ImVec2(
			c.x + cos(a + t*8) * radius,
			c.y + sin(a + t*8) * radius));
	}

	drawList->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), 0, thickness);
}

ImTextureID Gui::GetTextureID(const ImageView& image, const vk::Filter filter) {
	if (!mImGuiDescriptorPool)
		return 0;

	auto device = mDevice.lock();
	if (!device)
		return 0;

	auto it = mTextureIDs.find(std::make_pair(image, filter));
	if (it == mTextureIDs.end()) {
		vk::raii::Sampler sampler(**device, vk::SamplerCreateInfo{
			.magFilter = filter,
			.minFilter = filter,
			.mipmapMode = filter == vk::Filter::eLinear ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest });
		vk::raii::DescriptorSet descriptorSet(
			**device,
			ImGui_ImplVulkan_AddTexture(*sampler, *image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			**mImGuiDescriptorPool);
		it = mTextureIDs.emplace(std::make_pair(image, filter), std::make_pair( std::move(descriptorSet), std::move(sampler) )).first;
	}
	mFrameTextures.emplace(image);
	return (VkDescriptorSet)*it->second.first;
}

void StyleColorsDark() {
	auto& colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.1f, 0.1f, 0.9f };
	colors[ImGuiCol_DockingEmptyBg] = colors[ImGuiCol_WindowBg];

	colors[ImGuiCol_Header] = colors[ImGuiCol_WindowBg];
	colors[ImGuiCol_HeaderActive]  = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
	colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.20f, 0.20f, 0.20f, 1.0f };

	colors[ImGuiCol_TitleBg]          = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
	colors[ImGuiCol_TitleBgActive]    = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };
	colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_TitleBg];

	colors[ImGuiCol_Tab]                = colors[ImGuiCol_TitleBgActive];
	colors[ImGuiCol_TabHovered]         = ImVec4{ 0.45f, 0.45f, 0.45f, 1.0f };
	colors[ImGuiCol_TabActive]          = ImVec4{ 0.35f, 0.35f, 0.35f, 1.0f };
	colors[ImGuiCol_TabUnfocused]       = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_TitleBg];

	colors[ImGuiCol_FrameBg]            = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
	colors[ImGuiCol_FrameBgHovered]     = ImVec4{ 0.19f, 0.19f, 0.19f, 1.0f };
	colors[ImGuiCol_FrameBgActive]      = ImVec4{ 0.18f, 0.18f, 0.18f, 1.0f };

	colors[ImGuiCol_Button]             = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };
	colors[ImGuiCol_ButtonHovered]      = ImVec4{ 0.25f, 0.25f, 0.25f, 1.0f };
	colors[ImGuiCol_ButtonActive]       = ImVec4{ 0.175f, 0.175f, 0.175f, 1.0f };
	colors[ImGuiCol_CheckMark]          = ImVec4{ 0.75f, 0.75f, 0.75f, 1.0f };
	colors[ImGuiCol_SliderGrab]         = ImVec4{ 0.75f, 0.75f, 0.75f, 1.0f };
	colors[ImGuiCol_SliderGrabActive]   = ImVec4{ 0.8f, 0.8f, 0.8f, 1.0f };

	colors[ImGuiCol_ResizeGrip]        = colors[ImGuiCol_ButtonActive];
	colors[ImGuiCol_ResizeGripActive]  = colors[ImGuiCol_ButtonActive];
	colors[ImGuiCol_ResizeGripHovered] = colors[ImGuiCol_ButtonActive];

	colors[ImGuiCol_DragDropTarget]    = colors[ImGuiCol_ButtonActive];
}

void StyleColorsSpectrum() {
	auto& colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text]                  = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY800); // text on hovered controls is gray900
	colors[ImGuiCol_TextDisabled]          = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY500);
	colors[ImGuiCol_WindowBg]              = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY100);
	colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_PopupBg]               = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY50); // not sure about this. Note: applies to tooltips too.
	colors[ImGuiCol_Border]                = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY300);
	colors[ImGuiCol_BorderShadow]          = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::Static::NONE); // We don't want shadows. Ever.
	colors[ImGuiCol_FrameBg]               = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY75); // this isnt right, spectrum does not do this, but it's a good fallback
	colors[ImGuiCol_FrameBgHovered]        = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY50);
	colors[ImGuiCol_FrameBgActive]         = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY200);
	colors[ImGuiCol_TitleBg]               = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY300); // those titlebar values are totally made up, spectrum does not have this.
	colors[ImGuiCol_TitleBgActive]         = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY200);
	colors[ImGuiCol_TitleBgCollapsed]      = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY400);
	colors[ImGuiCol_MenuBarBg]             = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY100);
	colors[ImGuiCol_ScrollbarBg]           = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY100); // same as regular background
	colors[ImGuiCol_ScrollbarGrab]         = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY400);
	colors[ImGuiCol_ScrollbarGrabHovered]  = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY600);
	colors[ImGuiCol_ScrollbarGrabActive]   = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY700);
	colors[ImGuiCol_CheckMark]             = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::BLUE500);
	colors[ImGuiCol_SliderGrab]            = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::BLUE600);
	colors[ImGuiCol_SliderGrabActive]      = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::BLUE700);
	colors[ImGuiCol_Button]                = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY75); // match default button to Spectrum's 'Action Button'.
	colors[ImGuiCol_ButtonHovered]         = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY50);
	colors[ImGuiCol_ButtonActive]          = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY200);
	colors[ImGuiCol_Header]                = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY400);
	colors[ImGuiCol_HeaderHovered]         = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY500);
	colors[ImGuiCol_HeaderActive]          = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY600);
	colors[ImGuiCol_Separator]             = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY400);
	colors[ImGuiCol_SeparatorHovered]      = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY600);
	colors[ImGuiCol_SeparatorActive]       = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY700);
	colors[ImGuiCol_ResizeGrip]            = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY400);
	colors[ImGuiCol_ResizeGripHovered]     = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY600);
	colors[ImGuiCol_ResizeGripActive]      = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::GRAY700);
	colors[ImGuiCol_PlotLines]             = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::BLUE400);
	colors[ImGuiCol_PlotLinesHovered]      = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::BLUE600);
	colors[ImGuiCol_PlotHistogram]         = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::BLUE400);
	colors[ImGuiCol_PlotHistogramHovered]  = ImGui::ColorConvertU32ToFloat4(ImGui::Spectrum::BLUE600);
	colors[ImGuiCol_TextSelectedBg]        = ImGui::ColorConvertU32ToFloat4((ImGui::Spectrum::BLUE400 & 0x00FFFFFF) | 0x33000000);
	colors[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight]          = ImGui::ColorConvertU32ToFloat4((ImGui::Spectrum::GRAY900 & 0x00FFFFFF) | 0x0A000000);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}

void Gui::Initialize(CommandContext& context, const Window& window, const Swapchain& swapchain, const uint32_t queueFamily) {
	const auto& device = context.GetDeviceRef();

	if (*mRenderPass)
		Destroy();
	mQueueFamily = queueFamily;
	mDevice = device;

	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImNodes::CreateContext();
	ImNodes::LoadCurrentEditorStateFromIniFile("imnodes.ini");

	// Setup Dear ImGui style
	float scale = 1.25f;
	ImGui::GetStyle().ScaleAllSizes(scale);
	ImGui::GetStyle().IndentSpacing /= scale;
	ImGui::GetStyle().IndentSpacing *= 0.75f;
	ImGui::GetStyle().WindowRounding = 4.0f;
	ImGui::GetStyle().GrabRounding   = 4.0f;

	StyleColorsSpectrum();

	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

	ImGui_ImplGlfw_InitForVulkan(window.GetWindow(), true);

	// create renderpass
	vk::AttachmentReference attachmentReference{
		.attachment = 0,
		.layout = vk::ImageLayout::eColorAttachmentOptimal };
	vk::SubpassDescription subpass{
		.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
		.colorAttachmentCount = 1,
		.pColorAttachments = &attachmentReference };
	vk::AttachmentDescription attachment{
		.format = swapchain.GetFormat().format,
		.samples = vk::SampleCountFlagBits::e1,
		.loadOp = vk::AttachmentLoadOp::eLoad,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
		.stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
		.initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
		.finalLayout = vk::ImageLayout::eColorAttachmentOptimal };
	mRenderPass = vk::raii::RenderPass(**device, vk::RenderPassCreateInfo{
		.attachmentCount = 1,
		.pAttachments = &attachment,
		.subpassCount = 1,
		.pSubpasses = &subpass });

	std::vector<vk::DescriptorPoolSize> poolSizes {
		vk::DescriptorPoolSize{ vk::DescriptorType::eSampler,              std::min(1024u, device->Limits().maxDescriptorSetSamplers) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, std::min(1024u, device->Limits().maxDescriptorSetSampledImages) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eInputAttachment,      std::min(1024u, device->Limits().maxDescriptorSetInputAttachments) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eSampledImage,         std::min(1024u, device->Limits().maxDescriptorSetSampledImages) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageImage,         std::min(1024u, device->Limits().maxDescriptorSetStorageImages) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer,        std::min(1024u, device->Limits().maxDescriptorSetUniformBuffers) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBufferDynamic, std::min(1024u, device->Limits().maxDescriptorSetUniformBuffersDynamic) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBuffer,        std::min(1024u, device->Limits().maxDescriptorSetStorageBuffers) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBufferDynamic, std::min(1024u, device->Limits().maxDescriptorSetStorageBuffersDynamic) }
	};
	mImGuiDescriptorPool = make_ref<vk::raii::DescriptorPool>((*device)->createDescriptorPool(
		vk::DescriptorPoolCreateInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = 8192 }
		.setPoolSizes(poolSizes)));

	ImGui_ImplVulkan_InitInfo init_info = {
		.Instance = device->GetInstance(),
		.PhysicalDevice = *device->PhysicalDevice(),
		.Device = ***device,
		.QueueFamily = mQueueFamily,
		.Queue = *(*device)->getQueue(queueFamily, 0),
		.PipelineCache  = *device->PipelineCache(),
		.DescriptorPool = **mImGuiDescriptorPool,
		.Subpass = 0,
		.MinImageCount = std::max(swapchain.GetMinImageCount(), 2u),
		.ImageCount    = std::max(swapchain.ImageCount(), 2u),
		.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
		.Allocator = nullptr };
	//init_info.CheckVkResultFn = check_vk_result;
	ImGui_ImplVulkan_Init(&init_info, *mRenderPass);

	// Upload Fonts
	//
	std::filesystem::path source_path = getExecutableDir();
	std::filesystem::path font_path = source_path / "DroidSans.ttf";
	std::cout << "Font path" << font_path << std::endl;

	ImGui::GetIO().Fonts->AddFontFromFileTTF(font_path.string().c_str(), 16.f);
	mHeaderFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(font_path.string().c_str(), 20.f);

	context.Begin();
	ImGui_ImplVulkan_CreateFontsTexture(**context);
	device->Wait( context.Submit() );

	ImGui_ImplVulkan_DestroyFontUploadObjects();

}
void Gui::Destroy() {
	if (*mRenderPass) {
		ImNodes::SaveCurrentEditorStateToIniFile("imnodes.ini");
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImNodes::DestroyContext();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
		mRenderPass.clear();
		mFramebuffers.clear();
		mFrameTextures.clear();
		mTextureIDs.clear();
		mImGuiDescriptorPool.reset();
	}
}

void Gui::NewFrame() {
	ImGui_ImplGlfw_NewFrame();
	ImGui_ImplVulkan_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
}

void Gui::Render(CommandContext& context, const ImageView& renderTarget) {
	ImGui::Render();
	ImDrawData* drawData = ImGui::GetDrawData();
	if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) return;

	const vk::Extent2D extent {
		.width  = (uint32_t)drawData->DisplaySize.x,
		.height = (uint32_t)drawData->DisplaySize.y };

	// create framebuffer

	auto it = mFramebuffers.find(**renderTarget.GetImage());
	if (it == mFramebuffers.end()) {
		vk::FramebufferCreateInfo info {
			.renderPass = *mRenderPass,
			.width  = renderTarget.Extent().x,
			.height = renderTarget.Extent().y,
			.layers = 1 };
		info.setAttachments(*renderTarget);
		vk::raii::Framebuffer fb(*context.GetDevice(), info);
		it = mFramebuffers.emplace(**renderTarget.GetImage(), std::move(fb)).first;
	}
	auto framebuffer = *it->second;

	for (const ImageView& v : mFrameTextures)
		context.AddBarrier(v, Image::ResourceState{
			.layout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.stage  = vk::PipelineStageFlagBits2::eFragmentShader,
			.access = vk::AccessFlagBits2::eShaderRead,
			.queueFamily = context.QueueFamily() });
	mFrameTextures.clear();

	// render gui

	context.AddBarrier(renderTarget, Image::ResourceState{
		.layout = vk::ImageLayout::eColorAttachmentOptimal,
		.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		.access = vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
		.queueFamily = context.QueueFamily() });
	context.ExecuteBarriers();
	context->beginRenderPass(
		vk::RenderPassBeginInfo{
			.renderPass = *mRenderPass,
			.framebuffer = framebuffer,
			.renderArea = vk::Rect2D{ {0,0}, extent } },
		vk::SubpassContents::eInline);

	// Record dear imgui primitives into command buffer
	ImGui_ImplVulkan_RenderDrawData(drawData, **context);

	// Submit command buffer
	context->endRenderPass();
	renderTarget.SetState(Image::ResourceState{
		.layout = vk::ImageLayout::eColorAttachmentOptimal,
		.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		.access = vk::AccessFlagBits2::eColorAttachmentWrite,
		.queueFamily = context.QueueFamily() });
}

}
