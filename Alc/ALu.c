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

#define _CRT_SECURE_NO_DEPRECATE // get rid of sprintf security warnings on VS2005

#include "config.h"

#include <math.h>
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alThunk.h"
#include "alListener.h"
#include "alAuxEffectSlot.h"
#include "bs2b.h"

#if defined(HAVE_STDINT_H)
#include <stdint.h>
typedef int64_t ALint64;
#elif defined(HAVE___INT64)
typedef __int64 ALint64;
#elif (SIZEOF_LONG == 8)
typedef long ALint64;
#elif (SIZEOF_LONG_LONG == 8)
typedef long long ALint64;
#endif

#ifdef HAVE_SQRTF
#define aluSqrt(x) ((ALfloat)sqrtf((float)(x)))
#else
#define aluSqrt(x) ((ALfloat)sqrt((double)(x)))
#endif

#ifdef HAVE_ACOSF
#define aluAcos(x) ((ALfloat)acosf((float)(x)))
#else
#define aluAcos(x) ((ALfloat)acos((double)(x)))
#endif

// fixes for mingw32.
#if defined(max) && !defined(__max)
#define __max max
#endif
#if defined(min) && !defined(__min)
#define __min min
#endif

#define BUFFERSIZE 24000
#define FRACTIONBITS 14
#define FRACTIONMASK ((1L<<FRACTIONBITS)-1)
#define MAX_PITCH 4

enum {
    FRONT_LEFT = 0,
    FRONT_RIGHT,
    SIDE_LEFT,
    SIDE_RIGHT,
    BACK_LEFT,
    BACK_RIGHT,
    CENTER,
    LFE,

    OUTPUTCHANNELS
};

ALboolean DuplicateStereo = AL_FALSE;

/* NOTE: The AL_FORMAT_REAR* enums aren't handled here be cause they're
 *       converted to AL_FORMAT_QUAD* when loaded */
__inline ALuint aluBytesFromFormat(ALenum format)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_STEREO8:
        case AL_FORMAT_QUAD8_LOKI:
        case AL_FORMAT_QUAD8:
        case AL_FORMAT_51CHN8:
        case AL_FORMAT_61CHN8:
        case AL_FORMAT_71CHN8:
            return 1;

        case AL_FORMAT_MONO16:
        case AL_FORMAT_STEREO16:
        case AL_FORMAT_QUAD16_LOKI:
        case AL_FORMAT_QUAD16:
        case AL_FORMAT_51CHN16:
        case AL_FORMAT_61CHN16:
        case AL_FORMAT_71CHN16:
            return 2;

        case AL_FORMAT_MONO_FLOAT32:
        case AL_FORMAT_STEREO_FLOAT32:
        case AL_FORMAT_QUAD32:
        case AL_FORMAT_51CHN32:
        case AL_FORMAT_61CHN32:
        case AL_FORMAT_71CHN32:
            return 4;

        default:
            return 0;
    }
}

__inline ALuint aluChannelsFromFormat(ALenum format)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_MONO16:
        case AL_FORMAT_MONO_FLOAT32:
            return 1;

        case AL_FORMAT_STEREO8:
        case AL_FORMAT_STEREO16:
        case AL_FORMAT_STEREO_FLOAT32:
            return 2;

        case AL_FORMAT_QUAD8_LOKI:
        case AL_FORMAT_QUAD16_LOKI:
        case AL_FORMAT_QUAD8:
        case AL_FORMAT_QUAD16:
        case AL_FORMAT_QUAD32:
            return 4;

        case AL_FORMAT_51CHN8:
        case AL_FORMAT_51CHN16:
        case AL_FORMAT_51CHN32:
            return 6;

        case AL_FORMAT_61CHN8:
        case AL_FORMAT_61CHN16:
        case AL_FORMAT_61CHN32:
            return 7;

        case AL_FORMAT_71CHN8:
        case AL_FORMAT_71CHN16:
        case AL_FORMAT_71CHN32:
            return 8;

        default:
            return 0;
    }
}


static __inline ALshort aluF2S(ALfloat Value)
{
    ALint i;

    i = (ALint)Value;
    i = __min( 32767, i);
    i = __max(-32768, i);
    return ((ALshort)i);
}

static __inline ALvoid aluCrossproduct(ALfloat *inVector1,ALfloat *inVector2,ALfloat *outVector)
{
    outVector[0] = inVector1[1]*inVector2[2] - inVector1[2]*inVector2[1];
    outVector[1] = inVector1[2]*inVector2[0] - inVector1[0]*inVector2[2];
    outVector[2] = inVector1[0]*inVector2[1] - inVector1[1]*inVector2[0];
}

static __inline ALfloat aluDotproduct(ALfloat *inVector1,ALfloat *inVector2)
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

static __inline ALvoid aluMatrixVector(ALfloat *vector,ALfloat matrix[3][3])
{
    ALfloat result[3];

    result[0] = vector[0]*matrix[0][0] + vector[1]*matrix[1][0] + vector[2]*matrix[2][0];
    result[1] = vector[0]*matrix[0][1] + vector[1]*matrix[1][1] + vector[2]*matrix[2][1];
    result[2] = vector[0]*matrix[0][2] + vector[1]*matrix[1][2] + vector[2]*matrix[2][2];
    memcpy(vector, result, sizeof(result));
}

static __inline ALfloat aluComputeSample(ALfloat GainHF, ALfloat sample, ALfloat LowSample)
{
    return LowSample + ((sample - LowSample) * GainHF);
}

