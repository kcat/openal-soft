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

#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "al/debug.h"
#include "al/listener.h"
#include "alc/alu.h"
#include "alc/context.h"
#include "alc/inprogext.h"
#include "alnumeric.h"
#include "atomic.h"
#include "core/context.h"
#include "core/mixer/defs.h"
#include "core/voice.h"
#include "direct_defs.h"
#include "intrusive_ptr.h"
#include "opthelpers.h"
#include "strutils.h"

#ifdef ALSOFT_EAX
#include "alc/device.h"

#include "eax/globals.h"
#include "eax/x_ram.h"
#endif // ALSOFT_EAX


namespace {

[[nodiscard]] constexpr auto GetVendorString() noexcept { return "OpenAL Community"; }
[[nodiscard]] constexpr auto GetVersionString() noexcept { return "1.1 ALSOFT " ALSOFT_VERSION; }
[[nodiscard]] constexpr auto GetRendererString() noexcept { return "OpenAL Soft"; }

/* Error Messages */
[[nodiscard]] constexpr auto GetNoErrorString() noexcept { return "No Error"; }
[[nodiscard]] constexpr auto GetInvalidNameString() noexcept { return "Invalid Name"; }
[[nodiscard]] constexpr auto GetInvalidEnumString() noexcept { return "Invalid Enum"; }
[[nodiscard]] constexpr auto GetInvalidValueString() noexcept { return "Invalid Value"; }
[[nodiscard]] constexpr auto GetInvalidOperationString() noexcept { return "Invalid Operation"; }
[[nodiscard]] constexpr auto GetOutOfMemoryString() noexcept { return "Out of Memory"; }
[[nodiscard]] constexpr auto GetStackOverflowString() noexcept { return "Stack Overflow"; }
[[nodiscard]] constexpr auto GetStackUnderflowString() noexcept { return "Stack Underflow"; }

/* Resampler strings */
template<Resampler rtype> struct ResamplerName { };
template<> struct ResamplerName<Resampler::Point>
{ static constexpr const ALchar *Get() noexcept { return "Nearest"; } };
template<> struct ResamplerName<Resampler::Linear>
{ static constexpr const ALchar *Get() noexcept { return "Linear"; } };
template<> struct ResamplerName<Resampler::Cubic>
{ static constexpr const ALchar *Get() noexcept { return "Cubic"; } };
template<> struct ResamplerName<Resampler::FastBSinc12>
{ static constexpr const ALchar *Get() noexcept { return "11th order Sinc (fast)"; } };
template<> struct ResamplerName<Resampler::BSinc12>
{ static constexpr const ALchar *Get() noexcept { return "11th order Sinc"; } };
template<> struct ResamplerName<Resampler::FastBSinc24>
{ static constexpr const ALchar *Get() noexcept { return "23rd order Sinc (fast)"; } };
template<> struct ResamplerName<Resampler::BSinc24>
{ static constexpr const ALchar *Get() noexcept { return "23rd order Sinc"; } };

const ALchar *GetResamplerName(const Resampler rtype)
{
#define HANDLE_RESAMPLER(r) case r: return ResamplerName<r>::Get()
    switch(rtype)
    {
    HANDLE_RESAMPLER(Resampler::Point);
    HANDLE_RESAMPLER(Resampler::Linear);
    HANDLE_RESAMPLER(Resampler::Cubic);
    HANDLE_RESAMPLER(Resampler::FastBSinc12);
    HANDLE_RESAMPLER(Resampler::BSinc12);
    HANDLE_RESAMPLER(Resampler::FastBSinc24);
    HANDLE_RESAMPLER(Resampler::BSinc24);
    }
#undef HANDLE_RESAMPLER
    /* Should never get here. */
    throw std::runtime_error{"Unexpected resampler index"};
}

std::optional<DistanceModel> DistanceModelFromALenum(ALenum model)
{
    switch(model)
    {
    case AL_NONE: return DistanceModel::Disable;
    case AL_INVERSE_DISTANCE: return DistanceModel::Inverse;
    case AL_INVERSE_DISTANCE_CLAMPED: return DistanceModel::InverseClamped;
    case AL_LINEAR_DISTANCE: return DistanceModel::Linear;
    case AL_LINEAR_DISTANCE_CLAMPED: return DistanceModel::LinearClamped;
    case AL_EXPONENT_DISTANCE: return DistanceModel::Exponent;
    case AL_EXPONENT_DISTANCE_CLAMPED: return DistanceModel::ExponentClamped;
    }
    return std::nullopt;
}
ALenum ALenumFromDistanceModel(DistanceModel model)
{
    switch(model)
    {
    case DistanceModel::Disable: return AL_NONE;
    case DistanceModel::Inverse: return AL_INVERSE_DISTANCE;
    case DistanceModel::InverseClamped: return AL_INVERSE_DISTANCE_CLAMPED;
    case DistanceModel::Linear: return AL_LINEAR_DISTANCE;
    case DistanceModel::LinearClamped: return AL_LINEAR_DISTANCE_CLAMPED;
    case DistanceModel::Exponent: return AL_EXPONENT_DISTANCE;
    case DistanceModel::ExponentClamped: return AL_EXPONENT_DISTANCE_CLAMPED;
    }
    throw std::runtime_error{"Unexpected distance model "+std::to_string(static_cast<int>(model))};
}

enum PropertyValue : ALenum {
    DopplerFactor = AL_DOPPLER_FACTOR,
    DopplerVelocity = AL_DOPPLER_VELOCITY,
    DistanceModel = AL_DISTANCE_MODEL,
    SpeedOfSound = AL_SPEED_OF_SOUND,
    DeferredUpdates = AL_DEFERRED_UPDATES_SOFT,
    GainLimit = AL_GAIN_LIMIT_SOFT,
    NumResamplers = AL_NUM_RESAMPLERS_SOFT,
    DefaultResampler = AL_DEFAULT_RESAMPLER_SOFT,
    DebugLoggedMessages = AL_DEBUG_LOGGED_MESSAGES_EXT,
    DebugNextLoggedMessageLength = AL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_EXT,
    MaxDebugMessageLength = AL_MAX_DEBUG_MESSAGE_LENGTH_EXT,
    MaxDebugLoggedMessages = AL_MAX_DEBUG_LOGGED_MESSAGES_EXT,
    MaxDebugGroupDepth = AL_MAX_DEBUG_GROUP_STACK_DEPTH_EXT,
    MaxLabelLength = AL_MAX_LABEL_LENGTH_EXT,
    ContextFlags = AL_CONTEXT_FLAGS_EXT,
#ifdef ALSOFT_EAX
    EaxRamSize = AL_EAX_RAM_SIZE,
    EaxRamFree = AL_EAX_RAM_FREE,
#endif
};

template<typename T>
struct PropertyCastType {
    template<typename U>
    constexpr auto operator()(U&& value) const noexcept
    { return static_cast<T>(std::forward<U>(value)); }
};
/* Special-case ALboolean to be an actual bool instead of a char type. */
template<>
struct PropertyCastType<ALboolean> {
    template<typename U>
    constexpr ALboolean operator()(U&& value) const noexcept
    { return static_cast<bool>(std::forward<U>(value)) ? AL_TRUE : AL_FALSE; }
};


template<typename T>
void GetValue(ALCcontext *context, ALenum pname, T *values)
{
    auto cast_value = PropertyCastType<T>{};

    switch(static_cast<PropertyValue>(pname))
    {
    case AL_DOPPLER_FACTOR:
        *values = cast_value(context->mDopplerFactor);
        return;

    case AL_DOPPLER_VELOCITY:
        if(context->mContextFlags.test(ContextFlags::DebugBit)) UNLIKELY
            context->debugMessage(DebugSource::API, DebugType::DeprecatedBehavior, 0,
                DebugSeverity::Medium,
                "AL_DOPPLER_VELOCITY is deprecated in AL 1.1, use AL_SPEED_OF_SOUND; "
                "AL_DOPPLER_VELOCITY -> AL_SPEED_OF_SOUND / 343.3f");
        *values = cast_value(context->mDopplerVelocity);
        return;

    case AL_SPEED_OF_SOUND:
        *values = cast_value(context->mSpeedOfSound);
        return;

    case AL_GAIN_LIMIT_SOFT:
        *values = cast_value(GainMixMax / context->mGainBoost);
        return;

    case AL_DEFERRED_UPDATES_SOFT:
        *values = cast_value(context->mDeferUpdates ? AL_TRUE : AL_FALSE);
        return;

    case AL_DISTANCE_MODEL:
        *values = cast_value(ALenumFromDistanceModel(context->mDistanceModel));
        return;

    case AL_NUM_RESAMPLERS_SOFT:
        *values = cast_value(al::to_underlying(Resampler::Max) + 1);
        return;

    case AL_DEFAULT_RESAMPLER_SOFT:
        *values = cast_value(al::to_underlying(ResamplerDefault));
        return;

    case AL_DEBUG_LOGGED_MESSAGES_EXT:
    {
        std::lock_guard<std::mutex> debuglock{context->mDebugCbLock};
        *values = cast_value(context->mDebugLog.size());
        return;
    }

    case AL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_EXT:
    {
        std::lock_guard<std::mutex> debuglock{context->mDebugCbLock};
        *values = cast_value(context->mDebugLog.empty() ? 0_uz
            : (context->mDebugLog.front().mMessage.size()+1));
        return;
    }

    case AL_MAX_DEBUG_MESSAGE_LENGTH_EXT:
        *values = cast_value(MaxDebugMessageLength);
        return;

    case AL_MAX_DEBUG_LOGGED_MESSAGES_EXT:
        *values = cast_value(MaxDebugLoggedMessages);
        return;

    case AL_MAX_DEBUG_GROUP_STACK_DEPTH_EXT:
        *values = cast_value(MaxDebugGroupDepth);
        return;

    case AL_MAX_LABEL_LENGTH_EXT:
        *values = cast_value(MaxObjectLabelLength);
        return;

    case AL_CONTEXT_FLAGS_EXT:
        *values = cast_value(context->mContextFlags.to_ulong());
        return;

#ifdef ALSOFT_EAX

#define EAX_ERROR "[alGetInteger] EAX not enabled."

    case AL_EAX_RAM_SIZE:
        if(eax_g_is_enabled)
        {
            *values = cast_value(eax_x_ram_max_size);
            return;
        }
        context->setError(AL_INVALID_ENUM, EAX_ERROR);
        return;

    case AL_EAX_RAM_FREE:
        if(eax_g_is_enabled)
        {
            auto device = context->mALDevice.get();
            std::lock_guard<std::mutex> device_lock{device->BufferLock};
            *values = cast_value(device->eax_x_ram_free_size);
            return;
        }
        context->setError(AL_INVALID_ENUM, EAX_ERROR);
        return;

#undef EAX_ERROR

#endif // ALSOFT_EAX
    }
    context->setError(AL_INVALID_ENUM, "Invalid context property 0x%04x", pname);
}


inline void UpdateProps(ALCcontext *context)
{
    if(!context->mDeferUpdates)
        UpdateContextProps(context);
    else
        context->mPropsDirty = true;
}

} // namespace

