/**
 * OpenAL cross platform audio library
 * Copyright (C) 2013 by Anis A. Hireche
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

#include <stdlib.h>

#include "alMain.h"
#include "alcontext.h"
#include "alu.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "vecmat.h"


#define AMP_ENVELOPE_MIN  0.5f
#define AMP_ENVELOPE_MAX  2.0f

#define ATTACK_TIME  0.1f /* 100ms to rise from min to max */
#define RELEASE_TIME 0.2f /* 200ms to drop from max to min */


struct ALcompressorState final : public EffectState {
    /* Effect gains for each channel */
    ALfloat mGain[MAX_EFFECT_CHANNELS][MAX_OUTPUT_CHANNELS]{};

    /* Effect parameters */
    ALboolean mEnabled{AL_TRUE};
    ALfloat mAttackMult{1.0f};
    ALfloat mReleaseMult{1.0f};
    ALfloat mEnvFollower{1.0f};


    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props, const EffectTarget target) override;
    void process(ALsizei samplesToDo, const ALfloat (*RESTRICT samplesIn)[BUFFERSIZE], ALfloat (*RESTRICT samplesOut)[BUFFERSIZE], ALsizei numChannels) override;

    DEF_NEWDEL(ALcompressorState)
};

ALboolean ALcompressorState::deviceUpdate(const ALCdevice *device)
{
    /* Number of samples to do a full attack and release (non-integer sample
     * counts are okay).
     */
    const ALfloat attackCount  = static_cast<ALfloat>(device->Frequency) * ATTACK_TIME;
    const ALfloat releaseCount = static_cast<ALfloat>(device->Frequency) * RELEASE_TIME;

    /* Calculate per-sample multipliers to attack and release at the desired
     * rates.
     */
    mAttackMult  = powf(AMP_ENVELOPE_MAX/AMP_ENVELOPE_MIN, 1.0f/attackCount);
    mReleaseMult = powf(AMP_ENVELOPE_MIN/AMP_ENVELOPE_MAX, 1.0f/releaseCount);

    return AL_TRUE;
}

void ALcompressorState::update(const ALCcontext* UNUSED(context), const ALeffectslot *slot, const ALeffectProps *props, const EffectTarget target)
{
    mEnabled = props->Compressor.OnOff;

    mOutBuffer = target.FOAOut->Buffer;
    mOutChannels = target.FOAOut->NumChannels;
    for(ALsizei i{0};i < 4;i++)
        ComputePanGains(target.FOAOut, alu::Matrix::Identity()[i].data(),
            slot->Params.Gain, mGain[i]);
}

void ALcompressorState::process(ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    ALsizei i, j, k;
    ALsizei base;

    for(base = 0;base < SamplesToDo;)
    {
        ALfloat gains[256];
        ALsizei td = mini(256, SamplesToDo-base);
        ALfloat env = mEnvFollower;

        /* Generate the per-sample gains from the signal envelope. */
        if(mEnabled)
        {
            for(i = 0;i < td;++i)
            {
                /* Clamp the absolute amplitude to the defined envelope limits,
                 * then attack or release the envelope to reach it.
                 */
                ALfloat amplitude = clampf(fabsf(SamplesIn[0][base+i]),
                                           AMP_ENVELOPE_MIN, AMP_ENVELOPE_MAX);
                if(amplitude > env)
                    env = minf(env*mAttackMult, amplitude);
                else if(amplitude < env)
                    env = maxf(env*mReleaseMult, amplitude);

                /* Apply the reciprocal of the envelope to normalize the volume
                 * (compress the dynamic range).
                 */
                gains[i] = 1.0f / env;
            }
        }
        else
        {
            /* Same as above, except the amplitude is forced to 1. This helps
             * ensure smooth gain changes when the compressor is turned on and
             * off.
             */
            for(i = 0;i < td;++i)
            {
                ALfloat amplitude = 1.0f;
                if(amplitude > env)
                    env = minf(env*mAttackMult, amplitude);
                else if(amplitude < env)
                    env = maxf(env*mReleaseMult, amplitude);

                gains[i] = 1.0f / env;
            }
        }
        mEnvFollower = env;

        /* Now compress the signal amplitude to output. */
        for(j = 0;j < MAX_EFFECT_CHANNELS;j++)
        {
            for(k = 0;k < NumChannels;k++)
            {
                ALfloat gain = mGain[j][k];
                if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
                    continue;

                for(i = 0;i < td;i++)
                    SamplesOut[k][base+i] += SamplesIn[j][base+i] * gains[i] * gain;
            }
        }

        base += td;
    }
}


struct CompressorStateFactory final : public EffectStateFactory {
    EffectState *create() override;
};

EffectState *CompressorStateFactory::create()
{ return new ALcompressorState{}; }

EffectStateFactory *CompressorStateFactory_getFactory(void)
{
    static CompressorStateFactory CompressorFactory{};
    return &CompressorFactory;
}


void ALcompressor_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_COMPRESSOR_ONOFF:
            if(!(val >= AL_COMPRESSOR_MIN_ONOFF && val <= AL_COMPRESSOR_MAX_ONOFF))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Compressor state out of range");
            props->Compressor.OnOff = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid compressor integer property 0x%04x",
                       param);
    }
}
void ALcompressor_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{ ALcompressor_setParami(effect, context, param, vals[0]); }
void ALcompressor_setParamf(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat UNUSED(val))
{ alSetError(context, AL_INVALID_ENUM, "Invalid compressor float property 0x%04x", param); }
void ALcompressor_setParamfv(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, const ALfloat *UNUSED(vals))
{ alSetError(context, AL_INVALID_ENUM, "Invalid compressor float-vector property 0x%04x", param); }

void ALcompressor_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{ 
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_COMPRESSOR_ONOFF:
            *val = props->Compressor.OnOff;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid compressor integer property 0x%04x",
                       param);
    }
}
void ALcompressor_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{ ALcompressor_getParami(effect, context, param, vals); }
void ALcompressor_getParamf(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat *UNUSED(val))
{ alSetError(context, AL_INVALID_ENUM, "Invalid compressor float property 0x%04x", param); }
void ALcompressor_getParamfv(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat *UNUSED(vals))
{ alSetError(context, AL_INVALID_ENUM, "Invalid compressor float-vector property 0x%04x", param); }

DEFINE_ALEFFECT_VTABLE(ALcompressor);
