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
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alListener.h"
#include "alAuxEffectSlot.h"
#include "alu.h"
#include "bs2b.h"


ALuint aluBytesFromFormat(ALenum format)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_STEREO8:
        case AL_FORMAT_QUAD8_LOKI:
        case AL_FORMAT_QUAD8:
        case AL_FORMAT_REAR8:
        case AL_FORMAT_51CHN8:
        case AL_FORMAT_61CHN8:
        case AL_FORMAT_71CHN8:
            return 1;

        case AL_FORMAT_MONO16:
        case AL_FORMAT_STEREO16:
        case AL_FORMAT_QUAD16_LOKI:
        case AL_FORMAT_QUAD16:
        case AL_FORMAT_REAR16:
        case AL_FORMAT_51CHN16:
        case AL_FORMAT_61CHN16:
        case AL_FORMAT_71CHN16:
            return 2;

        case AL_FORMAT_MONO_FLOAT32:
        case AL_FORMAT_STEREO_FLOAT32:
        case AL_FORMAT_QUAD32:
        case AL_FORMAT_REAR32:
        case AL_FORMAT_51CHN32:
        case AL_FORMAT_61CHN32:
        case AL_FORMAT_71CHN32:
            return 4;

        case AL_FORMAT_MONO_DOUBLE_EXT:
        case AL_FORMAT_STEREO_DOUBLE_EXT:
            return 8;

        case AL_FORMAT_MONO_MULAW:
        case AL_FORMAT_STEREO_MULAW:
        case AL_FORMAT_QUAD_MULAW:
        case AL_FORMAT_REAR_MULAW:
        case AL_FORMAT_51CHN_MULAW:
        case AL_FORMAT_61CHN_MULAW:
        case AL_FORMAT_71CHN_MULAW:
            return 1;

        default:
            return 0;
    }
}
ALuint aluChannelsFromFormat(ALenum format)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_MONO16:
        case AL_FORMAT_MONO_FLOAT32:
        case AL_FORMAT_MONO_DOUBLE_EXT:
        case AL_FORMAT_MONO_MULAW:
            return 1;

        case AL_FORMAT_STEREO8:
        case AL_FORMAT_STEREO16:
        case AL_FORMAT_STEREO_FLOAT32:
        case AL_FORMAT_STEREO_DOUBLE_EXT:
        case AL_FORMAT_STEREO_MULAW:
            return 2;

        case AL_FORMAT_QUAD8_LOKI:
        case AL_FORMAT_QUAD16_LOKI:
        case AL_FORMAT_QUAD8:
        case AL_FORMAT_QUAD16:
        case AL_FORMAT_QUAD32:
        case AL_FORMAT_QUAD_MULAW:
            return 4;

        case AL_FORMAT_REAR8:
        case AL_FORMAT_REAR16:
        case AL_FORMAT_REAR32:
        case AL_FORMAT_REAR_MULAW:
            return 2;

        case AL_FORMAT_51CHN8:
        case AL_FORMAT_51CHN16:
        case AL_FORMAT_51CHN32:
        case AL_FORMAT_51CHN_MULAW:
            return 6;

        case AL_FORMAT_61CHN8:
        case AL_FORMAT_61CHN16:
        case AL_FORMAT_61CHN32:
        case AL_FORMAT_61CHN_MULAW:
            return 7;

        case AL_FORMAT_71CHN8:
        case AL_FORMAT_71CHN16:
        case AL_FORMAT_71CHN32:
        case AL_FORMAT_71CHN_MULAW:
            return 8;

        default:
            return 0;
    }
}


static __inline ALvoid aluCrossproduct(const ALfloat *inVector1, const ALfloat *inVector2, ALfloat *outVector)
{
    outVector[0] = inVector1[1]*inVector2[2] - inVector1[2]*inVector2[1];
    outVector[1] = inVector1[2]*inVector2[0] - inVector1[0]*inVector2[2];
    outVector[2] = inVector1[0]*inVector2[1] - inVector1[1]*inVector2[0];
}

static __inline ALfloat aluDotproduct(const ALfloat *inVector1, const ALfloat *inVector2)
{
    return inVector1[0]*inVector2[0] + inVector1[1]*inVector2[1] +
           inVector1[2]*inVector2[2];
}

static __inline ALvoid aluNormalize(ALfloat *inVector)
{
    ALfloat length, inverse_length;

    length = aluSqrt(aluDotproduct(inVector, inVector));
    if(length != 0.0f)
    {
        inverse_length = 1.0f/length;
        inVector[0] *= inverse_length;
        inVector[1] *= inverse_length;
        inVector[2] *= inverse_length;
    }
}

static __inline ALvoid aluMatrixVector(ALfloat *vector,ALfloat w,ALfloat matrix[4][4])
{
    ALfloat temp[4] = {
        vector[0], vector[1], vector[2], w
    };

    vector[0] = temp[0]*matrix[0][0] + temp[1]*matrix[1][0] + temp[2]*matrix[2][0] + temp[3]*matrix[3][0];
    vector[1] = temp[0]*matrix[0][1] + temp[1]*matrix[1][1] + temp[2]*matrix[2][1] + temp[3]*matrix[3][1];
    vector[2] = temp[0]*matrix[0][2] + temp[1]*matrix[1][2] + temp[2]*matrix[2][2] + temp[3]*matrix[3][2];
}


