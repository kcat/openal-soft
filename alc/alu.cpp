/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
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
#include "config_simd.h"

#include "alu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "alnumeric.h"
#include "alstring.h"
#include "atomic.h"
#include "core/ambidefs.h"
#include "core/async_event.h"
#include "core/bformatdec.h"
#include "core/bs2b.h"
#include "core/bsinc_defs.h"
#include "core/bsinc_tables.h"
#include "core/bufferline.h"
#include "core/buffer_storage.h"
#include "core/context.h"
#include "core/cpu_caps.h"
#include "core/cubic_tables.h"
#include "core/devformat.h"
#include "core/device.h"
#include "core/effects/base.h"
#include "core/effectslot.h"
#include "core/filters/biquad.h"
#include "core/filters/nfc.h"
#include "core/fpu_ctrl.h"
#include "core/hrtf.h"
#include "core/mastering.h"
#include "core/mixer.h"
#include "core/mixer/defs.h"
#include "core/mixer/hrtfdefs.h"
#include "core/resampler_limits.h"
#include "core/storage_formats.h"
#include "core/uhjfilter.h"
#include "core/voice.h"
#include "core/voice_change.h"
#include "core/front_stablizer.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"
#include "opthelpers.h"
#include "ringbuffer.h"
#include "strutils.hpp"
#include "vecmat.h"


static_assert((MaxResamplerPadding&1) == 0, "MaxResamplerPadding is not a multiple of two");

namespace {

using namespace std::chrono;
using namespace std::string_view_literals;

[[nodiscard]]
auto InitConeScale() noexcept -> f32
{
    auto ret = 1.0f;
    if(auto const optval = al::getenv("__ALSOFT_HALF_ANGLE_CONES"))
    {
        if(al::case_compare(*optval, "true"sv) == 0
            || strtol(optval->c_str(), nullptr, 0) == 1)
            ret *= 0.5f;
    }
    return ret;
}
/* Cone scalar */
auto const ConeScale = InitConeScale();

/* Localized scalars for mono sources (initialized in aluInit, after
 * configuration is loaded).
 */
auto XScale = 1.0f;
auto YScale = 1.0f;
auto ZScale = 1.0f;

/* Source distance scale for NFC filters. */
auto NfcScale = 1.0f;


using HrtfDirectMixerFunc = void(*)(FloatBufferSpan LeftOut, FloatBufferSpan RightOut,
    std::span<FloatBufferLine const> InSamples, std::span<f32x2> AccumSamples,
    std::span<f32, BufferLineSize> TempBuf, std::span<HrtfChannelState> ChanState, usize IrSize,
    usize SamplesToDo);

constinit auto MixDirectHrtf = HrtfDirectMixerFunc{MixDirectHrtf_C};

[[nodiscard]]
auto SelectHrtfMixer() -> HrtfDirectMixerFunc
{
#if HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixDirectHrtf_NEON;
#endif
#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixDirectHrtf_SSE;
#endif

    return MixDirectHrtf_C;
}


void BsincPrepare(u32 const increment, BsincState *const state, BSincTable const *const table)
{
    auto si = usize{BSincScaleCount - 1};
    auto sf = 0.0f;

    if(increment > MixerFracOne)
    {
        sf = MixerFracOne/gsl::narrow_cast<f32>(increment) - table->scaleBase;
        sf = std::max(0.0f, BSincScaleCount*sf*table->scaleRange - 1.0f);
        si = float2uint(sf);
        /* The interpolation factor is fit to this diagonally-symmetric curve
         * to reduce the transition ripple caused by interpolating different
         * scales of the sinc function.
         */
        sf -= gsl::narrow_cast<f32>(si);
        sf = 1.0f - std::sqrt(1.0f - sf*sf);
    }

    state->sf = sf;
    state->m = table->m[si];
    state->l = (state->m/2) - 1;
    state->filter = table->Tab.subspan(table->filterOffset[si]);
}

[[nodiscard]]
auto SelectResampler(Resampler const resampler, u32 const increment) -> ResamplerFunc
{
    switch(resampler)
    {
    case Resampler::Point:
        return Resample_Point_C;
    case Resampler::Linear:
#if HAVE_NEON
        if((CPUCapFlags&CPU_CAP_NEON))
            return Resample_Linear_NEON;
#endif
#if HAVE_SSE4_1
        if((CPUCapFlags&CPU_CAP_SSE4_1))
            return Resample_Linear_SSE4;
#endif
#if HAVE_SSE2
        if((CPUCapFlags&CPU_CAP_SSE2))
            return Resample_Linear_SSE2;
#endif
        return Resample_Linear_C;
    case Resampler::Spline:
    case Resampler::Gaussian:
#if HAVE_NEON
        if((CPUCapFlags&CPU_CAP_NEON))
            return Resample_Cubic_NEON;
#endif
#if HAVE_SSE4_1
        if((CPUCapFlags&CPU_CAP_SSE4_1))
            return Resample_Cubic_SSE4;
#endif
#if HAVE_SSE2
        if((CPUCapFlags&CPU_CAP_SSE2))
            return Resample_Cubic_SSE2;
#endif
#if HAVE_SSE
        if((CPUCapFlags&CPU_CAP_SSE))
            return Resample_Cubic_SSE;
#endif
        return Resample_Cubic_C;
    case Resampler::BSinc12:
    case Resampler::BSinc24:
    case Resampler::BSinc48:
        if(increment > MixerFracOne)
        {
#if HAVE_NEON
            if((CPUCapFlags&CPU_CAP_NEON))
                return Resample_BSinc_NEON;
#endif
#if HAVE_SSE
            if((CPUCapFlags&CPU_CAP_SSE))
                return Resample_BSinc_SSE;
#endif
            return Resample_BSinc_C;
        }
        [[fallthrough]];
    case Resampler::FastBSinc12:
    case Resampler::FastBSinc24:
    case Resampler::FastBSinc48:
#if HAVE_NEON
        if((CPUCapFlags&CPU_CAP_NEON))
            return Resample_FastBSinc_NEON;
#endif
#if HAVE_SSE
        if((CPUCapFlags&CPU_CAP_SSE))
            return Resample_FastBSinc_SSE;
#endif
        return Resample_FastBSinc_C;
    }

    return Resample_Point_C;
}

} // namespace

void aluInit(CompatFlagBitset const flags, f32 const nfcscale)
{
    MixDirectHrtf = SelectHrtfMixer();
    XScale = flags.test(CompatFlags::ReverseX) ? -1.0f : 1.0f;
    YScale = flags.test(CompatFlags::ReverseY) ? -1.0f : 1.0f;
    ZScale = flags.test(CompatFlags::ReverseZ) ? -1.0f : 1.0f;

    NfcScale = std::clamp(nfcscale, 0.0001f, 10000.0f);
}


auto PrepareResampler(Resampler const resampler, u32 const increment, InterpState *const state)
    -> ResamplerFunc
{
    switch(resampler)
    {
    case Resampler::Point:
    case Resampler::Linear:
        break;
    case Resampler::Spline:
        state->emplace<CubicState>(std::span{gSplineFilter.mTable});
        break;
    case Resampler::Gaussian:
        state->emplace<CubicState>(std::span{gGaussianFilter.mTable});
        break;
    case Resampler::FastBSinc12:
    case Resampler::BSinc12:
        BsincPrepare(increment, &state->emplace<BsincState>(), &gBSinc12);
        break;
    case Resampler::FastBSinc24:
    case Resampler::BSinc24:
        BsincPrepare(increment, &state->emplace<BsincState>(), &gBSinc24);
        break;
    case Resampler::FastBSinc48:
    case Resampler::BSinc48:
        BsincPrepare(increment, &state->emplace<BsincState>(), &gBSinc48);
        break;
    }
    return SelectResampler(resampler, increment);
}


void DeviceBase::Process(AmbiDecPostProcess const &proc, usize const SamplesToDo) const
{
    proc.mAmbiDecoder->process(RealOut.Buffer, Dry.Buffer, SamplesToDo);
}

void DeviceBase::Process(HrtfPostProcess const &proc, usize const SamplesToDo)
{
    /* HRTF is stereo output only. */
    auto const lidx = RealOut.ChannelIndex[FrontLeft];
    auto const ridx = RealOut.ChannelIndex[FrontRight];

    MixDirectHrtf(RealOut.Buffer[lidx.c_val], RealOut.Buffer[ridx.c_val], Dry.Buffer,
        HrtfAccumData, proc.mHrtfState->mTemp, proc.mHrtfState->mChannels,
        proc.mHrtfState->mIrSize, SamplesToDo);
}

void DeviceBase::Process(UhjPostProcess const &proc, usize const SamplesToDo)
{
    /* UHJ is stereo output only. */
    auto const lidx = RealOut.ChannelIndex[FrontLeft];
    auto const ridx = RealOut.ChannelIndex[FrontRight];

    /* Encode to stereo-compatible 2-channel UHJ output. */
    proc.mUhjEncoder->encode(std::span{RealOut.Buffer[lidx.c_val]}.first(SamplesToDo),
        std::span{RealOut.Buffer[ridx.c_val]}.first(SamplesToDo),
        {{std::span{Dry.Buffer[0]}.first(SamplesToDo),
            std::span{Dry.Buffer[1]}.first(SamplesToDo),
            std::span{Dry.Buffer[2]}.first(SamplesToDo)}});
}

void DeviceBase::Process(TsmePostProcess const &proc, usize const SamplesToDo)
{
    /* TSME is stereo output only. */
    auto const lidx = RealOut.ChannelIndex[FrontLeft];
    auto const ridx = RealOut.ChannelIndex[FrontRight];

    /* Encode to stereo-compatible 2-channel output. */
    proc.mUhjEncoder->encode(std::span{RealOut.Buffer[lidx.c_val]}.first(SamplesToDo),
        std::span{RealOut.Buffer[ridx.c_val]}.first(SamplesToDo),
        {{std::span{Dry.Buffer[0]}.first(SamplesToDo),
            std::span{Dry.Buffer[1]}.first(SamplesToDo),
            std::span{Dry.Buffer[2]}.first(SamplesToDo),
            std::span{Dry.Buffer[3]}.first(SamplesToDo)}});
}

void DeviceBase::Process(StablizerPostProcess const &proc, usize const SamplesToDo)
{
    /* Decode with front image stabilization. */
    auto const lidx = usize{RealOut.ChannelIndex[FrontLeft].c_val};
    auto const ridx = usize{RealOut.ChannelIndex[FrontRight].c_val};
    auto const cidx = usize{RealOut.ChannelIndex[FrontCenter].c_val};

    /* Move the existing direct L/R signal out so it doesn't get processed by
     * the stabilizer.
     */
    auto const leftout = std::span{RealOut.Buffer[lidx]}.first(SamplesToDo);
    auto const rightout = std::span{RealOut.Buffer[ridx]}.first(SamplesToDo);
    auto const mid = std::span{proc.mStablizer->MidDirect}.first(SamplesToDo);
    auto const side = std::span{proc.mStablizer->Side}.first(SamplesToDo);
    std::ranges::transform(leftout, rightout, mid.begin(), std::plus{});
    std::ranges::transform(leftout, rightout, side.begin(), std::minus{});
    std::ranges::fill(leftout, 0.0f);
    std::ranges::fill(rightout, 0.0f);

    /* Decode the B-Format mix to OutBuffer. */
    proc.mAmbiDecoder->process(RealOut.Buffer, Dry.Buffer, SamplesToDo);

    /* Include the decoded side signal with the direct side signal. */
    for(auto i = 0_uz;i < SamplesToDo;++i)
        side[i] += leftout[i] - rightout[i];

    /* Get the decoded mid signal and band-split it. */
    auto const tmpsamples = std::span{proc.mStablizer->Temp}.first(SamplesToDo);
    std::ranges::transform(leftout, rightout, tmpsamples.begin(), std::plus{});

    proc.mStablizer->MidFilter.process(tmpsamples, proc.mStablizer->MidHF, proc.mStablizer->MidLF);

    /* Apply an all-pass to all channels to match the band-splitter's phase
     * shift. This is to keep the phase synchronized between the existing
     * signal and the split mid signal.
     */
    for(const auto i : std::views::iota(0_uz, RealOut.Buffer.size()))
    {
        /* Skip the left and right channels, which are going to get overwritten,
         * and substitute the direct mid signal and direct+decoded side signal.
         */
        if(i == lidx)
            proc.mStablizer->ChannelFilters[i].processAllPass(mid);
        else if(i == ridx)
            proc.mStablizer->ChannelFilters[i].processAllPass(side);
        else
            proc.mStablizer->ChannelFilters[i].processAllPass(
                std::span{RealOut.Buffer[i]}.first(SamplesToDo));
    }

    /* This pans the separate low- and high-frequency signals between being on
     * the center channel and the left+right channels. The low-frequency signal
     * is panned 1/3rd toward center and the high-frequency signal is panned
     * 1/4th toward center. These values can be tweaked.
     */
    auto const mid_lf = std::cos(1.0f/3.0f * (std::numbers::pi_v<f32>*0.5f));
    auto const mid_hf = std::cos(1.0f/4.0f * (std::numbers::pi_v<f32>*0.5f));
    auto const center_lf = std::sin(1.0f/3.0f * (std::numbers::pi_v<f32>*0.5f));
    auto const center_hf = std::sin(1.0f/4.0f * (std::numbers::pi_v<f32>*0.5f));
    auto const centerout = std::span{RealOut.Buffer[cidx]}.first(SamplesToDo);
    for(auto i = 0_uz;i < SamplesToDo;++i)
    {
        /* Add the direct mid signal to the processed mid signal so it can be
         * properly combined with the direct+decoded side signal.
         */
        auto const m = proc.mStablizer->MidLF[i]*mid_lf+proc.mStablizer->MidHF[i]*mid_hf + mid[i];
        auto const c = proc.mStablizer->MidLF[i]*center_lf + proc.mStablizer->MidHF[i]*center_hf;
        auto const s = side[i];

        /* The generated center channel signal adds to the existing signal,
         * while the modified left and right channels replace.
         */
        leftout[i] = (m + s) * 0.5f;
        rightout[i] = (m - s) * 0.5f;
        centerout[i] += c * 0.5f;
    }
}