static ALvoid CalcSourceParams(ALCcontext *ALContext, ALsource *ALSource,
                               ALenum isMono, ALenum OutputFormat,
                               ALfloat *drysend, ALfloat *wetsend,
                               ALfloat *pitch, ALfloat *drygainhf,
                               ALfloat *wetgainhf)
{
    ALfloat InnerAngle,OuterAngle,Angle,Distance,DryMix,WetMix=0.0f;
    ALfloat Direction[3],Position[3],SourceToListener[3];
    ALfloat MinVolume,MaxVolume,MinDist,MaxDist,Rolloff,OuterGainHF;
    ALfloat ConeVolume,SourceVolume,PanningFB,PanningLR,ListenerGain;
    ALfloat U[3],V[3],N[3];
    ALfloat DopplerFactor, DopplerVelocity, flSpeedOfSound, flMaxVelocity;
    ALfloat Matrix[3][3];
    ALfloat flAttenuation;
    ALfloat RoomAttenuation;
    ALfloat MetersPerUnit;
    ALfloat RoomRolloff;
    ALfloat DryGainHF = 1.0f;
    ALfloat WetGainHF = 1.0f;

    //Get context properties
    DopplerFactor   = ALContext->DopplerFactor * ALSource->DopplerFactor;
    DopplerVelocity = ALContext->DopplerVelocity;
    flSpeedOfSound  = ALContext->flSpeedOfSound;

    //Get listener properties
    ListenerGain = ALContext->Listener.Gain;
    MetersPerUnit = ALContext->Listener.MetersPerUnit;

    //Get source properties
    SourceVolume = ALSource->flGain;
    memcpy(Position,  ALSource->vPosition,    sizeof(ALSource->vPosition));
    memcpy(Direction, ALSource->vOrientation, sizeof(ALSource->vOrientation));
    MinVolume    = ALSource->flMinGain;
    MaxVolume    = ALSource->flMaxGain;
    MinDist      = ALSource->flRefDistance;
    MaxDist      = ALSource->flMaxDistance;
    Rolloff      = ALSource->flRollOffFactor;
    InnerAngle   = ALSource->flInnerAngle;
    OuterAngle   = ALSource->flOuterAngle;
    OuterGainHF  = ALSource->OuterGainHF;
    RoomRolloff  = ALSource->RoomRolloffFactor;

    //Only apply 3D calculations for mono buffers
    if(isMono != AL_FALSE)
    {
        //1. Translate Listener to origin (convert to head relative)
        // Note that Direction and SourceToListener are *not* transformed.
        // SourceToListener is used with the source and listener velocities,
        // which are untransformed, and Direction is used with SourceToListener
        // for the sound cone
        if(ALSource->bHeadRelative==AL_FALSE)
        {
            // Build transform matrix
            aluCrossproduct(ALContext->Listener.Forward, ALContext->Listener.Up, U); // Right-vector
            aluNormalize(U);  // Normalized Right-vector
            memcpy(V, ALContext->Listener.Up, sizeof(V));   // Up-vector
            aluNormalize(V);  // Normalized Up-vector
            memcpy(N, ALContext->Listener.Forward, sizeof(N));  // At-vector
            aluNormalize(N);  // Normalized At-vector
            Matrix[0][0] = U[0]; Matrix[0][1] = V[0]; Matrix[0][2] = -N[0];
            Matrix[1][0] = U[1]; Matrix[1][1] = V[1]; Matrix[1][2] = -N[1];
            Matrix[2][0] = U[2]; Matrix[2][1] = V[2]; Matrix[2][2] = -N[2];

            // Translate source position into listener space
            Position[0] -= ALContext->Listener.Position[0];
            Position[1] -= ALContext->Listener.Position[1];
            Position[2] -= ALContext->Listener.Position[2];

            SourceToListener[0] = -Position[0];
            SourceToListener[1] = -Position[1];
            SourceToListener[2] = -Position[2];

            // Transform source position and direction into listener space
            aluMatrixVector(Position, Matrix);
        }
        else
        {
            SourceToListener[0] = -Position[0];
            SourceToListener[1] = -Position[1];
            SourceToListener[2] = -Position[2];
        }
        aluNormalize(SourceToListener);
        aluNormalize(Direction);

        //2. Calculate distance attenuation
        Distance = aluSqrt(aluDotproduct(Position, Position));

        if(ALSource->Send[0].Slot && !ALSource->Send[0].Slot->AuxSendAuto)
        {
            if(ALSource->Send[0].Slot->effect.type == AL_EFFECT_REVERB)
                RoomRolloff += ALSource->Send[0].Slot->effect.Reverb.RoomRolloffFactor;
        }

        flAttenuation = 1.0f;
        RoomAttenuation = 1.0f;
        switch (ALContext->DistanceModel)
        {
            case AL_INVERSE_DISTANCE_CLAMPED:
                Distance=__max(Distance,MinDist);
                Distance=__min(Distance,MaxDist);
                if (MaxDist < MinDist)
                    break;
                //fall-through
            case AL_INVERSE_DISTANCE:
                if (MinDist > 0.0f)
                {
                    if ((MinDist + (Rolloff * (Distance - MinDist))) > 0.0f)
                        flAttenuation = MinDist / (MinDist + (Rolloff * (Distance - MinDist)));
                    if ((MinDist + (RoomRolloff * (Distance - MinDist))) > 0.0f)
                        RoomAttenuation = MinDist / (MinDist + (RoomRolloff * (Distance - MinDist)));
                }
                break;

            case AL_LINEAR_DISTANCE_CLAMPED:
                Distance=__max(Distance,MinDist);
                Distance=__min(Distance,MaxDist);
                if (MaxDist < MinDist)
                    break;
                //fall-through
            case AL_LINEAR_DISTANCE:
                Distance=__min(Distance,MaxDist);
                if (MaxDist != MinDist)
                {
                    flAttenuation = 1.0f - (Rolloff*(Distance-MinDist)/(MaxDist - MinDist));
                    RoomAttenuation = 1.0f - (RoomRolloff*(Distance-MinDist)/(MaxDist - MinDist));
                }
                break;

            case AL_EXPONENT_DISTANCE_CLAMPED:
                Distance=__max(Distance,MinDist);
                Distance=__min(Distance,MaxDist);
                if (MaxDist < MinDist)
                    break;
                //fall-through
            case AL_EXPONENT_DISTANCE:
                if ((Distance > 0.0f) && (MinDist > 0.0f))
                {
                    flAttenuation = (ALfloat)pow(Distance/MinDist, -Rolloff);
                    RoomAttenuation = (ALfloat)pow(Distance/MinDist, -RoomRolloff);
                }
                break;

            case AL_NONE:
            default:
                flAttenuation = 1.0f;
                RoomAttenuation = 1.0f;
                break;
        }

        // Source Gain + Attenuation and clamp to Min/Max Gain
        DryMix = SourceVolume * flAttenuation;
        DryMix = __min(DryMix,MaxVolume);
        DryMix = __max(DryMix,MinVolume);

        WetMix = SourceVolume * (ALSource->WetGainAuto ?
                                 RoomAttenuation : 1.0f);
        WetMix = __min(WetMix,MaxVolume);
        WetMix = __max(WetMix,MinVolume);

        //3. Apply directional soundcones
        Angle = aluAcos(aluDotproduct(Direction,SourceToListener)) * 180.0f /
                3.141592654f;
        if(Angle >= InnerAngle && Angle <= OuterAngle)
        {
            ALfloat scale = (Angle-InnerAngle) / (OuterAngle-InnerAngle);
            ConeVolume = (1.0f+(ALSource->flOuterGain-1.0f)*scale);
            if(ALSource->WetGainAuto)
                WetMix *= ConeVolume;
            if(ALSource->DryGainHFAuto)
                DryGainHF *= (1.0f+(OuterGainHF-1.0f)*scale);
            if(ALSource->WetGainHFAuto)
                WetGainHF *= (1.0f+(OuterGainHF-1.0f)*scale);
        }
        else if(Angle > OuterAngle)
        {
            ConeVolume = (1.0f+(ALSource->flOuterGain-1.0f));
            if(ALSource->WetGainAuto)
                WetMix *= ConeVolume;
            if(ALSource->DryGainHFAuto)
                DryGainHF *= (1.0f+(OuterGainHF-1.0f));
            if(ALSource->WetGainHFAuto)
                WetGainHF *= (1.0f+(OuterGainHF-1.0f));
        }
        else
            ConeVolume = 1.0f;

        //4. Calculate Velocity
        if(DopplerFactor != 0.0f)
        {
            ALfloat flVSS, flVLS = 0.0f;

            if(ALSource->bHeadRelative==AL_FALSE)
                flVLS = aluDotproduct(ALContext->Listener.Velocity, SourceToListener);
            flVSS = aluDotproduct(ALSource->vVelocity, SourceToListener);

            flMaxVelocity = (DopplerVelocity * flSpeedOfSound) / DopplerFactor;

            if (flVSS >= flMaxVelocity)
                flVSS = (flMaxVelocity - 1.0f);
            else if (flVSS <= -flMaxVelocity)
                flVSS = -flMaxVelocity + 1.0f;

            if (flVLS >= flMaxVelocity)
                flVLS = (flMaxVelocity - 1.0f);
            else if (flVLS <= -flMaxVelocity)
                flVLS = -flMaxVelocity + 1.0f;

            pitch[0] = ALSource->flPitch *
                       ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVLS)) /
                       ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVSS));
        }
        else
            pitch[0] = ALSource->flPitch;

        //5. Apply filter gains and filters
        switch(ALSource->DirectFilter.type)
        {
            case AL_FILTER_LOWPASS:
                DryMix *= ALSource->DirectFilter.Gain;
                DryGainHF *= ALSource->DirectFilter.GainHF;
                break;
        }

        switch(ALSource->Send[0].WetFilter.type)
        {
            case AL_FILTER_LOWPASS:
                WetMix *= ALSource->Send[0].WetFilter.Gain;
                WetGainHF *= ALSource->Send[0].WetFilter.GainHF;
                break;
        }

        if(ALSource->AirAbsorptionFactor > 0.0f)
            DryGainHF *= pow(ALSource->AirAbsorptionFactor * AIRABSORBGAINHF,
                             Distance * MetersPerUnit);

        if(ALSource->Send[0].Slot)
        {
            WetMix *= ALSource->Send[0].Slot->Gain;

            if(ALSource->Send[0].Slot->effect.type == AL_EFFECT_REVERB)
            {
                WetGainHF *= ALSource->Send[0].Slot->effect.Reverb.GainHF;
                WetGainHF *= pow(ALSource->Send[0].Slot->effect.Reverb.AirAbsorptionGainHF,
                                 Distance * MetersPerUnit);
            }
        }
        else
        {
            WetMix = 0.0f;
            WetGainHF = 1.0f;
        }

        DryMix *= ListenerGain * ConeVolume;
        WetMix *= ListenerGain;

        //6. Convert normalized position into pannings, then into channel volumes
        aluNormalize(Position);
        switch(aluChannelsFromFormat(OutputFormat))
        {
            case 1:
                drysend[FRONT_LEFT]  = DryMix * aluSqrt(1.0f); //Direct
                drysend[FRONT_RIGHT] = DryMix * aluSqrt(1.0f); //Direct
                wetsend[FRONT_LEFT]  = WetMix * aluSqrt(1.0f); //Room
                wetsend[FRONT_RIGHT] = WetMix * aluSqrt(1.0f); //Room
                break;
            case 2:
                PanningLR = 0.5f + 0.5f*Position[0];
                drysend[FRONT_LEFT]  = DryMix * aluSqrt(1.0f-PanningLR); //L Direct
                drysend[FRONT_RIGHT] = DryMix * aluSqrt(     PanningLR); //R Direct
                wetsend[FRONT_LEFT]  = WetMix * aluSqrt(1.0f-PanningLR); //L Room
                wetsend[FRONT_RIGHT] = WetMix * aluSqrt(     PanningLR); //R Room
                break;
            case 4:
            /* TODO: Add center/lfe channel in spatial calculations? */
            case 6:
                // Apply a scalar so each individual speaker has more weight
                PanningLR = 0.5f + (0.5f*Position[0]*1.41421356f);
                PanningLR = __min(1.0f, PanningLR);
                PanningLR = __max(0.0f, PanningLR);
                PanningFB = 0.5f + (0.5f*Position[2]*1.41421356f);
                PanningFB = __min(1.0f, PanningFB);
                PanningFB = __max(0.0f, PanningFB);
                drysend[FRONT_LEFT]  = DryMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB));
                drysend[FRONT_RIGHT] = DryMix * aluSqrt((     PanningLR)*(1.0f-PanningFB));
                drysend[BACK_LEFT]   = DryMix * aluSqrt((1.0f-PanningLR)*(     PanningFB));
                drysend[BACK_RIGHT]  = DryMix * aluSqrt((     PanningLR)*(     PanningFB));
                wetsend[FRONT_LEFT]  = WetMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB));
                wetsend[FRONT_RIGHT] = WetMix * aluSqrt((     PanningLR)*(1.0f-PanningFB));
                wetsend[BACK_LEFT]   = WetMix * aluSqrt((1.0f-PanningLR)*(     PanningFB));
                wetsend[BACK_RIGHT]  = WetMix * aluSqrt((     PanningLR)*(     PanningFB));
                break;
            case 7:
            case 8:
                PanningFB = 1.0f - fabs(Position[2]*1.15470054f);
                PanningFB = __min(1.0f, PanningFB);
                PanningFB = __max(0.0f, PanningFB);
                PanningLR = 0.5f + (0.5*Position[0]*((1.0f-PanningFB)*2.0f));
                PanningLR = __min(1.0f, PanningLR);
                PanningLR = __max(0.0f, PanningLR);
                if(Position[2] > 0.0f)
                {
                    drysend[BACK_LEFT]   = DryMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB));
                    drysend[BACK_RIGHT]  = DryMix * aluSqrt((     PanningLR)*(1.0f-PanningFB));
                    drysend[SIDE_LEFT]   = DryMix * aluSqrt((1.0f-PanningLR)*(     PanningFB));
                    drysend[SIDE_RIGHT]  = DryMix * aluSqrt((     PanningLR)*(     PanningFB));
                    drysend[FRONT_LEFT]  = 0.0f;
                    drysend[FRONT_RIGHT] = 0.0f;
                    wetsend[BACK_LEFT]   = WetMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB));
                    wetsend[BACK_RIGHT]  = WetMix * aluSqrt((     PanningLR)*(1.0f-PanningFB));
                    wetsend[SIDE_LEFT]   = WetMix * aluSqrt((1.0f-PanningLR)*(     PanningFB));
                    wetsend[SIDE_RIGHT]  = WetMix * aluSqrt((     PanningLR)*(     PanningFB));
                    wetsend[FRONT_LEFT]  = 0.0f;
                    wetsend[FRONT_RIGHT] = 0.0f;
                }
                else
                {
                    drysend[FRONT_LEFT]  = DryMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB));
                    drysend[FRONT_RIGHT] = DryMix * aluSqrt((     PanningLR)*(1.0f-PanningFB));
                    drysend[SIDE_LEFT]   = DryMix * aluSqrt((1.0f-PanningLR)*(     PanningFB));
                    drysend[SIDE_RIGHT]  = DryMix * aluSqrt((     PanningLR)*(     PanningFB));
                    drysend[BACK_LEFT]   = 0.0f;
                    drysend[BACK_RIGHT]  = 0.0f;
                    wetsend[FRONT_LEFT]  = WetMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB));
                    wetsend[FRONT_RIGHT] = WetMix * aluSqrt((     PanningLR)*(1.0f-PanningFB));
                    wetsend[SIDE_LEFT]   = WetMix * aluSqrt((1.0f-PanningLR)*(     PanningFB));
                    wetsend[SIDE_RIGHT]  = WetMix * aluSqrt((     PanningLR)*(     PanningFB));
                    wetsend[BACK_LEFT]   = 0.0f;
                    wetsend[BACK_RIGHT]  = 0.0f;
                }
            default:
                break;
        }

        *drygainhf = DryGainHF;
        *wetgainhf = WetGainHF;
    }
    else
    {
        //1. Multi-channel buffers always play "normal"
        pitch[0] = ALSource->flPitch;

        drysend[FRONT_LEFT]  = SourceVolume * ListenerGain;
        drysend[FRONT_RIGHT] = SourceVolume * ListenerGain;
        drysend[SIDE_LEFT]   = SourceVolume * ListenerGain;
        drysend[SIDE_RIGHT]  = SourceVolume * ListenerGain;
        drysend[BACK_LEFT]   = SourceVolume * ListenerGain;
        drysend[BACK_RIGHT]  = SourceVolume * ListenerGain;
        drysend[CENTER]      = SourceVolume * ListenerGain;
        drysend[LFE]         = SourceVolume * ListenerGain;
        wetsend[FRONT_LEFT]  = 0.0f;
        wetsend[FRONT_RIGHT] = 0.0f;
        wetsend[SIDE_LEFT]   = 0.0f;
        wetsend[SIDE_RIGHT]  = 0.0f;
        wetsend[BACK_LEFT]   = 0.0f;
        wetsend[BACK_RIGHT]  = 0.0f;
        wetsend[CENTER]      = 0.0f;
        wetsend[LFE]         = 0.0f;
        WetGainHF = 1.0f;

        *drygainhf = DryGainHF;
        *wetgainhf = WetGainHF;
    }
}

