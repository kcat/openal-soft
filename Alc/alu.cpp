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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <cmath>
#include <limits>
#include <numeric>
#include <algorithm>
#include <functional>

#include "alMain.h"
#include "alcontext.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alListener.h"
#include "alAuxEffectSlot.h"
#include "alu.h"
#include "bs2b.h"
#include "hrtf.h"
#include "mastering.h"
#include "uhjfilter.h"
#include "bformatdec.h"
#include "ringbuffer.h"
#include "filters/splitter.h"

#include "mixer/defs.h"
#include "fpu_modes.h"
#include "cpu_caps.h"
#include "bsinc_inc.h"


namespace {

using namespace std::placeholders;

ALfloat InitConeScale()
{
    ALfloat ret{1.0f};
    const char *str{getenv("__ALSOFT_HALF_ANGLE_CONES")};
    if(str && (strcasecmp(str, "true") == 0 || strtol(str, nullptr, 0) == 1))
        ret *= 0.5f;
    return ret;
}

ALfloat InitZScale()
{
    ALfloat ret{1.0f};
    const char *str{getenv("__ALSOFT_REVERSE_Z")};
    if(str && (strcasecmp(str, "true") == 0 || strtol(str, nullptr, 0) == 1))
        ret *= -1.0f;
    return ret;
}

ALboolean InitReverbSOS()
{
    ALboolean ret{AL_FALSE};
    const char *str{getenv("__ALSOFT_REVERB_IGNORES_SOUND_SPEED")};
    if(str && (strcasecmp(str, "true") == 0 || strtol(str, nullptr, 0) == 1))
        ret = AL_TRUE;
    return ret;
}

} // namespace

/* Cone scalar */
const ALfloat ConeScale{InitConeScale()};

/* Localized Z scalar for mono sources */
const ALfloat ZScale{InitZScale()};

/* Force default speed of sound for distance-related reverb decay. */
const ALboolean OverrideReverbSpeedOfSound{InitReverbSOS()};


namespace {

void ClearArray(ALfloat (&f)[MAX_OUTPUT_CHANNELS])
{
    std::fill(std::begin(f), std::end(f), 0.0f);
}

struct ChanMap {
    Channel channel;
    ALfloat angle;
    ALfloat elevation;
};

HrtfDirectMixerFunc MixDirectHrtf = MixDirectHrtf_<CTag>;
inline HrtfDirectMixerFunc SelectHrtfMixer(void)
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

} // namespace

void aluInit(void)
{
    MixDirectHrtf = SelectHrtfMixer();
}


void ProcessHrtf(ALCdevice *device, const ALsizei SamplesToDo)
{
    /* HRTF is stereo output only. */
    const int lidx{device->RealOut.ChannelIndex[FrontLeft]};
    const int ridx{device->RealOut.ChannelIndex[FrontRight]};
    ASSUME(lidx >= 0 && ridx >= 0);

    DirectHrtfState *state{device->mHrtfState.get()};
    MixDirectHrtf(device->RealOut.Buffer[lidx], device->RealOut.Buffer[ridx], device->Dry.Buffer,
        device->HrtfAccumData, state, SamplesToDo);
}

void ProcessAmbiDec(ALCdevice *device, const ALsizei SamplesToDo)
{
    BFormatDec *ambidec{device->AmbiDecoder.get()};
    ambidec->process(device->RealOut.Buffer, device->Dry.Buffer.data(), SamplesToDo);
}

void ProcessUhj(ALCdevice *device, const ALsizei SamplesToDo)
{
    /* UHJ is stereo output only. */
    const int lidx{device->RealOut.ChannelIndex[FrontLeft]};
    const int ridx{device->RealOut.ChannelIndex[FrontRight]};
    ASSUME(lidx >= 0 && ridx >= 0);

    /* Encode to stereo-compatible 2-channel UHJ output. */
    Uhj2Encoder *uhj2enc{device->Uhj_Encoder.get()};
    uhj2enc->encode(device->RealOut.Buffer[lidx], device->RealOut.Buffer[ridx],
        device->Dry.Buffer.data(), SamplesToDo);
}

void ProcessBs2b(ALCdevice *device, const ALsizei SamplesToDo)
{
    /* First, decode the ambisonic mix to the "real" output. */
    BFormatDec *ambidec{device->AmbiDecoder.get()};
    ambidec->process(device->RealOut.Buffer, device->Dry.Buffer.data(), SamplesToDo);

    /* BS2B is stereo output only. */
    const int lidx{device->RealOut.ChannelIndex[FrontLeft]};
    const int ridx{device->RealOut.ChannelIndex[FrontRight]};
    ASSUME(lidx >= 0 && ridx >= 0);

    /* Now apply the BS2B binaural/crossfeed filter. */
    bs2b_cross_feed(device->Bs2b.get(), device->RealOut.Buffer[lidx].data(),
        device->RealOut.Buffer[ridx].data(), SamplesToDo);
}


/* Prepares the interpolator for a given rate (determined by increment).
 *
 * With a bit of work, and a trade of memory for CPU cost, this could be
 * modified for use with an interpolated increment for buttery-smooth pitch
 * changes.
 */
void BsincPrepare(const ALuint increment, BsincState *state, const BSincTable *table)
{
    ALsizei si{BSINC_SCALE_COUNT - 1};
    ALfloat sf{0.0f};

    if(increment > FRACTIONONE)
    {
        sf = static_cast<ALfloat>FRACTIONONE / increment;
        sf = maxf(0.0f, (BSINC_SCALE_COUNT-1) * (sf-table->scaleBase) * table->scaleRange);
        si = float2int(sf);
        /* The interpolation factor is fit to this diagonally-symmetric curve
         * to reduce the transition ripple caused by interpolating different
         * scales of the sinc function.
         */
        sf = 1.0f - std::cos(std::asin(sf - si));
    }

    state->sf = sf;
    state->m = table->m[si];
    state->l = (state->m/2) - 1;
    state->filter = table->Tab + table->filterOffset[si];
}


