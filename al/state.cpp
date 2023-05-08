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

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "al/debug.h"
#include "albit.h"
#include "alc/alu.h"
#include "alc/context.h"
#include "alc/inprogext.h"
#include "alnumeric.h"
#include "atomic.h"
#include "core/context.h"
#include "core/except.h"
#include "core/mixer/defs.h"
#include "core/voice.h"
#include "intrusive_ptr.h"
#include "opthelpers.h"
#include "strutils.h"

#ifdef ALSOFT_EAX
#include "alc/device.h"

#include "eax/globals.h"
#include "eax/x_ram.h"
#endif // ALSOFT_EAX


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
constexpr ALchar alStackOverflow[] = "Stack Overflow";
constexpr ALchar alStackUnderflow[] = "Stack Underflow";

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
                DebugSeverity::Medium, -1,
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
        std::lock_guard<std::mutex> _{context->mDebugCbLock};
        *values = cast_value(context->mDebugLog.size());
        return;
    }

    case AL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_EXT:
    {
        std::lock_guard<std::mutex> _{context->mDebugCbLock};
        *values = cast_value(context->mDebugLog.empty() ? size_t{0}
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
AL_API const ALchar* AL_APIENTRY alsoft_get_version(void)
START_API_FUNC
{
    static const auto spoof = al::getenv("ALSOFT_SPOOF_VERSION");
    if(spoof) return spoof->c_str();
    return ALSOFT_VERSION;
}
END_API_FUNC


AL_API void AL_APIENTRY alEnable(ALenum capability)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        {
            std::lock_guard<std::mutex> _{context->mPropLock};
            context->mSourceDistanceModel = true;
            UpdateProps(context.get());
        }
        break;

    case AL_DEBUG_OUTPUT_EXT:
        context->mDebugEnabled = true;
        break;

    case AL_STOP_SOURCES_ON_DISCONNECT_SOFT:
        context->setError(AL_INVALID_OPERATION, "Re-enabling AL_STOP_SOURCES_ON_DISCONNECT_SOFT not yet supported");
        break;

    default:
        context->setError(AL_INVALID_VALUE, "Invalid enable property 0x%04x", capability);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alDisable(ALenum capability)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        {
            std::lock_guard<std::mutex> _{context->mPropLock};
            context->mSourceDistanceModel = false;
            UpdateProps(context.get());
        }
        break;

    case AL_DEBUG_OUTPUT_EXT:
        context->mDebugEnabled = false;
        break;

    case AL_STOP_SOURCES_ON_DISCONNECT_SOFT:
        context->mStopVoicesOnDisconnect = false;
        break;

    default:
        context->setError(AL_INVALID_VALUE, "Invalid disable property 0x%04x", capability);
    }
}
END_API_FUNC

AL_API ALboolean AL_APIENTRY alIsEnabled(ALenum capability)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return AL_FALSE;

    std::lock_guard<std::mutex> _{context->mPropLock};
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
        value = context->mStopVoicesOnDisconnect ? AL_TRUE : AL_FALSE;
        break;

    default:
        context->setError(AL_INVALID_VALUE, "Invalid is enabled property 0x%04x", capability);
    }

    return value;
}
END_API_FUNC

AL_API ALboolean AL_APIENTRY alGetBoolean(ALenum pname)
START_API_FUNC
{
    ALboolean value{AL_FALSE};
    alGetBooleanv(pname, &value);
    return value;
}
END_API_FUNC

AL_API ALdouble AL_APIENTRY alGetDouble(ALenum pname)
START_API_FUNC
{
    ALdouble value{0.0};
    alGetDoublev(pname, &value);
    return value;
}
END_API_FUNC

AL_API ALfloat AL_APIENTRY alGetFloat(ALenum pname)
START_API_FUNC
{
    ALfloat value{0.0f};
    alGetFloatv(pname, &value);
    return value;
}
END_API_FUNC

AL_API ALint AL_APIENTRY alGetInteger(ALenum pname)
START_API_FUNC
{
    ALint value{0};
    alGetIntegerv(pname, &value);
    return value;
}
END_API_FUNC

AL_API ALint64SOFT AL_APIENTRY alGetInteger64SOFT(ALenum pname)
START_API_FUNC
{
    ALint64SOFT value{0};
    alGetInteger64vSOFT(pname, &value);
    return value;
}
END_API_FUNC

AL_API ALvoid* AL_APIENTRY alGetPointerSOFT(ALenum pname)
START_API_FUNC
{
    ALvoid *value{nullptr};
    alGetPointervSOFT(pname, &value);
    return value;
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBooleanv(ALenum pname, ALboolean *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context.get(), pname, values);
}
END_API_FUNC

