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


#include "eax_al_object.h"

#include <cassert>

#include "AL/efx.h"

#include "eax_exception.h"


namespace eax
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

class AlObjectException :
	public Exception
{
public:
	explicit AlObjectException(
		const char* message)
		:
		Exception{"AL_OBJECT", message}
	{
	}
}; // AlObjectException

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


namespace
{


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

[[noreturn]]
void al_object_fail(
	const char* message)
{
	throw AlObjectException{message};
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

void EfxEffectSlotObjectDeleter::operator()(
	::ALuint al_name) const noexcept
{
	::alDeleteAuxiliaryEffectSlots(1, &al_name);
}

EfxEffectSlotObject make_efx_effect_slot_object()
{
	auto al_effect_slot_name = ::ALuint{};
	::alGenAuxiliaryEffectSlots(1, &al_effect_slot_name);

	if (al_effect_slot_name == 0)
	{
		al_object_fail("Failed to create EFX effect slot.");
	}

	return EfxEffectSlotObject{al_effect_slot_name, EfxEffectSlotObjectDeleter{}};
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

void EfxEffectObjectDeleter::operator()(
	::ALuint al_name) const noexcept
{
	::alDeleteEffects(1, &al_name);
}

EfxEffectObject make_efx_effect_object(
	::ALint al_effect_type)
{
	switch (al_effect_type)
	{
		case AL_EFFECT_NULL:
		case AL_EFFECT_CHORUS:
		case AL_EFFECT_DISTORTION:
		case AL_EFFECT_ECHO:
		case AL_EFFECT_FLANGER:
		case AL_EFFECT_FREQUENCY_SHIFTER:
		case AL_EFFECT_VOCAL_MORPHER:
		case AL_EFFECT_PITCH_SHIFTER:
		case AL_EFFECT_RING_MODULATOR:
		case AL_EFFECT_AUTOWAH:
		case AL_EFFECT_COMPRESSOR:
		case AL_EFFECT_EQUALIZER:
		case AL_EFFECT_EAXREVERB:
			break;

		default:
			al_object_fail("Unsupported AL effect type.");
	}

	auto al_effect = ::ALuint{};
	::alGenEffects(1, &al_effect);

	if (al_effect == AL_NONE)
	{
		al_object_fail("Failed to create AL effect object.");
	}

	auto efx_effect_object = EfxEffectObject{al_effect, EfxEffectObjectDeleter{}};

	auto new_al_effect_type = ::ALint{-1};

	::alEffecti(al_effect, AL_EFFECT_TYPE, al_effect_type);
	::alGetEffecti(al_effect, AL_EFFECT_TYPE, &new_al_effect_type);

	if (new_al_effect_type != al_effect_type)
	{
		al_object_fail("Failed to set AL effect type.");
	}

	return efx_effect_object;
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

void EfxFilterObjectDeleter::operator()(
	::ALuint al_name) const noexcept
{
	::alDeleteFilters(1, &al_name);
}

EfxFilterObject make_efx_filter_object()
{
	auto al_filter_name = ::ALuint{};
	::alGenFilters(1, &al_filter_name);

	if (al_filter_name == AL_NONE)
	{
		al_object_fail("Failed to create EFX filter.");
	}

	return EfxFilterObject{al_filter_name, EfxFilterObjectDeleter{}};
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


} // namespace eax
