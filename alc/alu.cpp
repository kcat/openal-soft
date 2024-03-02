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

#include "alu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <utility>

#include "almalloc.h"
#include "alnumbers.h"
#include "alnumeric.h"
#include "alspan.h"
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
#include "core/uhjfilter.h"
#include "core/voice.h"
#include "core/voice_change.h"
#include "intrusive_ptr.h"
#include "opthelpers.h"
#include "ringbuffer.h"
#include "strutils.h"
#include "vecmat.h"
#include "vector.h"

struct CTag;
#ifdef HAVE_SSE
struct SSETag;
#endif
#ifdef HAVE_SSE2
struct SSE2Tag;
#endif
#ifdef HAVE_SSE4_1
struct SSE4Tag;
#endif
#ifdef HAVE_NEON
struct NEONTag;
#endif
struct PointTag;
struct LerpTag;
struct CubicTag;
struct BSincTag;
struct FastBSincTag;


static_assert(!(MaxResamplerPadding&1), "MaxResamplerPadding is not a multiple of two");


namespace {

using uint = unsigned int;
using namespace std::chrono;
using namespace std::string_view_literals;

float InitConeScale()
{
    float ret{1.0f};
    if(auto optval = al::getenv("__ALSOFT_HALF_ANGLE_CONES"))
    {
        if(al::case_compare(*optval, "true"sv) == 0
            || strtol(optval->c_str(), nullptr, 0) == 1)
            ret *= 0.5f;
    }
    return ret;
}
/* Cone scalar */
const float ConeScale{InitConeScale()};

/* Localized scalars for mono sources (initialized in aluInit, after
 * configuration is loaded).
 */
float XScale{1.0f};
float YScale{1.0f};
float ZScale{1.0f};

/* Source distance scale for NFC filters. */
float NfcScale{1.0f};


using HrtfDirectMixerFunc = void(*)(const FloatBufferSpan LeftOut, const FloatBufferSpan RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, float *TempBuf,
    HrtfChannelState *ChanState, const size_t IrSize, const size_t BufferSize);

HrtfDirectMixerFunc MixDirectHrtf{MixDirectHrtf_<CTag>};

inline HrtfDirectMixerFunc SelectHrtfMixer()
{
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixDirectHrtf_<NEONTag>;
#endif
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixDirectHrtf_<SSETag>;
#endif

    return MixDirectHrtf_<CTag>;
}


inline void BsincPrepare(const uint increment, BsincState *state, const BSincTable *table)
{
    size_t si{BSincScaleCount - 1};
    float sf{0.0f};

    if(increment > MixerFracOne)
    {
        sf = MixerFracOne/static_cast<float>(increment) - table->scaleBase;
        sf = std::max(0.0f, BSincScaleCount*sf*table->scaleRange - 1.0f);
        si = float2uint(sf);
        /* The interpolation factor is fit to this diagonally-symmetric curve
         * to reduce the transition ripple caused by interpolating different
         * scales of the sinc function.
         */
        sf = 1.0f - std::cos(std::asin(sf - static_cast<float>(si)));
    }

    state->sf = sf;
    state->m = table->m[si];
    state->l = (state->m/2) - 1;
    state->filter = table->Tab + table->filterOffset[si];
}

inline ResamplerFunc SelectResampler(Resampler resampler, uint increment)
{
    switch(resampler)
    {
    case Resampler::Point:
        return Resample_<PointTag,CTag>;
    case Resampler::Linear:
#ifdef HAVE_NEON
        if((CPUCapFlags&CPU_CAP_NEON))
            return Resample_<LerpTag,NEONTag>;
#endif
#ifdef HAVE_SSE4_1
        if((CPUCapFlags&CPU_CAP_SSE4_1))
            return Resample_<LerpTag,SSE4Tag>;
#endif
#ifdef HAVE_SSE2
        if((CPUCapFlags&CPU_CAP_SSE2))
            return Resample_<LerpTag,SSE2Tag>;
#endif
        return Resample_<LerpTag,CTag>;
    case Resampler::Cubic:
#ifdef HAVE_NEON
        if((CPUCapFlags&CPU_CAP_NEON))
            return Resample_<CubicTag,NEONTag>;
#endif
#ifdef HAVE_SSE4_1
        if((CPUCapFlags&CPU_CAP_SSE4_1))
            return Resample_<CubicTag,SSE4Tag>;
#endif
#ifdef HAVE_SSE2
        if((CPUCapFlags&CPU_CAP_SSE2))
            return Resample_<CubicTag,SSE2Tag>;
#endif
#ifdef HAVE_SSE
        if((CPUCapFlags&CPU_CAP_SSE))
            return Resample_<CubicTag,SSETag>;
#endif
        return Resample_<CubicTag,CTag>;
    case Resampler::BSinc12:
    case Resampler::BSinc24:
        if(increment > MixerFracOne)
        {
#ifdef HAVE_NEON
            if((CPUCapFlags&CPU_CAP_NEON))
                return Resample_<BSincTag,NEONTag>;
#endif
#ifdef HAVE_SSE
            if((CPUCapFlags&CPU_CAP_SSE))
                return Resample_<BSincTag,SSETag>;
#endif
            return Resample_<BSincTag,CTag>;
        }
        /* fall-through */
    case Resampler::FastBSinc12:
    case Resampler::FastBSinc24:
#ifdef HAVE_NEON
        if((CPUCapFlags&CPU_CAP_NEON))
            return Resample_<FastBSincTag,NEONTag>;
#endif
#ifdef HAVE_SSE
        if((CPUCapFlags&CPU_CAP_SSE))
            return Resample_<FastBSincTag,SSETag>;
#endif
        return Resample_<FastBSincTag,CTag>;
    }

    return Resample_<PointTag,CTag>;
}

} // namespace

void aluInit(CompatFlagBitset flags, const float nfcscale)
{
    MixDirectHrtf = SelectHrtfMixer();
    XScale = flags.test(CompatFlags::ReverseX) ? -1.0f : 1.0f;
    YScale = flags.test(CompatFlags::ReverseY) ? -1.0f : 1.0f;
    ZScale = flags.test(CompatFlags::ReverseZ) ? -1.0f : 1.0f;

    NfcScale = std::clamp(nfcscale, 0.0001f, 10000.0f);
}


ResamplerFunc PrepareResampler(Resampler resampler, uint increment, InterpState *state)
{
    switch(resampler)
    {
    case Resampler::Point:
    case Resampler::Linear:
        break;
    case Resampler::Cubic:
        state->emplace<CubicState>(al::span{gGaussianFilter.mTable});
        break;
    case Resampler::FastBSinc12:
    case Resampler::BSinc12:
        BsincPrepare(increment, &state->emplace<BsincState>(), &gBSinc12);
        break;
    case Resampler::FastBSinc24:
    case Resampler::BSinc24:
        BsincPrepare(increment, &state->emplace<BsincState>(), &gBSinc24);
        break;
    }
    return SelectResampler(resampler, increment);
}


void DeviceBase::ProcessHrtf(const size_t SamplesToDo)
{
    /* HRTF is stereo output only. */
    const size_t lidx{RealOut.ChannelIndex[FrontLeft]};
    const size_t ridx{RealOut.ChannelIndex[FrontRight]};

    MixDirectHrtf(RealOut.Buffer[lidx], RealOut.Buffer[ridx], Dry.Buffer, HrtfAccumData.data(),
        mHrtfState->mTemp.data(), mHrtfState->mChannels.data(), mHrtfState->mIrSize, SamplesToDo);
}

void DeviceBase::ProcessAmbiDec(const size_t SamplesToDo)
{
    AmbiDecoder->process(RealOut.Buffer, Dry.Buffer.data(), SamplesToDo);
}

void DeviceBase::ProcessAmbiDecStablized(const size_t SamplesToDo)
{
    /* Decode with front image stablization. */
    const size_t lidx{RealOut.ChannelIndex[FrontLeft]};
    const size_t ridx{RealOut.ChannelIndex[FrontRight]};
    const size_t cidx{RealOut.ChannelIndex[FrontCenter]};

    AmbiDecoder->processStablize(RealOut.Buffer, Dry.Buffer.data(), lidx, ridx, cidx,
        SamplesToDo);
}

void DeviceBase::ProcessUhj(const size_t SamplesToDo)
{
    /* UHJ is stereo output only. */
    const size_t lidx{RealOut.ChannelIndex[FrontLeft]};
    const size_t ridx{RealOut.ChannelIndex[FrontRight]};

    /* Encode to stereo-compatible 2-channel UHJ output. */
    mUhjEncoder->encode(RealOut.Buffer[lidx].data(), RealOut.Buffer[ridx].data(),
        {{Dry.Buffer[0].data(), Dry.Buffer[1].data(), Dry.Buffer[2].data()}}, SamplesToDo);
}

void DeviceBase::ProcessBs2b(const size_t SamplesToDo)
{
    /* First, decode the ambisonic mix to the "real" output. */
    AmbiDecoder->process(RealOut.Buffer, Dry.Buffer.data(), SamplesToDo);

    /* BS2B is stereo output only. */
    const size_t lidx{RealOut.ChannelIndex[FrontLeft]};
    const size_t ridx{RealOut.ChannelIndex[FrontRight]};

    /* Now apply the BS2B binaural/crossfeed filter. */
    Bs2b->cross_feed(RealOut.Buffer[lidx].data(), RealOut.Buffer[ridx].data(), SamplesToDo);
}


namespace {

/* This RNG method was created based on the math found in opusdec. It's quick,
 * and starting with a seed value of 22222, is suitable for generating
 * whitenoise.
 */
inline uint dither_rng(uint *seed) noexcept
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
    const al::span<std::array<float,MaxAmbiChannels>,MaxAmbiChannels> output,
    const al::span<const std::array<float,MaxAmbiChannels>> upsampler,
    const al::span<std::array<float,MaxAmbiChannels>,MaxAmbiChannels> rotator, size_t coeffs_order)
{
    const size_t num_chans{AmbiChannelsFromOrder(coeffs_order)};
    for(size_t i{0};i < upsampler.size();++i)
        output[i].fill(0.0f);
    for(size_t i{0};i < upsampler.size();++i)
    {
        for(size_t k{0};k < num_chans;++k)
        {
            float *RESTRICT out{output[i].data()};
            /* Write the full number of channels. The compiler will have an
             * easier time optimizing if it has a fixed length.
             */
            for(size_t j{0};j < MaxAmbiChannels;++j)
                out[j] += upsampler[i][k] * rotator[k][j];
        }
    }
}