ALvoid CalcNonAttnSourceParams(ALsource *ALSource, const ALCcontext *ALContext)
{
    ALfloat SourceVolume,ListenerGain,MinVolume,MaxVolume;
    ALbufferlistitem *BufferListItem;
    enum DevFmtChannels DevChans;
    enum FmtChannels Channels;
    ALfloat DryGain, DryGainHF;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALint NumSends, Frequency;
    ALboolean DupStereo;
    ALfloat Pitch;
    ALfloat cw;
    ALint i;

    /* Get device properties */
    DevChans  = ALContext->Device->FmtChans;
    DupStereo = ALContext->Device->DuplicateStereo;
    NumSends  = ALContext->Device->NumAuxSends;
    Frequency = ALContext->Device->Frequency;

    /* Get listener properties */
    ListenerGain = ALContext->Listener.Gain;

    /* Get source properties */
    SourceVolume = ALSource->flGain;
    MinVolume    = ALSource->flMinGain;
    MaxVolume    = ALSource->flMaxGain;
    Pitch        = ALSource->flPitch;

    /* Calculate the stepping value */
    Channels = FmtMono;
    BufferListItem = ALSource->queue;
    while(BufferListItem != NULL)
    {
        ALbuffer *ALBuffer;
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            ALint maxstep = STACK_DATA_SIZE / FrameSizeFromFmt(ALBuffer->FmtChannels,
                                                               ALBuffer->FmtType);
            maxstep -= ResamplerPadding[ALSource->Resampler] +
                       ResamplerPrePadding[ALSource->Resampler] + 1;
            maxstep = min(maxstep, INT_MAX>>FRACTIONBITS);

            Pitch = Pitch * ALBuffer->Frequency / Frequency;
            if(Pitch > (ALfloat)maxstep)
                ALSource->Params.Step = maxstep<<FRACTIONBITS;
            else
            {
                ALSource->Params.Step = Pitch*FRACTIONONE;
                if(ALSource->Params.Step == 0)
                    ALSource->Params.Step = 1;
            }

            Channels = ALBuffer->FmtChannels;
            break;
        }
        BufferListItem = BufferListItem->next;
    }

    /* Calculate gains */
    DryGain = SourceVolume;
    DryGain = __min(DryGain,MaxVolume);
    DryGain = __max(DryGain,MinVolume);
    DryGainHF = 1.0f;

    switch(ALSource->DirectFilter.type)
    {
        case AL_FILTER_LOWPASS:
            DryGain *= ALSource->DirectFilter.Gain;
            DryGainHF *= ALSource->DirectFilter.GainHF;
            break;
    }

    if(Channels == FmtStereo)
    {
        for(i = 0;i < MAXCHANNELS;i++)
            ALSource->Params.DryGains[i] = 0.0f;

        if(DupStereo == AL_FALSE)
        {
            ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
            ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
        }
        else
        {
            switch(DevChans)
            {
            case DevFmtMono:
            case DevFmtStereo:
                ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
                break;

            case DevFmtQuad:
            case DevFmtX51:
                DryGain *= aluSqrt(2.0f/4.0f);
                ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
                ALSource->Params.DryGains[BACK_LEFT]   = DryGain * ListenerGain;
                ALSource->Params.DryGains[BACK_RIGHT]  = DryGain * ListenerGain;
                break;

            case DevFmtX61:
                DryGain *= aluSqrt(2.0f/4.0f);
                ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
                ALSource->Params.DryGains[SIDE_LEFT]   = DryGain * ListenerGain;
                ALSource->Params.DryGains[SIDE_RIGHT]  = DryGain * ListenerGain;
                break;

            case DevFmtX71:
                DryGain *= aluSqrt(2.0f/6.0f);
                ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
                ALSource->Params.DryGains[BACK_LEFT]   = DryGain * ListenerGain;
                ALSource->Params.DryGains[BACK_RIGHT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[SIDE_LEFT]   = DryGain * ListenerGain;
                ALSource->Params.DryGains[SIDE_RIGHT]  = DryGain * ListenerGain;
                break;
            }
        }
    }
    else
    {
        for(i = 0;i < MAXCHANNELS;i++)
            ALSource->Params.DryGains[i] = DryGain * ListenerGain;
    }

    for(i = 0;i < NumSends;i++)
    {
        WetGain[i] = SourceVolume;
        WetGain[i] = __min(WetGain[i],MaxVolume);
        WetGain[i] = __max(WetGain[i],MinVolume);
        WetGainHF[i] = 1.0f;

        switch(ALSource->Send[i].WetFilter.type)
        {
            case AL_FILTER_LOWPASS:
                WetGain[i] *= ALSource->Send[i].WetFilter.Gain;
                WetGainHF[i] *= ALSource->Send[i].WetFilter.GainHF;
                break;
        }

        ALSource->Params.Send[i].WetGain = WetGain[i] * ListenerGain;
    }

    /* Update filter coefficients. Calculations based on the I3DL2
     * spec. */
    cw = cos(2.0*M_PI * LOWPASSFREQCUTOFF / Frequency);

    /* We use two chained one-pole filters, so we need to take the
     * square root of the squared gain, which is the same as the base
     * gain. */
    ALSource->Params.iirFilter.coeff = lpCoeffCalc(DryGainHF, cw);

    for(i = 0;i < NumSends;i++)
    {
        /* We use a one-pole filter, so we need to take the squared gain */
        ALfloat a = lpCoeffCalc(WetGainHF[i]*WetGainHF[i], cw);
        ALSource->Params.Send[i].iirFilter.coeff = a;
    }
}

