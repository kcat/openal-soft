/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2000 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "version.h"

#include <stdlib.h>

#include "alMain.h"
#include "alcontext.h"
#include "alu.h"
#include "alError.h"

#include "backends/base.h"


namespace {

constexpr ALchar alVendor[] = "OpenAL Community";
constexpr ALchar alVersion[] = "1.1 ALSOFT " ALSOFT_VERSION;
constexpr ALchar alRenderer[] = "OpenAL Soft";

// Error Messages
constexpr ALchar alNoError[] = "No Error";
constexpr ALchar alErrInvalidName[] = "Invalid Name";
constexpr ALchar alErrInvalidEnum[] = "Invalid Enum";
constexpr ALchar alErrInvalidValue[] = "Invalid Value";
constexpr ALchar alErrInvalidOp[] = "Invalid Operation";
constexpr ALchar alErrOutOfMemory[] = "Out of Memory";

/* Resampler strings */
constexpr ALchar alPointResampler[] = "Nearest";
constexpr ALchar alLinearResampler[] = "Linear";
constexpr ALchar alCubicResampler[] = "Cubic";
constexpr ALchar alBSinc12Resampler[] = "11th order Sinc";
constexpr ALchar alBSinc24Resampler[] = "23rd order Sinc";

} // namespace

/* WARNING: Non-standard export! Not part of any extension, or exposed in the
 * alcFunctions list.
 */
extern "C" AL_API const ALchar* AL_APIENTRY alsoft_get_version(void)
{
    const char *spoof{getenv("ALSOFT_SPOOF_VERSION")};
    if(spoof && spoof[0] != '\0') return spoof;
    return ALSOFT_VERSION;
}

#define DO_UPDATEPROPS() do {                                                 \
    if(!ATOMIC_LOAD(&context->DeferUpdates, almemory_order_acquire))          \
        UpdateContextProps(context.get());                                    \
    else                                                                      \
        ATOMIC_STORE(&context->PropsClean, AL_FALSE, almemory_order_release); \
} while(0)


AL_API ALvoid AL_APIENTRY alEnable(ALenum capability)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<almtx_t> _{context->PropLock};
    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        context->SourceDistanceModel = AL_TRUE;
        DO_UPDATEPROPS();
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid enable property 0x%04x", capability);
    }
}

AL_API ALvoid AL_APIENTRY alDisable(ALenum capability)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<almtx_t> _{context->PropLock};
    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        context->SourceDistanceModel = AL_FALSE;
        DO_UPDATEPROPS();
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid disable property 0x%04x", capability);
    }
}

AL_API ALboolean AL_APIENTRY alIsEnabled(ALenum capability)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return AL_FALSE;

    std::lock_guard<almtx_t> _{context->PropLock};
    ALboolean value{AL_FALSE};
    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        value = context->SourceDistanceModel;
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid is enabled property 0x%04x", capability);
    }

    return value;
}

AL_API ALboolean AL_APIENTRY alGetBoolean(ALenum pname)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return AL_FALSE;

    std::lock_guard<almtx_t> _{context->PropLock};
    ALboolean value{AL_FALSE};
    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        if(context->DopplerFactor != 0.0f)
            value = AL_TRUE;
        break;

    case AL_DOPPLER_VELOCITY:
        if(context->DopplerVelocity != 0.0f)
            value = AL_TRUE;
        break;

    case AL_DISTANCE_MODEL:
        if(context->mDistanceModel == DistanceModel::Default)
            value = AL_TRUE;
        break;

    case AL_SPEED_OF_SOUND:
        if(context->SpeedOfSound != 0.0f)
            value = AL_TRUE;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        if(ATOMIC_LOAD(&context->DeferUpdates, almemory_order_acquire))
            value = AL_TRUE;
        break;

    case AL_GAIN_LIMIT_SOFT:
        if(GAIN_MIX_MAX/context->GainBoost != 0.0f)
            value = AL_TRUE;
        break;

    case AL_NUM_RESAMPLERS_SOFT:
        /* Always non-0. */
        value = AL_TRUE;
        break;

    case AL_DEFAULT_RESAMPLER_SOFT:
        value = ResamplerDefault ? AL_TRUE : AL_FALSE;
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid boolean property 0x%04x", pname);
    }

    return value;
}