namespace {

/* This RNG method was created based on the math found in opusdec. It's quick,
 * and starting with a seed value of 22222, is suitable for generating
 * whitenoise.
 */
inline ALuint dither_rng(ALuint *seed) noexcept
{
    *seed = (*seed * 96314165) + 907633515;
    return *seed;
}


inline alu::Vector aluCrossproduct(const alu::Vector &in1, const alu::Vector &in2)
{
    return alu::Vector{
        in1[1]*in2[2] - in1[2]*in2[1],
        in1[2]*in2[0] - in1[0]*in2[2],
        in1[0]*in2[1] - in1[1]*in2[0],
        0.0f
    };
}

inline ALfloat aluDotproduct(const alu::Vector &vec1, const alu::Vector &vec2)
{
    return vec1[0]*vec2[0] + vec1[1]*vec2[1] + vec1[2]*vec2[2];
}


alu::Vector operator*(const alu::Matrix &mtx, const alu::Vector &vec) noexcept
{
    return alu::Vector{
        vec[0]*mtx[0][0] + vec[1]*mtx[1][0] + vec[2]*mtx[2][0] + vec[3]*mtx[3][0],
        vec[0]*mtx[0][1] + vec[1]*mtx[1][1] + vec[2]*mtx[2][1] + vec[3]*mtx[3][1],
        vec[0]*mtx[0][2] + vec[1]*mtx[1][2] + vec[2]*mtx[2][2] + vec[3]*mtx[3][2],
        vec[0]*mtx[0][3] + vec[1]*mtx[1][3] + vec[2]*mtx[2][3] + vec[3]*mtx[3][3]
    };
}


bool CalcContextParams(ALCcontext *Context)
{
    ALcontextProps *props{Context->Update.exchange(nullptr, std::memory_order_acq_rel)};
    if(!props) return false;

    ALlistener &Listener = Context->Listener;
    Listener.Params.MetersPerUnit = props->MetersPerUnit;

    Listener.Params.DopplerFactor = props->DopplerFactor;
    Listener.Params.SpeedOfSound = props->SpeedOfSound * props->DopplerVelocity;
    if(!OverrideReverbSpeedOfSound)
        Listener.Params.ReverbSpeedOfSound = Listener.Params.SpeedOfSound *
                                             Listener.Params.MetersPerUnit;

    Listener.Params.SourceDistanceModel = props->SourceDistanceModel;
    Listener.Params.mDistanceModel = props->mDistanceModel;

    AtomicReplaceHead(Context->FreeContextProps, props);
    return true;
}

bool CalcListenerParams(ALCcontext *Context)
{
    ALlistener &Listener = Context->Listener;

    ALlistenerProps *props{Listener.Update.exchange(nullptr, std::memory_order_acq_rel)};
    if(!props) return false;

    /* AT then UP */
    alu::Vector N{props->OrientAt[0], props->OrientAt[1], props->OrientAt[2], 0.0f};
    N.normalize();
    alu::Vector V{props->OrientUp[0], props->OrientUp[1], props->OrientUp[2], 0.0f};
    V.normalize();
    /* Build and normalize right-vector */
    alu::Vector U{aluCrossproduct(N, V)};
    U.normalize();

    Listener.Params.Matrix = alu::Matrix{
        U[0], V[0], -N[0], 0.0f,
        U[1], V[1], -N[1], 0.0f,
        U[2], V[2], -N[2], 0.0f,
        0.0f, 0.0f,  0.0f, 1.0f
    };

    const alu::Vector P{Listener.Params.Matrix *
        alu::Vector{props->Position[0], props->Position[1], props->Position[2], 1.0f}};
    Listener.Params.Matrix.setRow(3, -P[0], -P[1], -P[2], 1.0f);

    const alu::Vector vel{props->Velocity[0], props->Velocity[1], props->Velocity[2], 0.0f};
    Listener.Params.Velocity = Listener.Params.Matrix * vel;

    Listener.Params.Gain = props->Gain * Context->GainBoost;

    AtomicReplaceHead(Context->FreeListenerProps, props);
    return true;
}

bool CalcEffectSlotParams(ALeffectslot *slot, ALCcontext *context, bool force)
{
    ALeffectslotProps *props{slot->Update.exchange(nullptr, std::memory_order_acq_rel)};
    if(!props && !force) return false;

    EffectState *state;
    if(!props)
        state = slot->Params.mEffectState;
    else
    {
        slot->Params.Gain = props->Gain;
        slot->Params.AuxSendAuto = props->AuxSendAuto;
        slot->Params.Target = props->Target;
        slot->Params.EffectType = props->Type;
        slot->Params.mEffectProps = props->Props;
        if(IsReverbEffect(props->Type))
        {
            slot->Params.RoomRolloff = props->Props.Reverb.RoomRolloffFactor;
            slot->Params.DecayTime = props->Props.Reverb.DecayTime;
            slot->Params.DecayLFRatio = props->Props.Reverb.DecayLFRatio;
            slot->Params.DecayHFRatio = props->Props.Reverb.DecayHFRatio;
            slot->Params.DecayHFLimit = props->Props.Reverb.DecayHFLimit;
            slot->Params.AirAbsorptionGainHF = props->Props.Reverb.AirAbsorptionGainHF;
        }
        else
        {
            slot->Params.RoomRolloff = 0.0f;
            slot->Params.DecayTime = 0.0f;
            slot->Params.DecayLFRatio = 0.0f;
            slot->Params.DecayHFRatio = 0.0f;
            slot->Params.DecayHFLimit = AL_FALSE;
            slot->Params.AirAbsorptionGainHF = 1.0f;
        }

        state = props->State;
        props->State = nullptr;
        EffectState *oldstate{slot->Params.mEffectState};
        slot->Params.mEffectState = state;

        /* Manually decrement the old effect state's refcount if it's greater
         * than 1. We need to be a bit clever here to avoid the refcount
         * reaching 0 since it can't be deleted in the mixer.
         */
        ALuint oldval{oldstate->mRef.load(std::memory_order_acquire)};
        while(oldval > 1 && !oldstate->mRef.compare_exchange_weak(oldval, oldval-1,
            std::memory_order_acq_rel, std::memory_order_acquire))
        {
            /* oldval was updated with the current value on failure, so just
             * try again.
             */
        }

        if(oldval < 2)
        {
            /* Otherwise, if it would be deleted, send it off with a release
             * event.
             */
            RingBuffer *ring{context->AsyncEvents.get()};
            auto evt_vec = ring->getWriteVector();
            if(LIKELY(evt_vec.first.len > 0))
            {
                AsyncEvent *evt{new (evt_vec.first.buf) AsyncEvent{EventType_ReleaseEffectState}};
                evt->u.mEffectState = oldstate;
                ring->writeAdvance(1);
                context->EventSem.post();
            }
            else
            {
                /* If writing the event failed, the queue was probably full.
                 * Store the old state in the property object where it can
                 * eventually be cleaned up sometime later (not ideal, but
                 * better than blocking or leaking).
                 */
                props->State = oldstate;
            }
        }

        AtomicReplaceHead(context->FreeEffectslotProps, props);
    }

    EffectTarget output;
    if(ALeffectslot *target{slot->Params.Target})
        output = EffectTarget{&target->Wet, nullptr};
    else
    {
        ALCdevice *device{context->Device};
        output = EffectTarget{&device->Dry, &device->RealOut};
    }
    state->update(context, slot, &slot->Params.mEffectProps, output);
    return true;
}


/* Scales the given azimuth toward the side (+/- pi/2 radians) for positions in
 * front.
 */
inline float ScaleAzimuthFront(float azimuth, float scale)
{
    const ALfloat abs_azi{std::fabs(azimuth)};
    if(!(abs_azi > al::MathDefs<float>::Pi()*0.5f))
        return minf(abs_azi*scale, al::MathDefs<float>::Pi()*0.5f) * std::copysign(1.0f, azimuth);
    return azimuth;
}

void CalcPanningAndFilters(ALvoice *voice, const ALfloat xpos, const ALfloat ypos,
    const ALfloat zpos, const ALfloat Distance, const ALfloat Spread, const ALfloat DryGain,
    const ALfloat DryGainHF, const ALfloat DryGainLF, const ALfloat (&WetGain)[MAX_SENDS],
    const ALfloat (&WetGainLF)[MAX_SENDS], const ALfloat (&WetGainHF)[MAX_SENDS],
    ALeffectslot *(&SendSlots)[MAX_SENDS], const ALvoicePropsBase *props,
    const ALlistener &Listener, const ALCdevice *Device)
{
    static constexpr ChanMap MonoMap[1]{
        { FrontCenter, 0.0f, 0.0f }
    }, RearMap[2]{
        { BackLeft,  Deg2Rad(-150.0f), Deg2Rad(0.0f) },
        { BackRight, Deg2Rad( 150.0f), Deg2Rad(0.0f) }
    }, QuadMap[4]{
        { FrontLeft,  Deg2Rad( -45.0f), Deg2Rad(0.0f) },
        { FrontRight, Deg2Rad(  45.0f), Deg2Rad(0.0f) },
        { BackLeft,   Deg2Rad(-135.0f), Deg2Rad(0.0f) },
        { BackRight,  Deg2Rad( 135.0f), Deg2Rad(0.0f) }
    }, X51Map[6]{
        { FrontLeft,   Deg2Rad( -30.0f), Deg2Rad(0.0f) },
        { FrontRight,  Deg2Rad(  30.0f), Deg2Rad(0.0f) },
        { FrontCenter, Deg2Rad(   0.0f), Deg2Rad(0.0f) },
        { LFE, 0.0f, 0.0f },
        { SideLeft,    Deg2Rad(-110.0f), Deg2Rad(0.0f) },
        { SideRight,   Deg2Rad( 110.0f), Deg2Rad(0.0f) }
    }, X61Map[7]{
        { FrontLeft,   Deg2Rad(-30.0f), Deg2Rad(0.0f) },
        { FrontRight,  Deg2Rad( 30.0f), Deg2Rad(0.0f) },
        { FrontCenter, Deg2Rad(  0.0f), Deg2Rad(0.0f) },
        { LFE, 0.0f, 0.0f },
        { BackCenter,  Deg2Rad(180.0f), Deg2Rad(0.0f) },
        { SideLeft,    Deg2Rad(-90.0f), Deg2Rad(0.0f) },
        { SideRight,   Deg2Rad( 90.0f), Deg2Rad(0.0f) }
    }, X71Map[8]{
        { FrontLeft,   Deg2Rad( -30.0f), Deg2Rad(0.0f) },
        { FrontRight,  Deg2Rad(  30.0f), Deg2Rad(0.0f) },
        { FrontCenter, Deg2Rad(   0.0f), Deg2Rad(0.0f) },
        { LFE, 0.0f, 0.0f },
        { BackLeft,    Deg2Rad(-150.0f), Deg2Rad(0.0f) },
        { BackRight,   Deg2Rad( 150.0f), Deg2Rad(0.0f) },
        { SideLeft,    Deg2Rad( -90.0f), Deg2Rad(0.0f) },
        { SideRight,   Deg2Rad(  90.0f), Deg2Rad(0.0f) }
    };

    ChanMap StereoMap[2]{
        { FrontLeft,  Deg2Rad(-30.0f), Deg2Rad(0.0f) },
        { FrontRight, Deg2Rad( 30.0f), Deg2Rad(0.0f) }
    };

    const auto Frequency = static_cast<ALfloat>(Device->Frequency);
    const ALsizei NumSends{Device->NumAuxSends};
    ASSUME(NumSends >= 0);

    bool DirectChannels{props->DirectChannels != AL_FALSE};
    const ChanMap *chans{nullptr};
    ALsizei num_channels{0};
    bool isbformat{false};
    ALfloat downmix_gain{1.0f};
    switch(voice->mFmtChannels)
    {
    case FmtMono:
        chans = MonoMap;
        num_channels = 1;
        /* Mono buffers are never played direct. */
        DirectChannels = false;
        break;

    case FmtStereo:
        /* Convert counter-clockwise to clockwise. */
        StereoMap[0].angle = -props->StereoPan[0];
        StereoMap[1].angle = -props->StereoPan[1];

        chans = StereoMap;
        num_channels = 2;
        downmix_gain = 1.0f / 2.0f;
        break;

    case FmtRear:
        chans = RearMap;
        num_channels = 2;
        downmix_gain = 1.0f / 2.0f;
        break;

    case FmtQuad:
        chans = QuadMap;
        num_channels = 4;
        downmix_gain = 1.0f / 4.0f;
        break;

    case FmtX51:
        chans = X51Map;
        num_channels = 6;
        /* NOTE: Excludes LFE. */
        downmix_gain = 1.0f / 5.0f;
        break;

    case FmtX61:
        chans = X61Map;
        num_channels = 7;
        /* NOTE: Excludes LFE. */
        downmix_gain = 1.0f / 6.0f;
        break;

    case FmtX71:
        chans = X71Map;
        num_channels = 8;
        /* NOTE: Excludes LFE. */
        downmix_gain = 1.0f / 7.0f;
        break;

    case FmtBFormat2D:
        num_channels = 3;
        isbformat = true;
        DirectChannels = false;
        break;

    case FmtBFormat3D:
        num_channels = 4;
        isbformat = true;
        DirectChannels = false;
        break;
    }
    ASSUME(num_channels > 0);

    std::for_each(voice->mChans.begin(), voice->mChans.begin()+num_channels,
        [NumSends](ALvoice::ChannelData &chandata) -> void
        {
            chandata.mDryParams.Hrtf.Target = HrtfFilter{};
            ClearArray(chandata.mDryParams.Gains.Target);
            std::for_each(chandata.mWetParams.begin(), chandata.mWetParams.begin()+NumSends,
                [](SendParams &params) -> void { ClearArray(params.Gains.Target); });
        });

    voice->mFlags &= ~(VOICE_HAS_HRTF | VOICE_HAS_NFC);
    if(isbformat)
    {
        /* Special handling for B-Format sources. */

        if(Distance > std::numeric_limits<float>::epsilon())
        {
            /* Panning a B-Format sound toward some direction is easy. Just pan
             * the first (W) channel as a normal mono sound and silence the
             * others.
             */

            if(Device->AvgSpeakerDist > 0.0f)
            {
                /* Clamp the distance for really close sources, to prevent
                 * excessive bass.
                 */
                const ALfloat mdist{maxf(Distance, Device->AvgSpeakerDist/4.0f)};
                const ALfloat w0{SPEEDOFSOUNDMETRESPERSEC / (mdist * Frequency)};

                /* Only need to adjust the first channel of a B-Format source. */
                voice->mChans[0].mDryParams.NFCtrlFilter.adjust(w0);

                voice->mFlags |= VOICE_HAS_NFC;
            }

            ALfloat coeffs[MAX_AMBI_CHANNELS];
            if(Device->mRenderMode != StereoPair)
                CalcDirectionCoeffs({xpos, ypos, zpos}, Spread, coeffs);
            else
            {
                /* Clamp Y, in case rounding errors caused it to end up outside
                 * of -1...+1.
                 */
                const ALfloat ev{std::asin(clampf(ypos, -1.0f, 1.0f))};
                /* Negate Z for right-handed coords with -Z in front. */
                const ALfloat az{std::atan2(xpos, -zpos)};

                /* A scalar of 1.5 for plain stereo results in +/-60 degrees
                 * being moved to +/-90 degrees for direct right and left
                 * speaker responses.
                 */
                CalcAngleCoeffs(ScaleAzimuthFront(az, 1.5f), ev, Spread, coeffs);
            }

            /* NOTE: W needs to be scaled due to FuMa normalization. */
            const ALfloat &scale0 = AmbiScale::FromFuMa[0];
            ComputePanGains(&Device->Dry, coeffs, DryGain*scale0,
                voice->mChans[0].mDryParams.Gains.Target);
            for(ALsizei i{0};i < NumSends;i++)
            {
                if(const ALeffectslot *Slot{SendSlots[i]})
                    ComputePanGains(&Slot->Wet, coeffs, WetGain[i]*scale0,
                        voice->mChans[0].mWetParams[i].Gains.Target);
            }
        }
        else
        {
            if(Device->AvgSpeakerDist > 0.0f)
            {
                /* NOTE: The NFCtrlFilters were created with a w0 of 0, which
                 * is what we want for FOA input. The first channel may have
                 * been previously re-adjusted if panned, so reset it.
                 */
                voice->mChans[0].mDryParams.NFCtrlFilter.adjust(0.0f);

                voice->mFlags |= VOICE_HAS_NFC;
            }

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
                N = Listener.Params.Matrix * N;
                V = Listener.Params.Matrix * V;
            }
            /* Build and normalize right-vector */
            alu::Vector U{aluCrossproduct(N, V)};
            U.normalize();

            /* Build a rotate + conversion matrix (FuMa -> ACN+N3D). NOTE: This
             * matrix is transposed, for the inputs to align on the rows and
             * outputs on the columns.
             */
            const ALfloat &wscale = AmbiScale::FromFuMa[0];
            const ALfloat &yscale = AmbiScale::FromFuMa[1];
            const ALfloat &zscale = AmbiScale::FromFuMa[2];
            const ALfloat &xscale = AmbiScale::FromFuMa[3];
            const ALfloat matrix[4][MAX_AMBI_CHANNELS]{
            //      ACN0          ACN1          ACN2          ACN3
                { wscale,         0.0f,         0.0f,         0.0f }, // FuMa W
                {   0.0f, -N[0]*xscale,  N[1]*xscale, -N[2]*xscale }, // FuMa X
                {   0.0f,  U[0]*yscale, -U[1]*yscale,  U[2]*yscale }, // FuMa Y
                {   0.0f, -V[0]*zscale,  V[1]*zscale, -V[2]*zscale }  // FuMa Z
            };

            for(ALsizei c{0};c < num_channels;c++)
            {
                ComputePanGains(&Device->Dry, matrix[c], DryGain,
                    voice->mChans[c].mDryParams.Gains.Target);

                for(ALsizei i{0};i < NumSends;i++)
                {
                    if(const ALeffectslot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, matrix[c], WetGain[i],
                            voice->mChans[c].mWetParams[i].Gains.Target);
                }
            }
        }
    }
    else if(DirectChannels)
    {
        /* Direct source channels always play local. Skip the virtual channels
         * and write inputs to the matching real outputs.
         */
        voice->mDirect.Buffer = Device->RealOut.Buffer;

        for(ALsizei c{0};c < num_channels;c++)
        {
            int idx{GetChannelIdxByName(Device->RealOut, chans[c].channel)};
            if(idx != -1) voice->mChans[c].mDryParams.Gains.Target[idx] = DryGain;
        }

        /* Auxiliary sends still use normal channel panning since they mix to
         * B-Format, which can't channel-match.
         */
        for(ALsizei c{0};c < num_channels;c++)
        {
            ALfloat coeffs[MAX_AMBI_CHANNELS];
            CalcAngleCoeffs(chans[c].angle, chans[c].elevation, 0.0f, coeffs);

            for(ALsizei i{0};i < NumSends;i++)
            {
                if(const ALeffectslot *Slot{SendSlots[i]})
                    ComputePanGains(&Slot->Wet, coeffs, WetGain[i],
                        voice->mChans[c].mWetParams[i].Gains.Target);
            }
        }
    }
    else if(Device->mRenderMode == HrtfRender)
    {
        /* Full HRTF rendering. Skip the virtual channels and render to the
         * real outputs.
         */
        voice->mDirect.Buffer = Device->RealOut.Buffer;

        if(Distance > std::numeric_limits<float>::epsilon())
        {
            const ALfloat ev{std::asin(clampf(ypos, -1.0f, 1.0f))};
            const ALfloat az{std::atan2(xpos, -zpos)};

            /* Get the HRIR coefficients and delays just once, for the given
             * source direction.
             */
            GetHrtfCoeffs(Device->mHrtf, ev, az, Distance, Spread,
                voice->mChans[0].mDryParams.Hrtf.Target.Coeffs,
                voice->mChans[0].mDryParams.Hrtf.Target.Delay);
            voice->mChans[0].mDryParams.Hrtf.Target.Gain = DryGain * downmix_gain;

            /* Remaining channels use the same results as the first. */
            for(ALsizei c{1};c < num_channels;c++)
            {
                /* Skip LFE */
                if(chans[c].channel == LFE) continue;
                voice->mChans[c].mDryParams.Hrtf.Target = voice->mChans[0].mDryParams.Hrtf.Target;
            }

            /* Calculate the directional coefficients once, which apply to all
             * input channels of the source sends.
             */
            ALfloat coeffs[MAX_AMBI_CHANNELS];
            CalcDirectionCoeffs({xpos, ypos, zpos}, Spread, coeffs);

            for(ALsizei c{0};c < num_channels;c++)
            {
                /* Skip LFE */
                if(chans[c].channel == LFE)
                    continue;
                for(ALsizei i{0};i < NumSends;i++)
                {
                    if(const ALeffectslot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i] * downmix_gain,
                            voice->mChans[c].mWetParams[i].Gains.Target);
                }
            }
        }
        else
        {
            /* Local sources on HRTF play with each channel panned to its
             * relative location around the listener, providing "virtual
             * speaker" responses.
             */
            for(ALsizei c{0};c < num_channels;c++)
            {
                /* Skip LFE */
                if(chans[c].channel == LFE)
                    continue;

                /* Get the HRIR coefficients and delays for this channel
                 * position.
                 */
                GetHrtfCoeffs(Device->mHrtf, chans[c].elevation, chans[c].angle,
                    std::numeric_limits<float>::infinity(), Spread,
                    voice->mChans[c].mDryParams.Hrtf.Target.Coeffs,
                    voice->mChans[c].mDryParams.Hrtf.Target.Delay);
                voice->mChans[c].mDryParams.Hrtf.Target.Gain = DryGain;

                /* Normal panning for auxiliary sends. */
                ALfloat coeffs[MAX_AMBI_CHANNELS];
                CalcAngleCoeffs(chans[c].angle, chans[c].elevation, Spread, coeffs);

                for(ALsizei i{0};i < NumSends;i++)
                {
                    if(const ALeffectslot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i],
                            voice->mChans[c].mWetParams[i].Gains.Target);
                }
            }
        }

        voice->mFlags |= VOICE_HAS_HRTF;
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
                const ALfloat mdist{maxf(Distance, Device->AvgSpeakerDist/4.0f)};
                const ALfloat w0{SPEEDOFSOUNDMETRESPERSEC / (mdist * Frequency)};

                /* Adjust NFC filters. */
                for(ALsizei c{0};c < num_channels;c++)
                    voice->mChans[c].mDryParams.NFCtrlFilter.adjust(w0);

                voice->mFlags |= VOICE_HAS_NFC;
            }

            /* Calculate the directional coefficients once, which apply to all
             * input channels.
             */
            ALfloat coeffs[MAX_AMBI_CHANNELS];
            if(Device->mRenderMode != StereoPair)
                CalcDirectionCoeffs({xpos, ypos, zpos}, Spread, coeffs);
            else
            {
                const ALfloat ev{std::asin(clampf(ypos, -1.0f, 1.0f))};
                const ALfloat az{std::atan2(xpos, -zpos)};
                CalcAngleCoeffs(ScaleAzimuthFront(az, 1.5f), ev, Spread, coeffs);
            }

            for(ALsizei c{0};c < num_channels;c++)
            {
                /* Special-case LFE */
                if(chans[c].channel == LFE)
                {
                    if(Device->Dry.Buffer.data() == Device->RealOut.Buffer.data())
                    {
                        int idx = GetChannelIdxByName(Device->RealOut, chans[c].channel);
                        if(idx != -1) voice->mChans[c].mDryParams.Gains.Target[idx] = DryGain;
                    }
                    continue;
                }

                ComputePanGains(&Device->Dry, coeffs, DryGain * downmix_gain,
                    voice->mChans[c].mDryParams.Gains.Target);
            }

            for(ALsizei c{0};c < num_channels;c++)
            {
                /* Skip LFE */
                if(chans[c].channel == LFE)
                    continue;
                for(ALsizei i{0};i < NumSends;i++)
                {
                    if(const ALeffectslot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i] * downmix_gain,
                            voice->mChans[c].mWetParams[i].Gains.Target);
                }
            }
        }
        else
        {
            if(Device->AvgSpeakerDist > 0.0f)
            {
                /* If the source distance is 0, set w0 to w1 to act as a pass-
                 * through. We still want to pass the signal through the
                 * filters so they keep an appropriate history, in case the
                 * source moves away from the listener.
                 */
                const ALfloat w0{SPEEDOFSOUNDMETRESPERSEC / (Device->AvgSpeakerDist * Frequency)};

                for(ALsizei c{0};c < num_channels;c++)
                    voice->mChans[c].mDryParams.NFCtrlFilter.adjust(w0);

                voice->mFlags |= VOICE_HAS_NFC;
            }

            for(ALsizei c{0};c < num_channels;c++)
            {
                /* Special-case LFE */
                if(chans[c].channel == LFE)
                {
                    if(Device->Dry.Buffer.data() == Device->RealOut.Buffer.data())
                    {
                        int idx = GetChannelIdxByName(Device->RealOut, chans[c].channel);
                        if(idx != -1) voice->mChans[c].mDryParams.Gains.Target[idx] = DryGain;
                    }
                    continue;
                }

                ALfloat coeffs[MAX_AMBI_CHANNELS];
                CalcAngleCoeffs(
                    (Device->mRenderMode==StereoPair) ? ScaleAzimuthFront(chans[c].angle, 3.0f)
                                                      : chans[c].angle,
                    chans[c].elevation, Spread, coeffs
                );

                ComputePanGains(&Device->Dry, coeffs, DryGain,
                    voice->mChans[c].mDryParams.Gains.Target);
                for(ALsizei i{0};i < NumSends;i++)
                {
                    if(const ALeffectslot *Slot{SendSlots[i]})
                        ComputePanGains(&Slot->Wet, coeffs, WetGain[i],
                            voice->mChans[c].mWetParams[i].Gains.Target);
                }
            }
        }
    }

    {
        const ALfloat hfScale{props->Direct.HFReference / Frequency};
        const ALfloat lfScale{props->Direct.LFReference / Frequency};
        const ALfloat gainHF{maxf(DryGainHF, 0.001f)}; /* Limit -60dB */
        const ALfloat gainLF{maxf(DryGainLF, 0.001f)};

        voice->mDirect.FilterType = AF_None;
        if(gainHF != 1.0f) voice->mDirect.FilterType |= AF_LowPass;
        if(gainLF != 1.0f) voice->mDirect.FilterType |= AF_HighPass;
        auto &lowpass = voice->mChans[0].mDryParams.LowPass;
        auto &highpass = voice->mChans[0].mDryParams.HighPass;
        lowpass.setParams(BiquadType::HighShelf, gainHF, hfScale,
            lowpass.rcpQFromSlope(gainHF, 1.0f));
        highpass.setParams(BiquadType::LowShelf, gainLF, lfScale,
            highpass.rcpQFromSlope(gainLF, 1.0f));
        for(ALsizei c{1};c < num_channels;c++)
        {
            voice->mChans[c].mDryParams.LowPass.copyParamsFrom(lowpass);
            voice->mChans[c].mDryParams.HighPass.copyParamsFrom(highpass);
        }
    }
    for(ALsizei i{0};i < NumSends;i++)
    {
        const ALfloat hfScale{props->Send[i].HFReference / Frequency};
        const ALfloat lfScale{props->Send[i].LFReference / Frequency};
        const ALfloat gainHF{maxf(WetGainHF[i], 0.001f)};
        const ALfloat gainLF{maxf(WetGainLF[i], 0.001f)};

        voice->mSend[i].FilterType = AF_None;
        if(gainHF != 1.0f) voice->mSend[i].FilterType |= AF_LowPass;
        if(gainLF != 1.0f) voice->mSend[i].FilterType |= AF_HighPass;

        auto &lowpass = voice->mChans[0].mWetParams[i].LowPass;
        auto &highpass = voice->mChans[0].mWetParams[i].HighPass;
        lowpass.setParams(BiquadType::HighShelf, gainHF, hfScale,
            lowpass.rcpQFromSlope(gainHF, 1.0f));
        highpass.setParams(BiquadType::LowShelf, gainLF, lfScale,
            highpass.rcpQFromSlope(gainLF, 1.0f));
        for(ALsizei c{1};c < num_channels;c++)
        {
            voice->mChans[c].mWetParams[i].LowPass.copyParamsFrom(lowpass);
            voice->mChans[c].mWetParams[i].HighPass.copyParamsFrom(highpass);
        }
    }
}