void DeviceBase::Process(Bs2bPostProcess const &proc, usize const SamplesToDo)
{
    /* BS2B is stereo output only. */
    auto const lidx = RealOut.ChannelIndex[FrontLeft];
    auto const ridx = RealOut.ChannelIndex[FrontRight];

    /* First, copy out the existing direct stereo signal so it doesn't get
     * processed by the BS2B filter.
     */
    auto const leftout = std::span{RealOut.Buffer[lidx.c_val]}.first(SamplesToDo);
    auto const rightout = std::span{RealOut.Buffer[ridx.c_val]}.first(SamplesToDo);
    auto const ldirect = std::span{proc.mBs2b->mStorage[0]}.first(SamplesToDo);
    auto const rdirect = std::span{proc.mBs2b->mStorage[1]}.first(SamplesToDo);
    std::ranges::copy(leftout, ldirect.begin());
    std::ranges::copy(rightout, rdirect.begin());
    std::ranges::fill(leftout, 0.0f);
    std::ranges::fill(rightout, 0.0f);

    /* Now, decode the ambisonic mix to the "real" output, and apply the BS2B
     * binaural/crossfeed filter.
     */
    proc.mAmbiDecoder->process(RealOut.Buffer, Dry.Buffer, SamplesToDo);
    proc.mBs2b->cross_feed(leftout, rightout);

    /* Finally, copy the direct signal back to the filtered output. */
    std::ranges::transform(leftout, ldirect, leftout.begin(), std::plus{});
    std::ranges::transform(rightout, rdirect, rightout.begin(), std::plus{});
}


namespace {

/* This RNG method was created based on the math found in opusdec. It's quick,
 * and starting with a seed value of 22222, is suitable for generating
 * whitenoise.
 */
[[nodiscard]]
auto dither_rng(u32 *const seed) noexcept -> u32
{
    *seed = (*seed * 96314165) + 907633515;
    return *seed;
}


/* Ambisonic upsampler function. It's effectively a matrix multiply. It takes
 * an 'upsampler' and 'rotator' as the input matrices, and creates a matrix
 * that behaves as if the B-Format input was first decoded to a speaker array
 * at its input order, encoded back into the higher order mix, then finally
 * rotated.
 */
void UpsampleBFormatTransform(
    std::span<std::array<f32, MaxAmbiChannels>,MaxAmbiChannels> const output,
    std::span<std::array<f32, MaxAmbiChannels> const> const upsampler,
    std::span<std::array<f32, MaxAmbiChannels> const,MaxAmbiChannels> const rotator,
    usize const ambi_order)
{
    auto const num_chans = AmbiChannelsFromOrder(ambi_order);
    std::ranges::fill(output | std::views::take(upsampler.size()) | std::views::join, 0.0f);
    for(auto const i : std::views::iota(0_uz, upsampler.size()))
    {
        for(auto const k : std::views::iota(0_uz, num_chans))
        {
            auto const a = upsampler[i][k];
            /* Write the full number of channels. The compiler will have an
             * easier time optimizing if it has a fixed length.
             */
            std::ranges::transform(rotator[k], output[i], output[i].begin(),
                [a](f32 const rot, f32 const dst) noexcept { return rot*a + dst; });
        }
    }
}


[[nodiscard]]
constexpr auto GetAmbiScales(AmbiScaling const scaletype) noexcept
{
    switch(scaletype)
    {
    case AmbiScaling::FuMa: return std::span{AmbiScale::FromFuMa};
    case AmbiScaling::SN3D: return std::span{AmbiScale::FromSN3D};
    case AmbiScaling::N3D: break;
    }
    return std::span{AmbiScale::FromN3D};
}

[[nodiscard]]
constexpr auto GetAmbiLayout(AmbiLayout const layouttype) noexcept
{
    if(layouttype == AmbiLayout::FuMa) return std::span{AmbiIndex::FromFuMa};
    return std::span{AmbiIndex::FromACN};
}

[[nodiscard]]
constexpr auto GetAmbi2DLayout(AmbiLayout const layouttype) noexcept
{
    if(layouttype == AmbiLayout::FuMa) return std::span{AmbiIndex::FromFuMa2D};
    return std::span{AmbiIndex::FromACN2D};
}


[[nodiscard]]
auto CalcContextParams(ContextBase *const ctx) -> bool
{
    auto *const props = ctx->mParams.ContextUpdate.exchange(nullptr, std::memory_order_acq_rel);
    if(!props) return false;

    auto const pos = al::Vector{props->Position[0], props->Position[1], props->Position[2], 1.0f};
    ctx->mParams.Position = pos;

    /* AT then UP */
    auto N = al::Vector{props->OrientAt[0], props->OrientAt[1], props->OrientAt[2], 0.0f};
    N.normalize();
    auto V = al::Vector{props->OrientUp[0], props->OrientUp[1], props->OrientUp[2], 0.0f};
    V.normalize();
    /* Build and normalize right-vector */
    auto U = al::Vector{N.cross_product(V)};
    U.normalize();

    auto const rot = al::Matrix{
        U[0],  V[0], -N[0],  0.0f,
        U[1],  V[1], -N[1],  0.0f,
        U[2],  V[2], -N[2],  0.0f,
        0.0f,  0.0f,  0.0f,  1.0f};
    auto const vel = al::Vector{props->Velocity[0], props->Velocity[1], props->Velocity[2], 0.0};

    ctx->mParams.Matrix = rot;
    ctx->mParams.Velocity = rot * vel;

    ctx->mParams.Gain = props->Gain * ctx->mGainBoost;
    ctx->mParams.MetersPerUnit = props->MetersPerUnit
#if ALSOFT_EAX
        * props->DistanceFactor
#endif
        ;
    ctx->mParams.AirAbsorptionGainHF = props->AirAbsorptionGainHF;

    ctx->mParams.DopplerFactor = props->DopplerFactor;
    ctx->mParams.SpeedOfSound = props->SpeedOfSound * props->DopplerVelocity
#if ALSOFT_EAX
        / props->DistanceFactor
#endif
        ;

    ctx->mParams.SourceDistanceModel = props->SourceDistanceModel;
    ctx->mParams.mDistanceModel = props->mDistanceModel;

    AtomicReplaceHead(ctx->mFreeContextProps, props);
    return true;
}

[[nodiscard]]
auto CalcEffectSlotParams(EffectSlotBase *const slot, EffectSlotBase **const sorted_slots,
    ContextBase *const context) ->bool
{
    auto *const props = slot->Update.exchange(nullptr, std::memory_order_acq_rel);
    if(!props) return false;

    /* If the effect slot target changed, clear the first sorted entry to force
     * a re-sort.
     */
    if(slot->Target != props->Target)
        *sorted_slots = nullptr;
    slot->Gain = props->Gain;
    slot->AuxSendAuto = props->AuxSendAuto;
    slot->Target = props->Target;
    slot->EffectType = props->Type;
    slot->mEffectProps = props->Props;

    slot->RoomRolloff = 0.0f;
    slot->DecayTime = 0.0f;
    slot->DecayLFRatio = 0.0f;
    slot->DecayHFRatio = 0.0f;
    slot->DecayHFLimit = false;
    slot->AirAbsorptionGainHF = 1.0f;
    if(auto *const reverbprops = std::get_if<ReverbProps>(&props->Props))
    {
        slot->RoomRolloff = reverbprops->RoomRolloffFactor;
        slot->AirAbsorptionGainHF = reverbprops->AirAbsorptionGainHF;
        /* If this effect slot's Auxiliary Send Auto is off, don't apply the
         * automatic send adjustments based on source distance.
         */
        if(slot->AuxSendAuto)
        {
            slot->DecayTime = reverbprops->DecayTime;
            slot->DecayLFRatio = reverbprops->DecayLFRatio;
            slot->DecayHFRatio = reverbprops->DecayHFRatio;
            slot->DecayHFLimit = reverbprops->DecayHFLimit;
        }
    }

    auto *const state = props->State.release();
    auto *const oldstate = slot->mEffectState.release();
    slot->mEffectState.reset(state);

    /* Only release the old state if it won't get deleted, since we can't be
     * deleting/freeing anything in the mixer.
     */
    if(!oldstate->releaseIfNoDelete())
    {
        /* Otherwise, if it would be deleted send it off with a release event. */
        auto *const ring = context->mAsyncEvents.get();
        if(auto const evt_vec = ring->getWriteVector(); !evt_vec[0].empty()) [[likely]]
        {
            auto &evt = InitAsyncEvent<AsyncEffectReleaseEvent>(evt_vec[0].front());
            evt.mEffectState = oldstate;
            ring->writeAdvance(1);
        }
        else
        {
            /* If writing the event failed, the queue was probably full. Store
             * the old state in the property object where it can eventually be
             * cleaned up sometime later (not ideal, but better than blocking
             * or leaking).
             */
            props->State.reset(oldstate);
        }
    }

    AtomicReplaceHead(context->mFreeEffectSlotProps, props);

    auto const output = std::invoke([slot,context]() -> EffectTarget
    {
        if(auto *const target = slot->Target)
            return EffectTarget{&target->Wet, nullptr};
        auto const device = al::get_not_null(context->mDevice);
        return EffectTarget{&device->Dry, &device->RealOut};
    });
    state->update(context, slot, &slot->mEffectProps, output);
    return true;
}


/* Scales the azimuth of the given vector by 3 if it's in front. Effectively
 * scales +/-30 degrees to +/-90 degrees, leaving > +90 and < -90 alone.
 */
[[nodiscard]]
auto ScaleAzimuthFront3(std::array<f32, 3> pos) -> std::array<f32, 3>
{
    if(pos[2] < 0.0f)
    {
        /* Normalize the length of the x,z components for a 2D vector of the
         * azimuth angle. Negate Z since {0,0,-1} is angle 0.
         */
        auto const len2d = std::sqrt(pos[0]*pos[0] + pos[2]*pos[2]);

        /* Z > cos(pi/6) = -30 < azimuth < 30 degrees. */
        if(auto z = -pos[2] / len2d; z > 0.866025403785f)
        {
            auto x = pos[0] / len2d;

            /* Triple the angle represented by x,z. */
            x = x*3.0f - x*x*x*4.0f;
            z = z*z*z*4.0f - z*3.0f;

            /* Scale the vector back to fit in 3D. */
            pos[0] = x * len2d;
            pos[2] = -z * len2d;
        }
        else
        {
            /* If azimuth >= 30 degrees, clamp to 90 degrees. */
            pos[0] = std::copysign(len2d, pos[0]);
            pos[2] = 0.0f;
        }
    }
    return pos;
}

/* Scales the azimuth of the given vector by 1.5 (3/2) if it's in front. */
[[nodiscard]]
auto ScaleAzimuthFront3_2(std::array<f32, 3> pos) -> std::array<f32, 3>
{
    if(pos[2] < 0.0f)
    {
        const auto len2d = std::sqrt(pos[0]*pos[0] + pos[2]*pos[2]);

        /* Z > cos(pi/3) = -60 < azimuth < 60 degrees. */
        if(auto z = -pos[2] / len2d; z > 0.5f)
        {
            auto x = pos[0] / len2d;

            /* Halve the angle represented by x,z. */
            x = std::copysign(std::sqrt((1.0f - z) * 0.5f), x);
            z = std::sqrt((1.0f + z) * 0.5f);

            /* Triple the angle represented by x,z. */
            x = x*3.0f - x*x*x*4.0f;
            z = z*z*z*4.0f - z*3.0f;

            /* Scale the vector back to fit in 3D. */
            pos[0] = x * len2d;
            pos[2] = -z * len2d;
        }
        else
        {
            /* If azimuth >= 60 degrees, clamp to 90 degrees. */
            pos[0] = std::copysign(len2d, pos[0]);
            pos[2] = 0.0f;
        }
    }
    return pos;
}


/* Begin ambisonic rotation helpers.
 *
 * Rotating first-order B-Format just needs a straight-forward X/Y/Z rotation
 * matrix. Higher orders, however, are more complicated. The method implemented
 * here is a recursive algorithm (the rotation for first-order is used to help
 * generate the second-order rotation, which helps generate the third-order
 * rotation, etc.).
 *
 * Adapted from
 * <https://github.com/polarch/Spherical-Harmonic-Transform/blob/master/getSHrotMtx.m>,
 * provided under the BSD 3-Clause license.
 *
 * Copyright (c) 2015, Archontis Politis
 * Copyright (c) 2019, Christopher Robinson
 *
 * The u, v, and w coefficients used for generating higher-order rotations are
 * precomputed since they're constant. The second-order coefficients are
 * followed by the third-order coefficients, etc.
 */
[[nodiscard]]
constexpr auto CalcRotatorSize(usize const l) noexcept -> usize
{
    if(l >= 2)
        return (l*2 + 1)*(l*2 + 1) + CalcRotatorSize(l-1);
    return 0;
}

struct RotatorCoeffs {
    struct CoeffValues {
        f32 u, v, w;
    };
    std::array<CoeffValues, CalcRotatorSize(MaxAmbiOrder)> mCoeffs{};

