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
#include "alc/device.h"
#include "alc/inprogext.h"
#include "alnumeric.h"
#include "atomic.h"
#include "core/context.h"
#include "core/logging.h"
#include "core/mixer/defs.h"
#include "core/voice.h"
#include "direct_defs.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"
#include "opthelpers.h"
#include "strutils.hpp"

#if ALSOFT_EAX
#include "eax/globals.h"
#include "eax/x_ram.h"
#endif // ALSOFT_EAX


namespace {

using ALvoidptr = ALvoid*;

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
template<> struct ResamplerName<Resampler::Spline>
{ static constexpr const ALchar *Get() noexcept { return "Cubic Spline"; } };
template<> struct ResamplerName<Resampler::Gaussian>
{ static constexpr const ALchar *Get() noexcept { return "4-point Gaussian"; } };
template<> struct ResamplerName<Resampler::FastBSinc12>
{ static constexpr const ALchar *Get() noexcept { return "11th order Sinc (fast)"; } };
template<> struct ResamplerName<Resampler::BSinc12>
{ static constexpr const ALchar *Get() noexcept { return "11th order Sinc"; } };
template<> struct ResamplerName<Resampler::FastBSinc24>
{ static constexpr const ALchar *Get() noexcept { return "23rd order Sinc (fast)"; } };
template<> struct ResamplerName<Resampler::BSinc24>
{ static constexpr const ALchar *Get() noexcept { return "23rd order Sinc"; } };
template<> struct ResamplerName<Resampler::FastBSinc48>
{ static constexpr const ALchar *Get() noexcept { return "47th order Sinc (fast)"; } };
template<> struct ResamplerName<Resampler::BSinc48>
{ static constexpr const ALchar *Get() noexcept { return "47th order Sinc"; } };

constexpr auto GetResamplerName(const Resampler rtype) -> const ALchar*
{
#define HANDLE_RESAMPLER(r) case r: return ResamplerName<r>::Get()
    switch(rtype)
    {
    HANDLE_RESAMPLER(Resampler::Point);
    HANDLE_RESAMPLER(Resampler::Linear);
    HANDLE_RESAMPLER(Resampler::Spline);
    HANDLE_RESAMPLER(Resampler::Gaussian);
    HANDLE_RESAMPLER(Resampler::FastBSinc12);
    HANDLE_RESAMPLER(Resampler::BSinc12);
    HANDLE_RESAMPLER(Resampler::FastBSinc24);
    HANDLE_RESAMPLER(Resampler::BSinc24);
    HANDLE_RESAMPLER(Resampler::FastBSinc48);
    HANDLE_RESAMPLER(Resampler::BSinc48);
    }
#undef HANDLE_RESAMPLER
    /* Should never get here. */
    throw std::runtime_error{"Unexpected resampler index"};
}

constexpr auto DistanceModelFromALenum(ALenum model) noexcept -> std::optional<DistanceModel>
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
constexpr auto ALenumFromDistanceModel(DistanceModel model) -> ALenum
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
    throw std::runtime_error{fmt::format("Unexpected distance model {:#x}",
        al::to_underlying(model))};
}

enum PropertyValue : ALenum {
    DopplerFactorProp = AL_DOPPLER_FACTOR,
    DopplerVelocityProp = AL_DOPPLER_VELOCITY,
    DistanceModelProp = AL_DISTANCE_MODEL,
    SpeedOfSoundProp = AL_SPEED_OF_SOUND,
    DeferredUpdatesProp = AL_DEFERRED_UPDATES_SOFT,
    GainLimitProp = AL_GAIN_LIMIT_SOFT,
    NumResamplersProp = AL_NUM_RESAMPLERS_SOFT,
    DefaultResamplerProp = AL_DEFAULT_RESAMPLER_SOFT,
    DebugLoggedMessagesProp = AL_DEBUG_LOGGED_MESSAGES_EXT,
    DebugNextLoggedMessageLengthProp = AL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_EXT,
    MaxDebugMessageLengthProp = AL_MAX_DEBUG_MESSAGE_LENGTH_EXT,
    MaxDebugLoggedMessagesProp = AL_MAX_DEBUG_LOGGED_MESSAGES_EXT,
    MaxDebugGroupDepthProp = AL_MAX_DEBUG_GROUP_STACK_DEPTH_EXT,
    MaxLabelLengthProp = AL_MAX_LABEL_LENGTH_EXT,
    ContextFlagsProp = AL_CONTEXT_FLAGS_EXT,
#if ALSOFT_EAX
    EaxRamSizeProp = AL_EAX_RAM_SIZE,
    EaxRamFreeProp = AL_EAX_RAM_FREE,
#endif
};