AL_API ALdouble AL_APIENTRY alGetDouble(ALenum pname)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return 0.0;

    std::lock_guard<almtx_t> _{context->PropLock};
    ALdouble value{0.0};
    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        value = (ALdouble)context->DopplerFactor;
        break;

    case AL_DOPPLER_VELOCITY:
        value = (ALdouble)context->DopplerVelocity;
        break;

    case AL_DISTANCE_MODEL:
        value = (ALdouble)context->mDistanceModel;
        break;

    case AL_SPEED_OF_SOUND:
        value = (ALdouble)context->SpeedOfSound;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        if(ATOMIC_LOAD(&context->DeferUpdates, almemory_order_acquire))
            value = (ALdouble)AL_TRUE;
        break;

    case AL_GAIN_LIMIT_SOFT:
        value = (ALdouble)GAIN_MIX_MAX/context->GainBoost;
        break;

    case AL_NUM_RESAMPLERS_SOFT:
        value = (ALdouble)(ResamplerMax + 1);
        break;

    case AL_DEFAULT_RESAMPLER_SOFT:
        value = (ALdouble)ResamplerDefault;
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid double property 0x%04x", pname);
    }

    return value;
}

AL_API ALfloat AL_APIENTRY alGetFloat(ALenum pname)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return 0.0f;

    std::lock_guard<almtx_t> _{context->PropLock};
    ALfloat value{0.0f};
    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        value = context->DopplerFactor;
        break;

    case AL_DOPPLER_VELOCITY:
        value = context->DopplerVelocity;
        break;

    case AL_DISTANCE_MODEL:
        value = (ALfloat)context->mDistanceModel;
        break;

    case AL_SPEED_OF_SOUND:
        value = context->SpeedOfSound;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        if(ATOMIC_LOAD(&context->DeferUpdates, almemory_order_acquire))
            value = (ALfloat)AL_TRUE;
        break;

    case AL_GAIN_LIMIT_SOFT:
        value = GAIN_MIX_MAX/context->GainBoost;
        break;

    case AL_NUM_RESAMPLERS_SOFT:
        value = (ALfloat)(ResamplerMax + 1);
        break;

    case AL_DEFAULT_RESAMPLER_SOFT:
        value = (ALfloat)ResamplerDefault;
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid float property 0x%04x", pname);
    }

    return value;
}

AL_API ALint AL_APIENTRY alGetInteger(ALenum pname)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return 0;

    std::lock_guard<almtx_t> _{context->PropLock};
    ALint value{0};
    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        value = (ALint)context->DopplerFactor;
        break;

    case AL_DOPPLER_VELOCITY:
        value = (ALint)context->DopplerVelocity;
        break;

    case AL_DISTANCE_MODEL:
        value = (ALint)context->mDistanceModel;
        break;

    case AL_SPEED_OF_SOUND:
        value = (ALint)context->SpeedOfSound;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        if(ATOMIC_LOAD(&context->DeferUpdates, almemory_order_acquire))
            value = (ALint)AL_TRUE;
        break;

    case AL_GAIN_LIMIT_SOFT:
        value = (ALint)(GAIN_MIX_MAX/context->GainBoost);
        break;

    case AL_NUM_RESAMPLERS_SOFT:
        value = ResamplerMax + 1;
        break;

    case AL_DEFAULT_RESAMPLER_SOFT:
        value = ResamplerDefault;
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid integer property 0x%04x", pname);
    }

    return value;
}

extern "C" AL_API ALint64SOFT AL_APIENTRY alGetInteger64SOFT(ALenum pname)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return 0;

    std::lock_guard<almtx_t> _{context->PropLock};
    ALint64SOFT value{0};
    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        value = (ALint64SOFT)context->DopplerFactor;
        break;

    case AL_DOPPLER_VELOCITY:
        value = (ALint64SOFT)context->DopplerVelocity;
        break;

    case AL_DISTANCE_MODEL:
        value = (ALint64SOFT)context->mDistanceModel;
        break;

    case AL_SPEED_OF_SOUND:
        value = (ALint64SOFT)context->SpeedOfSound;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        if(ATOMIC_LOAD(&context->DeferUpdates, almemory_order_acquire))
            value = (ALint64SOFT)AL_TRUE;
        break;

    case AL_GAIN_LIMIT_SOFT:
        value = (ALint64SOFT)(GAIN_MIX_MAX/context->GainBoost);
        break;

    case AL_NUM_RESAMPLERS_SOFT:
        value = (ALint64SOFT)(ResamplerMax + 1);
        break;

    case AL_DEFAULT_RESAMPLER_SOFT:
        value = (ALint64SOFT)ResamplerDefault;
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid integer64 property 0x%04x", pname);
    }

    return value;
}

AL_API void* AL_APIENTRY alGetPointerSOFT(ALenum pname)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return nullptr;

    std::lock_guard<almtx_t> _{context->PropLock};
    void *value{nullptr};
    switch(pname)
    {
    case AL_EVENT_CALLBACK_FUNCTION_SOFT:
        value = reinterpret_cast<void*>(context->EventCb);
        break;

    case AL_EVENT_CALLBACK_USER_PARAM_SOFT:
        value = context->EventParam;
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid pointer property 0x%04x", pname);
    }

    return value;
}