ALvoid CalcSourceParams(ALsource *ALSource, const ALCcontext *ALContext)
{
    const ALCdevice *Device = ALContext->Device;
    ALfloat InnerAngle,OuterAngle,Angle,Distance,OrigDist;
    ALfloat Direction[3],Position[3],SourceToListener[3];
    ALfloat Velocity[3],ListenerVel[3];
    ALfloat MinVolume,MaxVolume,MinDist,MaxDist,Rolloff,OuterGainHF;
    ALfloat ConeVolume,ConeHF,SourceVolume,ListenerGain;
    ALfloat DopplerFactor, DopplerVelocity, SpeedOfSound;
    ALfloat AirAbsorptionFactor;
    ALbufferlistitem *BufferListItem;
    ALfloat Attenuation, EffectiveDist;
    ALfloat RoomAttenuation[MAX_SENDS];
    ALfloat MetersPerUnit;
    ALfloat RoomRolloff[MAX_SENDS];
    ALfloat DryGain;
    ALfloat DryGainHF;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALfloat DirGain, AmbientGain;
    const ALfloat *SpeakerGain;
    ALfloat Pitch;
    ALfloat length;
    ALuint Frequency;
    ALint NumSends;
    ALint pos, s, i;
    ALfloat cw;

    DryGainHF = 1.0f;
    for(i = 0;i < MAX_SENDS;i++)
        WetGainHF[i] = 1.0f;

    //Get context properties
    DopplerFactor   = ALContext->DopplerFactor * ALSource->DopplerFactor;
    DopplerVelocity = ALContext->DopplerVelocity;
    SpeedOfSound    = ALContext->flSpeedOfSound;
    NumSends        = Device->NumAuxSends;
    Frequency       = Device->Frequency;

    //Get listener properties
    ListenerGain = ALContext->Listener.Gain;
    MetersPerUnit = ALContext->Listener.MetersPerUnit;
    memcpy(ListenerVel, ALContext->Listener.Velocity, sizeof(ALContext->Listener.Velocity));

    //Get source properties
    SourceVolume = ALSource->flGain;
    memcpy(Position,  ALSource->vPosition,    sizeof(ALSource->vPosition));
    memcpy(Direction, ALSource->vOrientation, sizeof(ALSource->vOrientation));
    memcpy(Velocity,  ALSource->vVelocity,    sizeof(ALSource->vVelocity));
    MinVolume    = ALSource->flMinGain;
    MaxVolume    = ALSource->flMaxGain;
    MinDist      = ALSource->flRefDistance;
    MaxDist      = ALSource->flMaxDistance;
    Rolloff      = ALSource->flRollOffFactor;
    InnerAngle   = ALSource->flInnerAngle;
    OuterAngle   = ALSource->flOuterAngle;
    OuterGainHF  = ALSource->OuterGainHF;
    AirAbsorptionFactor = ALSource->AirAbsorptionFactor;

    //1. Translate Listener to origin (convert to head relative)
    if(ALSource->bHeadRelative == AL_FALSE)
    {
        ALfloat U[3],V[3],N[3];
        ALfloat Matrix[4][4];

        // Build transform matrix
        memcpy(N, ALContext->Listener.Forward, sizeof(N));  // At-vector
        aluNormalize(N);  // Normalized At-vector
        memcpy(V, ALContext->Listener.Up, sizeof(V));  // Up-vector
        aluNormalize(V);  // Normalized Up-vector
        aluCrossproduct(N, V, U); // Right-vector
        aluNormalize(U);  // Normalized Right-vector
        Matrix[0][0] = U[0]; Matrix[0][1] = V[0]; Matrix[0][2] = -N[0]; Matrix[0][3] = 0.0f;
        Matrix[1][0] = U[1]; Matrix[1][1] = V[1]; Matrix[1][2] = -N[1]; Matrix[1][3] = 0.0f;
        Matrix[2][0] = U[2]; Matrix[2][1] = V[2]; Matrix[2][2] = -N[2]; Matrix[2][3] = 0.0f;
        Matrix[3][0] = 0.0f; Matrix[3][1] = 0.0f; Matrix[3][2] =  0.0f; Matrix[3][3] = 1.0f;

        // Translate position
        Position[0] -= ALContext->Listener.Position[0];
        Position[1] -= ALContext->Listener.Position[1];
        Position[2] -= ALContext->Listener.Position[2];

        // Transform source position and direction into listener space
        aluMatrixVector(Position, 1.0f, Matrix);
        aluMatrixVector(Direction, 0.0f, Matrix);
        // Transform source and listener velocity into listener space
        aluMatrixVector(Velocity, 0.0f, Matrix);
        aluMatrixVector(ListenerVel, 0.0f, Matrix);
    }
    else
        ListenerVel[0] = ListenerVel[1] = ListenerVel[2] = 0.0f;

    SourceToListener[0] = -Position[0];
    SourceToListener[1] = -Position[1];
    SourceToListener[2] = -Position[2];
    aluNormalize(SourceToListener);
    aluNormalize(Direction);

    //2. Calculate distance attenuation
    Distance = aluSqrt(aluDotproduct(Position, Position));
    OrigDist = Distance;

    Attenuation = 1.0f;
    for(i = 0;i < NumSends;i++)
    {
        RoomAttenuation[i] = 1.0f;

        RoomRolloff[i] = ALSource->RoomRolloffFactor;
        if(ALSource->Send[i].Slot &&
           (ALSource->Send[i].Slot->effect.type == AL_EFFECT_REVERB ||
            ALSource->Send[i].Slot->effect.type == AL_EFFECT_EAXREVERB))
            RoomRolloff[i] += ALSource->Send[i].Slot->effect.Reverb.RoomRolloffFactor;
    }

    switch(ALContext->SourceDistanceModel ? ALSource->DistanceModel :
                                            ALContext->DistanceModel)
    {
        case AL_INVERSE_DISTANCE_CLAMPED:
            Distance=__max(Distance,MinDist);
            Distance=__min(Distance,MaxDist);
            if(MaxDist < MinDist)
                break;
            //fall-through
        case AL_INVERSE_DISTANCE:
            if(MinDist > 0.0f)
            {
                if((MinDist + (Rolloff * (Distance - MinDist))) > 0.0f)
                    Attenuation = MinDist / (MinDist + (Rolloff * (Distance - MinDist)));
                for(i = 0;i < NumSends;i++)
                {
                    if((MinDist + (RoomRolloff[i] * (Distance - MinDist))) > 0.0f)
                        RoomAttenuation[i] = MinDist / (MinDist + (RoomRolloff[i] * (Distance - MinDist)));
                }
            }
            break;

        case AL_LINEAR_DISTANCE_CLAMPED:
            Distance=__max(Distance,MinDist);
            Distance=__min(Distance,MaxDist);
            if(MaxDist < MinDist)
                break;
            //fall-through
        case AL_LINEAR_DISTANCE:
            if(MaxDist != MinDist)
            {
                Attenuation = 1.0f - (Rolloff*(Distance-MinDist)/(MaxDist - MinDist));
                Attenuation = __max(Attenuation, 0.0f);
                for(i = 0;i < NumSends;i++)
                {
                    RoomAttenuation[i] = 1.0f - (RoomRolloff[i]*(Distance-MinDist)/(MaxDist - MinDist));
                    RoomAttenuation[i] = __max(RoomAttenuation[i], 0.0f);
                }
            }
            break;

        case AL_EXPONENT_DISTANCE_CLAMPED:
            Distance=__max(Distance,MinDist);
            Distance=__min(Distance,MaxDist);
            if(MaxDist < MinDist)
                break;
            //fall-through
        case AL_EXPONENT_DISTANCE:
            if(Distance > 0.0f && MinDist > 0.0f)
            {
                Attenuation = aluPow(Distance/MinDist, -Rolloff);
                for(i = 0;i < NumSends;i++)
                    RoomAttenuation[i] = aluPow(Distance/MinDist, -RoomRolloff[i]);
            }
            break;

        case AL_NONE:
            break;
    }

    // Source Gain + Attenuation
    DryGain = SourceVolume * Attenuation;
    for(i = 0;i < NumSends;i++)
        WetGain[i] = SourceVolume * RoomAttenuation[i];

    EffectiveDist = 0.0f;
    if(MinDist > 0.0f && Attenuation < 1.0f)
        EffectiveDist = (MinDist/Attenuation - MinDist)*MetersPerUnit;

    // Distance-based air absorption
    if(AirAbsorptionFactor > 0.0f && EffectiveDist > 0.0f)
    {
        ALfloat absorb;

        // Absorption calculation is done in dB
        absorb = (AirAbsorptionFactor*AIRABSORBGAINDBHF) *
                 EffectiveDist;
        // Convert dB to linear gain before applying
        absorb = aluPow(10.0f, absorb/20.0f);

        DryGainHF *= absorb;
    }

    //3. Apply directional soundcones
    Angle = aluAcos(aluDotproduct(Direction,SourceToListener)) * 180.0f/M_PI;
    if(Angle >= InnerAngle && Angle <= OuterAngle)
    {
        ALfloat scale = (Angle-InnerAngle) / (OuterAngle-InnerAngle);
        ConeVolume = (1.0f+(ALSource->flOuterGain-1.0f)*scale);
        ConeHF = (1.0f+(OuterGainHF-1.0f)*scale);
    }
    else if(Angle > OuterAngle)
    {
        ConeVolume = (1.0f+(ALSource->flOuterGain-1.0f));
        ConeHF = (1.0f+(OuterGainHF-1.0f));
    }
    else
    {
        ConeVolume = 1.0f;
        ConeHF = 1.0f;
    }

    // Apply some high-frequency attenuation for sources behind the listener
    // NOTE: This should be aluDotproduct({0,0,-1}, ListenerToSource), however
    // that is equivalent to aluDotproduct({0,0,1}, SourceToListener), which is
    // the same as SourceToListener[2]
    Angle = aluAcos(SourceToListener[2]) * 180.0f/M_PI;
    // Sources within the minimum distance attenuate less
    if(OrigDist < MinDist)
        Angle *= OrigDist/MinDist;
    if(Angle > 90.0f)
    {
        ALfloat scale = (Angle-90.0f) / (180.1f-90.0f); // .1 to account for fp errors
        ConeHF *= 1.0f - (Device->HeadDampen*scale);
    }

    DryGain *= ConeVolume;
    if(ALSource->DryGainHFAuto)
        DryGainHF *= ConeHF;

    // Clamp to Min/Max Gain
    DryGain = __min(DryGain,MaxVolume);
    DryGain = __max(DryGain,MinVolume);

    for(i = 0;i < NumSends;i++)
    {
        ALeffectslot *Slot = ALSource->Send[i].Slot;

        if(!Slot || Slot->effect.type == AL_EFFECT_NULL)
        {
            ALSource->Params.Send[i].WetGain = 0.0f;
            WetGainHF[i] = 1.0f;
            continue;
        }

        if(Slot->AuxSendAuto)
        {
            if(ALSource->WetGainAuto)
                WetGain[i] *= ConeVolume;
            if(ALSource->WetGainHFAuto)
                WetGainHF[i] *= ConeHF;

            // Clamp to Min/Max Gain
            WetGain[i] = __min(WetGain[i],MaxVolume);
            WetGain[i] = __max(WetGain[i],MinVolume);

            if(Slot->effect.type == AL_EFFECT_REVERB ||
               Slot->effect.type == AL_EFFECT_EAXREVERB)
            {
                /* Apply a decay-time transformation to the wet path, based on
                 * the attenuation of the dry path.
                 *
                 * Using the approximate (effective) source to listener
                 * distance, the initial decay of the reverb effect is
                 * calculated and applied to the wet path.
                 */
                WetGain[i] *= aluPow(10.0f, EffectiveDist /
                                            (SPEEDOFSOUNDMETRESPERSEC *
                                             Slot->effect.Reverb.DecayTime) *
                                            -60.0 / 20.0);

                WetGainHF[i] *= aluPow(Slot->effect.Reverb.AirAbsorptionGainHF,
                                       AirAbsorptionFactor * EffectiveDist);
            }
        }
        else
        {
            /* If the slot's auxiliary send auto is off, the data sent to the
             * effect slot is the same as the dry path, sans filter effects */
            WetGain[i] = DryGain;
            WetGainHF[i] = DryGainHF;
        }

        switch(ALSource->Send[i].WetFilter.type)
        {
            case AL_FILTER_LOWPASS:
                WetGain[i] *= ALSource->Send[i].WetFilter.Gain;
                WetGainHF[i] *= ALSource->Send[i].WetFilter.GainHF;
                break;
        }
        ALSource->Params.Send[i].WetGain = WetGain[i] * ListenerGain;
    }

    // Apply filter gains and filters
    switch(ALSource->DirectFilter.type)
    {
        case AL_FILTER_LOWPASS:
            DryGain *= ALSource->DirectFilter.Gain;
            DryGainHF *= ALSource->DirectFilter.GainHF;
            break;
    }
    DryGain *= ListenerGain;

    // Calculate Velocity
    Pitch = ALSource->flPitch;
    if(DopplerFactor != 0.0f)
    {
        ALfloat VSS, VLS;
        ALfloat MaxVelocity = (SpeedOfSound*DopplerVelocity) /
                              DopplerFactor;

        VSS = aluDotproduct(Velocity, SourceToListener);
        if(VSS >= MaxVelocity)
            VSS = (MaxVelocity - 1.0f);
        else if(VSS <= -MaxVelocity)
            VSS = -MaxVelocity + 1.0f;

        VLS = aluDotproduct(ListenerVel, SourceToListener);
        if(VLS >= MaxVelocity)
            VLS = (MaxVelocity - 1.0f);
        else if(VLS <= -MaxVelocity)
            VLS = -MaxVelocity + 1.0f;

        Pitch *= ((SpeedOfSound*DopplerVelocity) - (DopplerFactor*VLS)) /
                 ((SpeedOfSound*DopplerVelocity) - (DopplerFactor*VSS));
    }

    BufferListItem = ALSource->queue;
    while(BufferListItem != NULL)
    {
        ALbuffer *ALBuffer;
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            ALint maxstep = STACK_DATA_SIZE / FrameSizeFromFmt(ALBuffer->FmtChannels,
                                                               ALBuffer->FmtType);
            maxstep -= ResamplerPadding[ALSource->Resampler] +
                       ResamplerPrePadding[ALSource->Resampler] + 1;
            maxstep = min(maxstep, INT_MAX>>FRACTIONBITS);

            Pitch = Pitch * ALBuffer->Frequency / Frequency;
            if(Pitch > (ALfloat)maxstep)
                ALSource->Params.Step = maxstep<<FRACTIONBITS;
            else
            {
                ALSource->Params.Step = Pitch*FRACTIONONE;
                if(ALSource->Params.Step == 0)
                    ALSource->Params.Step = 1;
            }
            break;
        }
        BufferListItem = BufferListItem->next;
    }

    // Use energy-preserving panning algorithm for multi-speaker playback
    length = __max(OrigDist, MinDist);
    if(length > 0.0f)
    {
        ALfloat invlen = 1.0f/length;
        Position[0] *= invlen;
        Position[1] *= invlen;
        Position[2] *= invlen;
    }

    pos = aluCart2LUTpos(-Position[2], Position[0]);
    SpeakerGain = &Device->PanningLUT[MAXCHANNELS * pos];

    DirGain = aluSqrt(Position[0]*Position[0] + Position[2]*Position[2]);
    // elevation adjustment for directional gain. this sucks, but
    // has low complexity
    AmbientGain = aluSqrt(1.0/Device->NumChan);
    for(s = 0;s < MAXCHANNELS;s++)
        ALSource->Params.DryGains[s] = 0.0f;
    for(s = 0;s < (ALsizei)Device->NumChan;s++)
    {
        Channel chan = Device->Speaker2Chan[s];
        ALfloat gain = AmbientGain + (SpeakerGain[chan]-AmbientGain)*DirGain;
        ALSource->Params.DryGains[chan] = DryGain * gain;
    }

    /* Update filter coefficients. */
    cw = cos(2.0*M_PI * LOWPASSFREQCUTOFF / Frequency);

    /* Spatialized sources use four chained one-pole filters, so we need to
     * take the fourth root of the squared gain, which is the same as the
     * square root of the base gain. */
    ALSource->Params.iirFilter.coeff = lpCoeffCalc(aluSqrt(DryGainHF), cw);

    for(i = 0;i < NumSends;i++)
    {
        /* The wet path uses two chained one-pole filters, so take the
         * base gain (square root of the squared gain) */
        ALSource->Params.Send[i].iirFilter.coeff = lpCoeffCalc(WetGainHF[i], cw);
    }
}


