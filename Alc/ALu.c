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
#include "alThunk.h"
#include "alListener.h"
#include "alAuxEffectSlot.h"
#include "alu.h"
#include "bs2b.h"

#define FRACTIONBITS 14
#define FRACTIONMASK ((1L<<FRACTIONBITS)-1)
#define MAX_PITCH 65536

/* Minimum ramp length in milliseconds. The value below was chosen to
 * adequately reduce clicks and pops from harsh gain changes. */
#define MIN_RAMP_LENGTH  16


static __inline ALfloat aluF2F(ALfloat Value)
{
    return Value;
}

static __inline ALshort aluF2S(ALfloat Value)
{
    ALint i;

    if(Value < 0.0f)
    {
        i = (ALint)(Value*32768.0f);
        i = max(-32768, i);
    }
    else
    {
        i = (ALint)(Value*32767.0f);
        i = min( 32767, i);
    }
    return ((ALshort)i);
}

static __inline ALubyte aluF2UB(ALfloat Value)
{
    ALshort i = aluF2S(Value);
    return (i>>8)+128;
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

static ALvoid CalcNonAttnSourceParams(const ALCcontext *ALContext, ALsource *ALSource)
{
    ALfloat SourceVolume,ListenerGain,MinVolume,MaxVolume;
    ALfloat DryGain, DryGainHF;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALint NumSends, Frequency;
    ALfloat cw;
    ALint i;

    //Get context properties
    NumSends  = ALContext->Device->NumAuxSends;
    Frequency = ALContext->Device->Frequency;

    //Get listener properties
    ListenerGain = ALContext->Listener.Gain;

    //Get source properties
    SourceVolume = ALSource->flGain;
    MinVolume    = ALSource->flMinGain;
    MaxVolume    = ALSource->flMaxGain;

    //1. Multi-channel buffers always play "normal"
    ALSource->Params.Pitch = ALSource->flPitch;

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

    for(i = 0;i < OUTPUTCHANNELS;i++)
        ALSource->Params.DryGains[i] = DryGain * ListenerGain;

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

static ALvoid CalcSourceParams(const ALCcontext *ALContext, ALsource *ALSource)
{
    const ALCdevice *Device = ALContext->Device;
    ALfloat InnerAngle,OuterAngle,Angle,Distance,DryMix,OrigDist;
    ALfloat Direction[3],Position[3],SourceToListener[3];
    ALfloat Velocity[3],ListenerVel[3];
    ALfloat MinVolume,MaxVolume,MinDist,MaxDist,Rolloff,OuterGainHF;
    ALfloat ConeVolume,ConeHF,SourceVolume,ListenerGain;
    ALfloat DopplerFactor, DopplerVelocity, flSpeedOfSound;
    ALfloat Matrix[4][4];
    ALfloat flAttenuation, effectiveDist;
    ALfloat RoomAttenuation[MAX_SENDS];
    ALfloat MetersPerUnit;
    ALfloat RoomRolloff[MAX_SENDS];
    ALfloat DryGainHF = 1.0f;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALfloat DirGain, AmbientGain;
    const ALfloat *SpeakerGain;
    ALfloat length;
    ALuint Frequency;
    ALint NumSends;
    ALint pos, s, i;
    ALfloat cw;

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

    //1. Translate Listener to origin (convert to head relative)
    if(ALSource->bHeadRelative==AL_FALSE)
    {
        ALfloat U[3],V[3],N[3];

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
            Distance=__min(Distance,MaxDist);
            if(MaxDist != MinDist)
            {
                flAttenuation = 1.0f - (Rolloff*(Distance-MinDist)/(MaxDist - MinDist));
                for(i = 0;i < NumSends;i++)
                    RoomAttenuation[i] = 1.0f - (RoomRolloff[i]*(Distance-MinDist)/(MaxDist - MinDist));
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
    DryMix = SourceVolume * flAttenuation;
    for(i = 0;i < NumSends;i++)
        WetGain[i] = SourceVolume * RoomAttenuation[i];

    effectiveDist = 0.0f;
    if(MinDist > 0.0f)
        effectiveDist = (MinDist/flAttenuation - MinDist)*MetersPerUnit;

    // Distance-based air absorption
    if(ALSource->AirAbsorptionFactor > 0.0f && effectiveDist > 0.0f)
    {
        ALfloat absorb;

        // Absorption calculation is done in dB
        absorb = (ALSource->AirAbsorptionFactor*AIRABSORBGAINDBHF) *
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

    DryMix *= ConeVolume;
    if(ALSource->DryGainHFAuto)
        DryGainHF *= ConeHF;

    // Clamp to Min/Max Gain
    DryMix = __min(DryMix,MaxVolume);
    DryMix = __max(DryMix,MinVolume);

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

                WetGainHF[i] *= aluPow(10.0f,
                                       log10(Slot->effect.Reverb.AirAbsorptionGainHF) *
                                       ALSource->AirAbsorptionFactor * effectiveDist);
            }
        }
        else
        {
            /* If the slot's auxiliary send auto is off, the data sent to the
             * effect slot is the same as the dry path, sans filter effects */
            WetGain[i] = DryMix;
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
            DryMix *= ALSource->DirectFilter.Gain;
            DryGainHF *= ALSource->DirectFilter.GainHF;
            break;
    }
    DryMix *= ListenerGain;

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

        ALSource->Params.Pitch = ALSource->flPitch *
            ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVLS)) /
            ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVSS));
    }
    else
        ALSource->Params.Pitch = ALSource->flPitch;

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
        ALSource->Params.DryGains[chan] = DryMix * gain;
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