    RotatorCoeffs() noexcept
    {
        auto coeffs = mCoeffs.begin();

        for(auto const l : std::views::iota(2, int{MaxAmbiOrder+1}))
        {
            for(auto const n : std::views::iota(-l, l+1))
            {
                for(auto const m : std::views::iota(-l, l+1))
                {
                    /* compute u,v,w terms of Eq.8.1 (Table I)
                     *
                     * const bool d{m == 0}; // the delta function d_m0
                     * const double denom{(std::abs(n) == l) ?
                     *     (2*l) * (2*l - 1) : (l*l - n*n)};
                     *
                     * const int abs_m{std::abs(m)};
                     * coeffs->u = std::sqrt((l*l - m*m) / denom);
                     * coeffs->v = std::sqrt((l+abs_m-1) * (l+abs_m) / denom) *
                     *     (1.0+d) * (1.0 - 2.0*d) * 0.5;
                     * coeffs->w = std::sqrt((l-abs_m-1) * (l-abs_m) / denom) *
                     *     (1.0-d) * -0.5;
                     */

                    auto const denom = gsl::narrow_cast<f64>((std::abs(n) == l) ?
                          (2*l) * (2*l - 1) : (l*l - n*n));

                    if(m == 0)
                    {
                        coeffs->u = gsl::narrow_cast<f32>(std::sqrt(l * l / denom));
                        coeffs->v = gsl::narrow_cast<f32>(std::sqrt((l-1) * l / denom) * -1.0);
                        coeffs->w = 0.0f;
                    }
                    else
                    {
                        const auto abs_m = std::abs(m);
                        coeffs->u = gsl::narrow_cast<f32>(std::sqrt((l*l - m*m) / denom));
                        coeffs->v = gsl::narrow_cast<f32>(std::sqrt((l+abs_m-1) * (l+abs_m)
                            / denom) * 0.5);
                        coeffs->w = gsl::narrow_cast<f32>(std::sqrt((l-abs_m-1) * (l-abs_m)
                            / denom) * -0.5);
                    }
                    ++coeffs;
                }
            }
        }
    }
};
const auto RotatorCoeffArray = RotatorCoeffs{};

/**
 * Given the matrix, pre-filled with the (zeroth- and) first-order rotation
 * coefficients, this fills in the coefficients for the higher orders up to and
 * including the given order. The matrix is in ACN layout.
 */
void AmbiRotator(AmbiRotateMatrix &matrix, i32 const order)
{
    /* Don't do anything for < 2nd order. */
    if(order < 2) return;

    static constexpr auto P = [](isize const i, isize const l, isize const a, isize const n,
        usize const last_base, AmbiRotateMatrix const &R)
    {
        auto const ri1 =  R[ 1+2][gsl::narrow_cast<usize>(i+2_z)];
        auto const rim1 = R[-1+2][gsl::narrow_cast<usize>(i+2_z)];
        auto const ri0 =  R[ 0+2][gsl::narrow_cast<usize>(i+2_z)];

        auto const x = last_base + gsl::narrow_cast<usize>(a+l-1);
        if(n == -l)
            return ri1*R[last_base][x] + rim1*R[last_base + gsl::narrow_cast<usize>(l-1_z)*2][x];
        if(n == l)
            return ri1*R[last_base + gsl::narrow_cast<usize>(l-1_z)*2][x] - rim1*R[last_base][x];
        return ri0*R[last_base + gsl::narrow_cast<usize>(l-1_z+n)][x];
    };

    static constexpr auto U = [](isize const l, isize const m, isize const n,
        usize const last_base, AmbiRotateMatrix const &R)
    {
        return P(0, l, m, n, last_base, R);
    };
    static constexpr auto V = [](isize const l, isize const m, isize const n,
        usize const last_base, AmbiRotateMatrix const &R)
    {
        using namespace std::numbers;
        if(m > 0)
        {
            auto const d = (m == 1);
            auto const p0 = P( 1, l,  m-1, n, last_base, R);
            auto const p1 = P(-1, l, -m+1, n, last_base, R);
            return d ? p0*sqrt2_v<f32> : (p0 - p1);
        }
        auto const d = (m == -1);
        auto const p0 = P( 1, l,  m+1, n, last_base, R);
        auto const p1 = P(-1, l, -m-1, n, last_base, R);
        return d ? p1*sqrt2_v<f32> : (p0 + p1);
    };
    static constexpr auto W = [](isize const l, isize const m, isize const n,
        usize const last_base, AmbiRotateMatrix const &R)
    {
        Expects(m != 0);
        if(m > 0)
        {
            auto const p0 = P( 1, l,  m+1, n, last_base, R);
            auto const p1 = P(-1, l, -m-1, n, last_base, R);
            return p0 + p1;
        }
        auto const p0 = P( 1, l,  m-1, n, last_base, R);
        auto const p1 = P(-1, l, -m+1, n, last_base, R);
        return p0 - p1;
    };

    // compute rotation matrix of each subsequent band recursively
    auto coeffs = RotatorCoeffArray.mCoeffs.cbegin();
    auto base_idx = 4_uz;
    auto last_base = 1_uz;
    for(auto const l : std::views::iota(2_i32, order+1))
    {
        auto y = base_idx;
        for(auto const n : std::views::iota(-l, l+1))
        {
            auto x = base_idx;
            for(const auto m : std::views::iota(-l, l+1))
            {
                auto r = 0.0f;

                // computes Eq.8.1
                if(const auto u = coeffs->u; u != 0.0f)
                    r += u * U(l, m, n, last_base, matrix);
                if(const auto v = coeffs->v; v != 0.0f)
                    r += v * V(l, m, n, last_base, matrix);
                if(const auto w = coeffs->w; w != 0.0f)
                    r += w * W(l, m, n, last_base, matrix);

                matrix[y][x] = r;
                ++coeffs;
                ++x;
            }
            ++y;
        }
        last_base = base_idx;
        base_idx += gsl::narrow_cast<usize>(l)*2_uz + 1;
    }
}
/* End ambisonic rotation helpers. */


constexpr auto sin30 = 0.5f;
constexpr auto cos30 = 0.866025403785f;
constexpr auto sin45 = std::numbers::sqrt2_v<f32>*0.5f;
constexpr auto cos45 = std::numbers::sqrt2_v<f32>*0.5f;
constexpr auto sin110 =  0.939692620786f;
constexpr auto cos110 = -0.342020143326f;

struct ChanPosMap {
    Channel channel;
    std::array<f32, 3> pos;
};

struct GainTriplet { f32 Base, HF, LF; };


/**
 * Calculates panning gains for a voice playing an ambisonic buffer (B-Format,
 * UHJ, etc.).
 */
void CalcAmbisonicPanning(Voice *const voice, f32 const xpos, f32 const ypos, f32 const zpos,
    f32 const distance, f32 const spread, GainTriplet const &drygain,
    std::span<const GainTriplet, MaxSendCount> const wetgain,
    std::span<EffectSlotBase*const, MaxSendCount> const sendslots, ContextParams const &ctxparams,
    DeviceBase *const device)
{
    auto const samplerate = gsl::narrow_cast<f32>(device->mSampleRate);

    if(device->AvgSpeakerDist > 0.0f && voice->mFmtChannels != FmtUHJ2
        && voice->mFmtChannels != FmtSuperStereo)
    {
        if(!(distance > std::numeric_limits<f32>::epsilon()))
        {
            /* NOTE: The NFCtrlFilters should use a w0 of 0 for FOA input. */
            for(auto &chanparams : voice->mChans)
                chanparams.mDryParams.NFCtrlFilter.adjust(0.0f);
        }
        else
        {
            /* Clamp the distance for really close sources, to prevent
             * excessive bass.
             */
            auto const mdist = std::max(distance*NfcScale, device->AvgSpeakerDist/4.0f);
            auto const w0 = SpeedOfSoundMetersPerSec / (mdist * samplerate);

            /* Only need to adjust the first channel of a B-Format source. */
            voice->mChans[0].mDryParams.NFCtrlFilter.adjust(w0);
        }

        voice->mFlags.set(VoiceHasNfc);
    }

    /* Panning a B-Format sound toward some direction is easy. Just pan the
     * first (W) channel as a normal mono sound. The angular spread is used as
     * a directional scalar to blend between full coverage and full panning.
     */
    auto const coverage = !(distance > std::numeric_limits<f32>::epsilon()) ? 1.0f
        : (std::numbers::inv_pi_v<f32>*0.5f * spread);

    auto const scales = GetAmbiScales(voice->mAmbiScaling);
    auto coeffs = std::invoke([xpos,ypos,zpos,device]
    {
        if(device->mRenderMode != RenderMode::Pairwise)
            return CalcDirectionCoeffs(std::array{xpos, ypos, zpos}, 0.0f);
        const auto pos = ScaleAzimuthFront3_2(std::array{xpos, ypos, zpos});
        return CalcDirectionCoeffs(pos, 0.0f);
    });

    if(!(coverage > 0.0f))
    {
        ComputePanGains(&device->Dry, coeffs, drygain.Base*scales[0],
            std::span{voice->mChans[0].mDryParams.Gains.Target}.first<MaxAmbiChannels>());
        for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
        {
            if(auto const *const slot = sendslots[i])
                ComputePanGains(&slot->Wet, coeffs, wetgain[i].Base*scales[0],
                    voice->mChans[0].mWetParams[i].Gains.Target);
        }
        return;
    }

    auto const &props = voice->mProps;
    /* Local B-Format sources have their XYZ channels rotated according to the
     * orientation.
     */
    auto N = al::Vector{props.OrientAt[0], props.OrientAt[1], props.OrientAt[2], 0.0f};
    N.normalize();
    auto V = al::Vector{props.OrientUp[0], props.OrientUp[1], props.OrientUp[2], 0.0f};
    V.normalize();
    if(!props.HeadRelative)
    {
        N = ctxparams.Matrix * N;
        V = ctxparams.Matrix * V;
    }
    /* Build and normalize right-vector */
    auto U = al::Vector{N.cross_product(V)};
    U.normalize();

    /* Build a rotation matrix. Manually fill the zeroth- and first-order
     * elements, then construct the rotation for the higher orders.
     */
    auto &shrot = device->mAmbiRotateMatrix;
    std::ranges::fill(shrot | std::views::join, 0.0f);

    shrot[0][0] = 1.0f;
    shrot[1][1] =  U[0]; shrot[1][2] = -U[1]; shrot[1][3] =  U[2];
    shrot[2][1] = -V[0]; shrot[2][2] =  V[1]; shrot[2][3] = -V[2];
    shrot[3][1] = -N[0]; shrot[3][2] =  N[1]; shrot[3][3] = -N[2];
    AmbiRotator(shrot, gsl::narrow_cast<i32>(device->mAmbiOrder));

    /* If the device is higher order than the voice, "upsample" the matrix.
     *
     * NOTE: Starting with second-order, a 2D upsample needs to be applied with
     * a 2D format and 3D output, even when they're the same order. This is
     * because higher orders have a height offset on various channels (i.e.
     * when elevation=0, those height-related channels should be non-0).
     */
    auto &mixmatrix = device->mAmbiRotateMatrix2;
    if(device->mAmbiOrder > voice->mAmbiOrder || (device->mAmbiOrder >= 2 && !device->m2DMixing
            && Is2DAmbisonic(voice->mFmtChannels)))
    {
        if(voice->mAmbiOrder == 1)
        {
            auto const upsampler = Is2DAmbisonic(voice->mFmtChannels)
                ? std::span{AmbiScale::FirstOrder2DUp} : std::span{AmbiScale::FirstOrderUp};
            UpsampleBFormatTransform(mixmatrix, upsampler, shrot, device->mAmbiOrder);
        }
        else if(voice->mAmbiOrder == 2)
        {
            auto const upsampler = Is2DAmbisonic(voice->mFmtChannels)
                ? std::span{AmbiScale::SecondOrder2DUp} : std::span{AmbiScale::SecondOrderUp};
            UpsampleBFormatTransform(mixmatrix, upsampler, shrot, device->mAmbiOrder);
        }
        else if(voice->mAmbiOrder == 3)
        {
            auto const upsampler = Is2DAmbisonic(voice->mFmtChannels)
                ? std::span{AmbiScale::ThirdOrder2DUp} : std::span{AmbiScale::ThirdOrderUp};
            UpsampleBFormatTransform(mixmatrix, upsampler, shrot, device->mAmbiOrder);
        }
        else if(voice->mAmbiOrder == 4)
        {
            auto const upsampler = std::span{AmbiScale::FourthOrder2DUp};
            UpsampleBFormatTransform(mixmatrix, upsampler, shrot, device->mAmbiOrder);
        }
    }
    else
        mixmatrix = shrot;

    /* Convert the rotation matrix for input ordering and scaling, and whether
     * input is 2D or 3D.
     */
    auto const index_map = Is2DAmbisonic(voice->mFmtChannels)
        ? GetAmbi2DLayout(voice->mAmbiLayout).first(voice->mChans.size())
        : GetAmbiLayout(voice->mAmbiLayout).first(voice->mChans.size());

    /* Scale the panned W signal inversely to coverage (full coverage means no
     * panned signal), and according to the channel scaling.
     */
    std::ranges::transform(coeffs, coeffs.begin(),
        [scale=(1.0f-coverage)*scales[0]](f32 const coeff) { return coeff * scale; });

    for(const auto c : std::views::iota(0_uz, index_map.size()))
    {
        auto const acn = usize{index_map[c].c_val};
        auto const scale = scales[acn] * coverage;

        /* For channel 0, combine the B-Format signal (scaled according to the
         * coverage amount) with the directional pan. For all other channels,
         * use just the (scaled) B-Format signal.
         */
        std::ranges::transform(mixmatrix[acn], coeffs, coeffs.begin(),
            [scale](f32 const in, f32 const coeff) noexcept { return in*scale + coeff; });

        ComputePanGains(&device->Dry, coeffs, drygain.Base,
            std::span{voice->mChans[c].mDryParams.Gains.Target}.first<MaxAmbiChannels>());

        for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
        {
            if(auto const *const slot = sendslots[i])
                ComputePanGains(&slot->Wet, coeffs, wetgain[i].Base,
                    voice->mChans[c].mWetParams[i].Gains.Target);
        }

        coeffs.fill(0.0f);
    }
}

[[nodiscard]]
auto GetPanGainSelector(VoiceProps const &props)
{
    auto const lgain = std::min(1.0f - props.Panning, 1.0f);
    auto const rgain = std::min(1.0f + props.Panning, 1.0f);
    auto const mingain = std::min(lgain, rgain);
    return [lgain,rgain,mingain](Channel const chan) noexcept -> f32
    {
        switch(chan)
        {
        case FrontLeft: return lgain;
        case FrontRight: return rgain;
        case FrontCenter: break;
        case LFE: break;
        case BackLeft: return lgain;
        case BackRight: return rgain;
        case BackCenter: break;
        case SideLeft: return lgain;
        case SideRight: return rgain;
        case TopCenter: break;
        case TopFrontLeft: return lgain;
        case TopFrontCenter: break;
        case TopFrontRight: return rgain;
        case TopBackLeft: return lgain;
        case TopBackCenter: break;
        case TopBackRight: return rgain;
        case BottomFrontLeft: return lgain;
        case BottomFrontRight: return rgain;
        case BottomBackLeft: return lgain;
        case BottomBackRight: return rgain;
        case Aux0: case Aux1: case Aux2: case Aux3: case Aux4: case Aux5: case Aux6: case Aux7:
        case Aux8: case Aux9: case Aux10: case Aux11: case Aux12: case Aux13: case Aux14:
        case Aux15: case MaxChannels: break;
        }
        return mingain;
    };
}

/* With non-HRTF mixing, we can cheat for mono-as-stereo by adding the left and
 * right output gains and mix only one channel to output.
 */
void MergePannedMono(Voice *const voice,
    std::span<EffectSlotBase*const, MaxSendCount> const sendslots, DeviceBase *const device)
{
    auto const drytarget0 = std::span{voice->mChans[0].mDryParams.Gains.Target};
    auto const drytarget1 = std::span{voice->mChans[1].mDryParams.Gains.Target};
    std::ranges::transform(drytarget0, drytarget1, drytarget0.begin(), std::plus{});

    for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
    {
        if(!sendslots[i])
            continue;

        auto const wettarget0 = std::span{voice->mChans[0].mWetParams[i].Gains.Target};
        auto const wettarget1 = std::span{voice->mChans[1].mWetParams[i].Gains.Target};
        std::ranges::transform(wettarget0, wettarget1, wettarget0.begin(), std::plus{});
    }
}

/**
 * Calculates panning gains for a voice playing directly to the main output
 * buffer (bypassing the B-Format dry buffer).
 */
void CalcDirectPanning(Voice *const voice, DirectMode const directmode,
    std::span<ChanPosMap const> const chans, GainTriplet const &drygain,
    std::span<GainTriplet const, MaxSendCount> const wetgain,
    std::span<EffectSlotBase*const, MaxSendCount> const sendslots, DeviceBase *const device)
{
    auto const &props = voice->mProps;
    auto ChannelPanGain = GetPanGainSelector(props);

    for(auto const c : std::views::iota(0_uz, chans.size()))
    {
        auto const pangain = ChannelPanGain(chans[c].channel);
        if(auto idx = device->RealOut.ChannelIndex[chans[c].channel]; idx != InvalidChannelIndex)
            voice->mChans[c].mDryParams.Gains.Target[idx.c_val] = drygain.Base * pangain;
        else if(directmode == DirectMode::RemixMismatch)
        {
            auto const remap = std::ranges::find(device->RealOut.RemixMap, chans[c].channel,
                &InputRemixMap::channel);
            if(remap == device->RealOut.RemixMap.end())
                continue;

            for(auto const &target : remap->targets)
            {
                idx = device->RealOut.ChannelIndex[target.channel];
                if(idx != InvalidChannelIndex)
                    voice->mChans[c].mDryParams.Gains.Target[idx.c_val] = drygain.Base * pangain
                        * target.mix;
            }
        }
    }

    /* Auxiliary sends still use normal channel panning since they mix to
     * B-Format, which can't channel-match.
     */
    for(auto const c : std::views::iota(0_uz, chans.size()))
    {
        /* Skip LFE */
        if(chans[c].channel == LFE)
            continue;

        auto const pangain = ChannelPanGain(chans[c].channel);
        auto const coeffs = CalcDirectionCoeffs(chans[c].pos, 0.0f);

        for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
        {
            if(auto const *const slot = sendslots[i])
                ComputePanGains(&slot->Wet, coeffs, wetgain[i].Base * pangain,
                    voice->mChans[c].mWetParams[i].Gains.Target);
        }
    }

    if(voice->mFmtChannels == FmtMono && props.mPanningEnabled)
        MergePannedMono(voice, sendslots, device);
}

/** Calculates panning filters for a voice mixing with HRTF. */
void CalcHrtfPanning(Voice *const voice, f32 const xpos, f32 const ypos, f32 const zpos,
    f32 const distance, f32 const spread, std::span<ChanPosMap const> const chans,
    GainTriplet const &drygain, std::span<GainTriplet const, MaxSendCount> const wetgain,
    std::span<EffectSlotBase*const, MaxSendCount> const sendslots, DeviceBase *const device)
{
    auto const &props = voice->mProps;
    auto ChannelPanGain = GetPanGainSelector(props);

    if(distance > std::numeric_limits<f32>::epsilon())
    {
        if(voice->mFmtChannels == FmtMono && !props.mPanningEnabled)
        {
            auto const src_ev = std::asin(std::clamp(ypos, -1.0f, 1.0f));
            auto const src_az = std::atan2(xpos, -zpos);

            device->mHrtf->getCoeffs(src_ev, src_az, distance*NfcScale, spread,
                voice->mChans[0].mDryParams.Hrtf.Target.Coeffs,
                voice->mChans[0].mDryParams.Hrtf.Target.Delay);
            voice->mChans[0].mDryParams.Hrtf.Target.Gain = drygain.Base;

            auto const coeffs = CalcDirectionCoeffs(std::array{xpos, ypos, zpos}, spread);
            for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
            {
                if(auto const *const slot = sendslots[i])
                    ComputePanGains(&slot->Wet, coeffs, wetgain[i].Base,
                        voice->mChans[0].mWetParams[i].Gains.Target);
            }
            return;
        }

        for(auto const c : std::views::iota(0_uz, chans.size()))
        {
            /* Skip LFE */
            if(chans[c].channel == LFE) continue;
            auto const pangain = ChannelPanGain(chans[c].channel);

            /* Warp the channel position toward the source position as the
             * source spread decreases. With no spread, all channels are at
             * the source position, at full spread (pi*2), each channel is
             * left unchanged.
             */
            auto const a = 1.0f - (std::numbers::inv_pi_v<f32>*0.5f)*spread;
            auto pos = std::array{
                lerpf(chans[c].pos[0], xpos, a),
                lerpf(chans[c].pos[1], ypos, a),
                lerpf(chans[c].pos[2], zpos, a)};
            if(auto const len = std::sqrt(pos[0]*pos[0] + pos[1]*pos[1] + pos[2]*pos[2]);
                len < 1.0f)
            {
                pos[0] /= len;
                pos[1] /= len;
                pos[2] /= len;
            }

            auto const ev = std::asin(std::clamp(pos[1], -1.0f, 1.0f));
            auto const az = std::atan2(pos[0], -pos[2]);

            device->mHrtf->getCoeffs(ev, az, distance*NfcScale, 0.0f,
                voice->mChans[c].mDryParams.Hrtf.Target.Coeffs,
                voice->mChans[c].mDryParams.Hrtf.Target.Delay);
            voice->mChans[c].mDryParams.Hrtf.Target.Gain = drygain.Base * pangain;

            auto const coeffs = CalcDirectionCoeffs(pos, 0.0f);
            for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
            {
                if(auto const *slot = sendslots[i])
                    ComputePanGains(&slot->Wet, coeffs, wetgain[i].Base * pangain,
                        voice->mChans[c].mWetParams[i].Gains.Target);
            }
        }
        return;
    }

    /* With no distance, spread is only meaningful for 3D mono sources where it
     * can be 0 or 1 (non-mono sources are always treated as full spread here).
     */
    auto const spreadmult = gsl::narrow_cast<f32>(voice->mFmtChannels == FmtMono
        && !props.mPanningEnabled) * spread;

    /* Local sources on HRTF play with each channel panned to its relative
     * location around the listener, providing "virtual speaker" responses.
     */
    for(auto const c : std::views::iota(0_uz, chans.size()))
    {
        /* Skip LFE */
        if(chans[c].channel == LFE)
            continue;
        auto const pangain = ChannelPanGain(chans[c].channel);

        /* Get the HRIR coefficients and delays for this channel position. */
        auto const ev = std::asin(chans[c].pos[1]);
        auto const az = std::atan2(chans[c].pos[0], -chans[c].pos[2]);

        /* With no distance, spread is only meaningful for mono sources where
         * it can be 0 or 1 (non-mono sources are always treated as full spread
         * here).
         */
        device->mHrtf->getCoeffs(ev, az, std::numeric_limits<f32>::infinity(), spreadmult,
            voice->mChans[c].mDryParams.Hrtf.Target.Coeffs,
            voice->mChans[c].mDryParams.Hrtf.Target.Delay);
        voice->mChans[c].mDryParams.Hrtf.Target.Gain = drygain.Base * pangain;

        /* Normal panning for auxiliary sends. */
        auto const coeffs = CalcDirectionCoeffs(chans[c].pos, spread);

        for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
        {
            if(auto const *const slot = sendslots[i])
                ComputePanGains(&slot->Wet, coeffs, wetgain[i].Base * pangain,
                    voice->mChans[c].mWetParams[i].Gains.Target);
        }
    }
}

/** Calculates panning gains for a voice playing normally. */
void CalcNormalPanning(Voice *const voice, f32 const xpos, f32 const ypos, f32 const zpos,
    f32 const distance, f32 const spread, std::span<ChanPosMap const> const chans,
    GainTriplet const &drygain, std::span<GainTriplet const, MaxSendCount> const wetgain,
    std::span<EffectSlotBase*const, MaxSendCount> const sendslots, DeviceBase *const device)
{
    auto const &props = voice->mProps;
    auto ChannelPanGain = GetPanGainSelector(props);

    auto const samplerate = gsl::narrow_cast<f32>(device->mSampleRate);

    if(distance > std::numeric_limits<f32>::epsilon())
    {
        /* Calculate NFC filter coefficient if needed. */
        if(device->AvgSpeakerDist > 0.0f)
        {
            /* Clamp the distance for really close sources, to prevent
             * excessive bass.
             */
            auto const mdist = std::max(distance*NfcScale, device->AvgSpeakerDist/4.0f);
            auto const w0 = SpeedOfSoundMetersPerSec / (mdist * samplerate);

            /* Adjust NFC filters. */
            for(auto &chanparams : voice->mChans | std::views::take(chans.size()))
                chanparams.mDryParams.NFCtrlFilter.adjust(w0);

            voice->mFlags.set(VoiceHasNfc);
        }

        if(voice->mFmtChannels == FmtMono && !props.mPanningEnabled)
        {
            auto const coeffs = std::invoke([xpos,ypos,zpos,spread,device]
            {
                if(device->mRenderMode != RenderMode::Pairwise)
                    return CalcDirectionCoeffs(std::array{xpos, ypos, zpos}, spread);
                auto const pos = ScaleAzimuthFront3_2(std::array{xpos, ypos, zpos});
                return CalcDirectionCoeffs(pos, spread);
            });

            ComputePanGains(&device->Dry, coeffs, drygain.Base,
                std::span{voice->mChans[0].mDryParams.Gains.Target}.first<MaxAmbiChannels>());
            for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
            {
                if(auto const *const slot = sendslots[i])
                    ComputePanGains(&slot->Wet, coeffs, wetgain[i].Base,
                        voice->mChans[0].mWetParams[i].Gains.Target);
            }

            return;
        }

        for(auto const c : std::views::iota(0_uz, chans.size()))
        {
            auto const pangain = ChannelPanGain(chans[c].channel);

            /* Special-case LFE */
            if(chans[c].channel == LFE)
            {
                if(device->Dry.Buffer.data() == device->RealOut.Buffer.data())
                {
                    if(auto const idx = device->RealOut.ChannelIndex[chans[c].channel];
                        idx != InvalidChannelIndex)
                        voice->mChans[c].mDryParams.Gains.Target[idx.c_val] = drygain.Base*pangain;
                }
                continue;
            }

            /* Warp the channel position toward the source position as the
             * spread decreases. With no spread, all channels are at the source
             * position, at full spread (pi*2), each channel position is left
             * unchanged.
             */
            auto const a = 1.0f - (std::numbers::inv_pi_v<f32>*0.5f)*spread;
            auto pos = std::array{
                lerpf(chans[c].pos[0], xpos, a),
                lerpf(chans[c].pos[1], ypos, a),
                lerpf(chans[c].pos[2], zpos, a)};
            if(auto const len = std::sqrt(pos[0]*pos[0] + pos[1]*pos[1] + pos[2]*pos[2]);
                len < 1.0f)
            {
                pos[0] /= len;
                pos[1] /= len;
                pos[2] /= len;
            }

            if(device->mRenderMode == RenderMode::Pairwise)
                pos = ScaleAzimuthFront3(pos);
            auto const coeffs = CalcDirectionCoeffs(pos, 0.0f);

            ComputePanGains(&device->Dry, coeffs, drygain.Base * pangain,
                std::span{voice->mChans[c].mDryParams.Gains.Target}.first<MaxAmbiChannels>());
            for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
            {
                if(auto const *const slot = sendslots[i])
                    ComputePanGains(&slot->Wet, coeffs, wetgain[i].Base * pangain,
                        voice->mChans[c].mWetParams[i].Gains.Target);
            }
        }
    }
    else
    {
        if(device->AvgSpeakerDist > 0.0f)
        {
            /* If the source distance is 0, use an "identity" filter so it
             * aligns to the average speaker distance. This avoids excessive
             * high-pass effects on a sound that is at nominal volume, though
             * it does mean it will simulate the sound being at that distance
             * with ambisonic output when decoded with near-field compensation.
             */
            auto const w0 = SpeedOfSoundMetersPerSec / (device->AvgSpeakerDist * samplerate);
            for(auto &chanparams : voice->mChans | std::views::take(chans.size()))
                chanparams.mDryParams.NFCtrlFilter.adjust(w0);

            voice->mFlags.set(VoiceHasNfc);
        }

        /* With no distance, spread is only meaningful for 3D mono sources
         * where it can be 0 or full (non-mono sources are always full spread
         * here).
         */
        auto const spreadmult = gsl::narrow_cast<f32>(voice->mFmtChannels == FmtMono
            && !props.mPanningEnabled) * spread;

        for(auto const c : std::views::iota(0_uz, chans.size()))
        {
            auto const pangain = ChannelPanGain(chans[c].channel);

            /* Special-case LFE */
            if(chans[c].channel == LFE)
            {
                if(device->Dry.Buffer.data() == device->RealOut.Buffer.data())
                {
                    if(auto const idx = device->RealOut.ChannelIndex[chans[c].channel];
                        idx != InvalidChannelIndex)
                        voice->mChans[c].mDryParams.Gains.Target[idx.c_val] = drygain.Base*pangain;
                }
                continue;
            }

            auto const coeffs = CalcDirectionCoeffs((device->mRenderMode==RenderMode::Pairwise)
                ? ScaleAzimuthFront3(chans[c].pos) : chans[c].pos, spreadmult);

            ComputePanGains(&device->Dry, coeffs, drygain.Base * pangain,
                std::span{voice->mChans[c].mDryParams.Gains.Target}.first<MaxAmbiChannels>());
            for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
            {
                if(auto const *const slot = sendslots[i])
                    ComputePanGains(&slot->Wet, coeffs, wetgain[i].Base * pangain,
                        voice->mChans[c].mWetParams[i].Gains.Target);
            }
        }
    }

    if(voice->mFmtChannels == FmtMono && props.mPanningEnabled)
        MergePannedMono(voice, sendslots, device);
}

void CalcPanningAndFilters(Voice *const voice, f32 const xpos, f32 const ypos, f32 const zpos,
    f32 const distance, f32 const spread, GainTriplet const &drygain,
    std::span<GainTriplet const, MaxSendCount> const wetgain,
    std::span<EffectSlotBase*const, MaxSendCount> const sendslots, ContextParams const &ctxparams,
    DeviceBase *const device)
{
    static constexpr auto MonoMap = std::array{
        ChanPosMap{FrontCenter, std::array{0.0f, 0.0f, -1.0f}}
    };
    static constexpr auto RearMap = std::array{
        ChanPosMap{BackLeft,  std::array{-sin30, 0.0f, cos30}},
        ChanPosMap{BackRight, std::array{ sin30, 0.0f, cos30}},
    };
    static constexpr auto QuadMap = std::array{
        ChanPosMap{FrontLeft,  std::array{-sin45, 0.0f, -cos45}},
        ChanPosMap{FrontRight, std::array{ sin45, 0.0f, -cos45}},
        ChanPosMap{BackLeft,   std::array{-sin45, 0.0f,  cos45}},
        ChanPosMap{BackRight,  std::array{ sin45, 0.0f,  cos45}},
    };
    static constexpr auto X51Map = std::array{
        ChanPosMap{FrontLeft,   std::array{-sin30, 0.0f, -cos30}},
        ChanPosMap{FrontRight,  std::array{ sin30, 0.0f, -cos30}},
        ChanPosMap{FrontCenter, std::array{  0.0f, 0.0f, -1.0f}},
        ChanPosMap{LFE, {}},
        ChanPosMap{SideLeft,    std::array{-sin110, 0.0f, -cos110}},
        ChanPosMap{SideRight,   std::array{ sin110, 0.0f, -cos110}},
    };
    static constexpr auto X61Map = std::array{
        ChanPosMap{FrontLeft,   std::array{-sin30, 0.0f, -cos30}},
        ChanPosMap{FrontRight,  std::array{ sin30, 0.0f, -cos30}},
        ChanPosMap{FrontCenter, std::array{  0.0f, 0.0f, -1.0f}},
        ChanPosMap{LFE, {}},
        ChanPosMap{BackCenter,  std::array{ 0.0f, 0.0f, 1.0f}},
        ChanPosMap{SideLeft,    std::array{-1.0f, 0.0f, 0.0f}},
        ChanPosMap{SideRight,   std::array{ 1.0f, 0.0f, 0.0f}},
    };
    static constexpr auto X71Map = std::array{
        ChanPosMap{FrontLeft,   std::array{-sin30, 0.0f, -cos30}},
        ChanPosMap{FrontRight,  std::array{ sin30, 0.0f, -cos30}},
        ChanPosMap{FrontCenter, std::array{  0.0f, 0.0f, -1.0f}},
        ChanPosMap{LFE, {}},
        ChanPosMap{BackLeft,    std::array{-sin30, 0.0f, cos30}},
        ChanPosMap{BackRight,   std::array{ sin30, 0.0f, cos30}},
        ChanPosMap{SideLeft,    std::array{ -1.0f, 0.0f, 0.0f}},
        ChanPosMap{SideRight,   std::array{  1.0f, 0.0f, 0.0f}},
    };

    auto StereoMap = std::array{
        ChanPosMap{FrontLeft,   std::array{-sin30, 0.0f, -cos30}},
        ChanPosMap{FrontRight,  std::array{ sin30, 0.0f, -cos30}},
    };

    auto const numsends = device->NumAuxSends;

    auto const &props = voice->mProps;

    std::ranges::for_each(voice->mChans, [numsends](Voice::ChannelData &chandata)
    {
        chandata.mDryParams.Hrtf.Target = HrtfFilter{};
        chandata.mDryParams.Gains.Target.fill(0.0f);
        std::ranges::for_each(chandata.mWetParams | std::views::take(numsends),
            [](SendParams &params) -> void { params.Gains.Target.fill(0.0f); });
    });

    auto const [directmode, chans] = std::invoke([voice,&props,&StereoMap]()
        -> std::pair<DirectMode,std::span<ChanPosMap const>>
    {
        switch(voice->mFmtChannels)
        {
        case FmtMono:
            if(!props.mPanningEnabled)
            {
                /* 3D mono buffers are never played direct. */
                return {DirectMode::Off, std::span{MonoMap}};
            }
            /* Mono buffers with panning enabled are basically treated as
             * stereo, each channel being a copy of the buffer samples, using
             * the stereo channel positions and the left/right panning
             * affecting each channel appropriately.
             */
            [[fallthrough]];
        case FmtStereo:
            if(props.DirectChannels == DirectMode::Off)
            {
                auto chanpos = StereoMap | std::views::transform(&ChanPosMap::pos);
                std::ranges::transform(props.StereoPan, chanpos | std::views::elements<1>,
                    chanpos.begin(), [](f32 const a, f32 const y)
                {
                    /* StereoPan is counter-clockwise in radians. */
                    return std::array{-std::sin(a), y, -std::cos(a)};
                });
            }
            return {props.DirectChannels, std::span{StereoMap}};

        case FmtRear: return {props.DirectChannels, std::span{RearMap}};
        case FmtQuad: return {props.DirectChannels, std::span{QuadMap}};
        case FmtX51: return {props.DirectChannels, std::span{X51Map}};
        case FmtX61: return {props.DirectChannels, std::span{X61Map}};
        case FmtX71: return {props.DirectChannels, std::span{X71Map}};

        case FmtBFormat2D:
        case FmtBFormat3D:
        case FmtUHJ2:
        case FmtUHJ3:
        case FmtUHJ4:
        case FmtSuperStereo:
            return {DirectMode::Off, {}};
        }
        return {props.DirectChannels, {}};
    });

    voice->mFlags.reset(VoiceHasHrtf).reset(VoiceHasNfc);
    if(auto *const decoder = voice->mDecoder.get())
        decoder->mWidthControl = std::min(props.EnhWidth, 0.7f);

    if(IsAmbisonic(voice->mFmtChannels))
    {
        /* Special handling for B-Format and UHJ sources. */
        CalcAmbisonicPanning(voice, xpos, ypos, zpos, distance, spread, drygain, wetgain,
            sendslots, ctxparams, device);
    }
    else if(directmode != DirectMode::Off && !device->RealOut.RemixMap.empty())
    {
        /* Direct source channels always play local. Skip the virtual channels
         * and write inputs to the matching real outputs.
         */
        voice->mDirect.Buffer = device->RealOut.Buffer;
        CalcDirectPanning(voice, directmode, chans, drygain, wetgain, sendslots, device);
    }
    else if(device->mRenderMode == RenderMode::Hrtf)
    {
        /* Full HRTF rendering. Skip the virtual channels and render to the
         * real outputs with HRTF filters.
         */
        voice->mDirect.Buffer = device->RealOut.Buffer;
        CalcHrtfPanning(voice, xpos, ypos, zpos, distance, spread, chans, drygain, wetgain,
            sendslots, device);

        voice->mDuplicateMono = voice->mFmtChannels == FmtMono && props.mPanningEnabled;
        voice->mFlags.set(VoiceHasHrtf);
    }
    else
    {
        /* Non-HRTF rendering. Use normal panning to the normal output. */
        CalcNormalPanning(voice, xpos, ypos, zpos, distance, spread, chans, drygain, wetgain,
            sendslots, device);
    }

    const auto inv_samplerate = 1.0f / gsl::narrow_cast<f32>(device->mSampleRate);
    {
        auto const hfNorm = props.Direct.HFReference * inv_samplerate;
        auto const lfNorm = props.Direct.LFReference * inv_samplerate;

        voice->mDirect.FilterActive = false;
        if(drygain.HF != 1.0f || drygain.LF != 1.0f)
            voice->mDirect.FilterActive = true;

        auto &lowpass = voice->mChans[0].mDryParams.LowPass;
        auto &highpass = voice->mChans[0].mDryParams.HighPass;
        lowpass.setParamsFromSlope(BiquadType::HighShelf, hfNorm, drygain.HF, 1.0f);
        highpass.setParamsFromSlope(BiquadType::LowShelf, lfNorm, drygain.LF, 1.0f);
        for(Voice::ChannelData &chandata : voice->mChans | std::views::drop(1))
        {
            chandata.mDryParams.LowPass.copyParamsFrom(lowpass);
            chandata.mDryParams.HighPass.copyParamsFrom(highpass);
        }
    }
    for(auto const i : std::views::iota(0_uz, numsends))
    {
        auto const hfNorm = props.Send[i].HFReference * inv_samplerate;
        auto const lfNorm = props.Send[i].LFReference * inv_samplerate;

        voice->mSend[i].FilterActive = false;
        if(wetgain[i].HF != 1.0f || wetgain[i].LF != 1.0f)
            voice->mSend[i].FilterActive = true;

        auto &lowpass = voice->mChans[0].mWetParams[i].LowPass;
        auto &highpass = voice->mChans[0].mWetParams[i].HighPass;
        lowpass.setParamsFromSlope(BiquadType::HighShelf, hfNorm, wetgain[i].HF, 1.0f);
        highpass.setParamsFromSlope(BiquadType::LowShelf, lfNorm, wetgain[i].LF, 1.0f);
        for(Voice::ChannelData &chandata : voice->mChans | std::views::drop(1))
        {
            chandata.mWetParams[i].LowPass.copyParamsFrom(lowpass);
            chandata.mWetParams[i].HighPass.copyParamsFrom(highpass);
        }
    }
}

void CalcNonAttnVoiceParams(Voice *const voice, ContextBase const *const context)
{
    auto const &props = voice->mProps;
    auto const device = al::get_not_null(context->mDevice);
    auto sendslots = std::array<EffectSlotBase*,MaxSendCount>{};

    voice->mDirect.Buffer = device->Dry.Buffer;
    for(auto const i : std::views::iota(0_uz, device->NumAuxSends))
    {
        sendslots[i] = props.Send[i].Slot;
        if(!sendslots[i] || sendslots[i]->EffectType == EffectSlotType::None)
        {
            sendslots[i] = nullptr;
            voice->mSend[i].Buffer = {};
        }
        else
            voice->mSend[i].Buffer = sendslots[i]->Wet.Buffer;
    }

    /* Calculate the stepping value */
    auto const pitch = gsl::narrow_cast<f32>(voice->mFrequency) /
        gsl::narrow_cast<f32>(device->mSampleRate) * props.Pitch;
    if(pitch > f32{MaxPitch})
        voice->mStep = MaxPitch<<MixerFracBits;
    else
        voice->mStep = std::max(fastf2u(pitch * MixerFracOne), 1u);
    voice->mResampler = PrepareResampler(props.mResampler, voice->mStep, &voice->mResampleState);

    /* Calculate gains */
    auto const mingain = std::min(props.MinGain, props.MaxGain);
    auto const srcgain = std::clamp(props.Gain, mingain, props.MaxGain);
    auto const drygain = GainTriplet{
        .Base = std::min(GainMixMax, srcgain * props.Direct.Gain * context->mParams.Gain),
        .HF = props.Direct.GainHF,
        .LF = props.Direct.GainLF
    };

    auto wetgain = std::array<GainTriplet,MaxSendCount>{};
    std::ranges::transform(props.Send | std::views::take(device->NumAuxSends), wetgain.begin(),
        [context,srcgain](const VoiceProps::SendData &send) noexcept
    {
        return GainTriplet{
            .Base = std::min(GainMixMax, srcgain * send.Gain * context->mParams.Gain),
            .HF = send.GainHF,
            .LF = send.GainLF
        };
    });

    CalcPanningAndFilters(voice, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, drygain, wetgain, sendslots,
        context->mParams, device);
}

void CalcAttnVoiceParams(Voice *const voice, ContextBase const *const context)
{
    auto const &props = voice->mProps;
    auto const device = al::get_not_null(context->mDevice);
    auto const numsends = device->NumAuxSends;

    /* Set mixing buffers and get send parameters. */
    voice->mDirect.Buffer = device->Dry.Buffer;

    auto sendslots = std::array<EffectSlotBase*,MaxSendCount>{};
    auto roomrolloff = std::array<f32, MaxSendCount>{};
    for(auto const i : std::views::iota(0_uz, numsends))
    {
        sendslots[i] = props.Send[i].Slot;
        if(!sendslots[i] || sendslots[i]->EffectType == EffectSlotType::None)
        {
            sendslots[i] = nullptr;
            voice->mSend[i].Buffer = {};
        }
        else
        {
            /* NOTE: Contrary to the EFX docs, the effect's room rolloff factor
             * applies to the selected distance model along with the source's
             * room rolloff factor, not necessarily the inverse distance model.
             */
            roomrolloff[i] = props.RoomRolloffFactor + sendslots[i]->RoomRolloff;

            voice->mSend[i].Buffer = sendslots[i]->Wet.Buffer;
        }
    }

    /* Transform source to listener space (convert to head relative) */
    auto position = al::Vector{props.Position[0], props.Position[1], props.Position[2], 1.0f};
    auto velocity = al::Vector{props.Velocity[0], props.Velocity[1], props.Velocity[2], 0.0f};
    auto direction = al::Vector{props.Direction[0], props.Direction[1], props.Direction[2], 0.0f};
    if(!props.HeadRelative)
    {
        /* Transform source vectors */
        position = context->mParams.Matrix * (position - context->mParams.Position);
        velocity = context->mParams.Matrix * velocity;
        direction = context->mParams.Matrix * direction;
    }
    else
    {
        /* Offset the source velocity to be relative of the listener velocity */
        velocity += context->mParams.Velocity;
    }

    auto tosource = al::Vector{position[0], position[1], position[2], 0.0f};
    auto const distance = tosource.normalize();
    auto const directional = bool{direction.normalize() > 0.0f};

    /* Calculate distance attenuation */
    auto const distancemodel = context->mParams.SourceDistanceModel ? props.mDistanceModel
        : context->mParams.mDistanceModel;

    auto const attenDistance = std::invoke([distance,distancemodel,&props]
    {
        switch(distancemodel)
        {
        case DistanceModel::InverseClamped:
        case DistanceModel::LinearClamped:
        case DistanceModel::ExponentClamped:
            if(!(props.RefDistance <= props.MaxDistance))
                return props.RefDistance;
            return std::clamp(distance, props.RefDistance, props.MaxDistance);

        case DistanceModel::Inverse:
        case DistanceModel::Linear:
        case DistanceModel::Exponent:
        case DistanceModel::Disable:
            break;
        }
        return distance;
    });


    auto drygain = GainTriplet{ .Base = props.Gain, .HF = 1.0f, .LF = 1.0f };
    auto wetgain = std::array<GainTriplet,MaxSendCount>{};
    wetgain.fill(drygain);

    auto dryAttnBase = 1.0f;
    switch(distancemodel)
    {
    case DistanceModel::Inverse:
    case DistanceModel::InverseClamped:
        if(props.RefDistance > 0.0f)
        {
            if(auto const dist = lerpf(props.RefDistance, attenDistance, props.RolloffFactor);
                dist > 0.0f)
            {
                dryAttnBase = props.RefDistance / dist;
                drygain.Base *= dryAttnBase;
            }

            auto const wetbase = wetgain | std::views::transform(&GainTriplet::Base);
            std::ranges::transform(wetbase | std::views::take(numsends), roomrolloff,
                wetbase.begin(), [&props,attenDistance](f32 const gain, f32 const rolloff)
            {
                if(auto const dist = lerpf(props.RefDistance, attenDistance, rolloff);
                    dist > 0.0f)
                    return gain * (props.RefDistance / dist);
                return gain;
            });
        }
        break;

    case DistanceModel::Linear:
    case DistanceModel::LinearClamped:
        if(props.MaxDistance != props.RefDistance)
        {
            auto const scale = (attenDistance-props.RefDistance)
                / (props.MaxDistance-props.RefDistance);
            dryAttnBase = std::max(1.0f - scale*props.RolloffFactor, 0.0f);
            drygain.Base *= dryAttnBase;

            auto const wetbase = wetgain | std::views::transform(&GainTriplet::Base);
            std::ranges::transform(wetbase | std::views::take(numsends), roomrolloff,
                wetbase.begin(), [scale](f32 const gain, f32 const rolloff)
            { return gain * std::max(1.0f - scale*rolloff, 0.0f); });
        }
        break;

    case DistanceModel::Exponent:
    case DistanceModel::ExponentClamped:
        if(attenDistance > 0.0f && props.RefDistance > 0.0f)
        {
            auto const dist_ratio = attenDistance / props.RefDistance;
            dryAttnBase = std::pow(dist_ratio, -props.RolloffFactor);
            drygain.Base *= dryAttnBase;
            auto const wetbase = wetgain | std::views::transform(&GainTriplet::Base);
            std::ranges::transform(wetbase | std::views::take(numsends), roomrolloff,
                wetbase.begin(), [dist_ratio](f32 const gain, f32 const rolloff)
            { return gain * std::pow(dist_ratio, -rolloff); });
        }
        break;

    case DistanceModel::Disable:
        break;
    }

    /* Calculate directional sound cones */
    auto wetcone = 1.0f;
    auto wetconehf = 1.0f;
    if(directional && props.InnerAngle < 360.0f)
    {
        static constexpr auto Rad2Deg = gsl::narrow_cast<f32>(180.0 / std::numbers::pi);
        auto const angle = Rad2Deg*2.0f * std::acos(-direction.dot_product(tosource)) * ConeScale;

        auto conegain = 1.0f;
        auto conehf = 1.0f;
        if(angle >= props.OuterAngle)
        {
            conegain = props.OuterGain;
            conehf = props.OuterGainHF;
        }
        else if(angle >= props.InnerAngle)
        {
            const auto scale = (angle-props.InnerAngle) / (props.OuterAngle-props.InnerAngle);
            conegain = lerpf(1.0f, props.OuterGain, scale);
            conehf = lerpf(1.0f, props.OuterGainHF, scale);
        }

        drygain.Base *= conegain;
        if(props.DryGainHFAuto)
            drygain.HF *= conehf;
        if(props.WetGainAuto)
            wetcone = conegain;
        if(props.WetGainHFAuto)
            wetconehf = conehf;
    }

    /* Apply gain and frequency filters */
    auto const mingain = std::min(props.MinGain, props.MaxGain);
    auto const maxgain = props.MaxGain;

    drygain.Base = std::clamp(drygain.Base, mingain, maxgain) * props.Direct.Gain;
    drygain.Base = std::min(GainMixMax, drygain.Base * context->mParams.Gain);
    drygain.HF = drygain.HF * props.Direct.GainHF;
    drygain.LF = props.Direct.GainLF;

    std::ranges::transform(props.Send | std::views::take(numsends), wetgain, wetgain.begin(),
        [context,wetcone,wetconehf,mingain,maxgain](VoiceProps::SendData const &send,
            f32 const wetbase)
    {
        auto const gain = std::clamp(wetbase*wetcone, mingain, maxgain) * send.Gain;
        return GainTriplet{
            .Base = std::min(GainMixMax, gain * context->mParams.Gain),
            .HF = send.GainHF * wetconehf,
            .LF = send.GainLF
        };
    }, std::identity{}, &GainTriplet::Base);

    /* Distance-based air absorption and initial send decay. */
    if(distance > props.RefDistance) [[likely]]
    {
        /* FIXME: In keeping with EAX, the base air absorption gain should be
         * taken from the reverb property in the "primary fx slot" when it has
         * a reverb effect and the environment flag set, and be applied to the
         * direct path and all environment sends, rather than each path using
         * the air absorption gain associated with the given slot's effect. At
         * this point in the mixer, and even in EFX itself, there's no concept
         * of a "primary fx slot" so it's unclear which effect slot should be
         * checked.
         *
         * The HF reference is also intended to be handled the same way, but
         * again, there's no concept of a "primary fx slot" here and no way to
         * know which effect slot to look at for the reference frequency.
         */
        auto const distance_units = (distance-props.RefDistance) * props.RolloffFactor;
        auto const distance_meters = distance_units * context->mParams.MetersPerUnit;
        auto const absorb = distance_meters * props.AirAbsorptionFactor;
        if(absorb > std::numeric_limits<f32>::epsilon())
            drygain.HF *= std::pow(context->mParams.AirAbsorptionGainHF, absorb);

        /* If the source's Auxiliary Send Filter Gain Auto is off, no extra
         * adjustment is applied to the send gains.
         */
        for(auto const i : std::views::iota(props.WetGainAuto ? 0_uz : numsends, numsends))
        {
            if(!sendslots[i] || !(sendslots[i]->DecayTime > 0.0f))
                continue;

            if(sendslots[i]->AirAbsorptionGainHF < 1.0f
                && absorb > std::numeric_limits<f32>::epsilon())
                wetgain[i].HF *= std::pow(sendslots[i]->AirAbsorptionGainHF, absorb);

            auto const DecayDistance = sendslots[i]->DecayTime * SpeedOfSoundMetersPerSec;

            /* Apply a decay-time transformation to the wet path, based on the
             * source distance. The initial decay of the reverb effect is
             * calculated and applied to the wet path.
             *
             * FIXME: This is very likely not correct. It more likely should
             * work by calculating a rolloff dynamically based on the reverb
             * parameters (and source distance?) and add it to the room rolloff
             * with the reverb and source rolloff parameters.
             */
            auto const fact = distance_meters / DecayDistance;
            auto const gain = std::pow(ReverbDecayGain, fact)*(1.0f-dryAttnBase) + dryAttnBase;
            wetgain[i].Base *= gain;
        }
    }


    /* Initial source pitch */
    auto pitch = props.Pitch;

    /* Calculate velocity-based doppler effect */
    if(auto const DopplerFactor = props.DopplerFactor * context->mParams.DopplerFactor;
        DopplerFactor > 0.0f)
    {
        auto const &lvelocity = context->mParams.Velocity;
        auto const vss = velocity.dot_product(tosource) * -DopplerFactor;
        auto const vls = lvelocity.dot_product(tosource) * -DopplerFactor;

        if(auto const SpeedOfSound = context->mParams.SpeedOfSound; !(vls < SpeedOfSound))
        {
            /* Listener moving away from the source at the speed of sound.
             * Sound waves can't catch it.
             */
            pitch = 0.0f;
        }
        else if(!(vss < SpeedOfSound))
        {
            /* Source moving toward the listener at the speed of sound. Sound
             * waves bunch up to extreme frequencies.
             */
            pitch = std::numeric_limits<f32>::infinity();
        }
        else
        {
            /* Source and listener movement is nominal. Calculate the proper
             * doppler shift.
             */
            pitch *= (SpeedOfSound-vls) / (SpeedOfSound-vss);
        }
    }

    /* Adjust pitch based on the buffer and output frequencies, and calculate
     * fixed-point stepping value.
     */
    pitch *= gsl::narrow_cast<f32>(voice->mFrequency)
        / gsl::narrow_cast<f32>(device->mSampleRate);
    if(pitch > f32{MaxPitch})
        voice->mStep = MaxPitch<<MixerFracBits;
    else
        voice->mStep = std::max(fastf2u(pitch * MixerFracOne), 1u);
    voice->mResampler = PrepareResampler(props.mResampler, voice->mStep, &voice->mResampleState);

    auto spread = 0.0f;
    if(props.Radius > distance)
        spread = std::numbers::pi_v<f32>*2.0f - distance/props.Radius*std::numbers::pi_v<f32>;
    else if(distance > 0.0f)
        spread = std::asin(props.Radius/distance) * 2.0f;

    CalcPanningAndFilters(voice, tosource[0]*XScale, tosource[1]*YScale, tosource[2]*ZScale,
        distance, spread, drygain, wetgain, sendslots, context->mParams, device);
}

void CalcVoiceParams(Voice *const voice, ContextBase *const context, bool const force)
{
    if(auto *const props = voice->mUpdate.exchange(nullptr, std::memory_order_acq_rel))
    {
        voice->mProps = static_cast<VoiceProps&>(*props);
        AtomicReplaceHead(context->mFreeVoiceProps, props);
    }
    else if(!force)
        return;

    auto const &props = voice->mProps;
    if(auto const ismono3d = voice->mFmtChannels == FmtMono && !voice->mProps.mPanningEnabled;
        (props.DirectChannels != DirectMode::Off && !ismono3d && !IsAmbisonic(voice->mFmtChannels))
        || props.mSpatializeMode == SpatializeMode::Off
        || (props.mSpatializeMode == SpatializeMode::Auto && !ismono3d))
        CalcNonAttnVoiceParams(voice, context);
    else
        CalcAttnVoiceParams(voice, context);
}


void SendSourceStateEvent(ContextBase const *const context, u32 const id, VChangeState const state)
{
    auto *const ring = context->mAsyncEvents.get();
    auto const evt_vec = ring->getWriteVector();
    if(evt_vec[0].empty()) return;

    auto &evt = InitAsyncEvent<AsyncSourceStateEvent>(evt_vec[0].front());
    evt.mId = id;
    switch(state)
    {
    case VChangeState::Reset: evt.mState = AsyncSrcState::Reset; break;
    case VChangeState::Stop: evt.mState = AsyncSrcState::Stop; break;
    case VChangeState::Play: evt.mState = AsyncSrcState::Play; break;
    case VChangeState::Pause: evt.mState = AsyncSrcState::Pause; break;
    /* Shouldn't happen. */
    case VChangeState::Restart:
        break;
    }

    ring->writeAdvance(1);
}

void ProcessVoiceChanges(ContextBase *const ctx)
{
    auto *cur = ctx->mCurrentVoiceChange.load(std::memory_order_acquire);
    auto *next = cur->mNext.load(std::memory_order_acquire);
    if(!next) return;

    const auto enabledevt = ctx->mEnabledEvts.load(std::memory_order_acquire);
    while(next)
    {
        cur = next;

        auto sendevt = false;
        if(cur->mState == VChangeState::Reset || cur->mState == VChangeState::Stop)
        {
            if(auto *const voice = cur->mVoice)
            {
                voice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
                voice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
                /* A source ID indicates the voice was playing or paused, which
                 * gets a reset/stop event.
                 */
                sendevt = voice->mSourceID.exchange(0u, std::memory_order_relaxed) != 0u;
                auto oldvstate = Voice::Playing;
                voice->mPlayState.compare_exchange_strong(oldvstate, Voice::Stopping,
                    std::memory_order_relaxed, std::memory_order_acquire);
                voice->mPendingChange.store(false, std::memory_order_release);
            }
            /* Reset state change events are always sent, even if the voice is
             * already stopped or even if there is no voice.
             */
            sendevt |= (cur->mState == VChangeState::Reset);
        }
        else if(cur->mState == VChangeState::Pause)
        {
            auto *const voice = cur->mVoice;
            auto oldvstate = Voice::Playing;
            sendevt = voice->mPlayState.compare_exchange_strong(oldvstate, Voice::Stopping,
                std::memory_order_release, std::memory_order_acquire);
        }
        else if(cur->mState == VChangeState::Play)
        {
            /* NOTE: When playing a voice, sending a source state change event
             * depends on whether there's an old voice to stop and if that stop
             * is successful. If there is no old voice, a playing event is
             * always sent. If there is an old voice, an event is sent only if
             * the voice is already stopped.
             */
            if(auto *const oldvoice = cur->mOldVoice)
            {
                oldvoice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
                oldvoice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
                oldvoice->mSourceID.store(0u, std::memory_order_relaxed);
                auto oldvstate = Voice::Playing;
                sendevt = !oldvoice->mPlayState.compare_exchange_strong(oldvstate, Voice::Stopping,
                    std::memory_order_relaxed, std::memory_order_acquire);
                oldvoice->mPendingChange.store(false, std::memory_order_release);
            }
            else
                sendevt = true;

            auto *const voice = cur->mVoice;
            voice->mPlayState.store(Voice::Playing, std::memory_order_release);
        }
        else if(cur->mState == VChangeState::Restart)
        {
            /* Restarting a voice never sends a source change event. */
            auto *const oldvoice = cur->mOldVoice;
            oldvoice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
            oldvoice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
            /* If there's no sourceID, the old voice finished so don't start
             * the new one at its new offset.
             */
            if(oldvoice->mSourceID.exchange(0u, std::memory_order_relaxed) != 0u)
            {
                /* Otherwise, set the voice to stopping if it's not already (it
                 * might already be, if paused), and play the new voice as
                 * appropriate.
                 */
                auto oldvstate = Voice::Playing;
                oldvoice->mPlayState.compare_exchange_strong(oldvstate, Voice::Stopping,
                    std::memory_order_relaxed, std::memory_order_acquire);

                auto *const voice = cur->mVoice;
                voice->mPlayState.store((oldvstate == Voice::Playing) ? Voice::Playing
                    : Voice::Stopped, std::memory_order_release);
            }
            oldvoice->mPendingChange.store(false, std::memory_order_release);
        }
        if(sendevt && enabledevt.test(al::to_underlying(AsyncEnableBits::SourceState)))
            SendSourceStateEvent(ctx, cur->mSourceID, cur->mState);

        next = cur->mNext.load(std::memory_order_acquire);
    }
    ctx->mCurrentVoiceChange.store(cur, std::memory_order_release);
}

void ProcessParamUpdates(ContextBase *const ctx, std::span<EffectSlotBase*const> const slots,
    std::span<EffectSlotBase*> const sorted_slots, std::span<Voice*const> const voices)
{
    ProcessVoiceChanges(ctx);

    IncrementRef(ctx->mUpdateCount);
    if(!ctx->mHoldUpdates.load(std::memory_order_acquire)) [[likely]]
    {
        auto force = CalcContextParams(ctx);
        auto const sorted_slot_base = std::to_address(sorted_slots.begin());
        for(auto *slot : slots)
            force |= CalcEffectSlotParams(slot, sorted_slot_base, ctx);

        for(auto *const voice : voices)
        {
            /* Only update voices that have a source. */
            if(voice->mSourceID.load(std::memory_order_relaxed) != 0)
                CalcVoiceParams(voice, ctx, force);
        }
    }
    IncrementRef(ctx->mUpdateCount);
}

void ProcessContexts(DeviceBase const *const device, u32 const SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    auto const curtime = device->getClockTime();

    auto const contexts = std::span{*device->mContexts.load(std::memory_order_acquire)};
    std::ranges::for_each(contexts, [SamplesToDo,curtime](ContextBase *ctx)
    {
        auto const auxslotspan = std::span{*ctx->mActiveAuxSlots.load(std::memory_order_acquire)};
        auto const auxslots = auxslotspan.first(auxslotspan.size()>>1);
        auto const sorted_slots = auxslotspan.last(auxslotspan.size()>>1);
        auto const voices = ctx->getVoicesSpanAcquired();

        /* Process pending property updates for objects on the context. */
        ProcessParamUpdates(ctx, auxslots, sorted_slots, voices);

        /* Clear auxiliary effect slot mixing buffers. */
        std::ranges::fill(auxslots | std::views::transform(&EffectSlotBase::Wet)
            | std::views::transform(&MixParams::Buffer) | std::views::join | std::views::join,
            0.0f);

        /* Process voices that have a playing source. */
        std::ranges::for_each(voices, [ctx,curtime,SamplesToDo](Voice *voice)
        {
            if(auto const vstate = voice->mPlayState.load(std::memory_order_acquire);
                vstate != Voice::Stopped && vstate != Voice::Pending)
                voice->mix(vstate, ctx, curtime, SamplesToDo);
        });

        /* Process effects. */
        if(!auxslots.empty())
        {
            /* Sort the slots into extra storage, so that effect slots come
             * before their effect slot target (or their targets' target). Skip
             * sorting if it has already been done.
             */
            if(!sorted_slots[0])
            {
                /* First, copy the slots to the sorted list and partition them,
                 * so that all slots without a target slot go to the end.
                 */
                static constexpr auto has_target = [](EffectSlotBase const *const slot) noexcept
                { return slot->Target != nullptr; };
                auto split_point = std::partition_copy(auxslots.rbegin(), auxslots.rend(),
                    sorted_slots.begin(), sorted_slots.rbegin(), has_target).first;
                /* There must be at least one slot without a slot target. */
                Ensures(split_point != sorted_slots.end());

                /* Starting from the back of the sorted list, continue
                 * partitioning the front of the list given each target until
                 * all targets are accounted for. This ensures all slots
                 * without a target go last, all slots directly targeting those
                 * last slots go second-to-last, all slots directly targeting
                 * those second-last slots go third-to-last, etc.
                 */
                auto next_target = sorted_slots.end();
                while(std::distance(sorted_slots.begin(), split_point) > 1)
                {
                    /* This shouldn't happen, but if there's unsorted slots
                     * left that don't target any sorted slots, they can't
                     * contribute to the output, so leave them.
                     */
                    if(next_target == split_point) [[unlikely]]
                        break;

                    --next_target;
                    auto const not_next = [next_target](EffectSlotBase const *const slot) noexcept
                        -> bool
                    { return slot->Target != *next_target; };
                    split_point = std::partition(sorted_slots.begin(), split_point, not_next);
                }
            }

            std::ranges::for_each(sorted_slots, [SamplesToDo](EffectSlotBase const *const slot)
            {
                auto *const state = slot->mEffectState.get();
                state->process(SamplesToDo, slot->Wet.Buffer, state->mOutTarget);
            });
        }

        /* Signal the event handler if there are any events to read. */
        if(auto const *const ring = ctx->mAsyncEvents.get(); ring->readSpace() > 0)
        {
            ctx->mEventsPending.store(1_u32, std::memory_order_release);
            al::atomic_notify_all(ctx->mEventsPending);
        }
    });
}


void ApplyDistanceComp(std::span<FloatBufferLine> const Samples, usize const SamplesToDo,
    std::span<DistanceComp::ChanData const, MaxOutputChannels> const chandata)
{
    ASSUME(SamplesToDo > 0);

    std::ignore = std::ranges::mismatch(chandata, Samples,
        [SamplesToDo](const DistanceComp::ChanData &distcomp, FloatBufferSpan chanbuffer)
    {
        auto const gain = distcomp.Gain;
        auto const distbuf = distcomp.Buffer;

        auto const base = distbuf.size();
        if(base < 1) return true;

        auto const inout = chanbuffer.first(SamplesToDo);
        if(SamplesToDo >= base) [[likely]]
        {
            auto const inout_start = std::prev(inout.end(), gsl::narrow_cast<ptrdiff_t>(base));
            auto const delay_end = std::ranges::rotate(inout, inout_start).begin();
            std::ranges::swap_ranges(std::span{inout.begin(), delay_end}, distbuf);
        }
        else
        {
            auto const delay_start = std::ranges::swap_ranges(inout, distbuf).in2;
            std::ranges::rotate(distbuf, delay_start);
        }
        std::ranges::transform(inout, inout.begin(), [gain](f32 const s) noexcept -> f32
        { return s*gain; });

        return true;
    });
}

void ApplyDither(std::span<FloatBufferLine> const Samples, u32 *const dither_seed,
    f32 const quant_scale, usize const SamplesToDo)
{
    static constexpr auto invRNGRange = 1.0 / std::numeric_limits<u32>::max();
    ASSUME(SamplesToDo > 0);

    /* Dithering. Generate whitenoise (uniform distribution of random values
     * between -1 and +1) and add it to the sample values, after scaling up to
     * the desired quantization depth and before rounding.
     */
    auto const invscale = 1.0f / quant_scale;
    auto seed = *dither_seed;
    auto dither_sample = [&seed,invscale,quant_scale](f32 const sample) noexcept -> f32
    {
        auto val = sample * quant_scale;
        auto const rng0 = dither_rng(&seed);
        auto const rng1 = dither_rng(&seed);
        val += gsl::narrow_cast<f32>(rng0*invRNGRange - rng1*invRNGRange);
        return fast_roundf(val) * invscale;
    };
    for(FloatBufferSpan const inout : Samples)
        std::ranges::transform(inout.first(SamplesToDo), inout.begin(), dither_sample);
    *dither_seed = seed;
}


template<typename T> [[nodiscard]]
auto SampleConv(f32) noexcept -> T = delete;

template<> [[nodiscard]] auto SampleConv(f32 const val) noexcept -> f32
{ return val; }
template<> [[nodiscard]] auto SampleConv(f32 const val) noexcept -> i32
{
    /* Floats have a 23-bit mantissa, plus an implied 1 bit and a sign bit.
     * This means a normalized float has at most 25 bits of signed precision.
     * When scaling and clamping for a signed 32-bit integer, these following
     * values are the best a float can give.
     */
    return fastf2i(std::clamp(val*2147483648.0f, -2147483648.0f, 2147483520.0f));
}
template<> [[nodiscard]] auto SampleConv(f32 const val) noexcept -> i16
{ return i16{gsl::narrow_cast<i16::value_t>(fastf2i(std::clamp(val*32768.0f, -32768.0f, 32767.0f)))}; }
template<> [[nodiscard]] auto SampleConv(f32 const val) noexcept -> i8
{ return i8{gsl::narrow_cast<i8::value_t>(fastf2i(std::clamp(val*128.0f, -128.0f, 127.0f)))}; }

/* Define unsigned output variations. */
template<> [[nodiscard]] auto SampleConv(f32 const val) noexcept -> u32
{ return as_unsigned(SampleConv<i32>(val)) + 2147483648u; }
template<> [[nodiscard]] auto SampleConv(f32 const val) noexcept -> u16
{ return SampleConv<i16>(val).reinterpret_as<u16>() + 32768; }
template<> [[nodiscard]] auto SampleConv(f32 const val) noexcept -> u8
{ return SampleConv<i8>(val).reinterpret_as<u8>() + 128; }

template<typename T>
void Write(std::span<FloatBufferLine const> const InBuffer, void *const OutBuffer,
    usize const Offset, usize const SamplesToDo, usize const FrameStep)
{
    ASSUME(FrameStep > 0);
    ASSUME(SamplesToDo > 0);

    auto const output = std::span{static_cast<T*>(OutBuffer), (Offset+SamplesToDo)*FrameStep}
        .subspan(Offset*FrameStep);

    /* If there's extra channels in the interleaved output buffer to skip,
     * clear the whole output buffer. This is simpler to ensure the extra
     * channels are silent than trying to clear just the extra channels.
     */
    if(FrameStep > InBuffer.size())
        std::ranges::fill(output, SampleConv<T>(0.0f));

    auto outbase = output.begin();
    for(auto const &srcbuf : InBuffer)
    {
        auto out = outbase++;
        *out = SampleConv<T>(srcbuf.front());
        std::ranges::for_each(srcbuf | std::views::take(SamplesToDo) | std::views::drop(1),
            [FrameStep,&out](f32 const s) noexcept
        {
            std::advance(out, FrameStep);
            *out = SampleConv<T>(s);
        });
    }
}

template<typename T>
void Write(std::span<FloatBufferLine const> const InBuffer, std::span<void*const> const OutBuffers,
    usize const Offset, usize const SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    std::ignore = std::ranges::mismatch(OutBuffers, InBuffer,
        [Offset,SamplesToDo](void *const dstbuf, FloatConstBufferSpan const srcbuf)
    {
        auto const dst = std::span{static_cast<T*>(dstbuf), Offset+SamplesToDo}.subspan(Offset);
        std::ranges::transform(srcbuf | std::views::take(SamplesToDo), dst.begin(), SampleConv<T>);
        return true;
    });
}

} // namespace