static __inline ALfloat aluF2F(ALfloat Value)
{
    return Value;
}
static __inline ALshort aluF2US(ALfloat Value)
{
    ALint i;

    if(Value <= -1.0f) i = 0;
    else if(Value >= 1.0f) i = 65535;
    else i = (ALint)(Value*32767.0f) + 32768;

    return ((ALushort)i);
}
static __inline ALshort aluF2S(ALfloat Value)
{
    ALint i;

    if(Value <= -1.0f) i = -32768;
    else if(Value >= 1.0f) i = 32767;
    else i = (ALint)(Value*32767.0f);

    return ((ALshort)i);
}
static __inline ALubyte aluF2UB(ALfloat Value)
{
    ALshort i = aluF2US(Value);
    return i>>8;
}
static __inline ALubyte aluF2B(ALfloat Value)
{
    ALshort i = aluF2S(Value);
    return i>>8;
}

static const Channel MonoChans[] = { FRONT_CENTER };
static const Channel StereoChans[] = { FRONT_LEFT, FRONT_RIGHT };
static const Channel QuadChans[] = { FRONT_LEFT, FRONT_RIGHT,
                                     BACK_LEFT, BACK_RIGHT };
static const Channel X51Chans[] = { FRONT_LEFT, FRONT_RIGHT,
                                    FRONT_CENTER, LFE,
                                    BACK_LEFT, BACK_RIGHT };
