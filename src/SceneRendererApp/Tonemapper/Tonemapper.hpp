#pragma once

#include <Rose/Core/PipelineCache.hpp>
#include "Tonemapper.h"

namespace RoseEngine {

class Tonemapper {
private:
	PipelineCache maxReduce = PipelineCache(FindShaderPath("Tonemapper.cs.slang"), "MaxReduce");
	PipelineCache tonemap   = PipelineCache(FindShaderPath("Tonemapper.cs.slang"), "Tonemap");

	BufferRange<uint4> mMaxBuf;

	float mExposure = 0;
	bool mGammaCorrect = true;
	TonemapperMode mMode = TonemapperMode::eACES;

public:
	inline void DrawGui(CommandContext& context) {
		Gui::EnumDropdown<TonemapperMode>("Mode", mMode, TonemapperModeStrings);
		ImGui::PushItemWidth(40);
		ImGui::DragFloat("Exposure", &mExposure, .1f, -10, 10);
		ImGui::PopItemWidth();
		ImGui::Checkbox("Gamma correct", &mGammaCorrect);
	}

	inline void Render(CommandContext& context, const ImageView& input) {
		context.PushDebugLabel("TonemapPass::Render");

		ShaderDefines defines {
			{ "MODE", std::to_string((int)mMode) },
			{ "GAMMA_CORRECT", mGammaCorrect ? "1" : "0" }
		};

		if (!mMaxBuf) mMaxBuf = Buffer::Create(context.GetDevice(), sizeof(uint4), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);

		// get maximum value in image

		context.Fill(mMaxBuf.cast<uint32_t>(), 0u);

		ShaderParameter params;
		params["gImage"] = ImageParameter{ .image = input, .imageLayout = vk::ImageLayout::eGeneral };
		params["gExposure"] = std::pow(2.f, mExposure);
		params["gMax"] = (BufferParameter)mMaxBuf;

		maxReduce(context, input.Extent(), params, defines);

		tonemap(context, input.Extent(), params, defines);

		context.PopDebugLabel();
	}
};

}