AL_API void AL_APIENTRY alGetDoublev(ALenum pname, ALdouble *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context.get(), pname, values);
}
END_API_FUNC

AL_API void AL_APIENTRY alGetFloatv(ALenum pname, ALfloat *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context.get(), pname, values);
}
END_API_FUNC

AL_API void AL_APIENTRY alGetIntegerv(ALenum pname, ALint *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context.get(), pname, values);
}
END_API_FUNC

AL_API void AL_APIENTRY alGetInteger64vSOFT(ALenum pname, ALint64SOFT *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");
    GetValue(context.get(), pname, values);
}
END_API_FUNC

AL_API void AL_APIENTRY alGetPointervSOFT(ALenum pname, ALvoid **values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");

    switch(pname)
    {
    case AL_EVENT_CALLBACK_FUNCTION_SOFT:
        *values = al::bit_cast<void*>(context->mEventCb);
        break;

    case AL_EVENT_CALLBACK_USER_PARAM_SOFT:
        *values = context->mEventParam;
        break;

    case AL_DEBUG_CALLBACK_FUNCTION_EXT:
        *values = al::bit_cast<void*>(context->mDebugCb);
        break;

    case AL_DEBUG_CALLBACK_USER_PARAM_EXT:
        *values = context->mDebugParam;
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid context pointer property 0x%04x", pname);
    }
}
END_API_FUNC

AL_API const ALchar* AL_APIENTRY alGetString(ALenum pname)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return nullptr;

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
        value = context->mExtensionsString.c_str();
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

    case AL_STACK_OVERFLOW_EXT:
        value = alStackOverflow;
        break;

    case AL_STACK_UNDERFLOW_EXT:
        value = alStackUnderflow;
        break;

    default:
        context->setError(AL_INVALID_VALUE, "Invalid string property 0x%04x", pname);
    }
    return value;
}
END_API_FUNC

AL_API void AL_APIENTRY alDopplerFactor(ALfloat value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!(value >= 0.0f && std::isfinite(value)))
        context->setError(AL_INVALID_VALUE, "Doppler factor %f out of range", value);
    else
    {
        std::lock_guard<std::mutex> _{context->mPropLock};
        context->mDopplerFactor = value;
        UpdateProps(context.get());
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alDopplerVelocity(ALfloat value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(context->mContextFlags.test(ContextFlags::DebugBit)) UNLIKELY
        context->debugMessage(DebugSource::API, DebugType::DeprecatedBehavior, 0,
            DebugSeverity::Medium, -1,
            "alDopplerVelocity is deprecated in AL 1.1, use alSpeedOfSound; "
            "alDopplerVelocity(x) -> alSpeedOfSound(343.3f * x)");

    if(!(value >= 0.0f && std::isfinite(value)))
        context->setError(AL_INVALID_VALUE, "Doppler velocity %f out of range", value);
    else
    {
        std::lock_guard<std::mutex> _{context->mPropLock};
        context->mDopplerVelocity = value;
        UpdateProps(context.get());
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alSpeedOfSound(ALfloat value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!(value > 0.0f && std::isfinite(value)))
        context->setError(AL_INVALID_VALUE, "Speed of sound %f out of range", value);
    else
    {
        std::lock_guard<std::mutex> _{context->mPropLock};
        context->mSpeedOfSound = value;
        UpdateProps(context.get());
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alDistanceModel(ALenum value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(auto model = DistanceModelFromALenum(value))
    {
        std::lock_guard<std::mutex> _{context->mPropLock};
        context->mDistanceModel = *model;
        if(!context->mSourceDistanceModel)
            UpdateProps(context.get());
    }
    else
        context->setError(AL_INVALID_VALUE, "Distance model 0x%04x out of range", value);
}
END_API_FUNC


AL_API void AL_APIENTRY alDeferUpdatesSOFT(void)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    context->deferUpdates();
}
END_API_FUNC

AL_API void AL_APIENTRY alProcessUpdatesSOFT(void)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    context->processUpdates();
}
END_API_FUNC


AL_API const ALchar* AL_APIENTRY alGetStringiSOFT(ALenum pname, ALsizei index)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return nullptr;

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
END_API_FUNC


void UpdateContextProps(ALCcontext *context)
{
    /* Get an unused proprty container, or allocate a new one as needed. */
    ContextProps *props{context->mFreeContextProps.load(std::memory_order_acquire)};
    if(!props)
        props = new ContextProps{};
    else
    {
        ContextProps *next;
        do {
            next = props->next.load(std::memory_order_relaxed);
        } while(context->mFreeContextProps.compare_exchange_weak(props, next,
                std::memory_order_seq_cst, std::memory_order_acquire) == 0);
    }

    /* Copy in current property values. */
    ALlistener &listener = context->mListener;
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