ALvoid aluMixData(ALCcontext *ALContext,ALvoid *buffer,ALsizei size,ALenum format)
{
    static float DryBuffer[BUFFERSIZE][OUTPUTCHANNELS];
    static float WetBuffer[BUFFERSIZE][OUTPUTCHANNELS];
    static float ReverbBuffer[BUFFERSIZE];
    ALfloat DrySend[OUTPUTCHANNELS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    ALfloat WetSend[OUTPUTCHANNELS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    ALfloat DryGainHF = 0.0f;
    ALfloat WetGainHF = 0.0f;
    ALuint BlockAlign,BufferSize;
    ALuint DataSize=0,DataPosInt=0,DataPosFrac=0;
    ALuint Channels,Frequency,ulExtraSamples;
    ALboolean doReverb;
    ALfloat Pitch;
    ALint Looping,State;
    ALint fraction,increment;
    ALuint Buffer;
    ALuint SamplesToDo;
    ALsource *ALSource;
    ALbuffer *ALBuffer;
    ALeffectslot *ALEffectSlot;
    ALfloat value;
    ALshort *Data;
    ALuint i,j,k;
    ALbufferlistitem *BufferListItem;
    ALuint loop;
    ALint64 DataSize64,DataPos64;
    FILTER *Filter;

    SuspendContext(ALContext);

    //Figure output format variables
    BlockAlign  = aluChannelsFromFormat(format);
    BlockAlign *= aluBytesFromFormat(format);

    size /= BlockAlign;
    while(size > 0)
    {
        //Setup variables
        ALEffectSlot = (ALContext ? ALContext->AuxiliaryEffectSlot : NULL);
        ALSource = (ALContext ? ALContext->Source : NULL);
        SamplesToDo = min(size, BUFFERSIZE);

        //Clear mixing buffer
        memset(DryBuffer, 0, SamplesToDo*OUTPUTCHANNELS*sizeof(ALfloat));
        memset(WetBuffer, 0, SamplesToDo*OUTPUTCHANNELS*sizeof(ALfloat));
        memset(ReverbBuffer, 0, SamplesToDo*sizeof(ALfloat));

        //Actual mixing loop
        while(ALSource)
        {
            j = 0;
            State = ALSource->state;

            doReverb = ((ALSource->Send[0].Slot &&
                            ALSource->Send[0].Slot->effect.type == AL_EFFECT_REVERB) ?
                        AL_TRUE : AL_FALSE);

            while(State == AL_PLAYING && j < SamplesToDo)
            {
                DataSize = 0;
                DataPosInt = 0;
                DataPosFrac = 0;

                //Get buffer info
                if((Buffer = ALSource->ulBufferID))
                {
                    ALBuffer = (ALbuffer*)ALTHUNK_LOOKUPENTRY(Buffer);

                    Data      = ALBuffer->data;
                    Channels  = aluChannelsFromFormat(ALBuffer->format);
                    DataSize  = ALBuffer->size;
                    Frequency = ALBuffer->frequency;

                    CalcSourceParams(ALContext, ALSource,
                                        (Channels==1) ? AL_TRUE : AL_FALSE,
                                        format, DrySend, WetSend, &Pitch,
                                        &DryGainHF, &WetGainHF);


                    Pitch = (Pitch*Frequency) / ALContext->Frequency;
                    DataSize /= Channels * aluBytesFromFormat(ALBuffer->format);

                    //Get source info
                    DataPosInt = ALSource->position;
                    DataPosFrac = ALSource->position_fraction;
                    Filter = &ALSource->iirFilter;

                    //Compute 18.14 fixed point step
                    increment = (ALint)(Pitch*(ALfloat)(1L<<FRACTIONBITS));
                    if(increment > (MAX_PITCH<<FRACTIONBITS))
                        increment = (MAX_PITCH<<FRACTIONBITS);

                    //Figure out how many samples we can mix.
                    DataSize64 = DataSize;
                    DataSize64 <<= FRACTIONBITS;
                    DataPos64 = DataPosInt;
                    DataPos64 <<= FRACTIONBITS;
                    DataPos64 += DataPosFrac;
                    BufferSize = (ALuint)((DataSize64-DataPos64+(increment-1)) / increment);

                    BufferListItem = ALSource->queue;
                    for(loop = 0; loop < ALSource->BuffersPlayed; loop++)
                    {
                        if(BufferListItem)
                            BufferListItem = BufferListItem->next;
                    }
                    if (BufferListItem)
                    {
                        if (BufferListItem->next)
                        {
                            ALbuffer *NextBuf = (ALbuffer*)ALTHUNK_LOOKUPENTRY(BufferListItem->next->buffer);
                            if(NextBuf && NextBuf->data)
                            {
                                ulExtraSamples = min(NextBuf->size, (ALint)(ALBuffer->padding*Channels*2));
                                memcpy(&Data[DataSize*Channels], NextBuf->data, ulExtraSamples);
                            }
                        }
                        else if (ALSource->bLooping)
                        {
                            ALbuffer *NextBuf = (ALbuffer*)ALTHUNK_LOOKUPENTRY(ALSource->queue->buffer);
                            if (NextBuf && NextBuf->data)
                            {
                                ulExtraSamples = min(NextBuf->size, (ALint)(ALBuffer->padding*Channels*2));
                                memcpy(&Data[DataSize*Channels], NextBuf->data, ulExtraSamples);
                            }
                        }
                    }
                    BufferSize = min(BufferSize, (SamplesToDo-j));

                    //Actual sample mixing loop
                    Data += DataPosInt*Channels;
                    while(BufferSize--)
                    {
                        k = DataPosFrac>>FRACTIONBITS;
                        fraction = DataPosFrac&FRACTIONMASK;

                        if(Channels==1)
                        {
                            ALfloat sample, lowsamp, outsamp;
                            //First order interpolator
                            sample = (Data[k]*((1<<FRACTIONBITS)-fraction) +
                                      Data[k+1]*fraction) >> FRACTIONBITS;
                            lowsamp = lpFilter(Filter, sample);

                            //Direct path final mix buffer and panning
                            outsamp = aluComputeSample(DryGainHF, sample, lowsamp);
                            DryBuffer[j][FRONT_LEFT]  += outsamp*DrySend[FRONT_LEFT];
                            DryBuffer[j][FRONT_RIGHT] += outsamp*DrySend[FRONT_RIGHT];
                            DryBuffer[j][SIDE_LEFT]   += outsamp*DrySend[SIDE_LEFT];
                            DryBuffer[j][SIDE_RIGHT]  += outsamp*DrySend[SIDE_RIGHT];
                            DryBuffer[j][BACK_LEFT]   += outsamp*DrySend[BACK_LEFT];
                            DryBuffer[j][BACK_RIGHT]  += outsamp*DrySend[BACK_RIGHT];
                            //Room path final mix buffer and panning
                            outsamp = aluComputeSample(WetGainHF, sample, lowsamp);
                            if(doReverb)
                                ReverbBuffer[j] += outsamp;
                            else
                            {
                                WetBuffer[j][FRONT_LEFT]  += outsamp*WetSend[FRONT_LEFT];
                                WetBuffer[j][FRONT_RIGHT] += outsamp*WetSend[FRONT_RIGHT];
                                WetBuffer[j][SIDE_LEFT]   += outsamp*WetSend[SIDE_LEFT];
                                WetBuffer[j][SIDE_RIGHT]  += outsamp*WetSend[SIDE_RIGHT];
                                WetBuffer[j][BACK_LEFT]   += outsamp*WetSend[BACK_LEFT];
                                WetBuffer[j][BACK_RIGHT]  += outsamp*WetSend[BACK_RIGHT];
                            }
                        }
                        else
                        {
                            ALfloat samp1, samp2;
                            //First order interpolator (front left)
                            samp1 = (ALfloat)((ALshort)(((Data[k*Channels  ]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels  ]*(fraction)))>>FRACTIONBITS));
                            DryBuffer[j][FRONT_LEFT] += samp1*DrySend[FRONT_LEFT];
                            WetBuffer[j][FRONT_LEFT] += samp1*WetSend[FRONT_LEFT];
                            //First order interpolator (front right)
                            samp2 = (ALfloat)((ALshort)(((Data[k*Channels+1]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels+1]*(fraction)))>>FRACTIONBITS));
                            DryBuffer[j][FRONT_RIGHT] += samp2*DrySend[FRONT_RIGHT];
                            WetBuffer[j][FRONT_RIGHT] += samp2*WetSend[FRONT_RIGHT];
                            if(Channels >= 4)
                            {
                                int i = 2;
                                if(Channels >= 6)
                                {
                                    if(Channels != 7)
                                    {
                                        //First order interpolator (center)
                                        value = (ALfloat)((ALshort)(((Data[k*Channels+i]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels+i]*(fraction)))>>FRACTIONBITS));
                                        DryBuffer[j][CENTER] += value*DrySend[CENTER];
                                        WetBuffer[j][CENTER] += value*WetSend[CENTER];
                                        i++;
                                    }
                                    //First order interpolator (lfe)
                                    value = (ALfloat)((ALshort)(((Data[k*Channels+i]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels+i]*(fraction)))>>FRACTIONBITS));
                                    DryBuffer[j][LFE] += value*DrySend[LFE];
                                    WetBuffer[j][LFE] += value*WetSend[LFE];
                                    i++;
                                }
                                //First order interpolator (back left)
                                value = (ALfloat)((ALshort)(((Data[k*Channels+i]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels+i]*(fraction)))>>FRACTIONBITS));
                                DryBuffer[j][BACK_LEFT] += value*DrySend[BACK_LEFT];
                                WetBuffer[j][BACK_LEFT] += value*WetSend[BACK_LEFT];
                                i++;
                                //First order interpolator (back right)
                                value = (ALfloat)((ALshort)(((Data[k*Channels+i]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels+i]*(fraction)))>>FRACTIONBITS));
                                DryBuffer[j][BACK_RIGHT] += value*DrySend[BACK_RIGHT];
                                WetBuffer[j][BACK_RIGHT] += value*WetSend[BACK_RIGHT];
                                i++;
                                if(Channels >= 7)
                                {
                                    //First order interpolator (side left)
                                    value = (ALfloat)((ALshort)(((Data[k*Channels+i]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels+i]*(fraction)))>>FRACTIONBITS));
                                    DryBuffer[j][SIDE_LEFT] += value*DrySend[SIDE_LEFT];
                                    WetBuffer[j][SIDE_LEFT] += value*WetSend[SIDE_LEFT];
                                    i++;
                                    //First order interpolator (side right)
                                    value = (ALfloat)((ALshort)(((Data[k*Channels+i]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels+i]*(fraction)))>>FRACTIONBITS));
                                    DryBuffer[j][SIDE_RIGHT] += value*DrySend[SIDE_RIGHT];
                                    WetBuffer[j][SIDE_RIGHT] += value*WetSend[SIDE_RIGHT];
                                    i++;
                                }
                            }
                            else if(DuplicateStereo)
                            {
                                //Duplicate stereo channels on the back speakers
                                DryBuffer[j][BACK_LEFT] += samp1*DrySend[BACK_LEFT];
                                WetBuffer[j][BACK_LEFT] += samp1*WetSend[BACK_LEFT];
                                DryBuffer[j][BACK_RIGHT] += samp2*DrySend[BACK_RIGHT];
                                WetBuffer[j][BACK_RIGHT] += samp2*WetSend[BACK_RIGHT];
                            }
                        }
                        DataPosFrac += increment;
                        j++;
                    }
                    DataPosInt += (DataPosFrac>>FRACTIONBITS);
                    DataPosFrac = (DataPosFrac&FRACTIONMASK);

                    //Update source info
                    ALSource->position = DataPosInt;
                    ALSource->position_fraction = DataPosFrac;
                }

                //Handle looping sources
                if(!Buffer || DataPosInt >= DataSize)
                {
                    //queueing
                    if(ALSource->queue)
                    {
                        Looping = ALSource->bLooping;
                        if(ALSource->BuffersPlayed < (ALSource->BuffersInQueue-1))
                        {
                            BufferListItem = ALSource->queue;
                            for(loop = 0; loop <= ALSource->BuffersPlayed; loop++)
                            {
                                if(BufferListItem)
                                {
                                    if(!Looping)
                                        BufferListItem->bufferstate = PROCESSED;
                                    BufferListItem = BufferListItem->next;
                                }
                            }
                            if(!Looping)
                                ALSource->BuffersProcessed++;
                            if(BufferListItem)
                                ALSource->ulBufferID = BufferListItem->buffer;
                            ALSource->position = DataPosInt-DataSize;
                            ALSource->position_fraction = DataPosFrac;
                            ALSource->BuffersPlayed++;
                        }
                        else
                        {
                            if(!Looping)
                            {
                                /* alSourceStop */
                                ALSource->state = AL_STOPPED;
                                ALSource->inuse = AL_FALSE;
                                ALSource->BuffersPlayed = ALSource->BuffersProcessed = ALSource->BuffersInQueue;
                                BufferListItem = ALSource->queue;
                                while(BufferListItem != NULL)
                                {
                                    BufferListItem->bufferstate = PROCESSED;
                                    BufferListItem = BufferListItem->next;
                                }
                            }
                            else
                            {
                                /* alSourceRewind */
                                /* alSourcePlay */
                                ALSource->state = AL_PLAYING;
                                ALSource->inuse = AL_TRUE;
                                ALSource->play = AL_TRUE;
                                ALSource->BuffersPlayed = 0;
                                ALSource->BufferPosition = 0;
                                ALSource->lBytesPlayed = 0;
                                ALSource->BuffersProcessed = 0;
                                BufferListItem = ALSource->queue;
                                while(BufferListItem != NULL)
                                {
                                    BufferListItem->bufferstate = PENDING;
                                    BufferListItem = BufferListItem->next;
                                }
                                ALSource->ulBufferID = ALSource->queue->buffer;

                                ALSource->position = DataPosInt-DataSize;
                                ALSource->position_fraction = DataPosFrac;
                            }
                        }
                    }
                }

                //Get source state
                State = ALSource->state;
            }

            ALSource = ALSource->next;
        }

        // effect slot processing
        while(ALEffectSlot)
        {
            if(ALEffectSlot->effect.type == AL_EFFECT_REVERB)
            {
                ALfloat *DelayBuffer = ALEffectSlot->ReverbBuffer;
                ALuint Pos = ALEffectSlot->ReverbPos;
                ALuint LatePos = ALEffectSlot->ReverbLatePos;
                ALuint ReflectPos = ALEffectSlot->ReverbReflectPos;
                ALuint Length = ALEffectSlot->ReverbLength;
                ALfloat DecayGain = ALEffectSlot->ReverbDecayGain;
                ALfloat DecayHFRatio = ALEffectSlot->effect.Reverb.DecayHFRatio;
                ALfloat Gain = ALEffectSlot->effect.Reverb.Gain;
                ALfloat ReflectGain = ALEffectSlot->effect.Reverb.ReflectionsGain;
                ALfloat LateReverbGain = ALEffectSlot->effect.Reverb.LateReverbGain;
                ALfloat sample, lowsample;

                Filter = &ALEffectSlot->iirFilter;
                for(i = 0;i < SamplesToDo;i++)
                {
                    DelayBuffer[Pos] = ReverbBuffer[i] * Gain;

                    sample = DelayBuffer[ReflectPos] * ReflectGain;

                    DelayBuffer[LatePos] *= LateReverbGain;

                    Pos = (Pos+1) % Length;
                    lowsample = lpFilter(Filter, DelayBuffer[Pos]);
                    lowsample += (DelayBuffer[Pos]-lowsample) * DecayHFRatio;

                    DelayBuffer[LatePos] += lowsample * DecayGain;

                    sample += DelayBuffer[LatePos];

                    WetBuffer[i][FRONT_LEFT]  += sample;
                    WetBuffer[i][FRONT_RIGHT] += sample;
                    WetBuffer[i][SIDE_LEFT]   += sample;
                    WetBuffer[i][SIDE_RIGHT]  += sample;
                    WetBuffer[i][BACK_LEFT]   += sample;
                    WetBuffer[i][BACK_RIGHT]  += sample;

                    LatePos = (LatePos+1) % Length;
                    ReflectPos = (ReflectPos+1) % Length;
                }

                ALEffectSlot->ReverbPos = Pos;
                ALEffectSlot->ReverbLatePos = LatePos;
                ALEffectSlot->ReverbReflectPos = ReflectPos;
            }

            ALEffectSlot = ALEffectSlot->next;
        }

        //Post processing loop
        switch(format)
        {
            case AL_FORMAT_MONO8:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALubyte*)buffer)[0] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_LEFT]+DryBuffer[i][FRONT_RIGHT]+
                                                                WetBuffer[i][FRONT_LEFT]+WetBuffer[i][FRONT_RIGHT])>>8)+128);
                    buffer = ((ALubyte*)buffer) + 1;
                }
                break;
            case AL_FORMAT_STEREO8:
                if(ALContext && ALContext->bs2b)
                {
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        float samples[2];
                        samples[0] = DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT];
                        samples[1] = DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT];
                        bs2b_cross_feed(ALContext->bs2b, samples);
                        ((ALubyte*)buffer)[0] = (ALubyte)((aluF2S(samples[0])>>8)+128);
                        ((ALubyte*)buffer)[1] = (ALubyte)((aluF2S(samples[1])>>8)+128);
                        buffer = ((ALubyte*)buffer) + 2;
                    }
                }
                else
                {
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        ((ALubyte*)buffer)[0] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT])>>8)+128);
                        ((ALubyte*)buffer)[1] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT])>>8)+128);
                        buffer = ((ALubyte*)buffer) + 2;
                    }
                }
                break;
            case AL_FORMAT_QUAD8:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALubyte*)buffer)[0] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[1] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT])>>8)+128);
                    ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
                    buffer = ((ALubyte*)buffer) + 4;
                }
                break;
            case AL_FORMAT_51CHN8:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALubyte*)buffer)[0] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[1] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT])>>8)+128);