AL_API ALvoid AL_APIENTRY alGetBooleanv(ALenum pname, ALboolean *values)
{
    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
            case AL_GAIN_LIMIT_SOFT:
            case AL_NUM_RESAMPLERS_SOFT:
            case AL_DEFAULT_RESAMPLER_SOFT:
                values[0] = alGetBoolean(pname);
                return;
        }
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(pname)
    {
    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid boolean-vector property 0x%04x", pname);
    }
}

AL_API ALvoid AL_APIENTRY alGetDoublev(ALenum pname, ALdouble *values)
{
    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
            case AL_GAIN_LIMIT_SOFT:
            case AL_NUM_RESAMPLERS_SOFT:
            case AL_DEFAULT_RESAMPLER_SOFT:
                values[0] = alGetDouble(pname);
                return;
        }
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(pname)
    {
    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid double-vector property 0x%04x", pname);
    }
}

AL_API ALvoid AL_APIENTRY alGetFloatv(ALenum pname, ALfloat *values)
{
    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
            case AL_GAIN_LIMIT_SOFT:
            case AL_NUM_RESAMPLERS_SOFT:
            case AL_DEFAULT_RESAMPLER_SOFT:
                values[0] = alGetFloat(pname);
                return;
        }
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(pname)
    {
    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid float-vector property 0x%04x", pname);
    }
}

AL_API ALvoid AL_APIENTRY alGetIntegerv(ALenum pname, ALint *values)
{
    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
            case AL_GAIN_LIMIT_SOFT:
            case AL_NUM_RESAMPLERS_SOFT:
            case AL_DEFAULT_RESAMPLER_SOFT:
                values[0] = alGetInteger(pname);
                return;
        }
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(pname)
    {
    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid integer-vector property 0x%04x", pname);
    }
}

extern "C" AL_API void AL_APIENTRY alGetInteger64vSOFT(ALenum pname, ALint64SOFT *values)
{
    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
            case AL_GAIN_LIMIT_SOFT:
            case AL_NUM_RESAMPLERS_SOFT:
            case AL_DEFAULT_RESAMPLER_SOFT:
                values[0] = alGetInteger64SOFT(pname);
                return;
        }
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(pname)
    {
    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid integer64-vector property 0x%04x", pname);
    }
}

AL_API void AL_APIENTRY alGetPointervSOFT(ALenum pname, void **values)
{
    if(values)
    {
        switch(pname)
        {
            case AL_EVENT_CALLBACK_FUNCTION_SOFT:
            case AL_EVENT_CALLBACK_USER_PARAM_SOFT:
                values[0] = alGetPointerSOFT(pname);
                return;
        }
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(pname)
    {
    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid pointer-vector property 0x%04x", pname);
    }
}

AL_API const ALchar* AL_APIENTRY alGetString(ALenum pname)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return nullptr;

    const ALchar *value{nullptr};
    switch(pname)
    {
    case AL_VENDOR:
        value = alVendor;
        break;

    case AL_VERSION:
        value = alVersion;
        break;

    case AL_RENDERER:
        value = alRenderer;
        break;

    case AL_EXTENSIONS:
        value = context->ExtensionList;
        break;

    case AL_NO_ERROR:
        value = alNoError;
        break;

    case AL_INVALID_NAME:
        value = alErrInvalidName;
        break;

    case AL_INVALID_ENUM:
        value = alErrInvalidEnum;
        break;

    case AL_INVALID_VALUE:
        value = alErrInvalidValue;
        break;

    case AL_INVALID_OPERATION:
        value = alErrInvalidOp;
        break;

    case AL_OUT_OF_MEMORY:
        value = alErrOutOfMemory;
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid string property 0x%04x", pname);
    }
    return value;
}

AL_API ALvoid AL_APIENTRY alDopplerFactor(ALfloat value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(!(value >= 0.0f && isfinite(value)))
        alSetError(context.get(), AL_INVALID_VALUE, "Doppler factor %f out of range", value);
    else
    {
        std::lock_guard<almtx_t> _{context->PropLock};
        context->DopplerFactor = value;
        DO_UPDATEPROPS();
    }
}