/* WARNING: Non-standard export! Not part of any extension, or exposed in the
 * alcFunctions list.
 */
AL_API auto AL_APIENTRY alsoft_get_version() noexcept -> const ALchar*
{
    static const auto spoof = al::getenv("ALSOFT_SPOOF_VERSION");
    if(spoof) return spoof->c_str();
    return ALSOFT_VERSION;
}


AL_API DECL_FUNC1(void, alEnable, ALenum,capability)
FORCE_ALIGN void AL_APIENTRY alEnableDirect(ALCcontext *context, ALenum capability) noexcept
{
    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        {
            std::lock_guard<std::mutex> proplock{context->mPropLock};
            context->mSourceDistanceModel = true;
            UpdateProps(context);
        }
        break;

    case AL_DEBUG_OUTPUT_EXT:
        context->mDebugEnabled.store(true);
        break;

    case AL_STOP_SOURCES_ON_DISCONNECT_SOFT:
        context->setError(AL_INVALID_OPERATION, "Re-enabling AL_STOP_SOURCES_ON_DISCONNECT_SOFT not yet supported");
        break;

    default:
        context->setError(AL_INVALID_VALUE, "Invalid enable property 0x%04x", capability);
    }
}

AL_API DECL_FUNC1(void, alDisable, ALenum,capability)
FORCE_ALIGN void AL_APIENTRY alDisableDirect(ALCcontext *context, ALenum capability) noexcept
{
    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        {
            std::lock_guard<std::mutex> proplock{context->mPropLock};
            context->mSourceDistanceModel = false;
            UpdateProps(context);
        }
        break;

    case AL_DEBUG_OUTPUT_EXT:
        context->mDebugEnabled.store(false);
        break;

    case AL_STOP_SOURCES_ON_DISCONNECT_SOFT:
        context->mStopVoicesOnDisconnect.store(false);
        break;

    default:
        context->setError(AL_INVALID_VALUE, "Invalid disable property 0x%04x", capability);
    }
}

