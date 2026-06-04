//------------------------------------------------------------------------
// Copyright(c) 2026 My Plug-in Company.
//------------------------------------------------------------------------

#include "processor.h"
#include "cids.h"

#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdint>
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#ifdef _WIN32
#include <windows.h> // for OutputDebugStringA
#endif
#include <cstdio>

using namespace Steinberg;

namespace
{

	

	struct Biquad
	{
		float b0 = 1, b1 = 0, b2 = 0;
		float a1 = 0, a2 = 0;

		inline float process(float x, float& z1, float& z2)
		{
			float y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return y;
		}
	};
	inline float saturate(float x)
	{
		const float drive = 1.42f;
		float y = x * drive;

		// Tube-style soft asymmetry
		return y / (1.0f + fabsf(y));
	}

	inline void advancePhase(float& phase, float inc, float wrap)
	{
		phase += inc;
		if (phase >= wrap)
			phase -= wrap;
	}

	inline float readDelayLinear(const std::vector<float>& buffer,
		float pos,
		int bufferSize)
	{
		// wrap position
		while (pos < 0.0f) pos += bufferSize;
		while (pos >= bufferSize) pos -= bufferSize;

		int i0 = static_cast<int>(pos);
		int i1 = i0 + 1;
		if (i1 >= bufferSize) i1 -= bufferSize;

		float frac = pos - (float)i0;

		return buffer[i0] * (1.0f - frac)
			+ buffer[i1] * frac;
	}
	inline float lcgRandFloat(uint32_t& state)
	{
		state = state * 1664525u + 1013904223u;
		return float((state >> 9) & 0x007FFFFF) / float(0x00800000);
	}
}

namespace MyCompanyName {
	//------------------------------------------------------------------------
	// Analog_DelayProcessor
	//------------------------------------------------------------------------
	Analog_DelayProcessor::Analog_DelayProcessor()
	{
		//--- set the wanted controller for our processor
		setControllerClass(kAnalog_DelayControllerUID);
	}

	//------------------------------------------------------------------------
	Analog_DelayProcessor::~Analog_DelayProcessor()
	{
	}

	tresult PLUGIN_API Analog_DelayProcessor::queryInterface(const TUID iid, void** obj)
	{
		QUERY_INTERFACE(iid, obj, IProcessContextRequirements::iid, IProcessContextRequirements)
			return AudioEffect::queryInterface(iid, obj);
	}