void CalcNonAttnSourceParams(ALvoice *voice, const ALvoicePropsBase *props, const ALCcontext *ALContext)
{
    const ALCdevice *Device{ALContext->Device};
    ALeffectslot *SendSlots[MAX_SENDS];

    voice->mDirect.Buffer = Device->Dry.Buffer;
    for(ALsizei i{0};i < Device->NumAuxSends;i++)
    {
        SendSlots[i] = props->Send[i].Slot;
        if(!SendSlots[i] && i == 0)
            SendSlots[i] = ALContext->DefaultSlot.get();
        if(!SendSlots[i] || SendSlots[i]->Params.EffectType == AL_EFFECT_NULL)
        {
            SendSlots[i] = nullptr;
            voice->mSend[i].Buffer = {};
        }
        else
            voice->mSend[i].Buffer = SendSlots[i]->Wet.Buffer;
    }

    /* Calculate the stepping value */
    const auto Pitch = static_cast<ALfloat>(voice->mFrequency) /
        static_cast<ALfloat>(Device->Frequency) * props->Pitch;
    if(Pitch > static_cast<ALfloat>(MAX_PITCH))
        voice->mStep = MAX_PITCH<<FRACTIONBITS;
    else
        voice->mStep = maxi(fastf2i(Pitch * FRACTIONONE), 1);
    if(props->mResampler == BSinc24Resampler)
        BsincPrepare(voice->mStep, &voice->mResampleState.bsinc, &bsinc24);
    else if(props->mResampler == BSinc12Resampler)
        BsincPrepare(voice->mStep, &voice->mResampleState.bsinc, &bsinc12);
    voice->mResampler = SelectResampler(props->mResampler);

    /* Calculate gains */
    const ALlistener &Listener = ALContext->Listener;
    ALfloat DryGain{clampf(props->Gain, props->MinGain, props->MaxGain)};
    DryGain *= props->Direct.Gain * Listener.Params.Gain;
    DryGain  = minf(DryGain, GAIN_MIX_MAX);
    ALfloat DryGainHF{props->Direct.GainHF};
    ALfloat DryGainLF{props->Direct.GainLF};
    ALfloat WetGain[MAX_SENDS], WetGainHF[MAX_SENDS], WetGainLF[MAX_SENDS];
    for(ALsizei i{0};i < Device->NumAuxSends;i++)
    {
        WetGain[i]  = clampf(props->Gain, props->MinGain, props->MaxGain);
        WetGain[i] *= props->Send[i].Gain * Listener.Params.Gain;
        WetGain[i]  = minf(WetGain[i], GAIN_MIX_MAX);
        WetGainHF[i] = props->Send[i].GainHF;
        WetGainLF[i] = props->Send[i].GainLF;
    }

    CalcPanningAndFilters(voice, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, DryGain, DryGainHF, DryGainLF,
        WetGain, WetGainLF, WetGainHF, SendSlots, props, Listener, Device);
}