constexpr auto GetAmbiScales(AmbiScaling scaletype) noexcept
{
    switch(scaletype)
    {
    case AmbiScaling::FuMa: return al::span{AmbiScale::FromFuMa};
    case AmbiScaling::SN3D: return al::span{AmbiScale::FromSN3D};
    case AmbiScaling::UHJ: return al::span{AmbiScale::FromUHJ};
    case AmbiScaling::N3D: break;
    }
    return al::span{AmbiScale::FromN3D};
}

constexpr auto GetAmbiLayout(AmbiLayout layouttype) noexcept
{
    if(layouttype == AmbiLayout::FuMa) return al::span{AmbiIndex::FromFuMa};
    return al::span{AmbiIndex::FromACN};
}

constexpr auto GetAmbi2DLayout(AmbiLayout layouttype) noexcept
{
    if(layouttype == AmbiLayout::FuMa) return al::span{AmbiIndex::FromFuMa2D};
    return al::span{AmbiIndex::FromACN2D};
}


bool CalcContextParams(ContextBase *ctx)
{
    ContextProps *props{ctx->mParams.ContextUpdate.exchange(nullptr, std::memory_order_acq_rel)};
    if(!props) return false;

    const alu::Vector pos{props->Position[0], props->Position[1], props->Position[2], 1.0f};
    ctx->mParams.Position = pos;

    /* AT then UP */
    alu::Vector N{props->OrientAt[0], props->OrientAt[1], props->OrientAt[2], 0.0f};
    N.normalize();
    alu::Vector V{props->OrientUp[0], props->OrientUp[1], props->OrientUp[2], 0.0f};
    V.normalize();
    /* Build and normalize right-vector */
    alu::Vector U{N.cross_product(V)};
    U.normalize();

    const alu::Matrix rot{
        U[0], V[0], -N[0], 0.0,
        U[1], V[1], -N[1], 0.0,
        U[2], V[2], -N[2], 0.0,
         0.0,  0.0,   0.0, 1.0};
    const alu::Vector vel{props->Velocity[0], props->Velocity[1], props->Velocity[2], 0.0};

    ctx->mParams.Matrix = rot;
    ctx->mParams.Velocity = rot * vel;

    ctx->mParams.Gain = props->Gain * ctx->mGainBoost;
    ctx->mParams.MetersPerUnit = props->MetersPerUnit;
    ctx->mParams.AirAbsorptionGainHF = props->AirAbsorptionGainHF;

    ctx->mParams.DopplerFactor = props->DopplerFactor;
    ctx->mParams.SpeedOfSound = props->SpeedOfSound * props->DopplerVelocity;

    ctx->mParams.SourceDistanceModel = props->SourceDistanceModel;
    ctx->mParams.mDistanceModel = props->mDistanceModel;

    AtomicReplaceHead(ctx->mFreeContextProps, props);
    return true;
}