	tresult PLUGIN_API Analog_DelayProcessor::process(Vst::ProcessData& data)
	{
		// If there is no audio, inputs or outputs, do no process anything
		if (data.numSamples == 0 ||
			data.numInputs == 0 ||
			data.numOutputs == 0 ||
			data.symbolicSampleSize != Vst::kSample32 ||
			!data.inputs[0].channelBuffers32 ||
			!data.outputs[0].channelBuffers32)
		{
			return kResultOk;
		}
		// Get sample rate and buffer size, should only ever have 2 channels. If more, return
		if (bufferSize < 2)
		{
			resetDelayBuffers();
			if (bufferSize < 2)
				return kResultOk;
		}


		auto& in = data.inputs[0];
		auto& out = data.outputs[0];
		
		const int numInChannels = in.numChannels;
		const int numOutChannels = out.numChannels;

		float* inL = numInChannels > 0 ? in.channelBuffers32[0] : nullptr;
		float* inR = numInChannels > 1 ? in.channelBuffers32[1] : nullptr;
		float* outL = numOutChannels > 0 ? out.channelBuffers32[0] : nullptr;
		float* outR = numOutChannels > 1 ? out.channelBuffers32[1] : nullptr;

		// --- Read tempo from DAW ---
		if (data.processContext &&
			(data.processContext->state & Vst::ProcessContext::kTempoValid))
		{
			currentBPM = data.processContext->tempo;
		}
		// --- Parameter updates ---
		if (data.inputParameterChanges)
		{
			const int32 count = data.inputParameterChanges->getParameterCount();
			for (int32 i = 0; i < count; ++i)
			{
				auto* q = data.inputParameterChanges->getParameterData(i);
				if (!q) continue;

				int32 idx;
				Vst::ParamValue v;
				if (q->getPoint(q->getPointCount() - 1, idx, v) != kResultOk)
					continue;

				const float value = (float)v;

				switch (q->getParameterId())
				{
				case kBypassId:
					bypass = (value >= 0.5f);
					break;

				case kDelayTimeId:
					delayTimeMs = 50.0f + value * (1000.0f - 50.0f);
					break;

				case kFeedbackId:
					feedback = value * 0.95f;
					break;

				case kMixId:
					mix = std::clamp(value, 0.0f, 1.0f);
					break;

				case kToneId:
					delayEq = value;
					break;

				case kStereoWidthId:
					stereoWidth = value;
					break;
				case kTempoSyncId:
					delayTempoSync = static_cast<int>(value * 3.99f); // 4 options: off, quarter, dotted quarter, half
					break;
				}
			}
		}

		// --- True bypass ---
		// Copy input to output directly, no affect
		if (bypass)
		{
			const int ch = std::fmin(numInChannels, numOutChannels);
			for (int c = 0; c < ch; ++c)
			{
				memcpy(out.channelBuffers32[c],
					in.channelBuffers32[c],
					data.numSamples * sizeof(float));
			}
			return kResultOk;
		}
		// Will change in future, and change to a button
		float noteDivision = 0.0f; // Off for default

		switch (static_cast<int>(delayTempoSync)) {
		case 0: noteDivision = 0.0f;  break; // Off
		case 1: noteDivision = 1.0f;  break; // Quarter note (1/4)
		case 2: noteDivision = 0.5f;  break; // Eighth note (1/8)
		case 3: noteDivision = 0.75f; break; // Dotted Eighth note (1/8d)
		default: noteDivision = 0.0f; break;
		}

		// Guard against division by zero or invalid host BPM
		float tempoDelayMs = 0.0f;
		if (currentBPM > 0.0f) {
			tempoDelayMs = (60000.0f / currentBPM) * noteDivision;
		}
		// --- Delay math ---
		// Calculate samples required for delay time, limit to buffer size
		// I.E. 400 * 0.001 * 44100 = 17640 samples, limit to buffer size
		// 
		// Leave this for now, will be a button and have values 0,1,2,3 for off, quarter, eighth, dotted eigth divisions.
		delayTimeMs = (noteDivision > 0.0f) ? tempoDelayMs : delayTimeMs; //If Tempo Sync is on, override delay with tempo sync otherwise ignore.
		const int delaySamples = std::clamp((int)((delayTimeMs * 0.001f) * sampleRate), 1, bufferSize - 1);
		// alpha = e^(-2pi * fc / fs) or -6.283185307 * 2080 / 44100 = -0.296
		const float alpha = expf(-2.0f * 3.14159265f * toneCutoff / sampleRate);

		// One-pole coefficients
		const float lpAlpha = expf(-2.0f * 3.14159265f * lowPassFilterCutoff / sampleRate);
		const float hpAlpha = expf(-2.0f * 3.14159265f * highPassFilterCutoff / sampleRate);
		// Limit mix to 0 - 1
		const float wetGain = std::clamp(mix, 0.f, 1.f);
		const float dryGain = 1.0f;

		const float twoPi = 6.28318530718f;
		const float phaseInc = twoPi * modRateHz / sampleRate;

		const float stereoOffsetSamples = stereoOffsetMs * 0.001f * sampleRate; // Convert to seconds, then samples

		Biquad delayEqFilter;

		float eqAmount = (delayEq - 0.5f) * 2.0f; // Bin from -1 to 1
		

		// Dead zone for EQ
		if (fabs(eqAmount) < 0.01f) {
			delayEqFilter = {}; // flat
		} else {

			// Design peaking EQ filter, boost or cut based on eqAmount at low or high freq
			const float fs = (float)sampleRate;
			const float lowBoost = 600.0f;
			const float highBoost = 2200.0f;
			// Boost and Cut Frequencies
			float freq = (eqAmount > 0.f) ? highBoost : lowBoost;

			// Boost/cut range (musical)
			float gainDb = fabsf(eqAmount) * 5.5f; // 5.5 dB max

			float A = powf(10.f, gainDb / 40.f);
			float w0 = 2.f * 3.14159265f * freq / fs;
			float beta = sinf(w0) / (2.f * 0.8f);

			float cosw = cosf(w0);

			delayEqFilter.b0 = 1 + beta * A;
			delayEqFilter.b1 = -2 * cosw;
			delayEqFilter.b2 = 1 - beta * A;
			delayEqFilter.a1 = -2 * cosw;
			delayEqFilter.a2 = 1 - beta / A;

			// normalize
			float norm = 1.f / (1 + beta / A);
			delayEqFilter.b0 *= norm;
			delayEqFilter.b1 *= norm;
			delayEqFilter.b2 *= norm;
			delayEqFilter.a1 *= norm;
			delayEqFilter.a2 *= norm;
			}


		for (int i = 0; i < data.numSamples; ++i)
		{
			// --- Input ---
			const float inSampleL = inL ? inL[i] : 0.f;
			const float inSampleR = inR ? inR[i] : inSampleL;

			// --- Dry envelope follower ---
			float dryMono = 0.5f * (fabsf(inSampleL) + fabsf(inSampleR));
			dryEnv = fmaxf(dryMono, dryEnv * duckRelease);

			// --- Base read position ---
			const float readBase = (float)writeIndex - (float)delaySamples;

			// --- Mono modulation (shared for L/R, prevents drift) ---
			const float modOffset = sinf(modPhase) * (modDepthMs * 0.001f * (float)sampleRate);

			advancePhase(modPhase, phaseInc, twoPi);

			// --- Stereo offset (time-based width, NOT modulation) ---
			const float stereoOffsetSamples =
				stereoOffsetMs * 0.001f * (float)sampleRate;

			// --- True stereo delay reads ---
			const float delayedL = readDelayLinear(
				delayBufferL,
				readBase + modOffset + stereoOffsetSamples * 0.5f,
				bufferSize);

			const float delayedR = readDelayLinear(
				delayBufferR,
				readBase + modOffset - stereoOffsetSamples * 0.5f,
				bufferSize);

			// --- Stereo width via M/S (stable) ---
			float mid = 0.5f * (delayedL + delayedR);
			float side = 0.5f * (delayedL - delayedR);

			
			side *= stereoWidth;

			const float stereoL = mid + side;
			const float stereoR = mid - side;


			// --- Tone + saturation per channel ---
			// Overall darkness of each repeat, darkens for Analog style
			// Applied each repeat of the loop based on previous loop state
			toneStateL = (1.f - alpha) * stereoL + alpha * toneStateL;
			toneStateR = (1.f - alpha) * stereoR + alpha * toneStateR;



			// Saturate for analog style
			float satL = saturate(toneStateL);
			float satR = saturate(toneStateR);

			float duckGain = 1.0f / (1.0f + duckAmount * dryEnv);

			// Apply ducking ONLY to wet
			satL *= duckGain;
			satR *= duckGain;

			// ---- LOW PASS (8 kHz) ----
			lpStateL = (1.f - lpAlpha) * satL + lpAlpha * lpStateL;
			lpStateR = (1.f - lpAlpha) * satR + lpAlpha * lpStateR;

			// ---- HIGH PASS (220 Hz) ----
			hpStateL = hpAlpha * (hpStateL + lpStateL - prevL);
			hpStateR = hpAlpha * (hpStateR + lpStateR - prevR);

			prevL = lpStateL;
			prevR = lpStateR;

			satL = hpStateL;
			satR = hpStateR;

			// Apply EQ to left and right channels (Post Saturation)
			// Appyl boost/cut EQ after saturation to brighten or darken repeats
			if (fabs(eqAmount)) {
				satL = delayEqFilter.process(satL, eqZ1L, eqZ2L);
				satR = delayEqFilter.process(satR, eqZ1R, eqZ2R);
			}
			

			// --- Independent feedback (NO cross-feed) ---
			delayBufferL[writeIndex] =
				inSampleL + satL * feedback;

			delayBufferR[writeIndex] =
				inSampleR + satR * feedback;

			// --- Output ---
			if (outL) outL[i] = inSampleL * dryGain + satL * wetGain;
			if (outR) outR[i] = inSampleR * dryGain + satR * wetGain;

			// --- Advance write head ---
			if (++writeIndex >= bufferSize)
				writeIndex = 0;
		}

		return kResultOk;
	}
	