void CalcAttnSourceParams(ALvoice *voice, const ALvoicePropsBase *props, const ALCcontext *ALContext)
{
    const ALCdevice *Device{ALContext->Device};
    const ALsizei NumSends{Device->NumAuxSends};
    const ALlistener &Listener = ALContext->Listener;

    /* Set mixing buffers and get send parameters. */
    voice->mDirect.Buffer = Device->Dry.Buffer;
    ALeffectslot *SendSlots[MAX_SENDS];
    ALfloat RoomRolloff[MAX_SENDS];
    ALfloat DecayDistance[MAX_SENDS];
    ALfloat DecayLFDistance[MAX_SENDS];
    ALfloat DecayHFDistance[MAX_SENDS];
    for(ALsizei i{0};i < NumSends;i++)
    {
        SendSlots[i] = props->Send[i].Slot;
        if(!SendSlots[i] && i == 0)
            SendSlots[i] = ALContext->DefaultSlot.get();
        if(!SendSlots[i] || SendSlots[i]->Params.EffectType == AL_EFFECT_NULL)
        {
            SendSlots[i] = nullptr;
            RoomRolloff[i] = 0.0f;
            DecayDistance[i] = 0.0f;
            DecayLFDistance[i] = 0.0f;
            DecayHFDistance[i] = 0.0f;
        }
        else if(SendSlots[i]->Params.AuxSendAuto)
        {
            RoomRolloff[i] = SendSlots[i]->Params.RoomRolloff + props->RoomRolloffFactor;
            /* Calculate the distances to where this effect's decay reaches
             * -60dB.
             */
            DecayDistance[i] = SendSlots[i]->Params.DecayTime *
                               Listener.Params.ReverbSpeedOfSound;
            DecayLFDistance[i] = DecayDistance[i] * SendSlots[i]->Params.DecayLFRatio;
            DecayHFDistance[i] = DecayDistance[i] * SendSlots[i]->Params.DecayHFRatio;
            if(SendSlots[i]->Params.DecayHFLimit)
            {
                ALfloat airAbsorption{SendSlots[i]->Params.AirAbsorptionGainHF};
                if(airAbsorption < 1.0f)
                {
                    /* Calculate the distance to where this effect's air
                     * absorption reaches -60dB, and limit the effect's HF
                     * decay distance (so it doesn't take any longer to decay
                     * than the air would allow).
                     */
                    ALfloat absorb_dist{std::log10(REVERB_DECAY_GAIN) / std::log10(airAbsorption)};
                    DecayHFDistance[i] = minf(absorb_dist, DecayHFDistance[i]);
                }
            }
        }
        else
        {
            /* If the slot's auxiliary send auto is off, the data sent to the
             * effect slot is the same as the dry path, sans filter effects */
            RoomRolloff[i] = props->RolloffFactor;
            DecayDistance[i] = 0.0f;
            DecayLFDistance[i] = 0.0f;
            DecayHFDistance[i] = 0.0f;
        }

        if(!SendSlots[i])
            voice->mSend[i].Buffer = {};
        else
            voice->mSend[i].Buffer = SendSlots[i]->Wet.Buffer;
    }

    /* Transform source to listener space (convert to head relative) */
    alu::Vector Position{props->Position[0], props->Position[1], props->Position[2], 1.0f};
    alu::Vector Velocity{props->Velocity[0], props->Velocity[1], props->Velocity[2], 0.0f};
    alu::Vector Direction{props->Direction[0], props->Direction[1], props->Direction[2], 0.0f};
    if(props->HeadRelative == AL_FALSE)
    {
        /* Transform source vectors */
        Position = Listener.Params.Matrix * Position;
        Velocity = Listener.Params.Matrix * Velocity;
        Direction = Listener.Params.Matrix * Direction;
    }
    else
    {
        /* Offset the source velocity to be relative of the listener velocity */
        Velocity += Listener.Params.Velocity;
    }

    const bool directional{Direction.normalize() > 0.0f};
    alu::Vector ToSource{Position[0], Position[1], Position[2], 0.0f};
    const ALfloat Distance{ToSource.normalize()};

    /* Initial source gain */
    ALfloat DryGain{props->Gain};
    ALfloat DryGainHF{1.0f};
    ALfloat DryGainLF{1.0f};
    ALfloat WetGain[MAX_SENDS], WetGainHF[MAX_SENDS], WetGainLF[MAX_SENDS];
    for(ALsizei i{0};i < NumSends;i++)
    {
        WetGain[i] = props->Gain;
        WetGainHF[i] = 1.0f;
        WetGainLF[i] = 1.0f;
    }

    /* Calculate distance attenuation */
    ALfloat ClampedDist{Distance};

    switch(Listener.Params.SourceDistanceModel ?
           props->mDistanceModel : Listener.Params.mDistanceModel)
    {
        case DistanceModel::InverseClamped:
            ClampedDist = clampf(ClampedDist, props->RefDistance, props->MaxDistance);
            if(props->MaxDistance < props->RefDistance) break;
            /*fall-through*/
        case DistanceModel::Inverse:
            if(!(props->RefDistance > 0.0f))
                ClampedDist = props->RefDistance;
            else
            {
                ALfloat dist = lerp(props->RefDistance, ClampedDist, props->RolloffFactor);
                if(dist > 0.0f) DryGain *= props->RefDistance / dist;
                for(ALsizei i{0};i < NumSends;i++)
                {
                    dist = lerp(props->RefDistance, ClampedDist, RoomRolloff[i]);
                    if(dist > 0.0f) WetGain[i] *= props->RefDistance / dist;
                }
            }
            break;

        case DistanceModel::LinearClamped:
            ClampedDist = clampf(ClampedDist, props->RefDistance, props->MaxDistance);
            if(props->MaxDistance < props->RefDistance) break;
            /*fall-through*/
        case DistanceModel::Linear:
            if(!(props->MaxDistance != props->RefDistance))
                ClampedDist = props->RefDistance;
            else
            {
                ALfloat attn = props->RolloffFactor * (ClampedDist-props->RefDistance) /
                               (props->MaxDistance-props->RefDistance);
                DryGain *= maxf(1.0f - attn, 0.0f);
                for(ALsizei i{0};i < NumSends;i++)
                {
                    attn = RoomRolloff[i] * (ClampedDist-props->RefDistance) /
                           (props->MaxDistance-props->RefDistance);
                    WetGain[i] *= maxf(1.0f - attn, 0.0f);
                }
            }
            break;

        case DistanceModel::ExponentClamped:
            ClampedDist = clampf(ClampedDist, props->RefDistance, props->MaxDistance);
            if(props->MaxDistance < props->RefDistance) break;
            /*fall-through*/
        case DistanceModel::Exponent:
            if(!(ClampedDist > 0.0f && props->RefDistance > 0.0f))
                ClampedDist = props->RefDistance;
            else
            {
                DryGain *= std::pow(ClampedDist/props->RefDistance, -props->RolloffFactor);
                for(ALsizei i{0};i < NumSends;i++)
                    WetGain[i] *= std::pow(ClampedDist/props->RefDistance, -RoomRolloff[i]);
            }
            break;

        case DistanceModel::Disable:
            ClampedDist = props->RefDistance;
            break;
    }

    /* Calculate directional soundcones */
    if(directional && props->InnerAngle < 360.0f)
    {
        const ALfloat Angle{Rad2Deg(std::acos(-aluDotproduct(Direction, ToSource)) *
            ConeScale * 2.0f)};

        ALfloat ConeVolume, ConeHF;
        if(!(Angle > props->InnerAngle))
        {
            ConeVolume = 1.0f;
            ConeHF = 1.0f;
        }
        else if(Angle < props->OuterAngle)
        {
            ALfloat scale = (            Angle-props->InnerAngle) /
                            (props->OuterAngle-props->InnerAngle);
            ConeVolume = lerp(1.0f, props->OuterGain, scale);
            ConeHF = lerp(1.0f, props->OuterGainHF, scale);
        }
        else
        {
            ConeVolume = props->OuterGain;
            ConeHF = props->OuterGainHF;
        }

        DryGain *= ConeVolume;
        if(props->DryGainHFAuto)
            DryGainHF *= ConeHF;
        if(props->WetGainAuto)
            std::transform(std::begin(WetGain), std::begin(WetGain)+NumSends, std::begin(WetGain),
                [ConeVolume](ALfloat gain) noexcept -> ALfloat { return gain * ConeVolume; }
            );
        if(props->WetGainHFAuto)
            std::transform(std::begin(WetGainHF), std::begin(WetGainHF)+NumSends,
                std::begin(WetGainHF),
                [ConeHF](ALfloat gain) noexcept -> ALfloat { return gain * ConeHF; }
            );
    }

    /* Apply gain and frequency filters */
    DryGain = clampf(DryGain, props->MinGain, props->MaxGain);
    DryGain = minf(DryGain*props->Direct.Gain*Listener.Params.Gain, GAIN_MIX_MAX);
    DryGainHF *= props->Direct.GainHF;
    DryGainLF *= props->Direct.GainLF;
    for(ALsizei i{0};i < NumSends;i++)
    {
        WetGain[i] = clampf(WetGain[i], props->MinGain, props->MaxGain);
        WetGain[i] = minf(WetGain[i]*props->Send[i].Gain*Listener.Params.Gain, GAIN_MIX_MAX);
        WetGainHF[i] *= props->Send[i].GainHF;
        WetGainLF[i] *= props->Send[i].GainLF;
    }

    /* Distance-based air absorption and initial send decay. */
    if(ClampedDist > props->RefDistance && props->RolloffFactor > 0.0f)
    {
        ALfloat meters_base{(ClampedDist-props->RefDistance) * props->RolloffFactor *
                            Listener.Params.MetersPerUnit};
        if(props->AirAbsorptionFactor > 0.0f)
        {
            ALfloat hfattn{std::pow(AIRABSORBGAINHF, meters_base * props->AirAbsorptionFactor)};
            DryGainHF *= hfattn;
            std::transform(std::begin(WetGainHF), std::begin(WetGainHF)+NumSends,
                std::begin(WetGainHF),
                [hfattn](ALfloat gain) noexcept -> ALfloat { return gain * hfattn; }
            );
        }

        if(props->WetGainAuto)
        {
            /* Apply a decay-time transformation to the wet path, based on the
             * source distance in meters. The initial decay of the reverb
             * effect is calculated and applied to the wet path.
             */
            for(ALsizei i{0};i < NumSends;i++)
            {
                if(!(DecayDistance[i] > 0.0f))
                    continue;

                const ALfloat gain{std::pow(REVERB_DECAY_GAIN, meters_base/DecayDistance[i])};
                WetGain[i] *= gain;
                /* Yes, the wet path's air absorption is applied with
                 * WetGainAuto on, rather than WetGainHFAuto.
                 */
                if(gain > 0.0f)
                {
                    ALfloat gainhf{std::pow(REVERB_DECAY_GAIN, meters_base/DecayHFDistance[i])};
                    WetGainHF[i] *= minf(gainhf / gain, 1.0f);
                    ALfloat gainlf{std::pow(REVERB_DECAY_GAIN, meters_base/DecayLFDistance[i])};
                    WetGainLF[i] *= minf(gainlf / gain, 1.0f);
                }
            }
        }
    }


    /* Initial source pitch */
    ALfloat Pitch{props->Pitch};

    /* Calculate velocity-based doppler effect */
    ALfloat DopplerFactor{props->DopplerFactor * Listener.Params.DopplerFactor};
    if(DopplerFactor > 0.0f)
    {
        const alu::Vector &lvelocity = Listener.Params.Velocity;
        ALfloat vss{aluDotproduct(Velocity, ToSource) * -DopplerFactor};
        ALfloat vls{aluDotproduct(lvelocity, ToSource) * -DopplerFactor};

        const ALfloat SpeedOfSound{Listener.Params.SpeedOfSound};
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
    Pitch *= static_cast<ALfloat>(voice->mFrequency)/static_cast<ALfloat>(Device->Frequency);
    if(Pitch > static_cast<ALfloat>(MAX_PITCH))
        voice->mStep = MAX_PITCH<<FRACTIONBITS;
    else
        voice->mStep = maxi(fastf2i(Pitch * FRACTIONONE), 1);
    if(props->mResampler == BSinc24Resampler)
        BsincPrepare(voice->mStep, &voice->mResampleState.bsinc, &bsinc24);
    else if(props->mResampler == BSinc12Resampler)
        BsincPrepare(voice->mStep, &voice->mResampleState.bsinc, &bsinc12);
    voice->mResampler = SelectResampler(props->mResampler);

    ALfloat spread{0.0f};
    if(props->Radius > Distance)
        spread = al::MathDefs<float>::Tau() - Distance/props->Radius*al::MathDefs<float>::Pi();
    else if(Distance > 0.0f)
        spread = std::asin(props->Radius/Distance) * 2.0f;

    CalcPanningAndFilters(voice, ToSource[0], ToSource[1], ToSource[2]*ZScale,
        Distance*Listener.Params.MetersPerUnit, spread, DryGain, DryGainHF, DryGainLF, WetGain,
        WetGainLF, WetGainHF, SendSlots, props, Listener, Device);
}

void CalcSourceParams(ALvoice *voice, ALCcontext *context, bool force)
{
    ALvoiceProps *props{voice->mUpdate.exchange(nullptr, std::memory_order_acq_rel)};
    if(!props && !force) return;

    if(props)
    {
        voice->mProps = *props;

        AtomicReplaceHead(context->FreeVoiceProps, props);
    }

    if((voice->mProps.mSpatializeMode == SpatializeAuto && voice->mFmtChannels == FmtMono) ||
       voice->mProps.mSpatializeMode == SpatializeOn)
        CalcAttnSourceParams(voice, &voice->mProps, context);
    else
        CalcNonAttnSourceParams(voice, &voice->mProps, context);
}


void ProcessParamUpdates(ALCcontext *ctx, const ALeffectslotArray *slots)
{
    IncrementRef(&ctx->UpdateCount);
    if(LIKELY(!ctx->HoldUpdates.load(std::memory_order_acquire)))
    {
        bool cforce{CalcContextParams(ctx)};
        bool force{CalcListenerParams(ctx) || cforce};
        force = std::accumulate(slots->begin(), slots->end(), force,
            [ctx,cforce](bool force, ALeffectslot *slot) -> bool
            { return CalcEffectSlotParams(slot, ctx, cforce) | force; }
        );

        std::for_each(ctx->Voices->begin(),
            ctx->Voices->begin() + ctx->VoiceCount.load(std::memory_order_acquire),
            [ctx,force](ALvoice &voice) -> void
            {
                ALuint sid{voice.mSourceID.load(std::memory_order_acquire)};
                if(sid) CalcSourceParams(&voice, ctx, force);
            }
        );
    }
    IncrementRef(&ctx->UpdateCount);
}

void ProcessContext(ALCcontext *ctx, const ALsizei SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    const ALeffectslotArray *auxslots{ctx->ActiveAuxSlots.load(std::memory_order_acquire)};

    /* Process pending propery updates for objects on the context. */
    ProcessParamUpdates(ctx, auxslots);

    /* Clear auxiliary effect slot mixing buffers. */
    std::for_each(auxslots->begin(), auxslots->end(),
        [SamplesToDo](ALeffectslot *slot) -> void
        {
            for(auto &buffer : slot->MixBuffer)
                std::fill_n(buffer.begin(), SamplesToDo, 0.0f);
        }
    );

    /* Process voices that have a playing source. */
    std::for_each(ctx->Voices->begin(),
        ctx->Voices->begin() + ctx->VoiceCount.load(std::memory_order_acquire),
        [SamplesToDo,ctx](ALvoice &voice) -> void
        {
            const ALvoice::State vstate{voice.mPlayState.load(std::memory_order_acquire)};
            if(vstate == ALvoice::Stopped) return;
            const ALuint sid{voice.mSourceID.load(std::memory_order_relaxed)};
            if(voice.mStep < 1) return;

            MixVoice(&voice, vstate, sid, ctx, SamplesToDo);
        }
    );

    /* Process effects. */
    if(auxslots->size() < 1) return;
    auto slots = auxslots->data();
    auto slots_end = slots + auxslots->size();

    /* First sort the slots into scratch storage, so that effects come before
     * their effect target (or their targets' target).
     */
    auto sorted_slots = const_cast<ALeffectslot**>(slots_end);
    auto sorted_slots_end = sorted_slots;
    auto in_chain = [](const ALeffectslot *slot1, const ALeffectslot *slot2) noexcept -> bool
    {
        while((slot1=slot1->Params.Target) != nullptr) {
            if(slot1 == slot2) return true;
        }
        return false;
    };

    *sorted_slots_end = *slots;
    ++sorted_slots_end;
    while(++slots != slots_end)
    {
        /* If this effect slot targets an effect slot already in the list (i.e.
         * slots outputs to something in sorted_slots), directly or indirectly,
         * insert it prior to that element.
         */
        auto checker = sorted_slots;
        do {
            if(in_chain(*slots, *checker)) break;
        } while(++checker != sorted_slots_end);

        checker = std::move_backward(checker, sorted_slots_end, sorted_slots_end+1);
        *--checker = *slots;
        ++sorted_slots_end;
    }

    std::for_each(sorted_slots, sorted_slots_end,
        [SamplesToDo](const ALeffectslot *slot) -> void
        {
            EffectState *state{slot->Params.mEffectState};
            state->process(SamplesToDo, slot->Wet.Buffer.data(),
                static_cast<ALsizei>(slot->Wet.Buffer.size()), state->mOutTarget);
        }
    );
}


void ApplyStablizer(FrontStablizer *Stablizer, const al::span<FloatBufferLine> Buffer,
    const ALuint lidx, const ALuint ridx, const ALuint cidx, const ALsizei SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    /* Apply a delay to all channels, except the front-left and front-right, so
     * they maintain correct timing.
     */
    const size_t NumChannels{Buffer.size()};
    for(size_t i{0u};i < NumChannels;i++)
    {
        if(i == lidx || i == ridx)
            continue;

        auto &DelayBuf = Stablizer->DelayBuf[i];
        auto buffer_end = Buffer[i].begin() + SamplesToDo;
        if(LIKELY(SamplesToDo >= ALsizei{FrontStablizer::DelayLength}))
        {
            auto delay_end = std::rotate(Buffer[i].begin(),
                buffer_end - FrontStablizer::DelayLength, buffer_end);
            std::swap_ranges(Buffer[i].begin(), delay_end, std::begin(DelayBuf));
        }
        else
        {
            auto delay_start = std::swap_ranges(Buffer[i].begin(), buffer_end,
                std::begin(DelayBuf));
            std::rotate(std::begin(DelayBuf), delay_start, std::end(DelayBuf));
        }
    }

    ALfloat (&lsplit)[2][BUFFERSIZE] = Stablizer->LSplit;
    ALfloat (&rsplit)[2][BUFFERSIZE] = Stablizer->RSplit;
    auto &tmpbuf = Stablizer->TempBuf;

    /* This applies the band-splitter, preserving phase at the cost of some
     * delay. The shorter the delay, the more error seeps into the result.
     */
    auto apply_splitter = [&tmpbuf,SamplesToDo](const FloatBufferLine &Buffer,
        ALfloat (&DelayBuf)[FrontStablizer::DelayLength], BandSplitter &Filter,
        ALfloat (&splitbuf)[2][BUFFERSIZE]) -> void
    {
        /* Combine the delayed samples and the input samples into the temp
         * buffer, in reverse. Then copy the final samples back into the delay
         * buffer for next time. Note that the delay buffer's samples are
         * stored backwards here.
         */
        auto tmpbuf_end = std::begin(tmpbuf) + SamplesToDo;
        std::copy_n(std::begin(DelayBuf), FrontStablizer::DelayLength, tmpbuf_end);
        std::reverse_copy(Buffer.begin(), Buffer.begin()+SamplesToDo, std::begin(tmpbuf));
        std::copy_n(std::begin(tmpbuf), FrontStablizer::DelayLength, std::begin(DelayBuf));

        /* Apply an all-pass on the reversed signal, then reverse the samples
         * to get the forward signal with a reversed phase shift.
         */
        Filter.applyAllpass(tmpbuf, SamplesToDo+FrontStablizer::DelayLength);
        std::reverse(std::begin(tmpbuf), tmpbuf_end+FrontStablizer::DelayLength);

        /* Now apply the band-splitter, combining its phase shift with the
         * reversed phase shift, restoring the original phase on the split
         * signal.
         */
        Filter.process(splitbuf[1], splitbuf[0], tmpbuf, SamplesToDo);
    };
    apply_splitter(Buffer[lidx], Stablizer->DelayBuf[lidx], Stablizer->LFilter, lsplit);
    apply_splitter(Buffer[ridx], Stablizer->DelayBuf[ridx], Stablizer->RFilter, rsplit);

    for(ALsizei i{0};i < SamplesToDo;i++)
    {
        ALfloat lfsum{lsplit[0][i] + rsplit[0][i]};
        ALfloat hfsum{lsplit[1][i] + rsplit[1][i]};
        ALfloat s{lsplit[0][i] + lsplit[1][i] - rsplit[0][i] - rsplit[1][i]};

        /* This pans the separate low- and high-frequency sums between being on
         * the center channel and the left/right channels. The low-frequency
         * sum is 1/3rd toward center (2/3rds on left/right) and the high-
         * frequency sum is 1/4th toward center (3/4ths on left/right). These
         * values can be tweaked.
         */
        ALfloat m{lfsum*std::cos(1.0f/3.0f * (al::MathDefs<float>::Pi()*0.5f)) +
            hfsum*std::cos(1.0f/4.0f * (al::MathDefs<float>::Pi()*0.5f))};
        ALfloat c{lfsum*std::sin(1.0f/3.0f * (al::MathDefs<float>::Pi()*0.5f)) +
            hfsum*std::sin(1.0f/4.0f * (al::MathDefs<float>::Pi()*0.5f))};

        /* The generated center channel signal adds to the existing signal,
         * while the modified left and right channels replace.
         */
        Buffer[lidx][i] = (m + s) * 0.5f;
        Buffer[ridx][i] = (m - s) * 0.5f;
        Buffer[cidx][i] += c * 0.5f;
    }
}

void ApplyDistanceComp(const al::span<FloatBufferLine> Samples, const ALsizei SamplesToDo,
    const DistanceComp::DistData *distcomp)
{
    ASSUME(SamplesToDo > 0);

    for(auto &chanbuffer : Samples)
    {
        const ALfloat gain{distcomp->Gain};
        const ALsizei base{distcomp->Length};
        ALfloat *distbuf{al::assume_aligned<16>(distcomp->Buffer)};
        ++distcomp;

        if(base < 1)
            continue;

        ALfloat *inout{al::assume_aligned<16>(chanbuffer.data())};
        auto inout_end = inout + SamplesToDo;
        if(LIKELY(SamplesToDo >= base))
        {
            auto delay_end = std::rotate(inout, inout_end - base, inout_end);
            std::swap_ranges(inout, delay_end, distbuf);
        }
        else
        {
            auto delay_start = std::swap_ranges(inout, inout_end, distbuf);
            std::rotate(distbuf, delay_start, distbuf + base);
        }
        std::transform(inout, inout_end, inout, std::bind(std::multiplies<float>{}, _1, gain));
    }
}

void ApplyDither(const al::span<FloatBufferLine> Samples, ALuint *dither_seed,
    const ALfloat quant_scale, const ALsizei SamplesToDo)
{
    /* Dithering. Generate whitenoise (uniform distribution of random values
     * between -1 and +1) and add it to the sample values, after scaling up to
     * the desired quantization depth amd before rounding.
     */
    const ALfloat invscale{1.0f / quant_scale};
    ALuint seed{*dither_seed};
    auto dither_channel = [&seed,invscale,quant_scale,SamplesToDo](FloatBufferLine &input) -> void
    {
        ASSUME(SamplesToDo > 0);
        auto dither_sample = [&seed,invscale,quant_scale](const ALfloat sample) noexcept -> ALfloat
        {
            ALfloat val{sample * quant_scale};
            ALuint rng0{dither_rng(&seed)};
            ALuint rng1{dither_rng(&seed)};
            val += static_cast<ALfloat>(rng0*(1.0/UINT_MAX) - rng1*(1.0/UINT_MAX));
            return fast_roundf(val) * invscale;
        };
        std::transform(input.begin(), input.begin()+SamplesToDo, input.begin(), dither_sample);
    };
    std::for_each(Samples.begin(), Samples.end(), dither_channel);
    *dither_seed = seed;
}


/* Base template left undefined. Should be marked =delete, but Clang 3.8.1
 * chokes on that given the inline specializations.
 */
template<typename T>
inline T SampleConv(ALfloat) noexcept;

template<> inline ALfloat SampleConv(ALfloat val) noexcept
{ return val; }
template<> inline ALint SampleConv(ALfloat val) noexcept
{
    /* Floats have a 23-bit mantissa, plus an implied 1 bit and a sign bit.
     * This means a normalized float has at most 25 bits of signed precision.
     * When scaling and clamping for a signed 32-bit integer, these following
     * values are the best a float can give.
     */
    return fastf2i(clampf(val*2147483648.0f, -2147483648.0f, 2147483520.0f));
}
template<> inline ALshort SampleConv(ALfloat val) noexcept
{ return fastf2i(clampf(val*32768.0f, -32768.0f, 32767.0f)); }
template<> inline ALbyte SampleConv(ALfloat val) noexcept
{ return fastf2i(clampf(val*128.0f, -128.0f, 127.0f)); }

/* Define unsigned output variations. */
template<> inline ALuint SampleConv(ALfloat val) noexcept
{ return SampleConv<ALint>(val) + 2147483648u; }
template<> inline ALushort SampleConv(ALfloat val) noexcept
{ return SampleConv<ALshort>(val) + 32768; }
template<> inline ALubyte SampleConv(ALfloat val) noexcept
{ return SampleConv<ALbyte>(val) + 128; }

template<DevFmtType T>
void Write(const al::span<const FloatBufferLine> InBuffer, ALvoid *OutBuffer, const size_t Offset,
    const ALsizei SamplesToDo)
{
    using SampleType = typename DevFmtTypeTraits<T>::Type;

    const size_t numchans{InBuffer.size()};
    ASSUME(numchans > 0);

    SampleType *outbase = static_cast<SampleType*>(OutBuffer) + Offset*numchans;
    auto conv_channel = [&outbase,SamplesToDo,numchans](const FloatBufferLine &inbuf) -> void
    {
        ASSUME(SamplesToDo > 0);
        SampleType *out{outbase++};
        auto conv_sample = [numchans,&out](const ALfloat s) noexcept -> void
        {
            *out = SampleConv<SampleType>(s);
            out += numchans;
        };
        std::for_each(inbuf.begin(), inbuf.begin()+SamplesToDo, conv_sample);
    };
    std::for_each(InBuffer.cbegin(), InBuffer.cend(), conv_channel);
}

} // namespace