template<typename T>
struct PropertyCastType {
    template<typename U> [[nodiscard]]
    constexpr auto operator()(U&& value) const noexcept -> T
    { return gsl::narrow_cast<T>(std::forward<U>(value)); }
};
/* Special-case ALboolean to be an actual bool instead of a char type. */
template<>
struct PropertyCastType<ALboolean> {
    template<typename U> [[nodiscard]]
    constexpr auto operator()(U&& value) const noexcept -> ALboolean
    { return gsl::narrow_cast<bool>(std::forward<U>(value)) ? AL_TRUE : AL_FALSE; }
};


template<typename T>
void GetValue(gsl::strict_not_null<ALCcontext*> context, ALenum pname, T *values) noexcept
{
    static constexpr auto cast_value = PropertyCastType<T>{};

    if(!values) [[unlikely]]
        return context->setError(AL_INVALID_VALUE, "NULL pointer");

    switch(PropertyValue{pname})
    {
    case AL_DOPPLER_FACTOR:
        *values = cast_value(context->mDopplerFactor);
        return;

    case AL_DOPPLER_VELOCITY:
        if(context->mContextFlags.test(ContextFlags::DebugBit)) [[unlikely]]
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
        auto debuglock = std::lock_guard{context->mDebugCbLock};
        *values = cast_value(context->mDebugLog.size());
        return;
    }

    case AL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_EXT:
    {
        auto debuglock = std::lock_guard{context->mDebugCbLock};
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

#if ALSOFT_EAX
#define EAX_ERROR "[alGetInteger] EAX not enabled"

    case AL_EAX_RAM_SIZE:
        if(eax_g_is_enabled)
        {
            *values = cast_value(eax_x_ram_max_size);
            return;
        }
        ERR(EAX_ERROR);
        break;

    case AL_EAX_RAM_FREE:
        if(eax_g_is_enabled)
        {
            auto *device = std::to_address(context->mALDevice);
            auto devlock = std::lock_guard{device->BufferLock};
            *values = cast_value(device->eax_x_ram_free_size);
            return;
        }
        ERR(EAX_ERROR);
        break;

#undef EAX_ERROR
#endif // ALSOFT_EAX
    }
    context->setError(AL_INVALID_ENUM, "Invalid context property {:#04x}", as_unsigned(pname));
}

template<>
void GetValue(gsl::strict_not_null<ALCcontext*> context, ALenum pname, ALvoid **values) noexcept
{
    if(!values) [[unlikely]]
        return context->setError(AL_INVALID_VALUE, "NULL pointer");

    switch(pname)
    {
    case AL_EVENT_CALLBACK_FUNCTION_SOFT:
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        *values = reinterpret_cast<void*>(context->mEventCb);
        return;

    case AL_EVENT_CALLBACK_USER_PARAM_SOFT:
        *values = context->mEventParam;
        return;

    case AL_DEBUG_CALLBACK_FUNCTION_EXT:
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        *values = reinterpret_cast<void*>(context->mDebugCb);
        return;

    case AL_DEBUG_CALLBACK_USER_PARAM_EXT:
        *values = context->mDebugParam;
        return;
    }
    context->setError(AL_INVALID_ENUM, "Invalid context pointer property {:#04x}",
        as_unsigned(pname));
}


inline void UpdateProps(ALCcontext *context)
{
    if(!context->mDeferUpdates)
        UpdateContextProps(context);
    else
        context->mPropsDirty = true;
}


void alEnable(gsl::strict_not_null<ALCcontext*> context, ALenum capability) noexcept
{
    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
    {
        auto proplock = std::lock_guard{context->mPropLock};
        context->mSourceDistanceModel = true;
        UpdateProps(context);
    }
        return;

    case AL_DEBUG_OUTPUT_EXT:
        context->mDebugEnabled.store(true);
        return;

    case AL_STOP_SOURCES_ON_DISCONNECT_SOFT:
        context->setError(AL_INVALID_OPERATION, "Re-enabling AL_STOP_SOURCES_ON_DISCONNECT_SOFT not yet supported");
        return;
    }
    context->setError(AL_INVALID_VALUE, "Invalid enable property {:#04x}",
        as_unsigned(capability));
}