AL_API DECL_FUNC1(ALboolean, alIsEnabled, ALenum,capability)
FORCE_ALIGN ALboolean AL_APIENTRY alIsEnabledDirect(ALCcontext *context, ALenum capability) noexcept
{
    std::lock_guard<std::mutex> proplock{context->mPropLock};
    ALboolean value{AL_FALSE};
    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        value = context->mSourceDistanceModel ? AL_TRUE : AL_FALSE;
        break;

    case AL_DEBUG_OUTPUT_EXT:
        value = context->mDebugEnabled ? AL_TRUE : AL_FALSE;
        break;

    case AL_STOP_SOURCES_ON_DISCONNECT_SOFT:
        value = context->mStopVoicesOnDisconnect.load() ? AL_TRUE : AL_FALSE;
        break;

    default:
        context->setError(AL_INVALID_VALUE, "Invalid is enabled property 0x%04x", capability);
    }

    return value;
}

#define DECL_GETFUNC(R, Name, Ext)                                            \
AL_API auto AL_APIENTRY Name##Ext(ALenum pname) noexcept -> R                 \
{                                                                             \
    R value{};                                                                \
    auto context = GetContextRef();                                           \
    if(!context) UNLIKELY return value;                                       \
    Name##vDirect##Ext(GetContextRef().get(), pname, &value);                 \
    return value;                                                             \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context, ALenum pname) noexcept -> R \
{                                                                             \
    R value{};                                                                \
    Name##vDirect##Ext(context, pname, &value);                               \
    return value;                                                             \
}