static const Channel X61Chans[] = { FRONT_LEFT, FRONT_LEFT,
                                    FRONT_CENTER, LFE, BACK_CENTER,
                                    SIDE_LEFT, SIDE_RIGHT };
static const Channel X71Chans[] = { FRONT_LEFT, FRONT_RIGHT,
                                    FRONT_CENTER, LFE,
                                    BACK_LEFT, BACK_RIGHT,
                                    SIDE_LEFT, SIDE_RIGHT };

#define DECL_TEMPLATE(T, chans,N, func)                                       \
static void Write_##T##_##chans(ALCdevice *device, T *buffer, ALuint SamplesToDo)\
{                                                                             \
    ALfloat (*DryBuffer)[MAXCHANNELS] = device->DryBuffer;                    \
    ALfloat (*Matrix)[MAXCHANNELS] = device->ChannelMatrix;                   \
    const ALuint *ChanMap = device->DevChannels;                              \
    ALuint i, j, c;                                                           \
                                                                              \
    for(i = 0;i < SamplesToDo;i++)                                            \
    {                                                                         \
        for(j = 0;j < N;j++)                                                  \
        {                                                                     \
            ALfloat samp = 0.0f;                                              \
            for(c = 0;c < MAXCHANNELS;c++)                                    \
                samp += DryBuffer[i][c] * Matrix[c][chans[j]];                \
            ((T*)buffer)[ChanMap[chans[j]]] = func(samp);                     \
        }                                                                     \
        buffer = ((T*)buffer) + N;                                            \
    }                                                                         \
}