void alDisable(gsl::strict_not_null<ALCcontext*> context, ALenum capability) noexcept
{
    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
    {
        auto proplock = std::lock_guard{context->mPropLock};
        context->mSourceDistanceModel = false;
        UpdateProps(context);
    }
        return;

    case AL_DEBUG_OUTPUT_EXT:
        context->mDebugEnabled.store(false);
        return;

    case AL_STOP_SOURCES_ON_DISCONNECT_SOFT:
        context->mStopVoicesOnDisconnect.store(false);
        return;
    }
    context->setError(AL_INVALID_VALUE, "Invalid disable property {:#04x}",
        as_unsigned(capability));
}

auto alIsEnabled(gsl::strict_not_null<ALCcontext*> context, ALenum capability) noexcept
    -> ALboolean
{
    auto proplock = std::lock_guard{context->mPropLock};
    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL: return context->mSourceDistanceModel ? AL_TRUE : AL_FALSE;
    case AL_DEBUG_OUTPUT_EXT: return context->mDebugEnabled ? AL_TRUE : AL_FALSE;
    case AL_STOP_SOURCES_ON_DISCONNECT_SOFT:
        return context->mStopVoicesOnDisconnect.load() ? AL_TRUE : AL_FALSE;
    }
    context->setError(AL_INVALID_VALUE, "Invalid is enabled property {:#04x}",
        as_unsigned(capability));
    return AL_FALSE;
}


auto alGetString(gsl::strict_not_null<ALCcontext*> context, ALenum pname) noexcept -> const ALchar*
{
    switch(pname)
    {
    case AL_VENDOR:
        if(auto &device = context->mALDevice; !device->mVendorOverride.empty())
            return device->mVendorOverride.c_str();
        return GetVendorString();
    case AL_VERSION:
        if(auto &device = context->mALDevice; !device->mVersionOverride.empty())
            return device->mVersionOverride.c_str();
        return GetVersionString();
    case AL_RENDERER:
        if(auto &device = context->mALDevice; !device->mRendererOverride.empty())
            return device->mRendererOverride.c_str();
        return GetRendererString();
    case AL_EXTENSIONS: return context->mExtensionsString.c_str();
    case AL_NO_ERROR: return GetNoErrorString();
    case AL_INVALID_NAME: return GetInvalidNameString();
    case AL_INVALID_ENUM: return GetInvalidEnumString();
    case AL_INVALID_VALUE: return GetInvalidValueString();
    case AL_INVALID_OPERATION: return GetInvalidOperationString();
    case AL_OUT_OF_MEMORY: return GetOutOfMemoryString();
    case AL_STACK_OVERFLOW_EXT: return GetStackOverflowString();
    case AL_STACK_UNDERFLOW_EXT: return GetStackUnderflowString();
    }
    context->setError(AL_INVALID_VALUE, "Invalid string property {:#04x}", as_unsigned(pname));
    return nullptr;
}


void alDopplerFactor(gsl::strict_not_null<ALCcontext*> context, ALfloat value) noexcept
{
    if(!(value >= 0.0f && std::isfinite(value)))
        context->setError(AL_INVALID_VALUE, "Doppler factor {} out of range", value);
    else
    {
        auto proplock = std::lock_guard{context->mPropLock};
        context->mDopplerFactor = value;
        UpdateProps(context);
    }
}

void alSpeedOfSound(gsl::strict_not_null<ALCcontext*> context, ALfloat value) noexcept
{
    if(!(value > 0.0f && std::isfinite(value)))
        context->setError(AL_INVALID_VALUE, "Speed of sound {} out of range", value);
    else
    {
        auto proplock = std::lock_guard{context->mPropLock};
        context->mSpeedOfSound = value;
        UpdateProps(context);
    }
}

void alDistanceModel(gsl::strict_not_null<ALCcontext*> context, ALenum value) noexcept
{
    if(auto model = DistanceModelFromALenum(value))
    {
        auto proplock = std::lock_guard{context->mPropLock};
        context->mDistanceModel = *model;
        if(!context->mSourceDistanceModel)
            UpdateProps(context);
    }
    else
        context->setError(AL_INVALID_VALUE, "Distance model {:#04x} out of range",
            as_unsigned(value));
}


auto alGetStringiSOFT(gsl::strict_not_null<ALCcontext*> context, ALenum pname, ALsizei index)
    noexcept -> const ALchar*
{
    switch(pname)
    {
    case AL_RESAMPLER_NAME_SOFT:
        if(index >= 0 && index <= al::to_underlying(Resampler::Max))
            return GetResamplerName(gsl::narrow_cast<Resampler>(index));
        context->setError(AL_INVALID_VALUE, "Resampler name index {} out of range", index);
        return nullptr;
    }
    context->setError(AL_INVALID_VALUE, "Invalid string indexed property {:#04x}",
        as_unsigned(pname));
    return nullptr;
}