bool CalcEffectSlotParams(EffectSlot *slot, EffectSlot **sorted_slots, ContextBase *context)
{
    EffectSlotProps *props{slot->Update.exchange(nullptr, std::memory_order_acq_rel)};
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
    if(auto *reverbprops = std::get_if<ReverbProps>(&props->Props))
    {
        slot->RoomRolloff = reverbprops->RoomRolloffFactor;
        slot->DecayTime = reverbprops->DecayTime;
        slot->DecayLFRatio = reverbprops->DecayLFRatio;
        slot->DecayHFRatio = reverbprops->DecayHFRatio;
        slot->DecayHFLimit = reverbprops->DecayHFLimit;
        slot->AirAbsorptionGainHF = reverbprops->AirAbsorptionGainHF;
    }
    else
    {
        slot->RoomRolloff = 0.0f;
        slot->DecayTime = 0.0f;
        slot->DecayLFRatio = 0.0f;
        slot->DecayHFRatio = 0.0f;
        slot->DecayHFLimit = false;
        slot->AirAbsorptionGainHF = 1.0f;
    }

    EffectState *state{props->State.release()};
    EffectState *oldstate{slot->mEffectState.release()};
    slot->mEffectState.reset(state);

    /* Only release the old state if it won't get deleted, since we can't be
     * deleting/freeing anything in the mixer.
     */
    if(!oldstate->releaseIfNoDelete())
    {
        /* Otherwise, if it would be deleted send it off with a release event. */
        RingBuffer *ring{context->mAsyncEvents.get()};
        auto evt_vec = ring->getWriteVector();
        if(evt_vec.first.len > 0) LIKELY
        {
            auto &evt = InitAsyncEvent<AsyncEffectReleaseEvent>(evt_vec.first.buf);
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

    EffectTarget output;
    if(EffectSlot *target{slot->Target})
        output = EffectTarget{&target->Wet, nullptr};
    else
    {
        DeviceBase *device{context->mDevice};
        output = EffectTarget{&device->Dry, &device->RealOut};
    }
    state->update(context, slot, &slot->mEffectProps, output);
    return true;
}


/* Scales the azimuth of the given vector by 3 if it's in front. Effectively
 * scales +/-30 degrees to +/-90 degrees, leaving > +90 and < -90 alone.
 */
inline std::array<float,3> ScaleAzimuthFront3(std::array<float,3> pos)
{
    if(pos[2] < 0.0f)
    {
        /* Normalize the length of the x,z components for a 2D vector of the
         * azimuth angle. Negate Z since {0,0,-1} is angle 0.
         */
        const float len2d{std::sqrt(pos[0]*pos[0] + pos[2]*pos[2])};
        float x{pos[0] / len2d};
        float z{-pos[2] / len2d};

        /* Z > cos(pi/6) = -30 < azimuth < 30 degrees. */
        if(z > 0.866025403785f)
        {
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
inline std::array<float,3> ScaleAzimuthFront3_2(std::array<float,3> pos)
{
    if(pos[2] < 0.0f)
    {
        const float len2d{std::sqrt(pos[0]*pos[0] + pos[2]*pos[2])};
        float x{pos[0] / len2d};
        float z{-pos[2] / len2d};

        /* Z > cos(pi/3) = -60 < azimuth < 60 degrees. */
        if(z > 0.5f)
        {
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
 * rotation, etc).
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
constexpr size_t CalcRotatorSize(size_t l) noexcept
{
    if(l >= 2)
        return (l*2 + 1)*(l*2 + 1) + CalcRotatorSize(l-1);
    return 0;
}

struct RotatorCoeffs {
    struct CoeffValues {
        float u, v, w;
    };
    std::array<CoeffValues,CalcRotatorSize(MaxAmbiOrder)> mCoeffs{};

    RotatorCoeffs()
    {
        auto coeffs = mCoeffs.begin();

        for(int l=2;l <= MaxAmbiOrder;++l)
        {
            for(int n{-l};n <= l;++n)
            {
                for(int m{-l};m <= l;++m)
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

                    const double denom{static_cast<double>((std::abs(n) == l) ?
                          (2*l) * (2*l - 1) : (l*l - n*n))};

                    if(m == 0)
                    {
                        coeffs->u = static_cast<float>(std::sqrt(l * l / denom));
                        coeffs->v = static_cast<float>(std::sqrt((l-1) * l / denom) * -1.0);
                        coeffs->w = 0.0f;
                    }
                    else
                    {
                        const int abs_m{std::abs(m)};
                        coeffs->u = static_cast<float>(std::sqrt((l*l - m*m) / denom));
                        coeffs->v = static_cast<float>(std::sqrt((l+abs_m-1) * (l+abs_m) / denom) *
                            0.5);
                        coeffs->w = static_cast<float>(std::sqrt((l-abs_m-1) * (l-abs_m) / denom) *
                            -0.5);
                    }
                    ++coeffs;
                }
            }
        }
    }
};
const RotatorCoeffs RotatorCoeffArray{};

/**
 * Given the matrix, pre-filled with the (zeroth- and) first-order rotation
 * coefficients, this fills in the coefficients for the higher orders up to and
 * including the given order. The matrix is in ACN layout.
 */
void AmbiRotator(AmbiRotateMatrix &matrix, const int order)
{
    /* Don't do anything for < 2nd order. */
    if(order < 2) return;

    auto P = [](const int i, const int l, const int a, const int n, const size_t last_band,
        const AmbiRotateMatrix &R)
    {
        const float ri1{ R[ 1+2][static_cast<size_t>(i+2_z)]};
        const float rim1{R[-1+2][static_cast<size_t>(i+2_z)]};
        const float ri0{ R[ 0+2][static_cast<size_t>(i+2_z)]};

        const size_t y{last_band + static_cast<size_t>(a+l-1)};
        if(n == -l)
            return ri1*R[last_band][y] + rim1*R[last_band + static_cast<size_t>(l-1_z)*2][y];
        if(n == l)
            return ri1*R[last_band + static_cast<size_t>(l-1_z)*2][y] - rim1*R[last_band][y];
        return ri0*R[last_band + static_cast<size_t>(l-1_z+n)][y];
    };

    auto U = [P](const int l, const int m, const int n, const size_t last_band,
        const AmbiRotateMatrix &R)
    {
        return P(0, l, m, n, last_band, R);
    };
    auto V = [P](const int l, const int m, const int n, const size_t last_band,
        const AmbiRotateMatrix &R)
    {
        using namespace al::numbers;
        if(m > 0)
        {
            const bool d{m == 1};
            const float p0{P( 1, l,  m-1, n, last_band, R)};
            const float p1{P(-1, l, -m+1, n, last_band, R)};
            return d ? p0*sqrt2_v<float> : (p0 - p1);
        }
        const bool d{m == -1};
        const float p0{P( 1, l,  m+1, n, last_band, R)};
        const float p1{P(-1, l, -m-1, n, last_band, R)};
        return d ? p1*sqrt2_v<float> : (p0 + p1);
    };
    auto W = [P](const int l, const int m, const int n, const size_t last_band,
        const AmbiRotateMatrix &R)
    {
        assert(m != 0);
        if(m > 0)
        {
            const float p0{P( 1, l,  m+1, n, last_band, R)};
            const float p1{P(-1, l, -m-1, n, last_band, R)};
            return p0 + p1;
        }
        const float p0{P( 1, l,  m-1, n, last_band, R)};
        const float p1{P(-1, l, -m+1, n, last_band, R)};
        return p0 - p1;
    };

    // compute rotation matrix of each subsequent band recursively
    auto coeffs = RotatorCoeffArray.mCoeffs.cbegin();
    size_t band_idx{4}, last_band{1};
    for(int l{2};l <= order;++l)
    {
        size_t y{band_idx};
        for(int n{-l};n <= l;++n,++y)
        {
            size_t x{band_idx};
            for(int m{-l};m <= l;++m,++x)
            {
                float r{0.0f};

                // computes Eq.8.1
                if(const float u{coeffs->u}; u != 0.0f)
                    r += u * U(l, m, n, last_band, matrix);
                if(const float v{coeffs->v}; v != 0.0f)
                    r += v * V(l, m, n, last_band, matrix);
                if(const float w{coeffs->w}; w != 0.0f)
                    r += w * W(l, m, n, last_band, matrix);

                matrix[y][x] = r;
                ++coeffs;
            }
        }
        last_band = band_idx;
        band_idx += static_cast<uint>(l)*2_uz + 1;
    }
}
/* End ambisonic rotation helpers. */


constexpr float sin30{0.5f};
constexpr float cos30{0.866025403785f};
constexpr float sin45{al::numbers::sqrt2_v<float>*0.5f};
constexpr float cos45{al::numbers::sqrt2_v<float>*0.5f};
constexpr float sin110{ 0.939692620786f};
constexpr float cos110{-0.342020143326f};

struct ChanPosMap {
    Channel channel;
    std::array<float,3> pos;
};


struct GainTriplet { float Base, HF, LF; };

void CalcPanningAndFilters(Voice *voice, const float xpos, const float ypos, const float zpos,
    const float Distance, const float Spread, const GainTriplet &DryGain,
    const al::span<const GainTriplet,MaxSendCount> WetGain,
    const al::span<EffectSlot*,MaxSendCount> SendSlots, const VoiceProps *props,
    const ContextParams &Context, DeviceBase *Device)
{
    static constexpr std::array MonoMap{
        ChanPosMap{FrontCenter, std::array{0.0f, 0.0f, -1.0f}}
    };
    static constexpr std::array RearMap{
        ChanPosMap{BackLeft,  std::array{-sin30, 0.0f, cos30}},
        ChanPosMap{BackRight, std::array{ sin30, 0.0f, cos30}},
    };
    static constexpr std::array QuadMap{
        ChanPosMap{FrontLeft,  std::array{-sin45, 0.0f, -cos45}},
        ChanPosMap{FrontRight, std::array{ sin45, 0.0f, -cos45}},
        ChanPosMap{BackLeft,   std::array{-sin45, 0.0f,  cos45}},
        ChanPosMap{BackRight,  std::array{ sin45, 0.0f,  cos45}},
    };
    static constexpr std::array X51Map{
        ChanPosMap{FrontLeft,   std::array{-sin30, 0.0f, -cos30}},
        ChanPosMap{FrontRight,  std::array{ sin30, 0.0f, -cos30}},
        ChanPosMap{FrontCenter, std::array{  0.0f, 0.0f, -1.0f}},
        ChanPosMap{LFE, {}},
        ChanPosMap{SideLeft,    std::array{-sin110, 0.0f, -cos110}},
        ChanPosMap{SideRight,   std::array{ sin110, 0.0f, -cos110}},
    };
    static constexpr std::array X61Map{
        ChanPosMap{FrontLeft,   std::array{-sin30, 0.0f, -cos30}},
        ChanPosMap{FrontRight,  std::array{ sin30, 0.0f, -cos30}},
        ChanPosMap{FrontCenter, std::array{  0.0f, 0.0f, -1.0f}},
        ChanPosMap{LFE, {}},
        ChanPosMap{BackCenter,  std::array{ 0.0f, 0.0f, 1.0f}},
        ChanPosMap{SideLeft,    std::array{-1.0f, 0.0f, 0.0f}},
        ChanPosMap{SideRight,   std::array{ 1.0f, 0.0f, 0.0f}},
    };
    static constexpr std::array X71Map{
        ChanPosMap{FrontLeft,   std::array{-sin30, 0.0f, -cos30}},
        ChanPosMap{FrontRight,  std::array{ sin30, 0.0f, -cos30}},
        ChanPosMap{FrontCenter, std::array{  0.0f, 0.0f, -1.0f}},
        ChanPosMap{LFE, {}},
        ChanPosMap{BackLeft,    std::array{-sin30, 0.0f, cos30}},
        ChanPosMap{BackRight,   std::array{ sin30, 0.0f, cos30}},
        ChanPosMap{SideLeft,    std::array{ -1.0f, 0.0f, 0.0f}},
        ChanPosMap{SideRight,   std::array{  1.0f, 0.0f, 0.0f}},
    };

    std::array StereoMap{
        ChanPosMap{FrontLeft,   std::array{-sin30, 0.0f, -cos30}},
        ChanPosMap{FrontRight,  std::array{ sin30, 0.0f, -cos30}},
    };

    const auto Frequency = static_cast<float>(Device->Frequency);
    const uint NumSends{Device->NumAuxSends};

    const size_t num_channels{voice->mChans.size()};
    ASSUME(num_channels > 0);

    for(auto &chandata : voice->mChans)
    {
        chandata.mDryParams.Hrtf.Target = HrtfFilter{};
        chandata.mDryParams.Gains.Target.fill(0.0f);
        std::for_each(chandata.mWetParams.begin(), chandata.mWetParams.begin()+NumSends,
            [](SendParams &params) -> void { params.Gains.Target.fill(0.0f); });
    }

    const auto getChans = [props,&StereoMap](FmtChannels chanfmt) noexcept
        -> std::pair<DirectMode,al::span<const ChanPosMap>>
    {
        switch(chanfmt)
        {
        case FmtMono:
            /* Mono buffers are never played direct. */
            return {DirectMode::Off, al::span{MonoMap}};

        case FmtStereo:
            if(props->DirectChannels == DirectMode::Off)
            {
                for(size_t i{0};i < 2;++i)
                {
                    /* StereoPan is counter-clockwise in radians. */
                    const float a{props->StereoPan[i]};
                    StereoMap[i].pos[0] = -std::sin(a);
                    StereoMap[i].pos[2] = -std::cos(a);
                }
            }
            [[fallthrough]];
        case FmtMonoDup:
            return {props->DirectChannels, al::span{StereoMap}};
        case FmtRear: return {props->DirectChannels, al::span{RearMap}};
        case FmtQuad: return {props->DirectChannels, al::span{QuadMap}};
        case FmtX51: return {props->DirectChannels, al::span{X51Map}};
        case FmtX61: return {props->DirectChannels, al::span{X61Map}};
        case FmtX71: return {props->DirectChannels, al::span{X71Map}};

        case FmtBFormat2D:
        case FmtBFormat3D:
        case FmtUHJ2:
        case FmtUHJ3:
        case FmtUHJ4:
        case FmtSuperStereo:
            return {DirectMode::Off, {}};
        }
        return {props->DirectChannels, {}};
    };
    const auto [DirectChannels,chans] = getChans(voice->mFmtChannels);

    voice->mFlags.reset(VoiceHasHrtf).reset(VoiceHasNfc);
    if(auto *decoder{voice->mDecoder.get()})
        decoder->mWidthControl = std::min(props->EnhWidth, 0.7f);

    const float lpan{std::min(1.0f-props->Panning, 1.0f)};
    const float rpan{std::min(1.0f+props->Panning, 1.0f)};
    const float minpan{std::min(lpan, rpan)};
    auto SelectChannelPan = [lpan,rpan,minpan](const Channel chan) noexcept
    {
        switch(chan)
        {
        case FrontLeft: return lpan;
        case FrontRight: return rpan;
        case FrontCenter: break;
        case LFE: break;
        case BackLeft: return lpan;
        case BackRight: return rpan;
        case BackCenter: break;
        case SideLeft: return lpan;
        case SideRight: return rpan;
        case TopCenter: break;
        case TopFrontLeft: return lpan;
        case TopFrontCenter: break;
        case TopFrontRight: return rpan;
        case TopBackLeft: return lpan;
        case TopBackCenter: break;
        case TopBackRight: return rpan;
        case Aux0: case Aux1: case Aux2: case Aux3: case Aux4: case Aux5: case Aux6: case Aux7:
        case Aux8: case Aux9: case Aux10: case Aux11: case Aux12: case Aux13: case Aux14:
        case Aux15: case MaxChannels: break;
        }
        return minpan;
    };

    if(IsAmbisonic(voice->mFmtChannels))
    {
        /* Special handling for B-Format and UHJ sources. */

        if(Device->AvgSpeakerDist > 0.0f && voice->mFmtChannels != FmtUHJ2
            && voice->mFmtChannels != FmtSuperStereo)
        {
            if(!(Distance > std::numeric_limits<float>::epsilon()))
            {
                /* NOTE: The NFCtrlFilters were created with a w0 of 0, which
                 * is what we want for FOA input. The first channel may have
                 * been previously re-adjusted if panned, so reset it.
                 */
                voice->mChans[0].mDryParams.NFCtrlFilter.adjust(0.0f);
            }
            else
            {
                /* Clamp the distance for really close sources, to prevent
                 * excessive bass.
                 */
                const float mdist{std::max(Distance*NfcScale, Device->AvgSpeakerDist/4.0f)};
                const float w0{SpeedOfSoundMetersPerSec / (mdist * Frequency)};

                /* Only need to adjust the first channel of a B-Format source. */
                voice->mChans[0].mDryParams.NFCtrlFilter.adjust(w0);
            }

            voice->mFlags.set(VoiceHasNfc);
        }

        /* Panning a B-Format sound toward some direction is easy. Just pan the
         * first (W) channel as a normal mono sound. The angular spread is used
         * as a directional scalar to blend between full coverage and full
         * panning.
         */
        const float coverage{!(Distance > std::numeric_limits<float>::epsilon()) ? 1.0f :
            (al::numbers::inv_pi_v<float>/2.0f * Spread)};

        auto calc_coeffs = [xpos,ypos,zpos](RenderMode mode)
        {
            if(mode != RenderMode::Pairwise)
                return CalcDirectionCoeffs(std::array{xpos, ypos, zpos}, 0.0f);
            const auto pos = ScaleAzimuthFront3_2(std::array{xpos, ypos, zpos});
            return CalcDirectionCoeffs(pos, 0.0f);
        };
        const auto scales = GetAmbiScales(voice->mAmbiScaling);
        auto coeffs = calc_coeffs(Device->mRenderMode);

        if(!(coverage > 0.0f))
        {
            ComputePanGains(&Device->Dry, coeffs, DryGain.Base*scales[0],
                voice->mChans[0].mDryParams.Gains.Target);
            for(uint i{0};i < NumSends;i++)
            {
                if(const EffectSlot *Slot{SendSlots[i]})
                    ComputePanGains(&Slot->Wet, coeffs, WetGain[i].Base*scales[0],
                        voice->mChans[0].mWetParams[i].Gains.Target);
            }
        }
        else
        {
            /* Local B-Format sources have their XYZ channels rotated according
             * to the orientation.
             */
            /* AT then UP */
            alu::Vector N{props->OrientAt[0], props->OrientAt[1], props->OrientAt[2], 0.0f};
            N.normalize();
            alu::Vector V{props->OrientUp[0], props->OrientUp[1], props->OrientUp[2], 0.0f};
            V.normalize();
            if(!props->HeadRelative)
            {
                N = Context.Matrix * N;
                V = Context.Matrix * V;
            }
            /* Build and normalize right-vector */
            alu::Vector U{N.cross_product(V)};
            U.normalize();

            /* Build a rotation matrix. Manually fill the zeroth- and first-
             * order elements, then construct the rotation for the higher
             * orders.
             */
            AmbiRotateMatrix &shrot = Device->mAmbiRotateMatrix;
            shrot.fill(AmbiRotateMatrix::value_type{});

            shrot[0][0] = 1.0f;
            shrot[1][1] =  U[0]; shrot[1][2] = -U[1]; shrot[1][3] =  U[2];
            shrot[2][1] = -V[0]; shrot[2][2] =  V[1]; shrot[2][3] = -V[2];
            shrot[3][1] = -N[0]; shrot[3][2] =  N[1]; shrot[3][3] = -N[2];
            AmbiRotator(shrot, static_cast<int>(Device->mAmbiOrder));

            /* If the device is higher order than the voice, "upsample" the
             * matrix.
             *
             * NOTE: Starting with second-order, a 2D upsample needs to be
             * applied with a 2D source and 3D output, even when they're the
             * same order. This is because higher orders have a height offset
             * on various channels (i.e. when elevation=0, those height-related
             * channels should be non-0).
             */
            AmbiRotateMatrix &mixmatrix = Device->mAmbiRotateMatrix2;
            if(Device->mAmbiOrder > voice->mAmbiOrder
                || (Device->mAmbiOrder >= 2 && !Device->m2DMixing
                    && Is2DAmbisonic(voice->mFmtChannels)))
            {
                if(voice->mAmbiOrder == 1)
                {
                    const auto upsampler = Is2DAmbisonic(voice->mFmtChannels) ?
                        al::span{AmbiScale::FirstOrder2DUp} : al::span{AmbiScale::FirstOrderUp};
                    UpsampleBFormatTransform(mixmatrix, upsampler, shrot, Device->mAmbiOrder);
                }
                else if(voice->mAmbiOrder == 2)
                {
                    const auto upsampler = Is2DAmbisonic(voice->mFmtChannels) ?
                        al::span{AmbiScale::SecondOrder2DUp} : al::span{AmbiScale::SecondOrderUp};
                    UpsampleBFormatTransform(mixmatrix, upsampler, shrot, Device->mAmbiOrder);
                }
                else if(voice->mAmbiOrder == 3)
                {
                    const auto upsampler = Is2DAmbisonic(voice->mFmtChannels) ?
                        al::span{AmbiScale::ThirdOrder2DUp} : al::span{AmbiScale::ThirdOrderUp};
                    UpsampleBFormatTransform(mixmatrix, upsampler, shrot, Device->mAmbiOrder);
                }
                else if(voice->mAmbiOrder == 4)
                {
                    const auto upsampler = al::span{AmbiScale::FourthOrder2DUp};
                    UpsampleBFormatTransform(mixmatrix, upsampler, shrot, Device->mAmbiOrder);
                }
                else
                    al::unreachable();
            }
            else
                mixmatrix = shrot;

            /* Convert the rotation matrix for input ordering and scaling, and
             * whether input is 2D or 3D.
             */
            const uint8_t *index_map{Is2DAmbisonic(voice->mFmtChannels) ?
                GetAmbi2DLayout(voice->mAmbiLayout).data() :
                GetAmbiLayout(voice->mAmbiLayout).data()};

            /* Scale the panned W signal inversely to coverage (full coverage
             * means no panned signal), and according to the channel scaling.
             */
            std::for_each(coeffs.begin(), coeffs.end(),
                [scale=(1.0f-coverage)*scales[0]](float &coeff) noexcept { coeff *= scale; });

            for(size_t c{0};c < num_channels;c++)
            {
                const size_t acn{index_map[c]};
                const float scale{scales[acn] * coverage};

                /* For channel 0, combine the B-Format signal (scaled according
                 * to the coverage amount) with the directional pan. For all
                 * other channels, use just the (scaled) B-Format signal.
                 */
                for(size_t x{0};x < MaxAmbiChannels;++x)
                    coeffs[x] += mixmatrix[acn][x] * scale;

                ComputePanGains(&Device->Dry, coeffs, DryGain.Base,
                    voice->mChans[c].mDryParams.Gains.Target);

                for(uint i{0};i < NumSends;i++)
                {
                    if(const EffectSlot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i].Base,
                            voice->mChans[c].mWetParams[i].Gains.Target);
                }

                coeffs = std::array<float,MaxAmbiChannels>{};
            }
        }
    }
    else if(DirectChannels != DirectMode::Off && !Device->RealOut.RemixMap.empty())
    {
        /* Direct source channels always play local. Skip the virtual channels
         * and write inputs to the matching real outputs.
         */
        voice->mDirect.Buffer = Device->RealOut.Buffer;

        for(size_t c{0};c < num_channels;c++)
        {
            const float pangain{SelectChannelPan(chans[c].channel)};
            if(uint idx{Device->channelIdxByName(chans[c].channel)}; idx != InvalidChannelIndex)
                voice->mChans[c].mDryParams.Gains.Target[idx] = DryGain.Base * pangain;
            else if(DirectChannels == DirectMode::RemixMismatch)
            {
                auto match_channel = [channel=chans[c].channel](const InputRemixMap &map) noexcept
                { return channel == map.channel; };
                auto remap = std::find_if(Device->RealOut.RemixMap.cbegin(),
                    Device->RealOut.RemixMap.cend(), match_channel);
                if(remap != Device->RealOut.RemixMap.cend())
                {
                    for(const auto &target : remap->targets)
                    {
                        idx = Device->channelIdxByName(target.channel);
                        if(idx != InvalidChannelIndex)
                            voice->mChans[c].mDryParams.Gains.Target[idx] = DryGain.Base * pangain
                                * target.mix;
                    }
                }
            }
        }

        /* Auxiliary sends still use normal channel panning since they mix to
         * B-Format, which can't channel-match.
         */
        for(size_t c{0};c < num_channels;c++)
        {
            /* Skip LFE */
            if(chans[c].channel == LFE)
                continue;

            const float pangain{SelectChannelPan(chans[c].channel)};
            const auto coeffs = CalcDirectionCoeffs(chans[c].pos, 0.0f);

            for(uint i{0};i < NumSends;i++)
            {
                if(const EffectSlot *Slot{SendSlots[i]})
                    ComputePanGains(&Slot->Wet, coeffs, WetGain[i].Base * pangain,
                        voice->mChans[c].mWetParams[i].Gains.Target);
            }
        }
    }
    else if(Device->mRenderMode == RenderMode::Hrtf)
    {
        /* Full HRTF rendering. Skip the virtual channels and render to the
         * real outputs.
         */
        voice->mDirect.Buffer = Device->RealOut.Buffer;

        if(Distance > std::numeric_limits<float>::epsilon())
        {
            if(voice->mFmtChannels == FmtMono)
            {
                const float src_ev{std::asin(std::clamp(ypos, -1.0f, 1.0f))};
                const float src_az{std::atan2(xpos, -zpos)};

                Device->mHrtf->getCoeffs(src_ev, src_az, Distance*NfcScale, Spread,
                    voice->mChans[0].mDryParams.Hrtf.Target.Coeffs,
                    voice->mChans[0].mDryParams.Hrtf.Target.Delay);
                voice->mChans[0].mDryParams.Hrtf.Target.Gain = DryGain.Base;

                const auto coeffs = CalcDirectionCoeffs(std::array{xpos, ypos, zpos}, Spread);
                for(uint i{0};i < NumSends;i++)
                {
                    if(const EffectSlot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i].Base,
                            voice->mChans[0].mWetParams[i].Gains.Target);
                }
            }
            else for(size_t c{0};c < num_channels;c++)
            {
                using namespace al::numbers;

                /* Skip LFE */
                if(chans[c].channel == LFE) continue;
                const float pangain{SelectChannelPan(chans[c].channel)};

                /* Warp the channel position toward the source position as the
                 * source spread decreases. With no spread, all channels are at
                 * the source position, at full spread (pi*2), each channel is
                 * left unchanged.
                 */
                const float a{1.0f - (inv_pi_v<float>/2.0f)*Spread};
                std::array pos{
                    lerpf(chans[c].pos[0], xpos, a),
                    lerpf(chans[c].pos[1], ypos, a),
                    lerpf(chans[c].pos[2], zpos, a)};
                const float len{std::sqrt(pos[0]*pos[0] + pos[1]*pos[1] + pos[2]*pos[2])};
                if(len < 1.0f)
                {
                    pos[0] /= len;
                    pos[1] /= len;
                    pos[2] /= len;
                }

                const float ev{std::asin(std::clamp(pos[1], -1.0f, 1.0f))};
                const float az{std::atan2(pos[0], -pos[2])};

                Device->mHrtf->getCoeffs(ev, az, Distance*NfcScale, 0.0f,
                    voice->mChans[c].mDryParams.Hrtf.Target.Coeffs,
                    voice->mChans[c].mDryParams.Hrtf.Target.Delay);
                voice->mChans[c].mDryParams.Hrtf.Target.Gain = DryGain.Base * pangain;

                const auto coeffs = CalcDirectionCoeffs(pos, 0.0f);
                for(uint i{0};i < NumSends;i++)
                {
                    if(const EffectSlot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i].Base * pangain,
                            voice->mChans[c].mWetParams[i].Gains.Target);
                }
            }
        }
        else
        {
            /* With no distance, spread is only meaningful for mono sources
             * where it can be 0 or full (non-mono sources are always full
             * spread here).
             */
            const float spread{Spread * float(voice->mFmtChannels == FmtMono)};

            /* Local sources on HRTF play with each channel panned to its
             * relative location around the listener, providing "virtual
             * speaker" responses.
             */
            for(size_t c{0};c < num_channels;c++)
            {
                /* Skip LFE */
                if(chans[c].channel == LFE)
                    continue;
                const float pangain{SelectChannelPan(chans[c].channel)};

                /* Get the HRIR coefficients and delays for this channel
                 * position.
                 */
                const float ev{std::asin(chans[c].pos[1])};
                const float az{std::atan2(chans[c].pos[0], -chans[c].pos[2])};

                Device->mHrtf->getCoeffs(ev, az, std::numeric_limits<float>::infinity(), spread,
                    voice->mChans[c].mDryParams.Hrtf.Target.Coeffs,
                    voice->mChans[c].mDryParams.Hrtf.Target.Delay);
                voice->mChans[c].mDryParams.Hrtf.Target.Gain = DryGain.Base * pangain;

                /* Normal panning for auxiliary sends. */
                const auto coeffs = CalcDirectionCoeffs(chans[c].pos, spread);

                for(uint i{0};i < NumSends;i++)
                {
                    if(const EffectSlot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i].Base * pangain,
                            voice->mChans[c].mWetParams[i].Gains.Target);
                }
            }
        }

        voice->mFlags.set(VoiceHasHrtf);
    }
    else
    {
        /* Non-HRTF rendering. Use normal panning to the output. */

        if(Distance > std::numeric_limits<float>::epsilon())
        {
            /* Calculate NFC filter coefficient if needed. */
            if(Device->AvgSpeakerDist > 0.0f)
            {
                /* Clamp the distance for really close sources, to prevent
                 * excessive bass.
                 */
                const float mdist{std::max(Distance*NfcScale, Device->AvgSpeakerDist/4.0f)};
                const float w0{SpeedOfSoundMetersPerSec / (mdist * Frequency)};

                /* Adjust NFC filters. */
                for(size_t c{0};c < num_channels;c++)
                    voice->mChans[c].mDryParams.NFCtrlFilter.adjust(w0);

                voice->mFlags.set(VoiceHasNfc);
            }

            if(voice->mFmtChannels == FmtMono)
            {
                auto calc_coeffs = [xpos,ypos,zpos,Spread](RenderMode mode)
                {
                    if(mode != RenderMode::Pairwise)
                        return CalcDirectionCoeffs(std::array{xpos, ypos, zpos}, Spread);
                    const auto pos = ScaleAzimuthFront3_2(std::array{xpos, ypos, zpos});
                    return CalcDirectionCoeffs(pos, Spread);
                };
                const auto coeffs = calc_coeffs(Device->mRenderMode);

                ComputePanGains(&Device->Dry, coeffs, DryGain.Base,
                    voice->mChans[0].mDryParams.Gains.Target);
                for(uint i{0};i < NumSends;i++)
                {
                    if(const EffectSlot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i].Base,
                            voice->mChans[0].mWetParams[i].Gains.Target);
                }
            }
            else
            {
                using namespace al::numbers;

                for(size_t c{0};c < num_channels;c++)
                {
                    const float pangain{SelectChannelPan(chans[c].channel)};

                    /* Special-case LFE */
                    if(chans[c].channel == LFE)
                    {
                        if(Device->Dry.Buffer.data() == Device->RealOut.Buffer.data())
                        {
                            const uint idx{Device->channelIdxByName(chans[c].channel)};
                            if(idx != InvalidChannelIndex)
                                voice->mChans[c].mDryParams.Gains.Target[idx] = DryGain.Base
                                    * pangain;
                        }
                        continue;
                    }

                    /* Warp the channel position toward the source position as
                     * the spread decreases. With no spread, all channels are
                     * at the source position, at full spread (pi*2), each
                     * channel position is left unchanged.
                     */
                    const float a{1.0f - (inv_pi_v<float>/2.0f)*Spread};
                    std::array pos{
                        lerpf(chans[c].pos[0], xpos, a),
                        lerpf(chans[c].pos[1], ypos, a),
                        lerpf(chans[c].pos[2], zpos, a)};
                    const float len{std::sqrt(pos[0]*pos[0] + pos[1]*pos[1] + pos[2]*pos[2])};
                    if(len < 1.0f)
                    {
                        pos[0] /= len;
                        pos[1] /= len;
                        pos[2] /= len;
                    }

                    if(Device->mRenderMode == RenderMode::Pairwise)
                        pos = ScaleAzimuthFront3(pos);
                    const auto coeffs = CalcDirectionCoeffs(pos, 0.0f);

                    ComputePanGains(&Device->Dry, coeffs, DryGain.Base * pangain,
                        voice->mChans[c].mDryParams.Gains.Target);
                    for(uint i{0};i < NumSends;i++)
                    {
                        if(const EffectSlot *Slot{SendSlots[i]})
                            ComputePanGains(&Slot->Wet, coeffs, WetGain[i].Base * pangain,
                                voice->mChans[c].mWetParams[i].Gains.Target);
                    }
                }
            }
        }
        else
        {
            if(Device->AvgSpeakerDist > 0.0f)
            {
                /* If the source distance is 0, simulate a plane-wave by using
                 * infinite distance, which results in a w0 of 0.
                 */
                static constexpr float w0{0.0f};
                for(size_t c{0};c < num_channels;c++)
                    voice->mChans[c].mDryParams.NFCtrlFilter.adjust(w0);

                voice->mFlags.set(VoiceHasNfc);
            }

            /* With no distance, spread is only meaningful for mono sources
             * where it can be 0 or full (non-mono sources are always full
             * spread here).
             */
            const float spread{Spread * float(voice->mFmtChannels == FmtMono)};
            for(size_t c{0};c < num_channels;c++)
            {
                const float pangain{SelectChannelPan(chans[c].channel)};

                /* Special-case LFE */
                if(chans[c].channel == LFE)
                {
                    if(Device->Dry.Buffer.data() == Device->RealOut.Buffer.data())
                    {
                        const uint idx{Device->channelIdxByName(chans[c].channel)};
                        if(idx != InvalidChannelIndex)
                            voice->mChans[c].mDryParams.Gains.Target[idx] = DryGain.Base * pangain;
                    }
                    continue;
                }

                const auto coeffs = CalcDirectionCoeffs((Device->mRenderMode==RenderMode::Pairwise)
                    ? ScaleAzimuthFront3(chans[c].pos) : chans[c].pos, spread);

                ComputePanGains(&Device->Dry, coeffs, DryGain.Base * pangain,
                    voice->mChans[c].mDryParams.Gains.Target);
                for(uint i{0};i < NumSends;i++)
                {
                    if(const EffectSlot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i].Base * pangain,
                            voice->mChans[c].mWetParams[i].Gains.Target);
                }
            }
        }
    }

    {
        const float hfNorm{props->Direct.HFReference / Frequency};
        const float lfNorm{props->Direct.LFReference / Frequency};

        voice->mDirect.FilterType = AF_None;
        if(DryGain.HF != 1.0f) voice->mDirect.FilterType |= AF_LowPass;
        if(DryGain.LF != 1.0f) voice->mDirect.FilterType |= AF_HighPass;

        auto &lowpass = voice->mChans[0].mDryParams.LowPass;
        auto &highpass = voice->mChans[0].mDryParams.HighPass;
        lowpass.setParamsFromSlope(BiquadType::HighShelf, hfNorm, DryGain.HF, 1.0f);
        highpass.setParamsFromSlope(BiquadType::LowShelf, lfNorm, DryGain.LF, 1.0f);
        for(size_t c{1};c < num_channels;c++)
        {
            voice->mChans[c].mDryParams.LowPass.copyParamsFrom(lowpass);
            voice->mChans[c].mDryParams.HighPass.copyParamsFrom(highpass);
        }
    }
    for(uint i{0};i < NumSends;i++)
    {
        const float hfNorm{props->Send[i].HFReference / Frequency};
        const float lfNorm{props->Send[i].LFReference / Frequency};

        voice->mSend[i].FilterType = AF_None;
        if(WetGain[i].HF != 1.0f) voice->mSend[i].FilterType |= AF_LowPass;
        if(WetGain[i].LF != 1.0f) voice->mSend[i].FilterType |= AF_HighPass;

        auto &lowpass = voice->mChans[0].mWetParams[i].LowPass;
        auto &highpass = voice->mChans[0].mWetParams[i].HighPass;
        lowpass.setParamsFromSlope(BiquadType::HighShelf, hfNorm, WetGain[i].HF, 1.0f);
        highpass.setParamsFromSlope(BiquadType::LowShelf, lfNorm, WetGain[i].LF, 1.0f);
        for(size_t c{1};c < num_channels;c++)
        {
            voice->mChans[c].mWetParams[i].LowPass.copyParamsFrom(lowpass);
            voice->mChans[c].mWetParams[i].HighPass.copyParamsFrom(highpass);
        }
    }
}

