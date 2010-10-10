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
    ALfloat DryGain, DryGainHF;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALint NumSends, Frequency;
    ALboolean DupStereo;
    ALint Channels;
    ALfloat Pitch;
    ALenum Format;
    ALfloat cw;
    ALint i;

    //Get context properties
    Format    = ALContext->Device->Format;
    DupStereo = ALContext->Device->DuplicateStereo;
    NumSends  = ALContext->Device->NumAuxSends;
    Frequency = ALContext->Device->Frequency;

    //Get listener properties
    ListenerGain = ALContext->Listener.Gain;

    //Get source properties
    SourceVolume = ALSource->flGain;
    MinVolume    = ALSource->flMinGain;
    MaxVolume    = ALSource->flMaxGain;

    //1. Multi-channel buffers always play "normal"
    Channels = 0;
    Pitch = ALSource->flPitch;
    BufferListItem = ALSource->queue;
    while(BufferListItem != NULL)
    {
        ALbuffer *ALBuffer;
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            Channels = aluChannelsFromFormat(ALBuffer->format);
            Pitch = Pitch * ALBuffer->frequency / Frequency;
            break;
        }
        BufferListItem = BufferListItem->next;
    }

    if(Pitch > (float)MAX_PITCH)
        ALSource->Params.Step = MAX_PITCH<<FRACTIONBITS;
    else if(!(Pitch > 0.0f))
        ALSource->Params.Step = 1<<FRACTIONBITS;
    else
    {
        ALSource->Params.Step = Pitch*(1<<FRACTIONBITS);
        if(ALSource->Params.Step == 0)
            ALSource->Params.Step = 1;
    }

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

    if(Channels == 2)
    {
        for(i = 0;i < OUTPUTCHANNELS;i++)
            ALSource->Params.DryGains[i] = 0.0f;

        if(DupStereo == AL_FALSE)
        {
            ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
            ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
        }
        else
        {
            switch(Format)
            {
            case AL_FORMAT_MONO8:
            case AL_FORMAT_MONO16:
            case AL_FORMAT_MONO_FLOAT32:
            case AL_FORMAT_STEREO8:
            case AL_FORMAT_STEREO16:
            case AL_FORMAT_STEREO_FLOAT32:
                ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
                break;

            case AL_FORMAT_QUAD8:
            case AL_FORMAT_QUAD16:
            case AL_FORMAT_QUAD32:
            case AL_FORMAT_51CHN8:
            case AL_FORMAT_51CHN16:
            case AL_FORMAT_51CHN32:
                DryGain *= aluSqrt(2.0f/4.0f);
                ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
                ALSource->Params.DryGains[BACK_LEFT]   = DryGain * ListenerGain;
                ALSource->Params.DryGains[BACK_RIGHT]  = DryGain * ListenerGain;
                break;

            case AL_FORMAT_61CHN8:
            case AL_FORMAT_61CHN16:
            case AL_FORMAT_61CHN32:
                DryGain *= aluSqrt(2.0f/4.0f);
                ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
                ALSource->Params.DryGains[SIDE_LEFT]   = DryGain * ListenerGain;
                ALSource->Params.DryGains[SIDE_RIGHT]  = DryGain * ListenerGain;
                break;

            case AL_FORMAT_71CHN8:
            case AL_FORMAT_71CHN16:
            case AL_FORMAT_71CHN32:
                DryGain *= aluSqrt(2.0f/6.0f);
                ALSource->Params.DryGains[FRONT_LEFT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[FRONT_RIGHT] = DryGain * ListenerGain;
                ALSource->Params.DryGains[BACK_LEFT]   = DryGain * ListenerGain;
                ALSource->Params.DryGains[BACK_RIGHT]  = DryGain * ListenerGain;
                ALSource->Params.DryGains[SIDE_LEFT]   = DryGain * ListenerGain;
                ALSource->Params.DryGains[SIDE_RIGHT]  = DryGain * ListenerGain;
                break;

            default:
                break;
            }
        }
    }
    else
    {
        for(i = 0;i < OUTPUTCHANNELS;i++)
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

        ALSource->Params.WetGains[i] = WetGain[i] * ListenerGain;
    }
    for(i = NumSends;i < MAX_SENDS;i++)
    {
        ALSource->Params.WetGains[i] = 0.0f;
        WetGainHF[i] = 1.0f;
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
    ALfloat DopplerFactor, DopplerVelocity, flSpeedOfSound;
    ALfloat AirAbsorptionFactor;
    ALbufferlistitem *BufferListItem;
    ALfloat flAttenuation, effectiveDist;
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
    flSpeedOfSound  = ALContext->flSpeedOfSound;
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

    flAttenuation = 1.0f;
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
                    flAttenuation = MinDist / (MinDist + (Rolloff * (Distance - MinDist)));
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
                flAttenuation = 1.0f - (Rolloff*(Distance-MinDist)/(MaxDist - MinDist));
                flAttenuation = __max(flAttenuation, 0.0f);
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
                flAttenuation = aluPow(Distance/MinDist, -Rolloff);
                for(i = 0;i < NumSends;i++)
                    RoomAttenuation[i] = aluPow(Distance/MinDist, -RoomRolloff[i]);
            }
            break;

        case AL_NONE:
            break;
    }

    // Source Gain + Attenuation
    DryGain = SourceVolume * flAttenuation;
    for(i = 0;i < NumSends;i++)
        WetGain[i] = SourceVolume * RoomAttenuation[i];

    effectiveDist = 0.0f;
    if(MinDist > 0.0f && flAttenuation < 1.0f)
        effectiveDist = (MinDist/flAttenuation - MinDist)*MetersPerUnit;

    // Distance-based air absorption
    if(AirAbsorptionFactor > 0.0f && effectiveDist > 0.0f)
    {
        ALfloat absorb;

        // Absorption calculation is done in dB
        absorb = (AirAbsorptionFactor*AIRABSORBGAINDBHF) *
                 effectiveDist;
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
            ALSource->Params.WetGains[i] = 0.0f;
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
                WetGain[i] *= aluPow(10.0f, effectiveDist /
                                            (SPEEDOFSOUNDMETRESPERSEC *
                                             Slot->effect.Reverb.DecayTime) *
                                            -60.0 / 20.0);

                WetGainHF[i] *= aluPow(Slot->effect.Reverb.AirAbsorptionGainHF,
                                       AirAbsorptionFactor * effectiveDist);
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
        ALSource->Params.WetGains[i] = WetGain[i] * ListenerGain;
    }
    for(i = NumSends;i < MAX_SENDS;i++)
    {
        ALSource->Params.WetGains[i] = 0.0f;
        WetGainHF[i] = 1.0f;
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
    if(DopplerFactor != 0.0f)
    {
        ALfloat flVSS, flVLS;
        ALfloat flMaxVelocity = (DopplerVelocity * flSpeedOfSound) /
                                DopplerFactor;

        flVSS = aluDotproduct(Velocity, SourceToListener);
        if(flVSS >= flMaxVelocity)
            flVSS = (flMaxVelocity - 1.0f);
        else if(flVSS <= -flMaxVelocity)
            flVSS = -flMaxVelocity + 1.0f;

        flVLS = aluDotproduct(ListenerVel, SourceToListener);
        if(flVLS >= flMaxVelocity)
            flVLS = (flMaxVelocity - 1.0f);
        else if(flVLS <= -flMaxVelocity)
            flVLS = -flMaxVelocity + 1.0f;

        Pitch = ALSource->flPitch *
            ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVLS)) /
            ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVSS));
    }
    else
        Pitch = ALSource->flPitch;

    BufferListItem = ALSource->queue;
    while(BufferListItem != NULL)
    {
        ALbuffer *ALBuffer;
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            Pitch = Pitch * ALBuffer->frequency / Frequency;
            break;
        }
        BufferListItem = BufferListItem->next;
    }

    if(Pitch > (float)MAX_PITCH)
        ALSource->Params.Step = MAX_PITCH<<FRACTIONBITS;
    else if(!(Pitch > 0.0f))
        ALSource->Params.Step = 1<<FRACTIONBITS;
    else
    {
        ALSource->Params.Step = Pitch*(1<<FRACTIONBITS);
        if(ALSource->Params.Step == 0)
            ALSource->Params.Step = 1;
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
    SpeakerGain = &Device->PanningLUT[OUTPUTCHANNELS * pos];

    DirGain = aluSqrt(Position[0]*Position[0] + Position[2]*Position[2]);
    // elevation adjustment for directional gain. this sucks, but
    // has low complexity
    AmbientGain = 1.0/aluSqrt(Device->NumChan) * (1.0-DirGain);
    for(s = 0;s < OUTPUTCHANNELS;s++)
        ALSource->Params.DryGains[s] = 0.0f;
    for(s = 0;s < (ALsizei)Device->NumChan;s++)
    {
        Channel chan = Device->Speaker2Chan[s];
        ALfloat gain = SpeakerGain[chan]*DirGain + AmbientGain;
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
