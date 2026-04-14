// ------------------------------------------------------------------------
// Copyright(c) 2026 My Plug-in Company.
//------------------------------------------------------------------------

#include "controller.h"
#include "cids.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/futils.h"
#include <cstdio>
#include <cmath>

using namespace Steinberg;

namespace MyCompanyName {

	//------------------------------------------------------------------------
	// Analog_DelayController Implementation
	//------------------------------------------------------------------------
	tresult PLUGIN_API Analog_DelayController::initialize (FUnknown* context)
	{
		// Always call parent first
		tresult result = EditControllerEx1::initialize(context);
		if (result != kResultOk)
		{
			return result;
		}

		// --- Register parameters here ---

		// Bypass: discrete 0/1
		parameters.addParameter(
			STR16("Bypass"),
			nullptr,
			2,          // two steps: off/on
			0.0,        // default = OFF
			Steinberg::Vst::ParameterInfo::kCanAutomate |
			Steinberg::Vst::ParameterInfo::kIsBypass,
			kBypassId
		);

		// Delay time: 50 ms .. 1000 ms (normalized stored 0..1)
		// default = 400ms -> normalized = (400-50)/(1000-50)
		parameters.addParameter(
			STR16("Delay Time"),
			STR16("ms"),
			0, // continuous
			(400.0f - 50.0f) / (1000.0f - 50.0f),
			Steinberg::Vst::ParameterInfo::kCanAutomate,
			kDelayTimeId
		);

		// Mix: 0..1
		parameters.addParameter(
			STR16("Mix"),
			nullptr,
			0,
			0.50, // default (50%)
			Steinberg::Vst::ParameterInfo::kCanAutomate,
			kMixId
		);

		// Feedback: stored normalized (0..1) maps to actual feedback = normalized * 0.95
		// default actual ~24% -> store normalized = 0.24 / 0.95
		parameters.addParameter(
			STR16("Feedback"),
			nullptr,
			0,
			0.24f / 0.95f,
			Steinberg::Vst::ParameterInfo::kCanAutomate,
			kFeedbackId
		);

		// Tone: normalized [0..1], controller default 0.5 (center)
		parameters.addParameter(
			STR16("Tone"),
			nullptr,
			0,
			0.5,
			Steinberg::Vst::ParameterInfo::kCanAutomate,
			kToneId
		);

		parameters.addParameter(
			STR16("Stereo Width"),
			nullptr,
			0,     
			0.5,    // default = index 1 ("Normal")
			Steinberg::Vst::ParameterInfo::kCanAutomate,
			kStereoWidthId
		);

		// apply normalized defaults into parameter container
		setParamNormalized(kBypassId, 0.0);
		setParamNormalized(kDelayTimeId, (400.f - 50.f) / (1000.f - 50.f));
		setParamNormalized(kMixId, 0.50);
		setParamNormalized(kFeedbackId, 0.24f / 0.95f);
		setParamNormalized(kToneId, 0.5);
		setParamNormalized(kStereoWidthId, 0.5); // normalized index 1 of 0,1,2

		parameters.getParameter(kBypassId)->setNormalized(0.0);
		parameters.getParameter(kDelayTimeId)->setNormalized(
			(400.f - 50.f) / (1000.f - 50.f)
		);
		parameters.getParameter(kMixId)->setNormalized(0.50);
		parameters.getParameter(kFeedbackId)->setNormalized(0.24f / 0.95f);
		parameters.getParameter(kToneId)->setNormalized(0.5);
		parameters.getParameter(kStereoWidthId)->setNormalized(0.5);

		return kResultOk;
	}

	tresult PLUGIN_API Analog_DelayController::setParamNormalized(
		Steinberg::Vst::ParamID tag,
		Steinberg::Vst::ParamValue value)
	{
		if (tag == kBypassId)
		{
			// maintain local copy for quick access
			bypass = (value >= 0.5);

			// Let the base class actually set the parameter value/store it and notify listeners.
			return EditControllerEx1::setParamNormalized(tag, value);
		}

		return EditControllerEx1::setParamNormalized(tag, value);
	}