void CalcNonAttnSourceParams(Voice *voice, const VoiceProps *props, const ContextBase *context)
{
    DeviceBase *Device{context->mDevice};
    std::array<EffectSlot*,MaxSendCount> SendSlots;

    voice->mDirect.Buffer = Device->Dry.Buffer;
    for(uint i{0};i < Device->NumAuxSends;i++)
    {
        SendSlots[i] = props->Send[i].Slot;
        if(!SendSlots[i] || SendSlots[i]->EffectType == EffectSlotType::None)
        {
            SendSlots[i] = nullptr;
            voice->mSend[i].Buffer = {};
        }
        else
            voice->mSend[i].Buffer = SendSlots[i]->Wet.Buffer;
    }

    /* Calculate the stepping value */
    const auto Pitch = static_cast<float>(voice->mFrequency) /
        static_cast<float>(Device->Frequency) * props->Pitch;
    if(Pitch > float{MaxPitch})
        voice->mStep = MaxPitch<<MixerFracBits;
    else
        voice->mStep = std::max(fastf2u(Pitch * MixerFracOne), 1u);
    voice->mResampler = PrepareResampler(props->mResampler, voice->mStep, &voice->mResampleState);

    /* Calculate gains */
    GainTriplet DryGain;
    DryGain.Base  = std::min(std::clamp(props->Gain, props->MinGain, props->MaxGain) *
        props->Direct.Gain * context->mParams.Gain, GainMixMax);
    DryGain.HF = props->Direct.GainHF;
    DryGain.LF = props->Direct.GainLF;

    std::array<GainTriplet,MaxSendCount> WetGain;
    for(uint i{0};i < Device->NumAuxSends;i++)
    {
        WetGain[i].Base = std::min(std::clamp(props->Gain, props->MinGain, props->MaxGain) *
            props->Send[i].Gain * context->mParams.Gain, GainMixMax);
        WetGain[i].HF = props->Send[i].GainHF;
        WetGain[i].LF = props->Send[i].GainLF;
    }

    CalcPanningAndFilters(voice, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, DryGain, WetGain, SendSlots, props,
        context->mParams, Device);
}