#ifdef _WIN32 /* Of course, Windows can't use the same ordering... */
                    ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER])>>8)+128);
                    ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE])>>8)+128);
                    ((ALubyte*)buffer)[4] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[5] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
#else
                    ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
                    ((ALubyte*)buffer)[4] = (ALubyte)((aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER])>>8)+128);
                    ((ALubyte*)buffer)[5] = (ALubyte)((aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE])>>8)+128);
#endif
                    buffer = ((ALubyte*)buffer) + 6;
                }
                break;
            case AL_FORMAT_61CHN8:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALubyte*)buffer)[0] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[1] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT])>>8)+128);
#ifdef _WIN32
                    ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE])>>8)+128);
                    ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[4] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
#else
                    ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
                    ((ALubyte*)buffer)[4] = (ALubyte)((aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE])>>8)+128);
#endif
                    ((ALubyte*)buffer)[5] = (ALubyte)((aluF2S(DryBuffer[i][SIDE_LEFT]  +WetBuffer[i][SIDE_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[6] = (ALubyte)((aluF2S(DryBuffer[i][SIDE_RIGHT] +WetBuffer[i][SIDE_RIGHT])>>8)+128);
                    buffer = ((ALubyte*)buffer) + 7;
                }
                break;
            case AL_FORMAT_71CHN8:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALubyte*)buffer)[0] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[1] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT])>>8)+128);