DECL_TEMPLATE(ALfloat, MonoChans,1, aluF2F)
DECL_TEMPLATE(ALfloat, QuadChans,4, aluF2F)
DECL_TEMPLATE(ALfloat, X51Chans,6, aluF2F)
DECL_TEMPLATE(ALfloat, X61Chans,7, aluF2F)
DECL_TEMPLATE(ALfloat, X71Chans,8, aluF2F)

DECL_TEMPLATE(ALushort, MonoChans,1, aluF2US)
DECL_TEMPLATE(ALushort, QuadChans,4, aluF2US)
DECL_TEMPLATE(ALushort, X51Chans,6, aluF2US)
DECL_TEMPLATE(ALushort, X61Chans,7, aluF2US)
DECL_TEMPLATE(ALushort, X71Chans,8, aluF2US)

DECL_TEMPLATE(ALshort, MonoChans,1, aluF2S)
DECL_TEMPLATE(ALshort, QuadChans,4, aluF2S)
DECL_TEMPLATE(ALshort, X51Chans,6, aluF2S)
DECL_TEMPLATE(ALshort, X61Chans,7, aluF2S)
DECL_TEMPLATE(ALshort, X71Chans,8, aluF2S)

DECL_TEMPLATE(ALubyte, MonoChans,1, aluF2UB)
DECL_TEMPLATE(ALubyte, QuadChans,4, aluF2UB)
DECL_TEMPLATE(ALubyte, X51Chans,6, aluF2UB)
DECL_TEMPLATE(ALubyte, X61Chans,7, aluF2UB)
DECL_TEMPLATE(ALubyte, X71Chans,8, aluF2UB)