void CalcAttnSourceParams(Voice *voice, const VoiceProps *props, const ContextBase *context)
{
    DeviceBase *Device{context->mDevice};
    const uint NumSends{Device->NumAuxSends};

    /* Set mixing buffers and get send parameters. */
    voice->mDirect.Buffer = Device->Dry.Buffer;
    std::array<EffectSlot*,MaxSendCount> SendSlots{};
    std::array<float,MaxSendCount> RoomRolloff{};
    std::bitset<MaxSendCount> UseDryAttnForRoom{0};
    for(uint i{0};i < NumSends;i++)
    {
        SendSlots[i] = props->Send[i].Slot;
        if(!SendSlots[i] || SendSlots[i]->EffectType == EffectSlotType::None)
            SendSlots[i] = nullptr;
        else if(SendSlots[i]->AuxSendAuto)
        {
            /* NOTE: Contrary to the EFX docs, the effect's room rolloff factor
             * applies to the selected distance model along with the source's
             * room rolloff factor, not necessarily the inverse distance model.
             *
             * Generic Software also applies these rolloff factors regardless
             * of any setting. It doesn't seem to use the effect slot's send
             * auto for anything, though as far as I understand, it's supposed
             * to control whether the send gets the same gain/gainhf as the
             * direct path (excluding the filter).
             */
            RoomRolloff[i] = props->RoomRolloffFactor + SendSlots[i]->RoomRolloff;
        }
        else
            UseDryAttnForRoom.set(i);

        if(!SendSlots[i])
            voice->mSend[i].Buffer = {};
        else
            voice->mSend[i].Buffer = SendSlots[i]->Wet.Buffer;
    }

    /* Transform source to listener space (convert to head relative) */
    alu::Vector Position{props->Position[0], props->Position[1], props->Position[2], 1.0f};
    alu::Vector Velocity{props->Velocity[0], props->Velocity[1], props->Velocity[2], 0.0f};
    alu::Vector Direction{props->Direction[0], props->Direction[1], props->Direction[2], 0.0f};
    if(!props->HeadRelative)
    {
        /* Transform source vectors */
        Position = context->mParams.Matrix * (Position - context->mParams.Position);
        Velocity = context->mParams.Matrix * Velocity;
        Direction = context->mParams.Matrix * Direction;
    }
    else
    {
        /* Offset the source velocity to be relative of the listener velocity */
        Velocity += context->mParams.Velocity;
    }

    const bool directional{Direction.normalize() > 0.0f};
    alu::Vector ToSource{Position[0], Position[1], Position[2], 0.0f};
    const float Distance{ToSource.normalize()};

    /* Calculate distance attenuation */
    float ClampedDist{Distance};
    float DryGainBase{props->Gain};
    std::array<float,MaxSendCount> WetGainBase{};
    WetGainBase.fill(props->Gain);

    float DryAttnBase{1.0f};
    switch(context->mParams.SourceDistanceModel ? props->mDistanceModel
        : context->mParams.mDistanceModel)
    {
    case DistanceModel::InverseClamped:
        if(props->MaxDistance < props->RefDistance) break;
        ClampedDist = std::clamp(ClampedDist, props->RefDistance, props->MaxDistance);
        /*fall-through*/
    case DistanceModel::Inverse:
        if(props->RefDistance > 0.0f)
        {
            float dist{lerpf(props->RefDistance, ClampedDist, props->RolloffFactor)};
            if(dist > 0.0f)
            {
                DryAttnBase = props->RefDistance / dist;
                DryGainBase *= DryAttnBase;
            }

            for(size_t i{0};i < NumSends;++i)
            {
                dist = lerpf(props->RefDistance, ClampedDist, RoomRolloff[i]);
                if(dist > 0.0f) WetGainBase[i] *= props->RefDistance / dist;
            }
        }
        break;

    case DistanceModel::LinearClamped:
        if(props->MaxDistance < props->RefDistance) break;
        ClampedDist = std::clamp(ClampedDist, props->RefDistance, props->MaxDistance);
        /*fall-through*/
    case DistanceModel::Linear:
        if(props->MaxDistance != props->RefDistance)
        {
            float attn{(ClampedDist-props->RefDistance) /
                (props->MaxDistance-props->RefDistance) * props->RolloffFactor};
            DryAttnBase = std::max(1.0f - attn, 0.0f);
            DryGainBase *= DryAttnBase;

            for(size_t i{0};i < NumSends;++i)
            {
                attn = (ClampedDist-props->RefDistance) /
                    (props->MaxDistance-props->RefDistance) * RoomRolloff[i];
                WetGainBase[i] *= std::max(1.0f - attn, 0.0f);
            }
        }
        break;

    case DistanceModel::ExponentClamped:
        if(props->MaxDistance < props->RefDistance) break;
        ClampedDist = std::clamp(ClampedDist, props->RefDistance, props->MaxDistance);
        /*fall-through*/
    case DistanceModel::Exponent:
        if(ClampedDist > 0.0f && props->RefDistance > 0.0f)
        {
            const float dist_ratio{ClampedDist/props->RefDistance};
            DryAttnBase = std::pow(dist_ratio, -props->RolloffFactor);
            DryGainBase *= DryAttnBase;
            for(size_t i{0};i < NumSends;++i)
                WetGainBase[i] *= std::pow(dist_ratio, -RoomRolloff[i]);
        }
        break;

    case DistanceModel::Disable:
        break;
    }

    /* Calculate directional soundcones */
    float ConeHF{1.0f}, WetCone{1.0f}, WetConeHF{1.0f};
    if(directional && props->InnerAngle < 360.0f)
    {
        static constexpr float Rad2Deg{static_cast<float>(180.0 / al::numbers::pi)};
        const float Angle{Rad2Deg*2.0f * std::acos(-Direction.dot_product(ToSource)) * ConeScale};

        float ConeGain{1.0f};
        if(Angle >= props->OuterAngle)
        {
            ConeGain = props->OuterGain;
            if(props->DryGainHFAuto)
                ConeHF = props->OuterGainHF;
        }
        else if(Angle >= props->InnerAngle)
        {
            const float scale{(Angle-props->InnerAngle) / (props->OuterAngle-props->InnerAngle)};
            ConeGain = lerpf(1.0f, props->OuterGain, scale);
            if(props->DryGainHFAuto)
                ConeHF = lerpf(1.0f, props->OuterGainHF, scale);
        }

        DryGainBase *= ConeGain;
        if(props->WetGainAuto)
            WetCone = ConeGain;
        if(props->WetGainHFAuto)
            WetConeHF = ConeHF;
    }

    /* Apply gain and frequency filters */
    GainTriplet DryGain{};
    DryGainBase = std::clamp(DryGainBase, props->MinGain, props->MaxGain) * context->mParams.Gain;
    DryGain.Base = std::min(DryGainBase * props->Direct.Gain, GainMixMax);
    DryGain.HF = ConeHF * props->Direct.GainHF;
    DryGain.LF = props->Direct.GainLF;

    std::array<GainTriplet,MaxSendCount> WetGain{};
    for(uint i{0};i < NumSends;i++)
    {
        WetGainBase[i] = std::clamp(WetGainBase[i]*WetCone, props->MinGain, props->MaxGain) *
            context->mParams.Gain;
        /* If this effect slot's Auxiliary Send Auto is off, then use the dry
         * path distance and cone attenuation, otherwise use the wet (room)
         * path distance and cone attenuation. The send filter is used instead
         * of the direct filter, regardless.
         */
        const bool use_room{!UseDryAttnForRoom.test(i)};
        const float gain{use_room ? WetGainBase[i] : DryGainBase};
        WetGain[i].Base = std::min(gain * props->Send[i].Gain, GainMixMax);
        WetGain[i].HF = (use_room ? WetConeHF : ConeHF) * props->Send[i].GainHF;
        WetGain[i].LF = props->Send[i].GainLF;
    }

    /* Distance-based air absorption and initial send decay. */
    if(Distance > props->RefDistance) LIKELY
    {
        const float distance_base{(Distance-props->RefDistance) * props->RolloffFactor};
        const float distance_meters{distance_base * context->mParams.MetersPerUnit};
        const float dryabsorb{distance_meters * props->AirAbsorptionFactor};
        if(dryabsorb > std::numeric_limits<float>::epsilon())
            DryGain.HF *= std::pow(context->mParams.AirAbsorptionGainHF, dryabsorb);

        /* If the source's Auxiliary Send Filter Gain Auto is off, no extra
         * adjustment is applied to the send gains.
         */
        for(uint i{props->WetGainAuto ? 0u : NumSends};i < NumSends;++i)
        {
            if(!SendSlots[i] || !(SendSlots[i]->DecayTime > 0.0f))
                continue;

            if(distance_meters > std::numeric_limits<float>::epsilon())
                WetGain[i].HF *= std::pow(SendSlots[i]->AirAbsorptionGainHF, distance_meters);

            /* If this effect slot's Auxiliary Send Auto is off, don't apply
             * the automatic initial reverb decay.
             *
             * NOTE: Generic Software applies the initial decay regardless of
             * this setting. It doesn't seem to use it for anything, only the
             * source's send filter gain auto flag affects this.
             */
            if(!SendSlots[i]->AuxSendAuto)
                continue;

            const float DecayDistance{SendSlots[i]->DecayTime * SpeedOfSoundMetersPerSec};

            /* Apply a decay-time transformation to the wet path, based on the
             * source distance. The initial decay of the reverb effect is
             * calculated and applied to the wet path.
             *
             * FIXME: This is very likely not correct. It more likely should
             * work by calculating a rolloff dynamically based on the reverb
             * parameters (and source distance?) and add it to the room rolloff
             * with the reverb and source rolloff parameters.
             */
            const float baseAttn{DryAttnBase};
            const float fact{distance_base / DecayDistance};
            const float gain{std::pow(ReverbDecayGain, fact)*(1.0f-baseAttn) + baseAttn};
            WetGain[i].Base *= gain;
        }
    }


    /* Initial source pitch */
    float Pitch{props->Pitch};

    /* Calculate velocity-based doppler effect */
    float DopplerFactor{props->DopplerFactor * context->mParams.DopplerFactor};
    if(DopplerFactor > 0.0f)
    {
        const alu::Vector &lvelocity = context->mParams.Velocity;
        float vss{Velocity.dot_product(ToSource) * -DopplerFactor};
        float vls{lvelocity.dot_product(ToSource) * -DopplerFactor};

        const float SpeedOfSound{context->mParams.SpeedOfSound};
        if(!(vls < SpeedOfSound))
        {
            /* Listener moving away from the source at the speed of sound.
             * Sound waves can't catch it.
             */
            Pitch = 0.0f;
        }
        else if(!(vss < SpeedOfSound))
        {
            /* Source moving toward the listener at the speed of sound. Sound
             * waves bunch up to extreme frequencies.
             */
            Pitch = std::numeric_limits<float>::infinity();
        }
        else
        {
            /* Source and listener movement is nominal. Calculate the proper
             * doppler shift.
             */
            Pitch *= (SpeedOfSound-vls) / (SpeedOfSound-vss);
        }
    }

    /* Adjust pitch based on the buffer and output frequencies, and calculate
     * fixed-point stepping value.
     */
    Pitch *= static_cast<float>(voice->mFrequency) / static_cast<float>(Device->Frequency);
    if(Pitch > float{MaxPitch})
        voice->mStep = MaxPitch<<MixerFracBits;
    else
        voice->mStep = std::max(fastf2u(Pitch * MixerFracOne), 1u);
    voice->mResampler = PrepareResampler(props->mResampler, voice->mStep, &voice->mResampleState);

    float spread{0.0f};
    if(props->Radius > Distance)
        spread = al::numbers::pi_v<float>*2.0f - Distance/props->Radius*al::numbers::pi_v<float>;
    else if(Distance > 0.0f)
        spread = std::asin(props->Radius/Distance) * 2.0f;

    CalcPanningAndFilters(voice, ToSource[0]*XScale, ToSource[1]*YScale, ToSource[2]*ZScale,
        Distance, spread, DryGain, WetGain, SendSlots, props, context->mParams, Device);
}