static __inline ALfloat point(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1;
    (void)val2;
    (void)frac;
}
static __inline ALfloat lerp(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1 + ((val2-val1)*(frac * (1.0f/(1<<FRACTIONBITS))));
}
static __inline ALfloat cos_lerp(ALfloat val1, ALfloat val2, ALint frac)
{
    ALfloat mult = (1.0f-cos(frac * (1.0f/(1<<FRACTIONBITS)) * M_PI)) * 0.5f;
    return val1 + ((val2-val1)*mult);
}

static void MixSource(ALsource *ALSource, ALCcontext *ALContext,
                      float (*DryBuffer)[OUTPUTCHANNELS], ALuint SamplesToDo)
{
    static float DummyBuffer[BUFFERSIZE];
    ALfloat *WetBuffer[MAX_SENDS];
    ALfloat DrySend[OUTPUTCHANNELS];
    ALfloat dryGainStep[OUTPUTCHANNELS];
    ALfloat wetGainStep[MAX_SENDS];
    ALuint i, j, out;
    ALfloat value, outsamp;
    ALbufferlistitem *BufferListItem;
    ALint64 DataSize64,DataPos64;
    FILTER *DryFilter, *WetFilter[MAX_SENDS];
    ALfloat WetSend[MAX_SENDS];
    ALuint rampLength;
    ALboolean DuplicateStereo;
    ALuint DeviceFreq;
    ALint increment;
    ALuint DataPosInt, DataPosFrac;
    ALuint Channels, Bytes;
    ALuint Frequency;
    resampler_t Resampler;
    ALuint BuffersPlayed;
    ALboolean Looping;
    ALfloat Pitch;
    ALenum State;

    DuplicateStereo = ALContext->Device->DuplicateStereo;
    DeviceFreq = ALContext->Device->Frequency;

    rampLength = DeviceFreq * MIN_RAMP_LENGTH / 1000;
    rampLength = max(rampLength, SamplesToDo);

    /* Find buffer format */
    Frequency = 0;
    Channels = 0;
    Bytes = 0;
    BufferListItem = ALSource->queue;
    while(BufferListItem != NULL)
    {
        ALbuffer *ALBuffer;
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            Channels  = aluChannelsFromFormat(ALBuffer->format);
            Bytes     = aluBytesFromFormat(ALBuffer->format);
            Frequency = ALBuffer->frequency;
            break;
        }
        BufferListItem = BufferListItem->next;
    }

    if(ALSource->NeedsUpdate)
    {
        //Only apply 3D calculations for mono buffers
        if(Channels == 1)
            CalcSourceParams(ALContext, ALSource);
        else
            CalcNonAttnSourceParams(ALContext, ALSource);
        ALSource->NeedsUpdate = AL_FALSE;
    }

    /* Get source info */
    Resampler     = ALSource->Resampler;
    State         = ALSource->state;
    BuffersPlayed = ALSource->BuffersPlayed;
    DataPosInt    = ALSource->position;
    DataPosFrac   = ALSource->position_fraction;
    Looping       = ALSource->bLooping;

    /* Compute 18.14 fixed point step */
    Pitch = (ALSource->Params.Pitch*Frequency) / DeviceFreq;
    if(Pitch > (float)MAX_PITCH)  Pitch = (float)MAX_PITCH;
    increment = (ALint)(Pitch*(ALfloat)(1L<<FRACTIONBITS));
    if(increment <= 0)  increment = (1<<FRACTIONBITS);

    if(ALSource->FirstStart)
    {
        for(i = 0;i < OUTPUTCHANNELS;i++)
            DrySend[i] = ALSource->Params.DryGains[i];
        for(i = 0;i < MAX_SENDS;i++)
            WetSend[i] = ALSource->Params.WetGains[i];
    }
    else
    {
        for(i = 0;i < OUTPUTCHANNELS;i++)
            DrySend[i] = ALSource->DryGains[i];
        for(i = 0;i < MAX_SENDS;i++)
            WetSend[i] = ALSource->WetGains[i];
    }

    DryFilter = &ALSource->Params.iirFilter;
    for(i = 0;i < MAX_SENDS;i++)
    {
        WetFilter[i] = &ALSource->Params.Send[i].iirFilter;
        WetBuffer[i] = (ALSource->Send[i].Slot ?
                        ALSource->Send[i].Slot->WetBuffer :
                        DummyBuffer);
    }

    /* Get current buffer queue item */
    BufferListItem = ALSource->queue;
    for(i = 0;i < BuffersPlayed && BufferListItem;i++)
        BufferListItem = BufferListItem->next;

    j = 0;
    do {
        ALfloat *Data = NULL;
        ALuint LoopStart = 0;
        ALuint LoopEnd = 0;
        ALuint DataSize = 0;
        ALbuffer *ALBuffer;
        ALuint BufferSize;

        /* Get buffer info */
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            Data      = ALBuffer->data;
            DataSize  = ALBuffer->size;
            DataSize /= Channels * Bytes;
            LoopStart = ALBuffer->LoopStart;
            LoopEnd   = ALBuffer->LoopEnd;
        }

        if(Looping && ALSource->lSourceType == AL_STATIC)
        {
            /* If current offset is beyond the loop range, do not loop */
            if(DataPosInt >= LoopEnd)
                Looping = AL_FALSE;
        }
        if(!Looping || ALSource->lSourceType != AL_STATIC)
        {
            /* Non-looping and non-static sources ignore loop points */
            LoopStart = 0;
            LoopEnd = DataSize;
        }

        if(DataPosInt >= DataSize)
            goto skipmix;

        if(BufferListItem->next)
        {
            ALbuffer *NextBuf = BufferListItem->next->buffer;
            if(NextBuf && NextBuf->size)
            {
                ALint ulExtraSamples = BUFFER_PADDING*Channels*Bytes;
                ulExtraSamples = min(NextBuf->size, ulExtraSamples);
                memcpy(&Data[DataSize*Channels], NextBuf->data, ulExtraSamples);
            }
        }
        else if(Looping)
        {
            ALbuffer *NextBuf = ALSource->queue->buffer;
            if(NextBuf && NextBuf->size)
            {
                ALint ulExtraSamples = BUFFER_PADDING*Channels*Bytes;
                ulExtraSamples = min(NextBuf->size, ulExtraSamples);
                memcpy(&Data[DataSize*Channels], &NextBuf->data[LoopStart*Channels], ulExtraSamples);
            }
        }
        else
            memset(&Data[DataSize*Channels], 0, (BUFFER_PADDING*Channels*Bytes));

        /* Compute the gain steps for each output channel */
        for(i = 0;i < OUTPUTCHANNELS;i++)
            dryGainStep[i] = (ALSource->Params.DryGains[i]-DrySend[i]) /
                             rampLength;
        for(i = 0;i < MAX_SENDS;i++)
            wetGainStep[i] = (ALSource->Params.WetGains[i]-WetSend[i]) /
                             rampLength;

        /* Figure out how many samples we can mix. */
        DataSize64 = LoopEnd;
        DataSize64 <<= FRACTIONBITS;
        DataPos64 = DataPosInt;
        DataPos64 <<= FRACTIONBITS;
        DataPos64 += DataPosFrac;
        BufferSize = (ALuint)((DataSize64-DataPos64+(increment-1)) / increment);

        BufferSize = min(BufferSize, (SamplesToDo-j));

        /* Actual sample mixing loops */
        if(Channels == 1) /* Mono */
        {
#define DO_MIX(resampler) do {                                                \
    while(BufferSize--)                                                       \
    {                                                                         \
        for(i = 0;i < OUTPUTCHANNELS;i++)                                     \
            DrySend[i] += dryGainStep[i];                                     \
        for(i = 0;i < MAX_SENDS;i++)                                          \
            WetSend[i] += wetGainStep[i];                                     \
                                                                              \
        /* First order interpolator */                                        \
        value = (resampler)(Data[DataPosInt], Data[DataPosInt+1],             \
                            DataPosFrac);                                     \
                                                                              \
        /* Direct path final mix buffer and panning */                        \
        outsamp = lpFilter4P(DryFilter, 0, value);                            \
        DryBuffer[j][FRONT_LEFT]   += outsamp*DrySend[FRONT_LEFT];            \
        DryBuffer[j][FRONT_RIGHT]  += outsamp*DrySend[FRONT_RIGHT];           \
        DryBuffer[j][SIDE_LEFT]    += outsamp*DrySend[SIDE_LEFT];             \
        DryBuffer[j][SIDE_RIGHT]   += outsamp*DrySend[SIDE_RIGHT];            \
        DryBuffer[j][BACK_LEFT]    += outsamp*DrySend[BACK_LEFT];             \
        DryBuffer[j][BACK_RIGHT]   += outsamp*DrySend[BACK_RIGHT];            \
        DryBuffer[j][FRONT_CENTER] += outsamp*DrySend[FRONT_CENTER];          \
        DryBuffer[j][BACK_CENTER]  += outsamp*DrySend[BACK_CENTER];           \
                                                                              \
        /* Room path final mix buffer and panning */                          \
        for(i = 0;i < MAX_SENDS;i++)                                          \
        {                                                                     \
            outsamp = lpFilter2P(WetFilter[i], 0, value);                     \
            WetBuffer[i][j] += outsamp*WetSend[i];                            \
        }                                                                     \
                                                                              \
        DataPosFrac += increment;                                             \
        DataPosInt += DataPosFrac>>FRACTIONBITS;                              \
        DataPosFrac &= FRACTIONMASK;                                          \
        j++;                                                                  \
    }                                                                         \
} while(0)

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
#undef DO_MIX
        }
        else if(Channels == 2 && DuplicateStereo) /* Stereo */
        {
            const int chans[] = {
                FRONT_LEFT, FRONT_RIGHT
            };
            const int chans2[] = {
                BACK_LEFT, SIDE_LEFT, BACK_RIGHT, SIDE_RIGHT
            };
            const ALfloat dupscaler = aluSqrt(1.0f/3.0f);

#define DO_MIX(resampler) do {                                                \
    const ALfloat scaler = 1.0f/Channels;                                     \
    while(BufferSize--)                                                       \
    {                                                                         \
        for(i = 0;i < OUTPUTCHANNELS;i++)                                     \
            DrySend[i] += dryGainStep[i];                                     \
        for(i = 0;i < MAX_SENDS;i++)                                          \
            WetSend[i] += wetGainStep[i];                                     \
                                                                              \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = (resampler)(Data[DataPosInt*Channels + i],                \
                                Data[(DataPosInt+1)*Channels + i],            \
                                DataPosFrac);                                 \
            outsamp = lpFilter2P(DryFilter, chans[i]*2, value) * dupscaler;   \
            DryBuffer[j][chans[i]] += outsamp*DrySend[chans[i]];              \
            DryBuffer[j][chans2[i*2+0]] += outsamp*DrySend[chans2[i*2+0]];    \
            DryBuffer[j][chans2[i*2+1]] += outsamp*DrySend[chans2[i*2+1]];    \
            for(out = 0;out < MAX_SENDS;out++)                                \
            {                                                                 \
                outsamp = lpFilter1P(WetFilter[out], chans[i], value);        \
                WetBuffer[out][j] += outsamp*WetSend[out]*scaler;             \
            }                                                                 \
        }                                                                     \
                                                                              \
        DataPosFrac += increment;                                             \
        DataPosInt += DataPosFrac>>FRACTIONBITS;                              \
        DataPosFrac &= FRACTIONMASK;                                          \
        j++;                                                                  \
    }                                                                         \
} while(0)

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
#undef DO_MIX
        }
        else if(Channels == 2) /* Stereo */
        {
            const int chans[] = {
                FRONT_LEFT, FRONT_RIGHT
            };

#define DO_MIX(resampler) do {                                                \
    const ALfloat scaler = 1.0f/Channels;                                     \
    while(BufferSize--)                                                       \
    {                                                                         \
        for(i = 0;i < OUTPUTCHANNELS;i++)                                     \
            DrySend[i] += dryGainStep[i];                                     \
        for(i = 0;i < MAX_SENDS;i++)                                          \
            WetSend[i] += wetGainStep[i];                                     \
                                                                              \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = (resampler)(Data[DataPosInt*Channels + i],                \
                                Data[(DataPosInt+1)*Channels + i],            \
                                DataPosFrac);                                 \
            outsamp = lpFilter2P(DryFilter, chans[i]*2, value);               \
            DryBuffer[j][chans[i]] += outsamp*DrySend[chans[i]];              \
            for(out = 0;out < MAX_SENDS;out++)                                \
            {                                                                 \
                outsamp = lpFilter1P(WetFilter[out], chans[i], value);        \
                WetBuffer[out][j] += outsamp*WetSend[out]*scaler;             \
            }                                                                 \
        }                                                                     \
                                                                              \
        DataPosFrac += increment;                                             \
        DataPosInt += DataPosFrac>>FRACTIONBITS;                              \
        DataPosFrac &= FRACTIONMASK;                                          \
        j++;                                                                  \
    }                                                                         \
} while(0)

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
        }
        else if(Channels == 4) /* Quad */
        {
            const int chans[] = {
                FRONT_LEFT, FRONT_RIGHT,
                BACK_LEFT,  BACK_RIGHT
            };

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
        }
        else if(Channels == 6) /* 5.1 */
        {
            const int chans[] = {
                FRONT_LEFT,   FRONT_RIGHT,
                FRONT_CENTER, LFE,
                BACK_LEFT,    BACK_RIGHT
            };

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
        }
        else if(Channels == 7) /* 6.1 */
        {
            const int chans[] = {
                FRONT_LEFT,   FRONT_RIGHT,
                FRONT_CENTER, LFE,
                BACK_CENTER,
                SIDE_LEFT,    SIDE_RIGHT
            };

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
        }
        else if(Channels == 8) /* 7.1 */
        {
            const int chans[] = {
                FRONT_LEFT,   FRONT_RIGHT,
                FRONT_CENTER, LFE,
                BACK_LEFT,    BACK_RIGHT,
                SIDE_LEFT,    SIDE_RIGHT
            };

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
#undef DO_MIX
        }
        else /* Unknown? */
        {
            for(i = 0;i < OUTPUTCHANNELS;i++)
                DrySend[i] += dryGainStep[i]*BufferSize;
            for(i = 0;i < MAX_SENDS;i++)
                WetSend[i] += wetGainStep[i]*BufferSize;
            while(BufferSize--)
            {
                DataPosFrac += increment;
                DataPosInt += DataPosFrac>>FRACTIONBITS;
                DataPosFrac &= FRACTIONMASK;
                j++;
            }
        }

    skipmix:
        /* Handle looping sources */
        if(DataPosInt >= LoopEnd)
        {
            if(BuffersPlayed < (ALSource->BuffersInQueue-1))
            {
                BufferListItem = BufferListItem->next;
                BuffersPlayed++;
                DataPosInt -= DataSize;
            }
            else if(Looping)
            {
                BufferListItem = ALSource->queue;
                BuffersPlayed = 0;
                if(ALSource->lSourceType == AL_STATIC)
                    DataPosInt = ((DataPosInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                else
                    DataPosInt -= DataSize;
            }
            else
            {
                State = AL_STOPPED;
                BufferListItem = ALSource->queue;
                BuffersPlayed = ALSource->BuffersInQueue;
                DataPosInt = 0;
                DataPosFrac = 0;
            }
        }
    } while(State == AL_PLAYING && j < SamplesToDo);

    /* Update source info */
    ALSource->state             = State;
    ALSource->BuffersPlayed     = BuffersPlayed;
    ALSource->position          = DataPosInt;
    ALSource->position_fraction = DataPosFrac;
    ALSource->Buffer            = BufferListItem->buffer;

    for(i = 0;i < OUTPUTCHANNELS;i++)
        ALSource->DryGains[i] = DrySend[i];
    for(i = 0;i < MAX_SENDS;i++)
        ALSource->WetGains[i] = WetSend[i];

    ALSource->FirstStart = AL_FALSE;
}

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size)
{
    float (*DryBuffer)[OUTPUTCHANNELS];
    ALfloat (*Matrix)[OUTPUTCHANNELS];
    const ALuint *ChanMap;
    ALuint SamplesToDo;
    ALeffectslot *ALEffectSlot;
    ALCcontext *ALContext;
    ALfloat samp;
    int fpuState;
    ALuint i, j, c;
    ALsizei e, s;

#if defined(HAVE_FESETROUND)
    fpuState = fegetround();
    fesetround(FE_TOWARDZERO);
#elif defined(HAVE__CONTROLFP)
    fpuState = _controlfp(0, 0);
    _controlfp(_RC_CHOP, _MCW_RC);
#else
    (void)fpuState;
#endif

    DryBuffer = device->DryBuffer;
    while(size > 0)
    {
        /* Setup variables */
        SamplesToDo = min(size, BUFFERSIZE);

        /* Clear mixing buffer */
        memset(DryBuffer, 0, SamplesToDo*OUTPUTCHANNELS*sizeof(ALfloat));

        SuspendContext(NULL);
        for(c = 0;c < device->NumContexts;c++)
        {
            ALContext = device->Contexts[c];
            SuspendContext(ALContext);

            s = 0;
            while(s < ALContext->ActiveSourceCount)
            {
                ALsource *Source = ALContext->ActiveSources[s];
                if(Source->state != AL_PLAYING)
                {
                    ALsizei end = --(ALContext->ActiveSourceCount);
                    ALContext->ActiveSources[s] = ALContext->ActiveSources[end];
                    continue;
                }
                MixSource(Source, ALContext, DryBuffer, SamplesToDo);
                s++;
            }

            /* effect slot processing */
            for(e = 0;e < ALContext->EffectSlotMap.size;e++)
            {
                ALEffectSlot = ALContext->EffectSlotMap.array[e].value;
                if(ALEffectSlot->EffectState)
                    ALEffect_Process(ALEffectSlot->EffectState, ALEffectSlot, SamplesToDo, ALEffectSlot->WetBuffer, DryBuffer);

                for(i = 0;i < SamplesToDo;i++)
                    ALEffectSlot->WetBuffer[i] = 0.0f;
            }
            ProcessContext(ALContext);
        }
        device->SamplesPlayed += SamplesToDo;
        ProcessContext(NULL);

        //Post processing loop
        ChanMap = device->DevChannels;
        Matrix = device->ChannelMatrix;
        switch(device->Format)
        {
#define CHECK_WRITE_FORMAT(bits, type, func)                                  \
        case AL_FORMAT_MONO##bits:                                            \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                samp = 0.0f;                                                  \
                for(c = 0;c < OUTPUTCHANNELS;c++)                             \
                    samp += DryBuffer[i][c] * Matrix[c][FRONT_CENTER];        \
                ((type*)buffer)[ChanMap[FRONT_CENTER]] = (func)(samp);        \
                buffer = ((type*)buffer) + 1;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_STEREO##bits:                                          \
            if(device->Bs2b)                                                  \
            {                                                                 \
                for(i = 0;i < SamplesToDo;i++)                                \
                {                                                             \
                    float samples[2] = { 0.0f, 0.0f };                        \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                    {                                                         \
                        samples[0] += DryBuffer[i][c]*Matrix[c][FRONT_LEFT];  \
                        samples[1] += DryBuffer[i][c]*Matrix[c][FRONT_RIGHT]; \
                    }                                                         \
                    bs2b_cross_feed(device->Bs2b, samples);                   \
                    ((type*)buffer)[ChanMap[FRONT_LEFT]] = (func)(samples[0]);\
                    ((type*)buffer)[ChanMap[FRONT_RIGHT]]= (func)(samples[1]);\
                    buffer = ((type*)buffer) + 2;                             \
                }                                                             \
            }                                                                 \
            else                                                              \
            {                                                                 \
                for(i = 0;i < SamplesToDo;i++)                                \
                {                                                             \
                    static const Channel chans[] = {                          \
                        FRONT_LEFT, FRONT_RIGHT                               \
                    };                                                        \
                    for(j = 0;j < 2;j++)                                      \
                    {                                                         \
                        samp = 0.0f;                                          \
                        for(c = 0;c < OUTPUTCHANNELS;c++)                     \
                            samp += DryBuffer[i][c] * Matrix[c][chans[j]];    \
                        ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);    \
                    }                                                         \
                    buffer = ((type*)buffer) + 2;                             \
                }                                                             \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_QUAD##bits:                                            \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                static const Channel chans[] = {                              \
                    FRONT_LEFT, FRONT_RIGHT,                                  \
                    BACK_LEFT,  BACK_RIGHT,                                   \
                };                                                            \
                for(j = 0;j < 4;j++)                                          \
                {                                                             \
                    samp = 0.0f;                                              \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                        samp += DryBuffer[i][c] * Matrix[c][chans[j]];        \
                    ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);        \
                }                                                             \
                buffer = ((type*)buffer) + 4;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_51CHN##bits:                                           \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                static const Channel chans[] = {                              \
                    FRONT_LEFT, FRONT_RIGHT,                                  \
                    FRONT_CENTER, LFE,                                        \
                    BACK_LEFT,  BACK_RIGHT,                                   \
                };                                                            \
                for(j = 0;j < 6;j++)                                          \
                {                                                             \
                    samp = 0.0f;                                              \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                        samp += DryBuffer[i][c] * Matrix[c][chans[j]];        \
                    ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);        \
                }                                                             \
                buffer = ((type*)buffer) + 6;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_61CHN##bits:                                           \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                static const Channel chans[] = {                              \
                    FRONT_LEFT, FRONT_RIGHT,                                  \
                    FRONT_CENTER, LFE, BACK_CENTER,                           \
                    SIDE_LEFT,  SIDE_RIGHT,                                   \
                };                                                            \
                for(j = 0;j < 7;j++)                                          \
                {                                                             \
                    samp = 0.0f;                                              \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                        samp += DryBuffer[i][c] * Matrix[c][chans[j]];        \
                    ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);        \
                }                                                             \
                buffer = ((type*)buffer) + 7;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_71CHN##bits:                                           \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                static const Channel chans[] = {                              \
                    FRONT_LEFT, FRONT_RIGHT,                                  \
                    FRONT_CENTER, LFE,                                        \
                    BACK_LEFT,  BACK_RIGHT,                                   \
                    SIDE_LEFT,  SIDE_RIGHT                                    \
                };                                                            \
                for(j = 0;j < 8;j++)                                          \
                {                                                             \
                    samp = 0.0f;                                              \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                        samp += DryBuffer[i][c] * Matrix[c][chans[j]];        \
                    ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);        \
                }                                                             \
                buffer = ((type*)buffer) + 8;                                 \
            }                                                                 \
            break;

#define AL_FORMAT_MONO32 AL_FORMAT_MONO_FLOAT32
#define AL_FORMAT_STEREO32 AL_FORMAT_STEREO_FLOAT32
            CHECK_WRITE_FORMAT(8,  ALubyte, aluF2UB)
            CHECK_WRITE_FORMAT(16, ALshort, aluF2S)
            CHECK_WRITE_FORMAT(32, ALfloat, aluF2F)
#undef AL_FORMAT_STEREO32
#undef AL_FORMAT_MONO32
#undef CHECK_WRITE_FORMAT

            default:
                break;
        }

        size -= SamplesToDo;
    }

#if defined(HAVE_FESETROUND)
    fesetround(fpuState);
#elif defined(HAVE__CONTROLFP)
    _controlfp(fpuState, 0xfffff);
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