DECL_GETFUNC(ALboolean, alGetBoolean,)
DECL_GETFUNC(ALdouble, alGetDouble,)
DECL_GETFUNC(ALfloat, alGetFloat,)
DECL_GETFUNC(ALint, alGetInteger,)

DECL_GETFUNC(ALint64SOFT, alGetInteger64,SOFT)
DECL_GETFUNC(ALvoid*, alGetPointer,SOFT)

#undef DECL_GETFUNC


AL_API DECL_FUNC2(void, alGetBooleanv, ALenum,pname, ALboolean*,values)
FORCE_ALIGN void AL_APIENTRY alGetBooleanvDirect(ALCcontext *context, ALenum pname, ALboolean *values) noexcept
{
    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context, pname, values);
}

AL_API DECL_FUNC2(void, alGetDoublev, ALenum,pname, ALdouble*,values)
FORCE_ALIGN void AL_APIENTRY alGetDoublevDirect(ALCcontext *context, ALenum pname, ALdouble *values) noexcept
{
    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context, pname, values);
}

AL_API DECL_FUNC2(void, alGetFloatv, ALenum,pname, ALfloat*,values)
FORCE_ALIGN void AL_APIENTRY alGetFloatvDirect(ALCcontext *context, ALenum pname, ALfloat *values) noexcept
{
    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context, pname, values);
}

AL_API DECL_FUNC2(void, alGetIntegerv, ALenum,pname, ALint*,values)
FORCE_ALIGN void AL_APIENTRY alGetIntegervDirect(ALCcontext *context, ALenum pname, ALint *values) noexcept
{
    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context, pname, values);
}