void CalcSourceParams(Voice *voice, ContextBase *context, bool force)
{
    VoicePropsItem *props{voice->mUpdate.exchange(nullptr, std::memory_order_acq_rel)};
    if(!props && !force) return;

    if(props)
    {
        voice->mProps = static_cast<VoiceProps&>(*props);

        AtomicReplaceHead(context->mFreeVoiceProps, props);
    }

    if((voice->mProps.DirectChannels != DirectMode::Off && voice->mFmtChannels != FmtMono
            && !IsAmbisonic(voice->mFmtChannels))
        || voice->mProps.mSpatializeMode == SpatializeMode::Off
        || (voice->mProps.mSpatializeMode==SpatializeMode::Auto && voice->mFmtChannels != FmtMono))
        CalcNonAttnSourceParams(voice, &voice->mProps, context);
    else
        CalcAttnSourceParams(voice, &voice->mProps, context);
}


void SendSourceStateEvent(ContextBase *context, uint id, VChangeState state)
{
    RingBuffer *ring{context->mAsyncEvents.get()};
    auto evt_vec = ring->getWriteVector();
    if(evt_vec.first.len < 1) return;

    auto &evt = InitAsyncEvent<AsyncSourceStateEvent>(evt_vec.first.buf);
    evt.mId = id;
    switch(state)
    {
    case VChangeState::Reset:
        evt.mState = AsyncSrcState::Reset;
        break;
    case VChangeState::Stop:
        evt.mState = AsyncSrcState::Stop;
        break;
    case VChangeState::Play:
        evt.mState = AsyncSrcState::Play;
        break;
    case VChangeState::Pause:
        evt.mState = AsyncSrcState::Pause;
        break;
    /* Shouldn't happen. */
    case VChangeState::Restart:
        al::unreachable();
    }

    ring->writeAdvance(1);
}

