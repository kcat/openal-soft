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

#include "listener.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <ranges>
#include <span>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "core/except.h"
#include "core/logging.h"
#include "direct_defs.h"
#include "gsl/gsl"

using uint = unsigned int;


namespace {

inline void UpdateProps(gsl::not_null<al::Context*> context)
{
    if(!context->mDeferUpdates)
    {
        UpdateContextProps(context);
        return;
    }
    context->mPropsDirty = true;
}

inline void CommitAndUpdateProps(gsl::not_null<al::Context*> context)
{
    if(!context->mDeferUpdates)
    {
#if ALSOFT_EAX
        if(context->eaxNeedsCommit())
        {
            context->mPropsDirty = true;
            context->applyAllUpdates();
            return;
        }
#endif
        UpdateContextProps(context);
        return;
    }
    context->mPropsDirty = true;
}


void alListenerf(gsl::not_null<al::Context*> context, ALenum param, ALfloat value) noexcept
try {
    const auto proplock = std::lock_guard{context->mPropLock};
    auto &listener = context->mListener;
    switch(param)
    {
    case AL_GAIN:
        if(!(value >= 0.0f && std::isfinite(value)))
            context->throw_error(AL_INVALID_VALUE, "Listener gain {} out of range", value);
        listener.mGain = value;
        UpdateProps(context);
        return;

    case AL_METERS_PER_UNIT:
        if(!(value >= AL_MIN_METERS_PER_UNIT && value <= AL_MAX_METERS_PER_UNIT))
            context->throw_error(AL_INVALID_VALUE, "Listener meters per unit {} out of range",
                value);
        listener.mMetersPerUnit = value;
        UpdateProps(context);
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alListener3f(gsl::not_null<al::Context*> context, ALenum param, ALfloat value1,
    ALfloat value2, ALfloat value3) noexcept
try {
    auto &listener = context->mListener;
    const auto proplock = std::lock_guard{context->mPropLock};
    switch(param)
    {
    case AL_POSITION:
        if(!(std::isfinite(value1) && std::isfinite(value2) && std::isfinite(value3)))
            context->throw_error(AL_INVALID_VALUE, "Listener position out of range");
        listener.mPosition[0] = value1;
        listener.mPosition[1] = value2;
        listener.mPosition[2] = value3;
        CommitAndUpdateProps(context);
        return;

    case AL_VELOCITY:
        if(!(std::isfinite(value1) && std::isfinite(value2) && std::isfinite(value3)))
            context->throw_error(AL_INVALID_VALUE, "Listener velocity out of range");
        listener.mVelocity[0] = value1;
        listener.mVelocity[1] = value2;
        listener.mVelocity[2] = value3;
        CommitAndUpdateProps(context);
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener 3-float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alListenerfv(gsl::not_null<al::Context*> context, ALenum param, const ALfloat *values)
    noexcept
try {
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_GAIN:
    case AL_METERS_PER_UNIT:
        alListenerf(context, param, *values);
        return;

    case AL_POSITION:
    case AL_VELOCITY:
        const auto vals = std::span<const float,3>{values, 3_uz};
        alListener3f(context, param, vals[0], vals[1], vals[2]);
        return;
    }

    const auto proplock = std::lock_guard{context->mPropLock};
    auto &listener = context->mListener;
    switch(param)
    {
    case AL_ORIENTATION:
        const auto vals = std::span<const float,6>{values, 6_uz};
        if(!std::ranges::all_of(vals, [](float f){ return std::isfinite(f); }))
            context->throw_error(AL_INVALID_VALUE, "Listener orientation out of range");
        /* AT then UP */
        std::ranges::copy(vals | std::views::take(3), listener.mOrientAt.begin());
        std::ranges::copy(vals | std::views::drop(3), listener.mOrientUp.begin());
        CommitAndUpdateProps(context);
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener float-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alListeneri(gsl::not_null<al::Context*> context, ALenum param, ALint value) noexcept
try {
    const auto proplock = std::lock_guard{context->mPropLock};
    auto &listener = context->mListener;
    switch(param)
    {
    case AL_GAIN:
        if(value < 0)
            context->throw_error(AL_INVALID_VALUE, "Listener gain {} out of range", value);
        listener.mGain = gsl::narrow_cast<float>(value);
        UpdateProps(context);
        return;

    case AL_METERS_PER_UNIT:
        if(value < 1)
            context->throw_error(AL_INVALID_VALUE, "Listener meters per unit {} out of range",
                value);
        listener.mMetersPerUnit = gsl::narrow_cast<float>(value);
        UpdateProps(context);
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alListener3i(gsl::not_null<al::Context*> context, ALenum param, ALint value1, ALint value2,
    ALint value3) noexcept
try {
    switch(param)
    {
    case AL_POSITION:
    case AL_VELOCITY:
        alListener3f(context, param, gsl::narrow_cast<float>(value1),
            gsl::narrow_cast<float>(value2), gsl::narrow_cast<float>(value3));
        return;
    }

    const auto proplock [[maybe_unused]] = std::lock_guard{context->mPropLock};
    context->throw_error(AL_INVALID_ENUM, "Invalid listener 3-integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alListeneriv(gsl::not_null<al::Context*> context, ALenum param, const ALint *values) noexcept
try {
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    auto vals = std::span<const ALint>{};
    switch(param)
    {
    case AL_GAIN:
    case AL_METERS_PER_UNIT:
        alListeneri(context, param, *values);
        return;

    case AL_POSITION:
    case AL_VELOCITY:
        vals = {values, 3_uz};
        alListener3f(context, param, gsl::narrow_cast<float>(vals[0]),
            gsl::narrow_cast<float>(vals[1]), gsl::narrow_cast<float>(vals[2]));
        return;

    case AL_ORIENTATION:
        vals = {values, 6_uz};
        const auto fvals = std::array{gsl::narrow_cast<float>(vals[0]),
            gsl::narrow_cast<float>(vals[1]), gsl::narrow_cast<float>(vals[2]),
            gsl::narrow_cast<float>(vals[3]), gsl::narrow_cast<float>(vals[4]),
            gsl::narrow_cast<float>(vals[5]),
        };
        alListenerfv(context, param, fvals.data());
        return;
    }

    const auto proplock [[maybe_unused]] = std::lock_guard{context->mPropLock};
    context->throw_error(AL_INVALID_ENUM, "Invalid listener integer-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alGetListenerf(gsl::not_null<al::Context*> context, ALenum param, ALfloat *value) noexcept
try {
    if(!value)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto proplock = std::lock_guard{context->mPropLock};
    const auto &listener = context->mListener;
    switch(param)
    {
    case AL_GAIN: *value = listener.mGain; return;
    case AL_METERS_PER_UNIT: *value = listener.mMetersPerUnit; return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetListener3f(gsl::not_null<al::Context*> context, ALenum param, ALfloat *value1,
    ALfloat *value2, ALfloat *value3) noexcept
try {
    if(!value1 || !value2 || !value3)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto proplock = std::lock_guard{context->mPropLock};
    const auto &listener = context->mListener;
    switch(param)
    {
    case AL_POSITION:
        *value1 = listener.mPosition[0];
        *value2 = listener.mPosition[1];
        *value3 = listener.mPosition[2];
        return;

    case AL_VELOCITY:
        *value1 = listener.mVelocity[0];
        *value2 = listener.mVelocity[1];
        *value3 = listener.mVelocity[2];
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener 3-float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetListenerfv(gsl::not_null<al::Context*> context, ALenum param, ALfloat *values) noexcept
try {
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_GAIN:
    case AL_METERS_PER_UNIT:
        alGetListenerf(context, param, values);
        return;

    case AL_POSITION:
    case AL_VELOCITY:
        const auto vals = std::span{values, 3_uz};
        alGetListener3f(context, param, &vals[0], &vals[1], &vals[2]);
        return;
    }

    const auto proplock = std::lock_guard{context->mPropLock};
    const auto &listener = context->mListener;
    switch(param)
    {
    case AL_ORIENTATION:
        const auto vals = std::span{values, 6_uz};
        // AT then UP
        auto oiter = std::ranges::copy(listener.mOrientAt, vals.begin()).out;
        std::ranges::copy(listener.mOrientUp, oiter);
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener float-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alGetListeneri(gsl::not_null<al::Context*> context, ALenum param, ALint *value) noexcept
try {
    /* The largest float value that can fit in an int. */
    static constexpr auto float_int_max = 2147483520.0f;

    if(!value)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto proplock = std::lock_guard{context->mPropLock};
    const auto &listener = context->mListener;
    switch(param)
    {
    case AL_GAIN:
        *value = gsl::narrow_cast<int>(std::min(listener.mGain, float_int_max));
        return;
    case AL_METERS_PER_UNIT:
        *value = gsl::narrow_cast<int>(std::clamp(listener.mMetersPerUnit, 1.0f, float_int_max));
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetListener3i(gsl::not_null<al::Context*> context, ALenum param, ALint *value1,
    ALint *value2, ALint *value3) noexcept
try {
    if(!value1 || !value2 || !value3)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    const auto proplock = std::lock_guard{context->mPropLock};
    const auto &listener = context->mListener;
    switch(param)
    {
    case AL_POSITION:
        *value1 = gsl::narrow_cast<int>(listener.mPosition[0]);
        *value2 = gsl::narrow_cast<int>(listener.mPosition[1]);
        *value3 = gsl::narrow_cast<int>(listener.mPosition[2]);
        return;

    case AL_VELOCITY:
        *value1 = gsl::narrow_cast<int>(listener.mVelocity[0]);
        *value2 = gsl::narrow_cast<int>(listener.mVelocity[1]);
        *value3 = gsl::narrow_cast<int>(listener.mVelocity[2]);
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener 3-integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetListeneriv(gsl::not_null<al::Context*> context, ALenum param, ALint *values) noexcept
try {
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_GAIN:
    case AL_METERS_PER_UNIT:
        alGetListeneri(context, param, values);
        return;

    case AL_POSITION:
    case AL_VELOCITY:
        const auto vals = std::span{values, 3_uz};
        alGetListener3i(context, param, &vals[0], &vals[1], &vals[2]);
        return;
    }

    const auto proplock = std::lock_guard{context->mPropLock};
    const auto &listener = context->mListener;

    static constexpr auto f2i = [](const float val) { return gsl::narrow_cast<int>(val); };
    switch(param)
    {
    case AL_ORIENTATION:
        const auto vals = std::span{values, 6_uz};
        // AT then UP
        auto oiter = std::ranges::transform(listener.mOrientAt, vals.begin(), f2i).out;
        std::ranges::transform(listener.mOrientUp, oiter, f2i);
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid listener integer-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

} // namespace

AL_API DECL_FUNC2(void, alListenerf, ALenum,param, ALfloat,value)
AL_API DECL_FUNC4(void, alListener3f, ALenum,param, ALfloat,value1, ALfloat,value2, ALfloat,value3)
AL_API DECL_FUNC2(void, alListenerfv, ALenum,param, const ALfloat*,values)

AL_API DECL_FUNC2(void, alListeneri, ALenum,param, ALint,value)
AL_API DECL_FUNC4(void, alListener3i, ALenum,param, ALint,value1, ALint,value2, ALint,value3)
AL_API DECL_FUNC2(void, alListeneriv, ALenum,param, const ALint*,values)

AL_API DECL_FUNC2(void, alGetListenerf, ALenum,param, ALfloat*,value)
AL_API DECL_FUNC4(void, alGetListener3f, ALenum,param, ALfloat*,value1, ALfloat*,value2, ALfloat*,value3)
AL_API DECL_FUNC2(void, alGetListenerfv, ALenum,param, ALfloat*,values)

AL_API DECL_FUNC2(void, alGetListeneri, ALenum,param, ALint*,value)
AL_API DECL_FUNC4(void, alGetListener3i, ALenum,param, ALint*,value1, ALint*,value2, ALint*,value3)
AL_API DECL_FUNC2(void, alGetListeneriv, ALenum,param, ALint*,values)