DECL_TEMPLATE(ALbyte, MonoChans,1, aluF2B)
DECL_TEMPLATE(ALbyte, QuadChans,4, aluF2B)
DECL_TEMPLATE(ALbyte, X51Chans,6, aluF2B)
DECL_TEMPLATE(ALbyte, X61Chans,7, aluF2B)
DECL_TEMPLATE(ALbyte, X71Chans,8, aluF2B)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T, chans,N, func)                                       \
static void Write_##T##_##chans(ALCdevice *device, T *buffer, ALuint SamplesToDo)\
{                                                                             \
    ALfloat (*DryBuffer)[MAXCHANNELS] = device->DryBuffer;                    \
    ALfloat (*Matrix)[MAXCHANNELS] = device->ChannelMatrix;                   \
    const ALuint *ChanMap = device->DevChannels;                              \
    ALuint i, j, c;                                                           \
                                                                              \
    if(device->Bs2b)                                                          \
    {                                                                         \
        for(i = 0;i < SamplesToDo;i++)                                        \
        {                                                                     \
            float samples[2] = { 0.0f, 0.0f };                                \
            for(c = 0;c < MAXCHANNELS;c++)                                    \
            {                                                                 \
                samples[0] += DryBuffer[i][c]*Matrix[c][FRONT_LEFT];          \
                samples[1] += DryBuffer[i][c]*Matrix[c][FRONT_RIGHT];         \
            }                                                                 \
            bs2b_cross_feed(device->Bs2b, samples);                           \
            ((T*)buffer)[ChanMap[FRONT_LEFT]]  = func(samples[0]);            \
            ((T*)buffer)[ChanMap[FRONT_RIGHT]] = func(samples[1]);            \
            buffer = ((T*)buffer) + 2;                                        \
        }                                                                     \
    }                                                                         \
    else                                                                      \
    {                                                                         \
        for(i = 0;i < SamplesToDo;i++)                                        \
        {                                                                     \
            for(j = 0;j < N;j++)                                              \
            {                                                                 \
                ALfloat samp = 0.0f;                                          \
                for(c = 0;c < MAXCHANNELS;c++)                                \
                    samp += DryBuffer[i][c] * Matrix[c][chans[j]];            \
                ((T*)buffer)[ChanMap[chans[j]]] = func(samp);                 \
            }                                                                 \
            buffer = ((T*)buffer) + N;                                        \
        }                                                                     \
    }                                                                         \
}

DECL_TEMPLATE(ALfloat, StereoChans,2, aluF2F)
DECL_TEMPLATE(ALushort, StereoChans,2, aluF2US)
DECL_TEMPLATE(ALshort, StereoChans,2, aluF2S)
DECL_TEMPLATE(ALubyte, StereoChans,2, aluF2UB)
DECL_TEMPLATE(ALbyte, StereoChans,2, aluF2B)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T, func)                                                \
static void Write_##T(ALCdevice *device, T *buffer, ALuint SamplesToDo)       \
{                                                                             \
    switch(device->FmtChans)                                                  \
    {                                                                         \
        case DevFmtMono:                                                      \
            Write_##T##_MonoChans(device, buffer, SamplesToDo);               \
            break;                                                            \
        case DevFmtStereo:                                                    \
            Write_##T##_StereoChans(device, buffer, SamplesToDo);             \
            break;                                                            \
        case DevFmtQuad:                                                      \
            Write_##T##_QuadChans(device, buffer, SamplesToDo);               \
            break;                                                            \
        case DevFmtX51:                                                       \
            Write_##T##_X51Chans(device, buffer, SamplesToDo);                \
            break;                                                            \
        case DevFmtX61:                                                       \
            Write_##T##_X61Chans(device, buffer, SamplesToDo);                \
            break;                                                            \
        case DevFmtX71:                                                       \
            Write_##T##_X71Chans(device, buffer, SamplesToDo);                \
            break;                                                            \
    }                                                                         \
}

