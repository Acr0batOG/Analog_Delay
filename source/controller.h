//------------------------------------------------------------------------
// Copyright(c) 2026 My Plug-in Company.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "cids.h"

namespace MyCompanyName {

//------------------------------------------------------------------------
//  Analog_DelayController
//------------------------------------------------------------------------
class Analog_DelayController : public Steinberg::Vst::EditControllerEx1

{
public:
//------------------------------------------------------------------------
	Analog_DelayController () = default;
	~Analog_DelayController () SMTG_OVERRIDE = default;
	

    // Create function
	static Steinberg::FUnknown* createInstance (void* /*context*/)
	{
		return (Steinberg::Vst::IEditController*)new Analog_DelayController;
	}

	//--- from IPluginBase -----------------------------------------------
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

	//--- from EditController --------------------------------------------
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

 	//--- optional: string <-> value helpers for nicer UI text -----------
 	Steinberg::tresult PLUGIN_API getParamStringByValue (Steinberg::Vst::ParamID tag,Steinberg::Vst::ParamValue valueNormalized,Steinberg::Vst::String128 string) SMTG_OVERRIDE;



  	//---Interface---------
 	DEFINE_INTERFACES
 		// Here you can add more supported VST3 interfaces
 		// DEF_INTERFACE (Vst::IXXX)
 	END_DEFINE_INTERFACES(EditControllerEx1)
 	DELEGATE_REFCOUNT(EditControllerEx1)


//------------------------------------------------------------------------
protected:
	bool bypass = false;
};

//------------------------------------------------------------------------
} // namespace MyCompanyName