void ProcessVoiceChanges(ContextBase *ctx)
{
    VoiceChange *cur{ctx->mCurrentVoiceChange.load(std::memory_order_acquire)};
    VoiceChange *next{cur->mNext.load(std::memory_order_acquire)};
    if(!next) return;

    const auto enabledevt = ctx->mEnabledEvts.load(std::memory_order_acquire);
    do {
        cur = next;

        bool sendevt{false};
        if(cur->mState == VChangeState::Reset || cur->mState == VChangeState::Stop)
        {
            if(Voice *voice{cur->mVoice})
            {
                voice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
                voice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
                /* A source ID indicates the voice was playing or paused, which
                 * gets a reset/stop event.
                 */
                sendevt = voice->mSourceID.exchange(0u, std::memory_order_relaxed) != 0u;
                Voice::State oldvstate{Voice::Playing};
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
            Voice *voice{cur->mVoice};
            Voice::State oldvstate{Voice::Playing};
            sendevt = voice->mPlayState.compare_exchange_strong(oldvstate, Voice::Stopping,
                std::memory_order_release, std::memory_order_acquire);
        }
        else if(cur->mState == VChangeState::Play)
        {
            /* NOTE: When playing a voice, sending a source state change event
             * depends if there's an old voice to stop and if that stop is
             * successful. If there is no old voice, a playing event is always
             * sent. If there is an old voice, an event is sent only if the
             * voice is already stopped.
             */
            if(Voice *oldvoice{cur->mOldVoice})
            {
                oldvoice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
                oldvoice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
                oldvoice->mSourceID.store(0u, std::memory_order_relaxed);
                Voice::State oldvstate{Voice::Playing};
                sendevt = !oldvoice->mPlayState.compare_exchange_strong(oldvstate, Voice::Stopping,
                    std::memory_order_relaxed, std::memory_order_acquire);
                oldvoice->mPendingChange.store(false, std::memory_order_release);
            }
            else
                sendevt = true;

            Voice *voice{cur->mVoice};
            voice->mPlayState.store(Voice::Playing, std::memory_order_release);
        }
        else if(cur->mState == VChangeState::Restart)
        {
            /* Restarting a voice never sends a source change event. */
            Voice *oldvoice{cur->mOldVoice};
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
                Voice::State oldvstate{Voice::Playing};
                oldvoice->mPlayState.compare_exchange_strong(oldvstate, Voice::Stopping,
                    std::memory_order_relaxed, std::memory_order_acquire);

                Voice *voice{cur->mVoice};
                voice->mPlayState.store((oldvstate == Voice::Playing) ? Voice::Playing
                    : Voice::Stopped, std::memory_order_release);
            }
            oldvoice->mPendingChange.store(false, std::memory_order_release);
        }
        if(sendevt && enabledevt.test(al::to_underlying(AsyncEnableBits::SourceState)))
            SendSourceStateEvent(ctx, cur->mSourceID, cur->mState);

        next = cur->mNext.load(std::memory_order_acquire);
    } while(next);
    ctx->mCurrentVoiceChange.store(cur, std::memory_order_release);
}

void ProcessParamUpdates(ContextBase *ctx, const al::span<EffectSlot*> slots,
    const al::span<Voice*> voices)
{
    ProcessVoiceChanges(ctx);

    IncrementRef(ctx->mUpdateCount);
    if(!ctx->mHoldUpdates.load(std::memory_order_acquire)) LIKELY
    {
        bool force{CalcContextParams(ctx)};
        auto sorted_slots = al::to_address(slots.end());
        for(EffectSlot *slot : slots)
            force |= CalcEffectSlotParams(slot, sorted_slots, ctx);

        for(Voice *voice : voices)
        {
            /* Only update voices that have a source. */
            if(voice->mSourceID.load(std::memory_order_relaxed) != 0)
                CalcSourceParams(voice, ctx, force);
        }
    }
    IncrementRef(ctx->mUpdateCount);
}

void ProcessContexts(DeviceBase *device, const uint SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    const nanoseconds curtime{device->mClockBase.load(std::memory_order_relaxed) +
        nanoseconds{seconds{device->mSamplesDone.load(std::memory_order_relaxed)}}/
        device->Frequency};

    for(ContextBase *ctx : *device->mContexts.load(std::memory_order_acquire))
    {
        auto auxslots = al::span{*ctx->mActiveAuxSlots.load(std::memory_order_acquire)};
        const al::span<Voice*> voices{ctx->getVoicesSpanAcquired()};

        /* Process pending property updates for objects on the context. */
        ProcessParamUpdates(ctx, auxslots, voices);

        /* Clear auxiliary effect slot mixing buffers. */
        for(EffectSlot *slot : auxslots)
        {
            for(auto &buffer : slot->Wet.Buffer)
                buffer.fill(0.0f);
        }

        /* Process voices that have a playing source. */
        for(Voice *voice : voices)
        {
            const Voice::State vstate{voice->mPlayState.load(std::memory_order_acquire)};
            if(vstate != Voice::Stopped && vstate != Voice::Pending)
                voice->mix(vstate, ctx, curtime, SamplesToDo);
        }

        /* Process effects. */
        if(!auxslots.empty())
        {
            /* Sort the slots into extra storage, so that effect slots come
             * before their effect slot target (or their targets' target).
             */
            const al::span sorted_slots{al::to_address(auxslots.end()), auxslots.size()};
            /* Skip sorting if it has already been done. */
            if(!sorted_slots[0])
            {
                /* First, copy the slots to the sorted list, then partition the
                 * sorted list so that all slots without a target slot go to
                 * the end.
                 */
                std::copy(auxslots.begin(), auxslots.end(), sorted_slots.begin());
                auto split_point = std::partition(sorted_slots.begin(), sorted_slots.end(),
                    [](const EffectSlot *slot) noexcept -> bool
                    { return slot->Target != nullptr; });
                /* There must be at least one slot without a slot target. */
                assert(split_point != sorted_slots.end());

                /* Simple case: no more than 1 slot has a target slot. Either
                 * all slots go right to the output, or the remaining one must
                 * target an already-partitioned slot.
                 */
                if(split_point - sorted_slots.begin() > 1)
                {
                    /* At least two slots target other slots. Starting from the
                     * back of the sorted list, continue partitioning the front
                     * of the list given each target until all targets are
                     * accounted for. This ensures all slots without a target
                     * go last, all slots directly targeting those last slots
                     * go second-to-last, all slots directly targeting those
                     * second-last slots go third-to-last, etc.
                     */
                    auto next_target = sorted_slots.end();
                    do {
                        /* This shouldn't happen, but if there's unsorted slots
                         * left that don't target any sorted slots, they can't
                         * contribute to the output, so leave them.
                         */
                        if(next_target == split_point) UNLIKELY
                            break;

                        --next_target;
                        split_point = std::partition(sorted_slots.begin(), split_point,
                            [next_target](const EffectSlot *slot) noexcept -> bool
                            { return slot->Target != *next_target; });
                    } while(split_point - sorted_slots.begin() > 1);
                }
            }

            for(const EffectSlot *slot : sorted_slots)
            {
                EffectState *state{slot->mEffectState.get()};
                state->process(SamplesToDo, slot->Wet.Buffer, state->mOutTarget);
            }
        }

        /* Signal the event handler if there are any events to read. */
        RingBuffer *ring{ctx->mAsyncEvents.get()};
        if(ring->readSpace() > 0)
            ctx->mEventSem.post();
    }
}