	//------------------------------------------------------------------------
	tresult PLUGIN_API Analog_DelayController::terminate ()
	{
		// Here the Plug-in will be de-instantiated, last possibility to remove some memory!

		//---do not forget to call parent ------
		return EditControllerEx1::terminate ();
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API Analog_DelayController::setComponentState (IBStream* state)
	{
		// Host may call this to restore component/processor state into the controller (UI)
		if (!state)
			return kResultFalse;

		IBStreamer streamer(state, kLittleEndian);

		int32 firstInt = 0;
		if (!streamer.readInt32(firstInt))
			return kResultFalse;

		// If old format (single savedBypass written by older code), firstInt will be 0/1.
		// If new format, firstInt == number of parameter entries.
		if (firstInt == kParamCount)
		{
			const int32 numParams = firstInt;
			for (int32 i = 0; i < numParams; ++i)
			{
				int32 id = 0;
				float val = 0.f;
				if (!streamer.readInt32(id))
					continue;
				if (!streamer.readFloat(val))
					continue;

				// Update controller parameter (this will notify UI)
				setParamNormalized(static_cast<Vst::ParamID>(id), static_cast<Vst::ParamValue>(val));
			}
			return kResultOk;
		}
		else
		{
			// Legacy: treat firstInt as savedBypass
			int32 savedBypass = firstInt;
			bypass = (savedBypass != 0);
			setParamNormalized(kBypassId, bypass ? 1.0 : 0.0);
			return kResultOk;
		}
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API Analog_DelayController::setState (IBStream* state)
	{
		// Called by the host to restore the controller state (non-audio) - keep for compatibility
		return setComponentState(state);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API Analog_DelayController::getState (IBStream* state)
	{
		// Called by the host to store the controller state (we write all parameter normalized values)
		if (!state)
			return kResultFalse;

		IBStreamer streamer(state, kLittleEndian);

		// write number of params, then pairs (id, float normalized)
		const int32 numParams = kParamCount;
		if (!streamer.writeInt32(numParams))
			return kResultFalse;

		auto writeParam = [&](Vst::ParamID id, float normalized) -> bool {
			if (!streamer.writeInt32(static_cast<int32>(id)))
				return false;
			if (!streamer.writeFloat(normalized))
				return false;
			return true;
		};

		// fetch normalized values from parameter objects so we persist exactly what's in controller
		if (auto* p = parameters.getParameter(kBypassId))
		{
			writeParam(kBypassId, static_cast<float>(p->getNormalized()));
		}
		else writeParam(kBypassId, bypass ? 1.0f : 0.0f);

		if (auto* p = parameters.getParameter(kDelayTimeId))
		{
			writeParam(kDelayTimeId, static_cast<float>(p->getNormalized()));
		}
		if (auto* p = parameters.getParameter(kMixId))
		{
			writeParam(kMixId, static_cast<float>(p->getNormalized()));
		}
		if (auto* p = parameters.getParameter(kFeedbackId))
		{
			writeParam(kFeedbackId, static_cast<float>(p->getNormalized()));
		}
		if (auto* p = parameters.getParameter(kToneId))
		{
			writeParam(kToneId, static_cast<float>(p->getNormalized()));
		}
		if (auto* p = parameters.getParameter(kStereoWidthId))
		{
			writeParam(kStereoWidthId, static_cast<float>(p->getNormalized()));
		}

		return kResultOk;
	}

	//------------------------------------------------------------------------
	IPlugView* PLUGIN_API Analog_DelayController::createView (FIDString name)
	{
		// Here the Host wants to open your editor (if you have one)
		if (FIDStringsEqual (name, Vst::ViewType::kEditor))
		{
			// create your editor here and return a IPlugView ptr of it
			auto* view = new VSTGUI::VST3Editor (this, "view", "editor.uidesc");
			return view;
		}
		return nullptr;
	}

	//------------------------------------------------------------------------
	// Convert parameter normalized value -> textual representation for UI
	tresult PLUGIN_API Analog_DelayController::getParamStringByValue (Vst::ParamID tag,
		Vst::ParamValue valueNormalized,
		Vst::String128 string)
	{
		if (!string)
			return kResultFalse;

		char buf[128] = { 0 };

		switch (tag)
		{
		case kBypassId:
			snprintf(buf, sizeof(buf), "%s", (valueNormalized >= 0.5) ? "On" : "Off");
			break;

		case kDelayTimeId:
		{
			// Map normalized -> ms using same mapping as processor: 50..1000 ms
			const float ms = 50.0f + static_cast<float>(valueNormalized) * (1000.0f - 50.0f);
			// Show integer ms
			snprintf(buf, sizeof(buf), "%.0f ms", std::round(ms));
			break;
		}

		case kMixId:
		{
			// Mix is 0..1 wet gain relative to dry. Display percent 0..100%
			int pct = static_cast<int>(std::round(valueNormalized * 100.0));
			snprintf(buf, sizeof(buf), "%d%%", pct);
			break;
		}

		case kFeedbackId:
		{
			// stored normalized maps to actual feedback = value * 0.9
			float fb = static_cast<float>(valueNormalized) * 1.0f;
			int pct = static_cast<int>(std::round(fb * 100.0f));
			snprintf(buf, sizeof(buf), "%d%%", pct);
			break;
		}

		case kToneId:
		{
			// Tone 0-20% Darker, 21-40% Dark, 41-60% Neutral, 61-80% Bright, 81-100% Brighter
			int pct = static_cast<int>(std::round(valueNormalized * 100.0));
			int idx = 0;
			const char* labels[5] = { "Darker", "Dark", "Neutral", "Bright", "Brighter" };
			if (pct <= 20)
				idx = 0;
			else if(pct <= 40)
				idx = 1;
			else if(pct <= 60)
				idx = 2;
			else if(pct <= 80)
				idx = 3;
			else
				idx = 4;
			snprintf(buf, sizeof(buf), "%s", labels[idx]);
			break;
		}

		case kStereoWidthId:
		{
			// Tone 0-20% Darker, 21-40% Dark, 41-60% Neutral, 61-80% Bright, 81-100% Brighter
			int pct = static_cast<int>(std::round(valueNormalized * 100.0));
			int idx = 0;
			const char* labels[4] = { "Mono", "Narrow", "Stereo", "Wide" };
			if (pct <= 10.0f)
				idx = 0;
			else if (pct <= 36.0f)
				idx = 1;
			else if (pct <= 80.0f)
				idx = 2;
			else
				idx = 3;
			snprintf(buf, sizeof(buf), "%s", labels[idx]);
			break;
		}

		default:
			// fall back to base class formatting (numbers, etc.)
			return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
		}

		
		// Use UString128 to convert ASCII/UTF-8 into SDK TChar buffer safely
		Steinberg::UString128 ustr;
		ustr.fromAscii(buf);
		ustr.copyTo(string, 128);

		return kResultTrue;
	}

	//------------------------------------------------------------------------
} // namespace MyCompanyName