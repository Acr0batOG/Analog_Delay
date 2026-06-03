//------------------------------------------------------------------------
// Copyright(c) 2026 My Plug-in Company.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

namespace MyCompanyName {

//------------------------------------------------------------------------
//  Analog_DelayProcessor
//------------------------------------------------------------------------
class Analog_DelayProcessor : public Steinberg::Vst::AudioEffect,
	public Steinberg::Vst::IProcessContextRequirements
{
public:
	Analog_DelayProcessor ();
	~Analog_DelayProcessor () SMTG_OVERRIDE;

    // Create function
	static Steinberg::FUnknown* createInstance (void* /*context*/) 
	{ 
		return (Steinberg::Vst::IAudioProcessor*)new Analog_DelayProcessor; 
	}

	// IProcessContextRequirements
	Steinberg::uint32 PLUGIN_API getProcessContextRequirements() SMTG_OVERRIDE
	{
		return IProcessContextRequirements::kNeedTempo;
	}

	// FUnknown query chain
	DELEGATE_REFCOUNT(Steinberg::Vst::AudioEffect) 
		Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) SMTG_OVERRIDE;


	//--- ---------------------------------------------------------------------
	// AudioEffect overrides:
	//--- ---------------------------------------------------------------------
	/** Called at first after constructor */
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	
	/** Called at the end before destructor */
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	
	/** Switch the Plug-in on/off */
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;

	/** Will be called before any process call */
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
	
	/** Asks if a given sample size is supported see SymbolicSampleSizes. */
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;

	/** Here we go...the process call */
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
		
	/** For persistence */
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;


//------------------------------------------------------------------------
protected:

	// Parameters (temporary hardcoded defaults) to add the UI
	
	// Will be changed with On/Off button
	bool bypass = false;

	double currentBPM = 120.0; // fallback tempo

	// This will be a knob, called "Time" Valued 0 - 1000 ms
	float delayTimeMs = 400.0f;   // base delay
	// This will be a knob called "Feedback" Valued 0 - 100%
	float feedback = 0.24f;       // swell
	// This will be a knob called "Mix" Valued 0 - 100%
	float mix = 0.50f;            // dry/wet
	// This will be a knob called "Tone" Valued 0 - 1
	float delayEq = .5f;      // 0 = darkest, 0.5 = neutral, 1 = brightest
	
	//This will be a 3 step knob, 0 - .5 - 1.0
	float stereoWidth = 0.5f; // 0 = mono, .5 = narrow, 1.0 = wide
	//static constexpr float kStereoWidthTable[3] = { 0.0f, .5f, 1.0f };

	// The following are internal variables for the delay effect, I.E. not changing
	// stereo offset (ms) — this controls width by splitting +/- half to L/R
	float stereoOffsetMs = 15.0f; // ms

	// modulation parameters (per-repeat modulation applied in mono)
	// depth in milliseconds (how much the delay time is modulated)
	// The modulation values will be unchanging for now
	float modDepthMs = 3.6f;
	// rate in Hz (LFO)
	float modRateHz = .20f;
	// internal phase (advances in process)
	float modPhase = 0.0f;
	// small stereo phase offset is no longer used for modulation (mono modulation now)
	float modPhaseOffset = 1.57079632679f; // kept for compatibility, not used for separate L/R modulation
	
	float lowPassFilterCutoff = 8000.0f; // Hz
	float highPassFilterCutoff = 180.0f; // Hz

	// One-pole LPF @ 8 kHz
	float lpStateL = 0.f;
	float lpStateR = 0.f;

	// One-pole HPF @ 220 Hz
	float hpStateL = 0.f;
	float hpStateR = 0.f;

	float prevL = 0.f;
	float prevR = 0.f;

	float dryEnv = 0.0f;
	float duckAmount = 0.40f;   // 0 = off, 0.3–0.5 = musical
	float duckRelease = 0.9995f;

	// vintage noise level added into each repeat (small)
	float noiseLevel = 0.002f;

	// rising-edge detector state to fire noise once per repeat (mono)
	float lastDelayedMono = 0.0f;
	// threshold to consider a delayed sample a new repeat (tweak to taste)
	float noiseGateThreshold = 0.001f;

	float maxDelaySeconds = 2.0f;
	int writeIndex = 0;
	int bufferSize = 0;
	double sampleRate = 44100.0;

	// Biquad state per channel
	float eqZ1L = 0.f, eqZ2L = 0.f;
	float eqZ1R = 0.f, eqZ2R = 0.f;

	// tone state (mono used for feedback) + per-channel tone state for output smoothing
	float toneStateL = 0.0f;
	float toneStateR = 0.0f;
	float toneCutoff = 2020.0f; // Hz (analog darkness)
// Private values only accesible by the processor.cpp file
private:
	// Delay buffer (mono). We reuse delayBufferL as the mono circular buffer.
	std::vector<float> delayBufferL;
	// right buffer still present but unused for mono storage (kept for compatibility)
	std::vector<float> delayBufferR;

	// small LCG RNG state for vintage noise
	uint32_t rngState = 2223;

	void resetDelayBuffers();
	

};

//------------------------------------------------------------------------
} // namespace MyCompanyName