auto DeviceBase::renderSamples(u32 const numSamples) -> u32
{
    auto const samplesToDo = std::min(numSamples, u32{BufferLineSize});

    /* Clear main mixing buffers. */
    std::ranges::fill(MixBuffer | std::views::join, 0.0f);

    {
        auto const mixLock = getWriteMixLock();

        /* Process and mix each context's sources and effects. */
        ProcessContexts(this, samplesToDo);

        /* Every second's worth of samples is converted and added to clock base
         * so that large sample counts don't overflow during conversion. This
         * also guarantees a stable conversion.
         */
        auto const samplesDone = mSamplesDone.load(std::memory_order_relaxed) + samplesToDo;
        auto const clockBaseSec = mClockBaseSec.load(std::memory_order_relaxed) +
            seconds32{samplesDone/mSampleRate};
        mSamplesDone.store(samplesDone%mSampleRate, std::memory_order_relaxed);
        mClockBaseSec.store(clockBaseSec, std::memory_order_relaxed);
    }

    /* Apply any needed post-process for finalizing the Dry mix to the RealOut
     * (Ambisonic decode, UHJ encode, etc.).
     */
    std::visit([this,samplesToDo](auto &arg) { this->Process(arg, samplesToDo); }, mPostProcess);

    /* Apply compression, limiting sample amplitude if needed or desired. */
    if(Limiter) Limiter->process(samplesToDo, RealOut.Buffer);

    /* Apply delays and attenuation for mismatched speaker distances. */
    if(ChannelDelays)
        ApplyDistanceComp(RealOut.Buffer, samplesToDo, ChannelDelays->mChannels);

    /* Apply dithering. The compressor should have left enough headroom for the
     * dither noise to not saturate.
     */
    if(DitherDepth > 0.0f)
        ApplyDither(RealOut.Buffer, &DitherSeed, DitherDepth, samplesToDo);

    return samplesToDo;
}