void alDeferUpdatesSOFT(gsl::strict_not_null<ALCcontext*> context) noexcept
{
    auto proplock = std::lock_guard{context->mPropLock};
    context->deferUpdates();
}

void alProcessUpdatesSOFT(gsl::strict_not_null<ALCcontext*> context) noexcept
{
    auto proplock = std::lock_guard{context->mPropLock};
    context->processUpdates();
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
AL_API DECL_FUNC1(void, alDisable, ALenum,capability)
AL_API DECL_FUNC1(ALboolean, alIsEnabled, ALenum,capability)

#define DECL_GETFUNC(DECL, R, Name, Ext)                                      \
DECL auto AL_APIENTRY Name##Ext(ALenum pname) noexcept -> R                   \
{                                                                             \
    auto value = R{};                                                         \
    auto context = GetContextRef();                                           \
    if(context) [[likely]]                                                    \
        GetValue(gsl::make_not_null(context.get()), pname, &value);           \
    return value;                                                             \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##Direct##Ext(ALCcontext *context,           \
    ALenum pname) noexcept -> R                                               \
{                                                                             \
    auto value = R{};                                                         \
    GetValue(gsl::make_not_null(context), pname, &value);                     \
    return value;                                                             \
}                                                                             \
DECL auto AL_APIENTRY Name##v##Ext(ALenum pname, R *values) noexcept -> void  \
{                                                                             \
    auto context = GetContextRef();                                           \
    if(context) [[likely]]                                                    \
        GetValue(gsl::make_not_null(context.get()), pname, values);           \
}                                                                             \
FORCE_ALIGN auto AL_APIENTRY Name##v##Direct##Ext(ALCcontext *context,        \
    ALenum pname, R *values) noexcept -> void                                 \
{                                                                             \
    GetValue(gsl::make_not_null(context), pname, values);                     \
}

DECL_GETFUNC(AL_API, ALboolean, alGetBoolean,)
DECL_GETFUNC(AL_API, ALdouble, alGetDouble,)
DECL_GETFUNC(AL_API, ALfloat, alGetFloat,)
DECL_GETFUNC(AL_API, ALint, alGetInteger,)

DECL_GETFUNC(FORCE_ALIGN, ALvoidptr, alGetPointer,EXT)
DECL_GETFUNC(AL_API, ALint64SOFT, alGetInteger64,SOFT)
DECL_GETFUNC(AL_API, ALvoidptr, alGetPointer,SOFT)
#undef DECL_GETFUNC


AL_API DECL_FUNC1(const ALchar*, alGetString, ALenum,pname)

AL_API DECL_FUNC1(void, alDopplerFactor, ALfloat,value)
AL_API DECL_FUNC1(void, alSpeedOfSound, ALfloat,value)
AL_API DECL_FUNC1(void, alDistanceModel, ALenum,value)

AL_API DECL_FUNCEXT(void, alDeferUpdates,SOFT)
AL_API DECL_FUNCEXT(void, alProcessUpdates,SOFT)

AL_API DECL_FUNCEXT2(const ALchar*, alGetStringi,SOFT, ALenum,pname, ALsizei,index)


AL_API void AL_APIENTRY alDopplerVelocity(ALfloat value) noexcept
{
    auto context = GetContextRef();
    if(!context) [[unlikely]] return;

    if(context->mContextFlags.test(ContextFlags::DebugBit)) [[unlikely]]
        context->debugMessage(DebugSource::API, DebugType::DeprecatedBehavior, 1,
            DebugSeverity::Medium,
            "alDopplerVelocity is deprecated in AL 1.1, use alSpeedOfSound; "
            "alDopplerVelocity(x) -> alSpeedOfSound(343.3f * x)");

    if(!(value >= 0.0f && std::isfinite(value)))
        context->setError(AL_INVALID_VALUE, "Doppler velocity {} out of range", value);
    else
    {
        auto proplock = std::lock_guard{context->mPropLock};
        context->mDopplerVelocity = value;
        UpdateProps(context.get());
    }
}


void UpdateContextProps(ALCcontext *context)
{
    /* Get an unused property container, or allocate a new one as needed. */
    auto *props = context->mFreeContextProps.load(std::memory_order_acquire);
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
#if ALSOFT_EAX
    props->DistanceFactor = context->eaxGetDistanceFactor();
#endif

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
