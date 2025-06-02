#pragma once

namespace RoseEngine {

enum class TonemapperMode {
	eRaw,
	eReinhard,
	eReinhardExtended,
	eReinhardLuminance,
	eReinhardLuminanceExtended,
	eUncharted2,
	eFilmic,
	eACES,
	eACESApprox,
	eViridisR,
	eViridisLengthRGB,
	eTonemapModeCount
};
#ifdef __cplusplus
static const char* TonemapperModeStrings[] = {
	"Raw",
	"Reinhard",
	"Reinhard Extended",
	"Reinhard Luminance",
	"Reinhard Luminance Extended",
	"Uncharted 2",
	"Filmic",
	"ACES",
	"ACES Approximated",
	"Viridis R",
	"Viridis length(RGB)",
};
#endif

}