AL_API ALvoid AL_APIENTRY alDopplerVelocity(ALfloat value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if((ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed)&EventType_Deprecated))
    {
        static constexpr ALCchar msg[] =
            "alDopplerVelocity is deprecated in AL1.1, use alSpeedOfSound";
        const ALsizei msglen = (ALsizei)strlen(msg);
        std::lock_guard<almtx_t> _{context->EventCbLock};
        ALbitfieldSOFT enabledevts{ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed)};
        if((enabledevts&EventType_Deprecated) && context->EventCb)
            (*context->EventCb)(AL_EVENT_TYPE_DEPRECATED_SOFT, 0, 0, msglen, msg,
                                context->EventParam);
    }

    if(!(value >= 0.0f && isfinite(value)))
        alSetError(context.get(), AL_INVALID_VALUE, "Doppler velocity %f out of range", value);
    else
    {
        std::lock_guard<almtx_t> _{context->PropLock};
        context->DopplerVelocity = value;
        DO_UPDATEPROPS();
    }
}

AL_API ALvoid AL_APIENTRY alSpeedOfSound(ALfloat value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(!(value > 0.0f && isfinite(value)))
        alSetError(context.get(), AL_INVALID_VALUE, "Speed of sound %f out of range", value);
    else
    {
        std::lock_guard<almtx_t> _{context->PropLock};
        context->SpeedOfSound = value;
        DO_UPDATEPROPS();
    }
}

AL_API ALvoid AL_APIENTRY alDistanceModel(ALenum value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(!(value == AL_INVERSE_DISTANCE || value == AL_INVERSE_DISTANCE_CLAMPED ||
         value == AL_LINEAR_DISTANCE || value == AL_LINEAR_DISTANCE_CLAMPED ||
         value == AL_EXPONENT_DISTANCE || value == AL_EXPONENT_DISTANCE_CLAMPED ||
         value == AL_NONE))
        alSetError(context.get(), AL_INVALID_VALUE, "Distance model 0x%04x out of range", value);
    else
    {
        std::lock_guard<almtx_t> _{context->PropLock};
        context->mDistanceModel = static_cast<DistanceModel>(value);
        if(!context->SourceDistanceModel)
            DO_UPDATEPROPS();
    }
}


AL_API ALvoid AL_APIENTRY alDeferUpdatesSOFT(void)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALCcontext_DeferUpdates(context.get());
}

AL_API ALvoid AL_APIENTRY alProcessUpdatesSOFT(void)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALCcontext_ProcessUpdates(context.get());
}


AL_API const ALchar* AL_APIENTRY alGetStringiSOFT(ALenum pname, ALsizei index)
{
    const char *ResamplerNames[] = {
        alPointResampler, alLinearResampler,
        alCubicResampler, alBSinc12Resampler,
        alBSinc24Resampler,
    };
    static_assert(COUNTOF(ResamplerNames) == ResamplerMax+1, "Incorrect ResamplerNames list");

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return nullptr;

    const ALchar *value{nullptr};
    switch(pname)
    {
    case AL_RESAMPLER_NAME_SOFT:
        if(index < 0 || (size_t)index >= COUNTOF(ResamplerNames))
            alSetError(context.get(), AL_INVALID_VALUE, "Resampler name index %d out of range",
                       index);
        else
            value = ResamplerNames[index];
        break;

    default:
        alSetError(context.get(), AL_INVALID_VALUE, "Invalid string indexed property");
    }
    return value;
}


void UpdateContextProps(ALCcontext *context)
{
    /* Get an unused proprty container, or allocate a new one as needed. */
    struct ALcontextProps *props{ATOMIC_LOAD(&context->FreeContextProps, almemory_order_acquire)};
    if(!props)
        props = static_cast<ALcontextProps*>(al_calloc(16, sizeof(*props)));
    else
    {
        struct ALcontextProps *next;
        do {
            next = ATOMIC_LOAD(&props->next, almemory_order_relaxed);
        } while(ATOMIC_COMPARE_EXCHANGE_WEAK(&context->FreeContextProps, &props, next,
                almemory_order_seq_cst, almemory_order_acquire) == 0);
    }

    /* Copy in current property values. */
    props->MetersPerUnit = context->MetersPerUnit;

    props->DopplerFactor = context->DopplerFactor;
    props->DopplerVelocity = context->DopplerVelocity;
    props->SpeedOfSound = context->SpeedOfSound;

    props->SourceDistanceModel = context->SourceDistanceModel;
    props->mDistanceModel = context->mDistanceModel;

    /* Set the new container for updating internal parameters. */
    props = ATOMIC_EXCHANGE(&context->Update, props, almemory_order_acq_rel);
    if(props)
    {
        /* If there was an unused update container, put it back in the
         * freelist.
         */
        ATOMIC_REPLACE_HEAD(struct ALcontextProps*, &context->FreeContextProps, props);
    }
}
