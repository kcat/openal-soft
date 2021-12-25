/*

EAX OpenAL Extension

Copyright (c) 2020-2021 Boris I. Bendovsky (bibendovsky@hotmail.com) and Contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

*/


#include "eax_eaxx_effect.h"

#include "eax_exception.h"

#include "eax_eaxx_null_effect.h"
#include "eax_eaxx_auto_wah_effect.h"
#include "eax_eaxx_chorus_effect.h"
#include "eax_eaxx_compressor_effect.h"
#include "eax_eaxx_distortion_effect.h"
#include "eax_eaxx_eax_reverb_effect.h"
#include "eax_eaxx_echo_effect.h"
#include "eax_eaxx_equalizer_effect.h"
#include "eax_eaxx_flanger_effect.h"
#include "eax_eaxx_frequency_shifter_effect.h"
#include "eax_eaxx_pitch_shifter_effect.h"
#include "eax_eaxx_ring_modulator_effect.h"
#include "eax_eaxx_vocal_morpher_effect.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEffectException :
	public Exception
{
public:
	explicit EaxxEffectException(
		const char* message)
		:
		Exception{"EAXX_EFFECT", message}
	{
	}
}; // EaxxEffectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

EaxxEffectUPtr make_eaxx_effect(
	const EaxxEffectParam& param)
{
	if (param.al_effect_slot == 0)
	{
		throw EaxxEffectException{"Null AL effect slot."};
	}

	switch (param.effect_type)
	{
		case EaxxEffectType::null:
			return std::make_unique<EaxxNullEffect>(param.al_effect_slot);

		case EaxxEffectType::auto_wah:
			return std::make_unique<EaxxAutoWahEffect>(param.al_effect_slot);

		case EaxxEffectType::chorus:
			return std::make_unique<EaxxChorusEffect>(param.al_effect_slot);

		case EaxxEffectType::compressor:
			return std::make_unique<EaxxCompressorEffect>(param.al_effect_slot);

		case EaxxEffectType::distortion:
			return std::make_unique<EaxxDistortionEffect>(param.al_effect_slot);

		case EaxxEffectType::eax_reverb:
			return std::make_unique<EaxxEaxReverbEffect>(param.al_effect_slot);

		case EaxxEffectType::echo:
			return std::make_unique<EaxxEchoEffect>(param.al_effect_slot);

		case EaxxEffectType::equalizer:
			return std::make_unique<EaxxEqualizerEffect>(param.al_effect_slot);

		case EaxxEffectType::flanger:
			return std::make_unique<EaxxFlangerEffect>(param.al_effect_slot);

		case EaxxEffectType::frequency_shifter:
			return std::make_unique<EaxxFrequencyShifterEffect>(param.al_effect_slot);

		case EaxxEffectType::pitch_shifter:
			return std::make_unique<EaxxPitchShifterEffect>(param.al_effect_slot);

		case EaxxEffectType::ring_modulator:
			return std::make_unique<EaxxRingModulatorEffect>(param.al_effect_slot);

		case EaxxEffectType::vocal_morpher:
			return std::make_unique<EaxxVocalMorpherEffect>(param.al_effect_slot);

		default:
			throw EaxxEffectException{"Unsupported type."};
	}
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