DECL_TEMPLATE(ALfloat, aluF2F)
DECL_TEMPLATE(ALushort, aluF2US)
DECL_TEMPLATE(ALshort, aluF2S)
DECL_TEMPLATE(ALubyte, aluF2UB)
DECL_TEMPLATE(ALbyte, aluF2B)

#undef DECL_TEMPLATE

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size)
{
    ALuint SamplesToDo;
    ALeffectslot *ALEffectSlot;
    ALCcontext **ctx, **ctx_end;
    ALsource **src, **src_end;
    int fpuState;
    ALuint i, c;
    ALsizei e;

#if defined(HAVE_FESETROUND)
    fpuState = fegetround();
    fesetround(FE_TOWARDZERO);
#elif defined(HAVE__CONTROLFP)
    fpuState = _controlfp(_RC_CHOP, _MCW_RC);
#else
    (void)fpuState;
#endif

    while(size > 0)
    {
        /* Setup variables */
        SamplesToDo = min(size, BUFFERSIZE);

        /* Clear mixing buffer */
        memset(device->DryBuffer, 0, SamplesToDo*MAXCHANNELS*sizeof(ALfloat));

        SuspendContext(NULL);
        ctx = device->Contexts;
        ctx_end = ctx + device->NumContexts;
        while(ctx != ctx_end)
        {
            SuspendContext(*ctx);

            src = (*ctx)->ActiveSources;
            src_end = src + (*ctx)->ActiveSourceCount;
            while(src != src_end)
            {
                if((*src)->state != AL_PLAYING)
                {
                    --((*ctx)->ActiveSourceCount);
                    *src = *(--src_end);
                    continue;
                }

                if((*src)->NeedsUpdate)
                {
                    ALsource_Update(*src, *ctx);
                    (*src)->NeedsUpdate = AL_FALSE;
                }

                MixSource(*src, device, SamplesToDo);
                src++;
            }

            /* effect slot processing */
            for(e = 0;e < (*ctx)->EffectSlotMap.size;e++)
            {
                ALEffectSlot = (*ctx)->EffectSlotMap.array[e].value;

                for(i = 0;i < SamplesToDo;i++)
                {
                    ALEffectSlot->ClickRemoval[0] -= ALEffectSlot->ClickRemoval[0] / 256.0f;
                    ALEffectSlot->WetBuffer[i] += ALEffectSlot->ClickRemoval[0];
                }
                for(i = 0;i < 1;i++)
                {
                    ALEffectSlot->ClickRemoval[i] += ALEffectSlot->PendingClicks[i];
                    ALEffectSlot->PendingClicks[i] = 0.0f;
                }

                ALEffect_Process(ALEffectSlot->EffectState, ALEffectSlot,
                                 SamplesToDo, ALEffectSlot->WetBuffer,
                                 device->DryBuffer);

                for(i = 0;i < SamplesToDo;i++)
                    ALEffectSlot->WetBuffer[i] = 0.0f;
            }

            ProcessContext(*ctx);
            ctx++;
        }
        ProcessContext(NULL);

        //Post processing loop
        for(i = 0;i < SamplesToDo;i++)
        {
            for(c = 0;c < MAXCHANNELS;c++)
            {
                device->ClickRemoval[c] -= device->ClickRemoval[c] / 256.0f;
                device->DryBuffer[i][c] += device->ClickRemoval[c];
            }
        }
        for(i = 0;i < MAXCHANNELS;i++)
        {
            device->ClickRemoval[i] += device->PendingClicks[i];
            device->PendingClicks[i] = 0.0f;
        }

        switch(device->FmtType)
        {
            case DevFmtByte:
                Write_ALbyte(device, buffer, SamplesToDo);
                break;
            case DevFmtUByte:
                Write_ALubyte(device, buffer, SamplesToDo);
                break;
            case DevFmtShort:
                Write_ALshort(device, buffer, SamplesToDo);
                break;
            case DevFmtUShort:
                Write_ALushort(device, buffer, SamplesToDo);
                break;
            case DevFmtFloat:
                Write_ALfloat(device, buffer, SamplesToDo);
                break;
        }

        size -= SamplesToDo;
    }

#if defined(HAVE_FESETROUND)
    fesetround(fpuState);
#elif defined(HAVE__CONTROLFP)
    _controlfp(fpuState, _MCW_RC);
#endif
}


ALvoid aluHandleDisconnect(ALCdevice *device)
{
    ALuint i;

    SuspendContext(NULL);
    for(i = 0;i < device->NumContexts;i++)
    {
        ALCcontext *Context = device->Contexts[i];
        ALsource *source;
        ALsizei pos;

        SuspendContext(Context);

        for(pos = 0;pos < Context->SourceMap.size;pos++)
        {
            source = Context->SourceMap.array[pos].value;
            if(source->state == AL_PLAYING)
            {
                source->state = AL_STOPPED;
                source->BuffersPlayed = source->BuffersInQueue;
                source->position = 0;
                source->position_fraction = 0;
            }
        }
        ProcessContext(Context);
    }

    device->Connected = ALC_FALSE;
    ProcessContext(NULL);
}