#ifdef _WIN32
                    ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER])>>8)+128);
                    ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE])>>8)+128);
                    ((ALubyte*)buffer)[4] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[5] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
#else
                    ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
                    ((ALubyte*)buffer)[4] = (ALubyte)((aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER])>>8)+128);
                    ((ALubyte*)buffer)[5] = (ALubyte)((aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE])>>8)+128);
#endif
                    ((ALubyte*)buffer)[6] = (ALubyte)((aluF2S(DryBuffer[i][SIDE_LEFT]  +WetBuffer[i][SIDE_LEFT])>>8)+128);
                    ((ALubyte*)buffer)[7] = (ALubyte)((aluF2S(DryBuffer[i][SIDE_RIGHT] +WetBuffer[i][SIDE_RIGHT])>>8)+128);
                    buffer = ((ALubyte*)buffer) + 8;
                }
                break;

            case AL_FORMAT_MONO16:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALshort*)buffer)[0] = aluF2S(DryBuffer[i][FRONT_LEFT]+DryBuffer[i][FRONT_RIGHT]+
                                                    WetBuffer[i][FRONT_LEFT]+WetBuffer[i][FRONT_RIGHT]);
                    buffer = ((ALshort*)buffer) + 1;
                }
                break;
            case AL_FORMAT_STEREO16:
                if(ALContext && ALContext->bs2b)
                {
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        float samples[2];
                        samples[0] = DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT];
                        samples[1] = DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT];
                        bs2b_cross_feed(ALContext->bs2b, samples);
                        ((ALshort*)buffer)[0] = aluF2S(samples[0]);
                        ((ALshort*)buffer)[1] = aluF2S(samples[1]);
                        buffer = ((ALshort*)buffer) + 2;
                    }
                }
                else
                {
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        ((ALshort*)buffer)[0] = aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT]);
                        ((ALshort*)buffer)[1] = aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT]);
                        buffer = ((ALshort*)buffer) + 2;
                    }
                }
                break;
            case AL_FORMAT_QUAD16:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALshort*)buffer)[0] = aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT]);
                    ((ALshort*)buffer)[1] = aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT]);
                    ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                    ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
                    buffer = ((ALshort*)buffer) + 4;
                }
                break;
            case AL_FORMAT_51CHN16:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALshort*)buffer)[0] = aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT]);
                    ((ALshort*)buffer)[1] = aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT]);
