//------------------------------------------------------------------------
// Copyright(c) 2026 My Plug-in Company.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace MyCompanyName {
//------------------------------------------------------------------------
static const Steinberg::FUID kAnalog_DelayProcessorUID (0x9C9936F8, 0x12225A07, 0x95357A84, 0x540A2494);
static const Steinberg::FUID kAnalog_DelayControllerUID (0x44F95DE1, 0xF51C5B50, 0xBE928CD7, 0xE72B508F);

#define Analog_DelayVST3Category "Fx"

enum ParamIDs : Steinberg::Vst::ParamID
{

	kBypassId = 0,

	kDelayTimeId,   // Time (0–1000 ms)
	kFeedbackId,    // 0–100%
	kMixId,         // 0–100%
	kToneId,     // Tone (0–1, tilt EQ)

	kStereoWidthId, // 0-1

	kTempoSyncId,    // 0 = off, 1 = quarter note, 2 = dotted quarter, 3 = half note

	kParamCount
};

//------------------------------------------------------------------------
} // namespace MyCompanyName