void ApplyDistanceComp(const al::span<FloatBufferLine> Samples, const size_t SamplesToDo,
    const al::span<const DistanceComp::ChanData,MaxOutputChannels> chandata)
{
    ASSUME(SamplesToDo > 0);

    auto distcomp = chandata.begin();
    for(auto &chanbuffer : Samples)
    {
        const float gain{distcomp->Gain};
        const size_t base{distcomp->Length};
        float *distbuf{al::assume_aligned<16>(distcomp->Buffer)};
        ++distcomp;

        if(base < 1)
            continue;

        float *inout{al::assume_aligned<16>(chanbuffer.data())};
        auto inout_end = inout + SamplesToDo;
        if(SamplesToDo >= base) LIKELY
        {
            auto delay_end = std::rotate(inout, inout_end - base, inout_end);
            std::swap_ranges(inout, delay_end, distbuf);
        }
        else
        {
            auto delay_start = std::swap_ranges(inout, inout_end, distbuf);
            std::rotate(distbuf, delay_start, distbuf + base);
        }
        std::transform(inout, inout_end, inout, [gain](float s) { return s * gain; });
    }
}

void ApplyDither(const al::span<FloatBufferLine> Samples, uint *dither_seed,
    const float quant_scale, const size_t SamplesToDo)
{
    static constexpr double invRNGRange{1.0 / std::numeric_limits<uint>::max()};
    ASSUME(SamplesToDo > 0);

    /* Dithering. Generate whitenoise (uniform distribution of random values
     * between -1 and +1) and add it to the sample values, after scaling up to
     * the desired quantization depth and before rounding.
     */
    const float invscale{1.0f / quant_scale};
    uint seed{*dither_seed};
    auto dither_sample = [&seed,invscale,quant_scale](const float sample) noexcept -> float
    {
        float val{sample * quant_scale};
        uint rng0{dither_rng(&seed)};
        uint rng1{dither_rng(&seed)};
        val += static_cast<float>(rng0*invRNGRange - rng1*invRNGRange);
        return fast_roundf(val) * invscale;
    };
    for(FloatBufferLine &inout : Samples)
        std::transform(inout.begin(), inout.begin()+SamplesToDo, inout.begin(), dither_sample);
    *dither_seed = seed;
}


/* Base template left undefined. Should be marked =delete, but Clang 3.8.1
 * chokes on that given the inline specializations.
 */
template<typename T>
inline T SampleConv(float) noexcept;

template<> inline float SampleConv(float val) noexcept
{ return val; }
template<> inline int32_t SampleConv(float val) noexcept
{
    /* Floats have a 23-bit mantissa, plus an implied 1 bit and a sign bit.
     * This means a normalized float has at most 25 bits of signed precision.
     * When scaling and clamping for a signed 32-bit integer, these following
     * values are the best a float can give.
     */
    return fastf2i(std::clamp(val*2147483648.0f, -2147483648.0f, 2147483520.0f));
}
template<> inline int16_t SampleConv(float val) noexcept
{ return static_cast<int16_t>(fastf2i(std::clamp(val*32768.0f, -32768.0f, 32767.0f))); }
template<> inline int8_t SampleConv(float val) noexcept
{ return static_cast<int8_t>(fastf2i(std::clamp(val*128.0f, -128.0f, 127.0f))); }

/* Define unsigned output variations. */
template<> inline uint32_t SampleConv(float val) noexcept
{ return static_cast<uint32_t>(SampleConv<int32_t>(val)) + 2147483648u; }
template<> inline uint16_t SampleConv(float val) noexcept
{ return static_cast<uint16_t>(SampleConv<int16_t>(val) + 32768); }
template<> inline uint8_t SampleConv(float val) noexcept
{ return static_cast<uint8_t>(SampleConv<int8_t>(val) + 128); }

template<DevFmtType T>
void Write(const al::span<const FloatBufferLine> InBuffer, void *OutBuffer, const size_t Offset,
    const size_t SamplesToDo, const size_t FrameStep)
{
    ASSUME(FrameStep > 0);
    ASSUME(SamplesToDo > 0);

    DevFmtType_t<T> *outbase{static_cast<DevFmtType_t<T>*>(OutBuffer) + Offset*FrameStep};
    size_t c{0};
    for(const FloatBufferLine &inbuf : InBuffer)
    {
        DevFmtType_t<T> *out{outbase++};
        auto conv_sample = [FrameStep,&out](const float s) noexcept -> void
        {
            *out = SampleConv<DevFmtType_t<T>>(s);
            out += FrameStep;
        };
        std::for_each(inbuf.begin(), inbuf.begin()+SamplesToDo, conv_sample);
        ++c;
    }
    if(const size_t extra{FrameStep - c})
    {
        const auto silence = SampleConv<DevFmtType_t<T>>(0.0f);
        for(size_t i{0};i < SamplesToDo;++i)
        {
            std::fill_n(outbase, extra, silence);
            outbase += FrameStep;
        }
    }
}

} // namespace

uint DeviceBase::renderSamples(const uint numSamples)
{
    const uint samplesToDo{std::min(numSamples, uint{BufferLineSize})};

    /* Clear main mixing buffers. */
    for(FloatBufferLine &buffer : MixBuffer)
        buffer.fill(0.0f);

    {
        const auto mixLock = getWriteMixLock();

        /* Process and mix each context's sources and effects. */
        ProcessContexts(this, samplesToDo);

        /* Every second's worth of samples is converted and added to clock base
         * so that large sample counts don't overflow during conversion. This
         * also guarantees a stable conversion.
         */
        auto samplesDone = mSamplesDone.load(std::memory_order_relaxed) + samplesToDo;
        auto clockBase = mClockBase.load(std::memory_order_relaxed) +
            std::chrono::seconds{samplesDone/Frequency};
        mSamplesDone.store(samplesDone%Frequency, std::memory_order_relaxed);
        mClockBase.store(clockBase, std::memory_order_relaxed);
    }

    /* Apply any needed post-process for finalizing the Dry mix to the RealOut
     * (Ambisonic decode, UHJ encode, etc).
     */
    postProcess(samplesToDo);

    /* Apply compression, limiting sample amplitude if needed or desired. */
    if(Limiter) Limiter->process(samplesToDo, RealOut.Buffer.data());

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

void DeviceBase::renderSamples(const al::span<float*> outBuffers, const uint numSamples)
{
    FPUCtl mixer_mode{};
    uint total{0};
    while(const uint todo{numSamples - total})
    {
        const uint samplesToDo{renderSamples(todo)};

        auto *srcbuf = RealOut.Buffer.data();
        for(auto *dstbuf : outBuffers)
        {
            std::copy_n(srcbuf->data(), samplesToDo, dstbuf + total);
            ++srcbuf;
        }

        total += samplesToDo;
    }
}

void DeviceBase::renderSamples(void *outBuffer, const uint numSamples, const size_t frameStep)
{
    FPUCtl mixer_mode{};
    uint total{0};
    while(const uint todo{numSamples - total})
    {
        const uint samplesToDo{renderSamples(todo)};

        if(outBuffer) LIKELY
        {
            /* Finally, interleave and convert samples, writing to the device's
             * output buffer.
             */
            switch(FmtType)
            {
#define HANDLE_WRITE(T) case T:                                               \
    Write<T>(RealOut.Buffer, outBuffer, total, samplesToDo, frameStep); break;
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

void DeviceBase::handleDisconnect(const char *msg, ...)
{
    const auto mixLock = getWriteMixLock();

    if(Connected.exchange(false, std::memory_order_acq_rel))
    {
        AsyncEvent evt{std::in_place_type<AsyncDisconnectEvent>};
        auto &disconnect = std::get<AsyncDisconnectEvent>(evt);

        /* NOLINTBEGIN(*-array-to-pointer-decay) */
        va_list args;
        va_start(args, msg);
        int msglen{vsnprintf(disconnect.msg.data(), disconnect.msg.size(), msg, args)};
        va_end(args);
        /* NOLINTEND(*-array-to-pointer-decay) */

        if(msglen < 0 || static_cast<size_t>(msglen) >= disconnect.msg.size())
            disconnect.msg[sizeof(disconnect.msg)-1] = 0;

        for(ContextBase *ctx : *mContexts.load())
        {
            RingBuffer *ring{ctx->mAsyncEvents.get()};
            auto evt_data = ring->getWriteVector().first;
            if(evt_data.len > 0)
            {
                al::construct_at(reinterpret_cast<AsyncEvent*>(evt_data.buf), evt);
                ring->writeAdvance(1);
                ctx->mEventSem.post();
            }

            if(!ctx->mStopVoicesOnDisconnect.load())
            {
                ProcessVoiceChanges(ctx);
                continue;
            }

            auto voicelist = ctx->getVoicesSpanAcquired();
            auto stop_voice = [](Voice *voice) -> void
            {
                voice->mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
                voice->mLoopBuffer.store(nullptr, std::memory_order_relaxed);
                voice->mSourceID.store(0u, std::memory_order_relaxed);
                voice->mPlayState.store(Voice::Stopped, std::memory_order_release);
            };
            std::for_each(voicelist.begin(), voicelist.end(), stop_voice);
        }
    }
}