AL_API DECL_FUNCEXT2(void, alGetInteger64v,SOFT, ALenum,pname, ALint64SOFT*,values)
FORCE_ALIGN void AL_APIENTRY alGetInteger64vDirectSOFT(ALCcontext *context, ALenum pname, ALint64SOFT *values) noexcept
{
    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context, pname, values);
}

AL_API DECL_FUNCEXT2(void, alGetPointerv,SOFT, ALenum,pname, ALvoid**,values)
FORCE_ALIGN void AL_APIENTRY alGetPointervDirectSOFT(ALCcontext *context, ALenum pname, ALvoid **values) noexcept
{
    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");

    switch(pname)
    {
    case AL_EVENT_CALLBACK_FUNCTION_SOFT:
        *values = reinterpret_cast<void*>(context->mEventCb);
        break;

    case AL_EVENT_CALLBACK_USER_PARAM_SOFT:
        *values = context->mEventParam;
        break;

    case AL_DEBUG_CALLBACK_FUNCTION_EXT:
        *values = reinterpret_cast<void*>(context->mDebugCb);
        break;

    case AL_DEBUG_CALLBACK_USER_PARAM_EXT:
        *values = context->mDebugParam;
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid context pointer property 0x%04x", pname);
    }
}

AL_API DECL_FUNC1(const ALchar*, alGetString, ALenum,pname)
FORCE_ALIGN const ALchar* AL_APIENTRY alGetStringDirect(ALCcontext *context, ALenum pname) noexcept
{
    const ALchar *value{nullptr};
    switch(pname)
    {
    case AL_VENDOR: value = GetVendorString(); break;
    case AL_VERSION: value = GetVersionString(); break;
    case AL_RENDERER: value = GetRendererString(); break;
    case AL_EXTENSIONS: value = context->mExtensionsString.c_str(); break;
    case AL_NO_ERROR: value = GetNoErrorString(); break;
    case AL_INVALID_NAME: value = GetInvalidNameString(); break;
    case AL_INVALID_ENUM: value = GetInvalidEnumString(); break;
    case AL_INVALID_VALUE: value = GetInvalidValueString(); break;
    case AL_INVALID_OPERATION: value = GetInvalidOperationString(); break;
    case AL_OUT_OF_MEMORY: value = GetOutOfMemoryString(); break;
    case AL_STACK_OVERFLOW_EXT: value = GetStackOverflowString(); break;
    case AL_STACK_UNDERFLOW_EXT: value = GetStackUnderflowString(); break;
    default:
        context->setError(AL_INVALID_VALUE, "Invalid string property 0x%04x", pname);
    }
    return value;
}

AL_API DECL_FUNC1(void, alDopplerFactor, ALfloat,value)
FORCE_ALIGN void AL_APIENTRY alDopplerFactorDirect(ALCcontext *context, ALfloat value) noexcept
{
    if(!(value >= 0.0f && std::isfinite(value)))
        context->setError(AL_INVALID_VALUE, "Doppler factor %f out of range", value);
    else
    {
        std::lock_guard<std::mutex> proplock{context->mPropLock};
        context->mDopplerFactor = value;
        UpdateProps(context);
    }
}

AL_API DECL_FUNC1(void, alSpeedOfSound, ALfloat,value)
FORCE_ALIGN void AL_APIENTRY alSpeedOfSoundDirect(ALCcontext *context, ALfloat value) noexcept
{
    if(!(value > 0.0f && std::isfinite(value)))
        context->setError(AL_INVALID_VALUE, "Speed of sound %f out of range", value);
    else
    {
        std::lock_guard<std::mutex> proplock{context->mPropLock};
        context->mSpeedOfSound = value;
        UpdateProps(context);
    }
}

AL_API DECL_FUNC1(void, alDistanceModel, ALenum,value)
FORCE_ALIGN void AL_APIENTRY alDistanceModelDirect(ALCcontext *context, ALenum value) noexcept
{
    if(auto model = DistanceModelFromALenum(value))
    {
        std::lock_guard<std::mutex> proplock{context->mPropLock};
        context->mDistanceModel = *model;
        if(!context->mSourceDistanceModel)
            UpdateProps(context);
    }
    else
        context->setError(AL_INVALID_VALUE, "Distance model 0x%04x out of range", value);
}