#ifdef _WIN32
                    ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER]);
                    ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE]);
                    ((ALshort*)buffer)[4] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                    ((ALshort*)buffer)[5] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
#else
                    ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                    ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
                    ((ALshort*)buffer)[4] = aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER]);
                    ((ALshort*)buffer)[5] = aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE]);
#endif
                    buffer = ((ALshort*)buffer) + 6;
                }
                break;
            case AL_FORMAT_61CHN16:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALshort*)buffer)[0] = aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT]);
                    ((ALshort*)buffer)[1] = aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT]);
#ifdef _WIN32
                    ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE]);
                    ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                    ((ALshort*)buffer)[4] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
#else
                    ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                    ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
                    ((ALshort*)buffer)[4] = aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE]);
#endif
                    ((ALshort*)buffer)[5] = aluF2S(DryBuffer[i][SIDE_LEFT]  +WetBuffer[i][SIDE_LEFT]);
                    ((ALshort*)buffer)[6] = aluF2S(DryBuffer[i][SIDE_RIGHT] +WetBuffer[i][SIDE_RIGHT]);
                    buffer = ((ALshort*)buffer) + 7;
                }
                break;
            case AL_FORMAT_71CHN16:
                for(i = 0;i < SamplesToDo;i++)
                {
                    ((ALshort*)buffer)[0] = aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT]);
                    ((ALshort*)buffer)[1] = aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT]);
#ifdef _WIN32
                    ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER]);
                    ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE]);
                    ((ALshort*)buffer)[4] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                    ((ALshort*)buffer)[5] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
#else
                    ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                    ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
                    ((ALshort*)buffer)[4] = aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER]);
                    ((ALshort*)buffer)[5] = aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE]);
#endif
                    ((ALshort*)buffer)[6] = aluF2S(DryBuffer[i][SIDE_LEFT]  +WetBuffer[i][SIDE_LEFT]);
                    ((ALshort*)buffer)[7] = aluF2S(DryBuffer[i][SIDE_RIGHT] +WetBuffer[i][SIDE_RIGHT]);
                    buffer = ((ALshort*)buffer) + 8;
                }
                break;

            default:
                break;
        }

        size -= SamplesToDo;
    }

    ProcessContext(ALContext);
}
