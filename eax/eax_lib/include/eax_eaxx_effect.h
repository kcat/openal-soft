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


#ifndef EAX_EAXX_EFFECT_INCLUDED
#define EAX_EAXX_EFFECT_INCLUDED


#include <cstdint>

#include <memory>

#include "AL/al.h"

#include "eax_eaxx_eax_call.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class EaxxEffect
{
public:
	EaxxEffect() = default;

	virtual ~EaxxEffect() = default;


	virtual void load() = 0;

	virtual void dispatch(
		const EaxxEaxCall& eax_call) = 0;
}; // EaxxEffect

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

enum class EaxxEffectType
{
	none,

	null,
	auto_wah,
	chorus,
	compressor,
	distortion,
	eax_reverb,
	echo,
	equalizer,
	flanger,
	frequency_shifter,
	pitch_shifter,
	ring_modulator,
	vocal_morpher,
}; // EaxxEffectType

struct EaxxEffectParam
{
	EaxxEffectType effect_type;
	::ALuint al_effect_slot;
}; // EaxxEffectParam

using EaxxEffectUPtr = std::unique_ptr<EaxxEffect>;


EaxxEffectUPtr make_eaxx_effect(
	const EaxxEffectParam& param);

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax


#endif // !EAX_EAXX_EFFECT_INCLUDED