void DeviceBase::renderSamples(std::span<void*const> const outBuffers, u32 const numSamples)
{
    auto mixer_mode = FPUCtl{};
    auto total = 0_u32;
    while(auto const todo = numSamples - total)
    {
        auto const samplesToDo = renderSamples(todo);

        switch(FmtType)
        {
#define HANDLE_WRITE(T) case T:                                               \
    Write<DevFmtType_t<T>>(RealOut.Buffer, outBuffers, total, samplesToDo); break;
        HANDLE_WRITE(DevFmtByte)
        HANDLE_WRITE(DevFmtUByte)
        HANDLE_WRITE(DevFmtShort)
        HANDLE_WRITE(DevFmtUShort)
        HANDLE_WRITE(DevFmtInt)
        HANDLE_WRITE(DevFmtUInt)
        HANDLE_WRITE(DevFmtFloat)
        }
#undef HANDLE_WRITE

        total += samplesToDo;
    }
}

void DeviceBase::renderSamples(void *const outBuffer, u32 const numSamples, usize const frameStep)
{
    auto mixer_mode = FPUCtl{};
    auto total = 0_u32;
    while(auto const todo = numSamples - total)
    {
        auto const samplesToDo = renderSamples(todo);

        if(outBuffer) [[likely]]
        {
            /* Finally, interleave and convert samples, writing to the device's
             * output buffer.
             */
            switch(FmtType)
            {
#define HANDLE_WRITE(T) case T:                                               \
    Write<DevFmtType_t<T>>(RealOut.Buffer, outBuffer, total, samplesToDo, frameStep); break;
            HANDLE_WRITE(DevFmtByte)
            HANDLE_WRITE(DevFmtUByte)
            HANDLE_WRITE(DevFmtShort)
            HANDLE_WRITE(DevFmtUShort)
            HANDLE_WRITE(DevFmtInt)
            HANDLE_WRITE(DevFmtUInt)
            HANDLE_WRITE(DevFmtFloat)
#undef HANDLE_WRITE
            }
        }

        total += samplesToDo;
    }
}

void DeviceBase::doDisconnect(std::string&& msg)
{
    const auto mixLock = getWriteMixLock();

    if(Connected.exchange(false, std::memory_order_acq_rel))
    {
        auto evt = std::array{AsyncEvent{std::in_place_type<AsyncDisconnectEvent>}};
        auto &disconnect = std::get<AsyncDisconnectEvent>(evt.front());
        disconnect.msg = std::move(msg);

        for(auto *ctx : *mContexts.load())
        {
            if(auto *const ring = ctx->mAsyncEvents.get();
                ring->write(evt) > 0)
            {
                ctx->mEventsPending.store(1_u32, std::memory_order_release);
                al::atomic_notify_all(ctx->mEventsPending);
            }

            if(!ctx->mStopVoicesOnDisconnect.load())
            {
                ProcessVoiceChanges(ctx);
                continue;
            }

            std::ranges::for_each(ctx->getVoicesSpanAcquired(), [](Voice *const voice)
            {
                voice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
                voice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
                voice->mSourceID.store(0u, std::memory_order_relaxed);
                voice->mPlayState.store(Voice::Stopped, std::memory_order_release);
            });
        }
    }
}