void aluMixData(ALCdevice *device, ALvoid *OutBuffer, ALsizei NumSamples)
{
    FPUCtl mixer_mode{};
    for(ALsizei SamplesDone{0};SamplesDone < NumSamples;)
    {
        const ALsizei SamplesToDo{mini(NumSamples-SamplesDone, BUFFERSIZE)};

        /* Clear main mixing buffers. */
        std::for_each(device->MixBuffer.begin(), device->MixBuffer.end(),
            [SamplesToDo](std::array<ALfloat,BUFFERSIZE> &buffer) -> void
            { std::fill_n(buffer.begin(), SamplesToDo, 0.0f); }
        );

        /* Increment the mix count at the start (lsb should now be 1). */
        IncrementRef(&device->MixCount);

        /* For each context on this device, process and mix its sources and
         * effects.
         */
        for(ALCcontext *ctx : *device->mContexts.load(std::memory_order_acquire))
            ProcessContext(ctx, SamplesToDo);

        /* Increment the clock time. Every second's worth of samples is
         * converted and added to clock base so that large sample counts don't
         * overflow during conversion. This also guarantees a stable
         * conversion.
         */
        device->SamplesDone += SamplesToDo;
        device->ClockBase += std::chrono::seconds{device->SamplesDone / device->Frequency};
        device->SamplesDone %= device->Frequency;

        /* Increment the mix count at the end (lsb should now be 0). */
        IncrementRef(&device->MixCount);

        /* Apply any needed post-process for finalizing the Dry mix to the
         * RealOut (Ambisonic decode, UHJ encode, etc).
         */
        if(LIKELY(device->PostProcess))
            device->PostProcess(device, SamplesToDo);
        const al::span<FloatBufferLine> RealOut{device->RealOut.Buffer};

        /* Apply front image stablization for surround sound, if applicable. */
        if(device->Stablizer)
        {
            const int lidx{GetChannelIdxByName(device->RealOut, FrontLeft)};
            const int ridx{GetChannelIdxByName(device->RealOut, FrontRight)};
            const int cidx{GetChannelIdxByName(device->RealOut, FrontCenter)};
            assert(lidx >= 0 && ridx >= 0 && cidx >= 0);

            ApplyStablizer(device->Stablizer.get(), RealOut, lidx, ridx, cidx, SamplesToDo);
        }

        /* Apply compression, limiting sample amplitude if needed or desired. */
        if(Compressor *comp{device->Limiter.get()})
            comp->process(SamplesToDo, RealOut.data());

        /* Apply delays and attenuation for mismatched speaker distances. */
        ApplyDistanceComp(RealOut, SamplesToDo, device->ChannelDelay.as_span().cbegin());

        /* Apply dithering. The compressor should have left enough headroom for
         * the dither noise to not saturate.
         */
        if(device->DitherDepth > 0.0f)
            ApplyDither(RealOut, &device->DitherSeed, device->DitherDepth, SamplesToDo);

        if(LIKELY(OutBuffer))
        {
            /* Finally, interleave and convert samples, writing to the device's
             * output buffer.
             */
            switch(device->FmtType)
            {
#define HANDLE_WRITE(T) case T:                                            \
    Write<T>(RealOut, OutBuffer, SamplesDone, SamplesToDo); break;
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

        SamplesDone += SamplesToDo;
    }
}


void aluHandleDisconnect(ALCdevice *device, const char *msg, ...)
{
    if(!device->Connected.exchange(false, std::memory_order_acq_rel))
        return;

    AsyncEvent evt{EventType_Disconnected};
    evt.u.user.type = AL_EVENT_TYPE_DISCONNECTED_SOFT;
    evt.u.user.id = 0;
    evt.u.user.param = 0;

    va_list args;
    va_start(args, msg);
    int msglen{vsnprintf(evt.u.user.msg, sizeof(evt.u.user.msg), msg, args)};
    va_end(args);

    if(msglen < 0 || static_cast<size_t>(msglen) >= sizeof(evt.u.user.msg))
        evt.u.user.msg[sizeof(evt.u.user.msg)-1] = 0;

    for(ALCcontext *ctx : *device->mContexts.load())
    {
        const ALbitfieldSOFT enabledevt{ctx->EnabledEvts.load(std::memory_order_acquire)};
        if((enabledevt&EventType_Disconnected))
        {
            RingBuffer *ring{ctx->AsyncEvents.get()};
            auto evt_data = ring->getWriteVector().first;
            if(evt_data.len > 0)
            {
                new (evt_data.buf) AsyncEvent{evt};
                ring->writeAdvance(1);
                ctx->EventSem.post();
            }
        }

        auto stop_voice = [](ALvoice &voice) -> void
        {
            voice.mCurrentBuffer.store(nullptr, std::memory_order_relaxed);
            voice.mLoopBuffer.store(nullptr, std::memory_order_relaxed);
            voice.mSourceID.store(0u, std::memory_order_relaxed);
            voice.mPlayState.store(ALvoice::Stopped, std::memory_order_release);
        };
        std::for_each(ctx->Voices->begin(),
            ctx->Voices->begin() + ctx->VoiceCount.load(std::memory_order_acquire),
            stop_voice);
    }
}