	//------------------------------------------------------------------------
	tresult PLUGIN_API Analog_DelayProcessor::setupProcessing(Vst::ProcessSetup& newSetup)
	{
		// log sample rate for debugging
		char buf[128];
		snprintf(buf, sizeof(buf), "setupProcessing: newSetup.sampleRate = %.2f, symbolicSampleSize = %d\n",
			newSetup.sampleRate, newSetup.symbolicSampleSize);
#ifdef _WIN32
		OutputDebugStringA(buf);
#else
		std::fprintf(stderr, "%s", buf);
#endif

		///Setup samples and buffer memory
		sampleRate = newSetup.sampleRate;
		resetDelayBuffers();

		
		return AudioEffect::setupProcessing(newSetup);
	}
	void Analog_DelayProcessor::resetDelayBuffers()
	{
		bufferSize = static_cast<int>(sampleRate * maxDelaySeconds);

		delayBufferL.assign(bufferSize, 0.f);
		delayBufferR.assign(bufferSize, 0.f);

		writeIndex = 0;
	}


	//------------------------------------------------------------------------
	tresult PLUGIN_API Analog_DelayProcessor::canProcessSampleSize(int32 symbolicSampleSize)
	{
		// by default kSample32 is supported
		if (symbolicSampleSize == Vst::kSample32)
			return kResultTrue;

		// disable the following comment if your processing support kSample64
		/* if (symbolicSampleSize == Vst::kSample64)
			return kResultTrue; */

		return kResultFalse;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API Analog_DelayProcessor::setState(IBStream* state)
	{
		// called when we load a preset, the model has to be reloaded
		if (!state)
			return kResultFalse;

		IBStreamer streamer(state, kLittleEndian);

		int32 firstInt = 0;
		if (!streamer.readInt32(firstInt))
			return kResultFalse;

		// New structured format: firstInt == number of params (kParamCount)
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

				switch (id)
				{
				case kBypassId:
					bypass = (val >= 0.5f);
					break;
				case kDelayTimeId:
					// map normalized -> ms (20..1000)
					delayTimeMs = 50.0f + val * (1000.0f - 50.0f);
					break;
				case kFeedbackId:
					feedback = std::clamp(val, 0.0f, 1.0f) * 0.95f;
					break;
				case kMixId:
					mix = std::clamp(val, 0.0f, 1.0f);
					break;
				case kToneId:
					delayEq = val;
					break;
				case kStereoWidthId:
					stereoWidth = val;
					break;
				}
			}

			return kResultOk;
		}
		else
		{
			// Legacy single-int bypass
			int32 savedBypass = firstInt;
			bypass = (savedBypass != 0);
			return kResultOk;
		}
	}


	//------------------------------------------------------------------------
	tresult PLUGIN_API Analog_DelayProcessor::getState(IBStream* state)
	{
		if (!state)
			return kResultFalse;

		IBStreamer streamer(state, kLittleEndian);

		// Write a structured list: param-count then pairs (id, normalized float)
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

		// Bypass
		if (!writeParam(kBypassId, bypass ? 1.0f : 0.0f))
			return kResultFalse;

		// DelayTime normalized (mapping used by controller: 50..1000 ms)
		// Ensure we compute normalized consistently: (delay - min) / (max - min)
		const float delayMin = 50.0f;
		const float delayMax = 1000.0f;
		float delayNorm = (delayTimeMs - delayMin) / (delayMax - delayMin);
		if (!writeParam(kDelayTimeId, delayNorm))
			return kResultFalse;

		// Mix
		if (!writeParam(kMixId, mix))
			return kResultFalse;

		// Feedback normalized (processor uses actual = normalized * 0.95)
		float fbNorm = 0.0f;
		if (feedback > 0.0f)
			fbNorm = feedback / 0.95f;
		if (!writeParam(kFeedbackId, fbNorm))
			return kResultFalse;

		// Tone
		if (!writeParam(kToneId, delayEq))
			return kResultFalse;

		// stereo width
		if (!writeParam(kStereoWidthId, stereoWidth))
			return kResultFalse;

		return kResultOk;
	}

	//------------------------------------------------------------------------
	Steinberg::tresult PLUGIN_API MyCompanyName::Analog_DelayProcessor::initialize(Steinberg::FUnknown* context)
	{
		// Here the Plug-in will be instantiated

		//---always initialize the parent-------
		Steinberg::tresult result = AudioEffect::initialize(context);
		// if everything Ok, continue
		if (result != Steinberg::kResultOk)
		{
			return result;
		}

		//--- create Audio IO ------
		addAudioInput(STR16("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
		addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

		/* If you don't need an event bus, you can remove the next line */
		addEventInput(STR16("Event In"), 1);

		return Steinberg::kResultOk;
	}
	

	//------------------------------------------------------------------------
	Steinberg::tresult PLUGIN_API MyCompanyName::Analog_DelayProcessor::setActive(Steinberg::TBool state)
	{
		//--- called when the Plug-in is enable/disable (On/Off) -----
		return AudioEffect::setActive(state);
	}

	//------------------------------------------------------------------------
	Steinberg::tresult PLUGIN_API MyCompanyName::Analog_DelayProcessor::terminate()
	{
		// Here the Plug-in will be de-instantiated, last possibility to remove some memory!

		//---do not forget to call parent ------
		return AudioEffect::terminate();
	}
}