AL_API DECL_FUNCEXT(void, alDeferUpdates,SOFT)
FORCE_ALIGN void AL_APIENTRY alDeferUpdatesDirectSOFT(ALCcontext *context) noexcept
{
    std::lock_guard<std::mutex> proplock{context->mPropLock};
    context->deferUpdates();
}

AL_API DECL_FUNCEXT(void, alProcessUpdates,SOFT)
FORCE_ALIGN void AL_APIENTRY alProcessUpdatesDirectSOFT(ALCcontext *context) noexcept
{
    std::lock_guard<std::mutex> proplock{context->mPropLock};
    context->processUpdates();
}


AL_API DECL_FUNCEXT2(const ALchar*, alGetStringi,SOFT, ALenum,pname, ALsizei,index)
FORCE_ALIGN const ALchar* AL_APIENTRY alGetStringiDirectSOFT(ALCcontext *context, ALenum pname, ALsizei index) noexcept
{
    const ALchar *value{nullptr};
    switch(pname)
    {
    case AL_RESAMPLER_NAME_SOFT:
        if(index < 0 || index > static_cast<ALint>(Resampler::Max))
            context->setError(AL_INVALID_VALUE, "Resampler name index %d out of range", index);
        else
            value = GetResamplerName(static_cast<Resampler>(index));
        break;

    default:
        context->setError(AL_INVALID_VALUE, "Invalid string indexed property");
    }
    return value;
}


AL_API void AL_APIENTRY alDopplerVelocity(ALfloat value) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(context->mContextFlags.test(ContextFlags::DebugBit)) UNLIKELY
        context->debugMessage(DebugSource::API, DebugType::DeprecatedBehavior, 0,
            DebugSeverity::Medium,
            "alDopplerVelocity is deprecated in AL 1.1, use alSpeedOfSound; "
            "alDopplerVelocity(x) -> alSpeedOfSound(343.3f * x)");

    if(!(value >= 0.0f && std::isfinite(value)))
        context->setError(AL_INVALID_VALUE, "Doppler velocity %f out of range", value);
    else
    {
        std::lock_guard<std::mutex> proplock{context->mPropLock};
        context->mDopplerVelocity = value;
        UpdateProps(context.get());
    }
}


void UpdateContextProps(ALCcontext *context)
{
    /* Get an unused property container, or allocate a new one as needed. */
    ContextProps *props{context->mFreeContextProps.load(std::memory_order_acquire)};
    if(!props)
    {
        context->allocContextProps();
        props = context->mFreeContextProps.load(std::memory_order_acquire);
    }
    ContextProps *next;
    do {
        next = props->next.load(std::memory_order_relaxed);
    } while(context->mFreeContextProps.compare_exchange_weak(props, next,
        std::memory_order_acq_rel, std::memory_order_acquire) == false);

    /* Copy in current property values. */
    const auto &listener = context->mListener;
    props->Position = listener.Position;
    props->Velocity = listener.Velocity;
    props->OrientAt = listener.OrientAt;
    props->OrientUp = listener.OrientUp;
    props->Gain = listener.Gain;
    props->MetersPerUnit = listener.mMetersPerUnit;

    props->AirAbsorptionGainHF = context->mAirAbsorptionGainHF;
    props->DopplerFactor = context->mDopplerFactor;
    props->DopplerVelocity = context->mDopplerVelocity;
    props->SpeedOfSound = context->mSpeedOfSound;

    props->SourceDistanceModel = context->mSourceDistanceModel;
    props->mDistanceModel = context->mDistanceModel;

    /* Set the new container for updating internal parameters. */
    props = context->mParams.ContextUpdate.exchange(props, std::memory_order_acq_rel);
    if(props)
    {
        /* If there was an unused update container, put it back in the
         * freelist.
         */
        AtomicReplaceHead(context->mFreeContextProps, props);
    }
}
