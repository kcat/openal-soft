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

#define FRACTIONBITS 14
#define FRACTIONMASK ((1L<<FRACTIONBITS)-1)
#define MAX_PITCH 65536

/* Minimum ramp length in milliseconds. The value below was chosen to
 * adequately reduce clicks and pops from harsh gain changes. */
#define MIN_RAMP_LENGTH  16

ALboolean DuplicateStereo = AL_FALSE;


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

static ALvoid SetSpeakerArrangement(const char *name, ALfloat SpeakerAngle[OUTPUTCHANNELS],
                                    ALint Speaker2Chan[OUTPUTCHANNELS], ALint chans)
{
    const char *confkey;
    const char *next;
    const char *sep;
    const char *end;
    int i, val;

    confkey = GetConfigValue(NULL, name, "");
    next = confkey;
    while(next && *next)
    {
        confkey = next;
        next = strchr(confkey, ',');
        if(next)
        {
            do {
                next++;
            } while(isspace(*next));
        }

        sep = strchr(confkey, '=');
        if(!sep || confkey == sep)
            continue;

        end = sep - 1;
        while(isspace(*end) && end != confkey)
            end--;
        end++;

        if(strncmp(confkey, "fl", end-confkey) == 0)
            val = FRONT_LEFT;
        else if(strncmp(confkey, "fr", end-confkey) == 0)
            val = FRONT_RIGHT;
        else if(strncmp(confkey, "fc", end-confkey) == 0)
            val = FRONT_CENTER;
        else if(strncmp(confkey, "bl", end-confkey) == 0)
            val = BACK_LEFT;
        else if(strncmp(confkey, "br", end-confkey) == 0)
            val = BACK_RIGHT;
        else if(strncmp(confkey, "bc", end-confkey) == 0)
            val = BACK_CENTER;
        else if(strncmp(confkey, "sl", end-confkey) == 0)
            val = SIDE_LEFT;
        else if(strncmp(confkey, "sr", end-confkey) == 0)
            val = SIDE_RIGHT;
        else
        {
            AL_PRINT("Unknown speaker for %s: \"%c%c\"\n", name, confkey[0], confkey[1]);
            continue;
        }

        sep++;
        while(isspace(*sep))
            sep++;

        for(i = 0;i < chans;i++)
        {
            if(Speaker2Chan[i] == val)
            {
                val = strtol(sep, NULL, 10);
                if(val >= -180 && val <= 180)
                    SpeakerAngle[i] = val * M_PI/180.0f;
                else
                    AL_PRINT("Invalid angle for speaker \"%c%c\": %d\n", confkey[0], confkey[1], val);
                break;
            }
        }
    }

    for(i = 1;i < chans;i++)
    {
        if(SpeakerAngle[i] <= SpeakerAngle[i-1])
        {
            AL_PRINT("Speaker %d of %d does not follow previous: %f > %f\n", i, chans,
                     SpeakerAngle[i-1] * 180.0f/M_PI, SpeakerAngle[i] * 180.0f/M_PI);
            SpeakerAngle[i] = SpeakerAngle[i-1] + 1 * 180.0f/M_PI;
        }
    }
}

static __inline ALfloat aluLUTpos2Angle(ALint pos)
{
    if(pos < QUADRANT_NUM)
        return aluAtan((ALfloat)pos / (ALfloat)(QUADRANT_NUM - pos));
    if(pos < 2 * QUADRANT_NUM)
        return M_PI_2 + aluAtan((ALfloat)(pos - QUADRANT_NUM) / (ALfloat)(2 * QUADRANT_NUM - pos));
    if(pos < 3 * QUADRANT_NUM)
        return aluAtan((ALfloat)(pos - 2 * QUADRANT_NUM) / (ALfloat)(3 * QUADRANT_NUM - pos)) - M_PI;
    return aluAtan((ALfloat)(pos - 3 * QUADRANT_NUM) / (ALfloat)(4 * QUADRANT_NUM - pos)) - M_PI_2;
}

ALvoid aluInitPanning(ALCcontext *Context)
{
    ALint pos, offset, s;
    ALfloat Alpha, Theta;
    ALfloat SpeakerAngle[OUTPUTCHANNELS];
    ALint Speaker2Chan[OUTPUTCHANNELS];

    for(s = 0;s < OUTPUTCHANNELS;s++)
    {
        int s2;
        for(s2 = 0;s2 < OUTPUTCHANNELS;s2++)
            Context->ChannelMatrix[s][s2] = ((s==s2) ? 1.0f : 0.0f);
    }

    switch(Context->Device->Format)
    {
        /* Mono is rendered as stereo, then downmixed during post-process */
        case AL_FORMAT_MONO8:
        case AL_FORMAT_MONO16:
        case AL_FORMAT_MONO_FLOAT32:
            Context->ChannelMatrix[FRONT_CENTER][FRONT_LEFT]  = aluSqrt(0.5);
            Context->ChannelMatrix[FRONT_CENTER][FRONT_RIGHT] = aluSqrt(0.5);
            Context->ChannelMatrix[SIDE_LEFT][FRONT_LEFT]     = 1.0f;
            Context->ChannelMatrix[SIDE_RIGHT][FRONT_RIGHT]   = 1.0f;
            Context->ChannelMatrix[BACK_LEFT][FRONT_LEFT]     = 1.0f;
            Context->ChannelMatrix[BACK_RIGHT][FRONT_RIGHT]   = 1.0f;
            Context->ChannelMatrix[BACK_CENTER][FRONT_LEFT]   = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_CENTER][FRONT_RIGHT]  = aluSqrt(0.5);
            Context->NumChan = 2;
            Speaker2Chan[0] = FRONT_LEFT;
            Speaker2Chan[1] = FRONT_RIGHT;
            SpeakerAngle[0] = -90.0f * M_PI/180.0f;
            SpeakerAngle[1] =  90.0f * M_PI/180.0f;
            break;

        case AL_FORMAT_STEREO8:
        case AL_FORMAT_STEREO16:
        case AL_FORMAT_STEREO_FLOAT32:
            Context->ChannelMatrix[FRONT_CENTER][FRONT_LEFT]  = aluSqrt(0.5);
            Context->ChannelMatrix[FRONT_CENTER][FRONT_RIGHT] = aluSqrt(0.5);
            Context->ChannelMatrix[SIDE_LEFT][FRONT_LEFT]     = 1.0f;
            Context->ChannelMatrix[SIDE_RIGHT][FRONT_RIGHT]   = 1.0f;
            Context->ChannelMatrix[BACK_LEFT][FRONT_LEFT]     = 1.0f;
            Context->ChannelMatrix[BACK_RIGHT][FRONT_RIGHT]   = 1.0f;
            Context->ChannelMatrix[BACK_CENTER][FRONT_LEFT]   = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_CENTER][FRONT_RIGHT]  = aluSqrt(0.5);
            Context->NumChan = 2;
            Speaker2Chan[0] = FRONT_LEFT;
            Speaker2Chan[1] = FRONT_RIGHT;
            SpeakerAngle[0] = -90.0f * M_PI/180.0f;
            SpeakerAngle[1] =  90.0f * M_PI/180.0f;
            SetSpeakerArrangement("layout_STEREO", SpeakerAngle, Speaker2Chan, Context->NumChan);
            break;

        case AL_FORMAT_QUAD8:
        case AL_FORMAT_QUAD16:
        case AL_FORMAT_QUAD32:
            Context->ChannelMatrix[FRONT_CENTER][FRONT_LEFT]  = aluSqrt(0.5);
            Context->ChannelMatrix[FRONT_CENTER][FRONT_RIGHT] = aluSqrt(0.5);
            Context->ChannelMatrix[SIDE_LEFT][FRONT_LEFT]     = aluSqrt(0.5);
            Context->ChannelMatrix[SIDE_LEFT][BACK_LEFT]      = aluSqrt(0.5);
            Context->ChannelMatrix[SIDE_RIGHT][FRONT_RIGHT]   = aluSqrt(0.5);
            Context->ChannelMatrix[SIDE_RIGHT][BACK_RIGHT]    = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_CENTER][BACK_LEFT]    = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_CENTER][BACK_RIGHT]   = aluSqrt(0.5);
            Context->NumChan = 4;
            Speaker2Chan[0] = BACK_LEFT;
            Speaker2Chan[1] = FRONT_LEFT;
            Speaker2Chan[2] = FRONT_RIGHT;
            Speaker2Chan[3] = BACK_RIGHT;
            SpeakerAngle[0] = -135.0f * M_PI/180.0f;
            SpeakerAngle[1] =  -45.0f * M_PI/180.0f;
            SpeakerAngle[2] =   45.0f * M_PI/180.0f;
            SpeakerAngle[3] =  135.0f * M_PI/180.0f;
            SetSpeakerArrangement("layout_QUAD", SpeakerAngle, Speaker2Chan, Context->NumChan);
            break;

        case AL_FORMAT_51CHN8:
        case AL_FORMAT_51CHN16:
        case AL_FORMAT_51CHN32:
            Context->ChannelMatrix[SIDE_LEFT][FRONT_LEFT]   = aluSqrt(0.5);
            Context->ChannelMatrix[SIDE_LEFT][BACK_LEFT]    = aluSqrt(0.5);
            Context->ChannelMatrix[SIDE_RIGHT][FRONT_RIGHT] = aluSqrt(0.5);
            Context->ChannelMatrix[SIDE_RIGHT][BACK_RIGHT]  = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_CENTER][BACK_LEFT]  = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_CENTER][BACK_RIGHT] = aluSqrt(0.5);
            Context->NumChan = 5;
            Speaker2Chan[0] = BACK_LEFT;
            Speaker2Chan[1] = FRONT_LEFT;
            Speaker2Chan[2] = FRONT_CENTER;
            Speaker2Chan[3] = FRONT_RIGHT;
            Speaker2Chan[4] = BACK_RIGHT;
            SpeakerAngle[0] = -110.0f * M_PI/180.0f;
            SpeakerAngle[1] =  -30.0f * M_PI/180.0f;
            SpeakerAngle[2] =    0.0f * M_PI/180.0f;
            SpeakerAngle[3] =   30.0f * M_PI/180.0f;
            SpeakerAngle[4] =  110.0f * M_PI/180.0f;
            SetSpeakerArrangement("layout_51CHN", SpeakerAngle, Speaker2Chan, Context->NumChan);
            break;

        case AL_FORMAT_61CHN8:
        case AL_FORMAT_61CHN16:
        case AL_FORMAT_61CHN32:
            Context->ChannelMatrix[BACK_LEFT][BACK_CENTER]  = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_LEFT][SIDE_LEFT]    = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_RIGHT][BACK_CENTER] = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_RIGHT][SIDE_RIGHT]  = aluSqrt(0.5);
            Context->NumChan = 6;
            Speaker2Chan[0] = SIDE_LEFT;
            Speaker2Chan[1] = FRONT_LEFT;
            Speaker2Chan[2] = FRONT_CENTER;
            Speaker2Chan[3] = FRONT_RIGHT;
            Speaker2Chan[4] = SIDE_RIGHT;
            Speaker2Chan[5] = BACK_CENTER;
            SpeakerAngle[0] = -90.0f * M_PI/180.0f;
            SpeakerAngle[1] = -30.0f * M_PI/180.0f;
            SpeakerAngle[2] =   0.0f * M_PI/180.0f;
            SpeakerAngle[3] =  30.0f * M_PI/180.0f;
            SpeakerAngle[4] =  90.0f * M_PI/180.0f;
            SpeakerAngle[5] = 180.0f * M_PI/180.0f;
            SetSpeakerArrangement("layout_61CHN", SpeakerAngle, Speaker2Chan, Context->NumChan);
            break;

        case AL_FORMAT_71CHN8:
        case AL_FORMAT_71CHN16:
        case AL_FORMAT_71CHN32:
            Context->ChannelMatrix[BACK_CENTER][BACK_LEFT]  = aluSqrt(0.5);
            Context->ChannelMatrix[BACK_CENTER][BACK_RIGHT] = aluSqrt(0.5);
            Context->NumChan = 7;
            Speaker2Chan[0] = BACK_LEFT;
            Speaker2Chan[1] = SIDE_LEFT;
            Speaker2Chan[2] = FRONT_LEFT;
            Speaker2Chan[3] = FRONT_CENTER;
            Speaker2Chan[4] = FRONT_RIGHT;
            Speaker2Chan[5] = SIDE_RIGHT;
            Speaker2Chan[6] = BACK_RIGHT;
            SpeakerAngle[0] = -150.0f * M_PI/180.0f;
            SpeakerAngle[1] =  -90.0f * M_PI/180.0f;
            SpeakerAngle[2] =  -30.0f * M_PI/180.0f;
            SpeakerAngle[3] =    0.0f * M_PI/180.0f;
            SpeakerAngle[4] =   30.0f * M_PI/180.0f;
            SpeakerAngle[5] =   90.0f * M_PI/180.0f;
            SpeakerAngle[6] =  150.0f * M_PI/180.0f;
            SetSpeakerArrangement("layout_71CHN", SpeakerAngle, Speaker2Chan, Context->NumChan);
            break;

        default:
            assert(0);
    }

    for(pos = 0; pos < LUT_NUM; pos++)
    {
        /* source angle */
        Theta = aluLUTpos2Angle(pos);

        /* clear all values */
        offset = OUTPUTCHANNELS * pos;
        for(s = 0; s < OUTPUTCHANNELS; s++)
            Context->PanningLUT[offset+s] = 0.0f;

        /* set panning values */
        for(s = 0; s < Context->NumChan - 1; s++)
        {
            if(Theta >= SpeakerAngle[s] && Theta < SpeakerAngle[s+1])
            {
                /* source between speaker s and speaker s+1 */
                Alpha = M_PI_2 * (Theta-SpeakerAngle[s]) /
                                 (SpeakerAngle[s+1]-SpeakerAngle[s]);
                Context->PanningLUT[offset + Speaker2Chan[s]]   = cos(Alpha);
                Context->PanningLUT[offset + Speaker2Chan[s+1]] = sin(Alpha);
                break;
            }
        }
        if(s == Context->NumChan - 1)
        {
            /* source between last and first speaker */
            if(Theta < SpeakerAngle[0])
                Theta += 2.0f * M_PI;
            Alpha = M_PI_2 * (Theta-SpeakerAngle[s]) /
                             (2.0f * M_PI + SpeakerAngle[0]-SpeakerAngle[s]);
            Context->PanningLUT[offset + Speaker2Chan[s]] = cos(Alpha);
            Context->PanningLUT[offset + Speaker2Chan[0]] = sin(Alpha);
        }
    }
}

static ALvoid CalcSourceParams(const ALCcontext *ALContext, ALsource *ALSource,
                               ALboolean isMono)
{
    ALfloat InnerAngle,OuterAngle,Angle,Distance,DryMix,OrigDist;
    ALfloat Direction[3],Position[3],SourceToListener[3];
    ALfloat Velocity[3],ListenerVel[3];
    ALfloat MinVolume,MaxVolume,MinDist,MaxDist,Rolloff,OuterGainHF;
    ALfloat ConeVolume,ConeHF,SourceVolume,ListenerGain;
    ALfloat DopplerFactor, DopplerVelocity, flSpeedOfSound;
    ALfloat Matrix[4][4];
    ALfloat flAttenuation;
    ALfloat RoomAttenuation[MAX_SENDS];
    ALfloat MetersPerUnit;
    ALfloat RoomRolloff[MAX_SENDS];
    ALfloat DryGainHF = 1.0f;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALfloat DirGain, AmbientGain;
    ALfloat length;
    const ALfloat *SpeakerGain;
    ALuint Frequency;
    ALint NumSends;
    ALint pos, s, i;
    ALfloat cw, a, g;

    for(i = 0;i < MAX_SENDS;i++)
        WetGainHF[i] = 1.0f;

    //Get context properties
    DopplerFactor   = ALContext->DopplerFactor * ALSource->DopplerFactor;
    DopplerVelocity = ALContext->DopplerVelocity;
    flSpeedOfSound  = ALContext->flSpeedOfSound;
    NumSends        = ALContext->Device->NumAuxSends;
    Frequency       = ALContext->Device->Frequency;

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

    //Only apply 3D calculations for mono buffers
    if(isMono == AL_FALSE)
    {
        //1. Multi-channel buffers always play "normal"
        ALSource->Params.Pitch = ALSource->flPitch;

        DryMix = SourceVolume;
        DryMix = __min(DryMix,MaxVolume);
        DryMix = __max(DryMix,MinVolume);

        switch(ALSource->DirectFilter.type)
        {
            case AL_FILTER_LOWPASS:
                DryMix *= ALSource->DirectFilter.Gain;
                DryGainHF *= ALSource->DirectFilter.GainHF;
                break;
        }

        ALSource->Params.DryGains[FRONT_LEFT]   = DryMix * ListenerGain;
        ALSource->Params.DryGains[FRONT_RIGHT]  = DryMix * ListenerGain;
        ALSource->Params.DryGains[SIDE_LEFT]    = DryMix * ListenerGain;
        ALSource->Params.DryGains[SIDE_RIGHT]   = DryMix * ListenerGain;
        ALSource->Params.DryGains[BACK_LEFT]    = DryMix * ListenerGain;
        ALSource->Params.DryGains[BACK_RIGHT]   = DryMix * ListenerGain;
        ALSource->Params.DryGains[FRONT_CENTER] = DryMix * ListenerGain;
        ALSource->Params.DryGains[BACK_CENTER]  = DryMix * ListenerGain;
        ALSource->Params.DryGains[LFE]          = DryMix * ListenerGain;

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
        g = __max(DryGainHF, 0.01f);
        a = 0.0f;
        /* Be careful with gains < 0.0001, as that causes the coefficient
         * head towards 1, which will flatten the signal */
        if(g < 0.9999f) /* 1-epsilon */
            a = (1 - g*cw - aluSqrt(2*g*(1-cw) - g*g*(1 - cw*cw))) /
                (1 - g);
        ALSource->Params.iirFilter.coeff = a;

        for(i = 0;i < NumSends;i++)
        {
            /* We use a one-pole filter, so we need to take the squared gain */
            g = __max(WetGainHF[i], 0.1f);
            g *= g;
            a = 0.0f;
            if(g < 0.9999f) /* 1-epsilon */
                a = (1 - g*cw - aluSqrt(2*g*(1-cw) - g*g*(1 - cw*cw))) /
                    (1 - g);
            ALSource->Params.Send[i].iirFilter.coeff = a;
        }

        return;
    }

    //1. Translate Listener to origin (convert to head relative)
    if(ALSource->bHeadRelative==AL_FALSE)
    {
        ALfloat U[3],V[3],N[3],P[3];

        // Build transform matrix
        memcpy(N, ALContext->Listener.Forward, sizeof(N));  // At-vector
        aluNormalize(N);  // Normalized At-vector
        memcpy(V, ALContext->Listener.Up, sizeof(V));  // Up-vector
        aluNormalize(V);  // Normalized Up-vector
        aluCrossproduct(N, V, U); // Right-vector
        aluNormalize(U);  // Normalized Right-vector
        P[0] = -(ALContext->Listener.Position[0]*U[0] + // Translation
                 ALContext->Listener.Position[1]*U[1] +
                 ALContext->Listener.Position[2]*U[2]);
        P[1] = -(ALContext->Listener.Position[0]*V[0] +
                 ALContext->Listener.Position[1]*V[1] +
                 ALContext->Listener.Position[2]*V[2]);
        P[2] = -(ALContext->Listener.Position[0]*-N[0] +
                 ALContext->Listener.Position[1]*-N[1] +
                 ALContext->Listener.Position[2]*-N[2]);
        Matrix[0][0] = U[0]; Matrix[0][1] = V[0]; Matrix[0][2] = -N[0]; Matrix[0][3] = 0.0f;
        Matrix[1][0] = U[1]; Matrix[1][1] = V[1]; Matrix[1][2] = -N[1]; Matrix[1][3] = 0.0f;
        Matrix[2][0] = U[2]; Matrix[2][1] = V[2]; Matrix[2][2] = -N[2]; Matrix[2][3] = 0.0f;
        Matrix[3][0] = P[0]; Matrix[3][1] = P[1]; Matrix[3][2] =  P[2]; Matrix[3][3] = 1.0f;

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
                flAttenuation = (ALfloat)pow(Distance/MinDist, -Rolloff);
                for(i = 0;i < NumSends;i++)
                    RoomAttenuation[i] = (ALfloat)pow(Distance/MinDist, -RoomRolloff[i]);
            }
            break;

        case AL_NONE:
            break;
    }

    // Source Gain + Attenuation
    DryMix = SourceVolume * flAttenuation;
    for(i = 0;i < NumSends;i++)
        WetGain[i] = SourceVolume * RoomAttenuation[i];

    // Distance-based air absorption
    if(ALSource->AirAbsorptionFactor > 0.0f && flAttenuation < 1.0f)
    {
        ALfloat absorb = 0.0f;

        // Absorption calculation is done in dB
        if(flAttenuation > 0.0f)
        {
            absorb = (MinDist/flAttenuation - MinDist)*MetersPerUnit *
                     (ALSource->AirAbsorptionFactor*AIRABSORBGAINDBHF);
            // Convert dB to linear gain before applying
            absorb = pow(10.0, absorb/20.0);
        }
        DryGainHF *= absorb;
        for(i = 0;i < NumSends;i++)
            WetGainHF[i] *= absorb;
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
        ConeHF *= 1.0f - (ALContext->Device->HeadDampen*scale);
    }

    DryMix *= ConeVolume;
    if(ALSource->DryGainHFAuto)
        DryGainHF *= ConeHF;

    // Clamp to Min/Max Gain
    DryMix = __min(DryMix,MaxVolume);
    DryMix = __max(DryMix,MinVolume);

    for(i = 0;i < NumSends;i++)
    {
        if(ALSource->Send[i].Slot &&
           ALSource->Send[i].Slot->effect.type != AL_EFFECT_NULL)
        {
            if(ALSource->Send[i].Slot->AuxSendAuto)
            {
                if(ALSource->WetGainAuto)
                    WetGain[i] *= ConeVolume;
                if(ALSource->WetGainHFAuto)
                    WetGainHF[i] *= ConeHF;

                // Clamp to Min/Max Gain
                WetGain[i] = __min(WetGain[i],MaxVolume);
                WetGain[i] = __max(WetGain[i],MinVolume);

                if(ALSource->Send[i].Slot->effect.type == AL_EFFECT_REVERB ||
                   ALSource->Send[i].Slot->effect.type == AL_EFFECT_EAXREVERB)
                {
                    /* Apply a decay-time transformation to the wet path,
                     * based on the attenuation of the dry path.  This should
                     * better approximate the statistical attenuation model
                     * for the reverb effect.
                     *
                     * This simple equation converts the distance attenuation
                     * into the time it would take to reach -60 dB. From
                     * there it establishes an origin (0.333s; the decay time
                     * that will produce equal attenuation) and applies the
                     * current decay time.  Finally, it converts the result
                     * back to an attenuation for the reverb path.
                     */
                    WetGain[i] *= pow(10.0f, log10(flAttenuation) * 0.333f /
                        ALSource->Send[i].Slot->effect.Reverb.DecayTime);
                }
            }
            else
            {
                // If the slot's auxiliary send auto is off, the data sent to
                // the effect slot is the same as the dry path, sans filter
                // effects
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
        else
        {
            ALSource->Params.WetGains[i] = 0.0f;
            WetGainHF[i] = 1.0f;
        }
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
    SpeakerGain = &ALContext->PanningLUT[OUTPUTCHANNELS * pos];

    DirGain = aluSqrt(Position[0]*Position[0] + Position[2]*Position[2]);
    // elevation adjustment for directional gain. this sucks, but
    // has low complexity
    AmbientGain = 1.0/aluSqrt(ALContext->NumChan) * (1.0-DirGain);
    for(s = 0; s < OUTPUTCHANNELS; s++)
    {
        ALfloat gain = SpeakerGain[s]*DirGain + AmbientGain;
        ALSource->Params.DryGains[s] = DryMix * gain;
    }

    /* Update filter coefficients. */
    cw = cos(2.0*M_PI * LOWPASSFREQCUTOFF / Frequency);
    /* Spatialized sources use four chained one-pole filters, so we need to
     * take the fourth root of the squared gain, which is the same as the
     * square root of the base gain. */
    g = aluSqrt(__max(DryGainHF, 0.0001f));
    a = 0.0f;
    if(g < 0.9999f) /* 1-epsilon */
        a = (1 - g*cw - aluSqrt(2*g*(1-cw) - g*g*(1 - cw*cw))) /
            (1 - g);
    ALSource->Params.iirFilter.coeff = a;

    for(i = 0;i < NumSends;i++)
    {
        /* The wet path uses two chained one-pole filters, so take the
         * base gain (square root of the squared gain) */
        g = __max(WetGainHF[i], 0.01f);
        a = 0.0f;
        if(g < 0.9999f) /* 1-epsilon */
            a = (1 - g*cw - aluSqrt(2*g*(1-cw) - g*g*(1 - cw*cw))) /
                (1 - g);
        ALSource->Params.Send[i].iirFilter.coeff = a;
    }
}

static __inline ALfloat lerp(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1 + ((val2-val1)*(frac * (1.0f/(1<<FRACTIONBITS))));
}

static void MixSomeSources(ALCcontext *ALContext, float (*DryBuffer)[OUTPUTCHANNELS], ALuint SamplesToDo)
{
    static float DummyBuffer[BUFFERSIZE];
    ALfloat *WetBuffer[MAX_SENDS];
    ALfloat (*Matrix)[OUTPUTCHANNELS] = ALContext->ChannelMatrix;
    ALfloat DrySend[OUTPUTCHANNELS];
    ALfloat dryGainStep[OUTPUTCHANNELS];
    ALfloat wetGainStep[MAX_SENDS];
    ALuint i, j, k, out;
    ALsource *ALSource;
    ALfloat value, outsamp;
    ALbufferlistitem *BufferListItem;
    ALint64 DataSize64,DataPos64;
    FILTER *DryFilter, *WetFilter[MAX_SENDS];
    ALfloat WetSend[MAX_SENDS];
    ALuint rampLength;
    ALuint DeviceFreq;
    ALint increment;
    ALuint DataPosInt, DataPosFrac;
    ALuint Channels, Bytes;
    ALuint Frequency;
    ALuint BuffersPlayed;
    ALfloat Pitch;
    ALenum State;

    if(!(ALSource=ALContext->Source))
        return;

    DeviceFreq = ALContext->Device->Frequency;

    rampLength = DeviceFreq * MIN_RAMP_LENGTH / 1000;
    rampLength = max(rampLength, SamplesToDo);

another_source:
    State = ALSource->state;
    if(State != AL_PLAYING)
    {
        if((ALSource=ALSource->next) != NULL)
            goto another_source;
        return;
    }
    j = 0;

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

    /* Get source info */
    BuffersPlayed = ALSource->BuffersPlayed;
    DataPosInt    = ALSource->position;
    DataPosFrac   = ALSource->position_fraction;

    if(ALSource->NeedsUpdate)
    {
        CalcSourceParams(ALContext, ALSource, (Channels==1)?AL_TRUE:AL_FALSE);
        ALSource->NeedsUpdate = AL_FALSE;
    }

    /* Compute 18.14 fixed point step */
    Pitch = (ALSource->Params.Pitch*Frequency) / DeviceFreq;
    if(Pitch > (float)MAX_PITCH)  Pitch = (float)MAX_PITCH;
    increment = (ALint)(Pitch*(ALfloat)(1L<<FRACTIONBITS));
    if(increment <= 0)  increment = (1<<FRACTIONBITS);

    /* Compute the gain steps for each output channel */
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

    if(DuplicateStereo && Channels == 2)
    {
        Matrix[FRONT_LEFT][SIDE_LEFT]   = 1.0f;
        Matrix[FRONT_RIGHT][SIDE_RIGHT] = 1.0f;
        Matrix[FRONT_LEFT][BACK_LEFT]   = 1.0f;
        Matrix[FRONT_RIGHT][BACK_RIGHT] = 1.0f;
    }
    else if(DuplicateStereo)
    {
        Matrix[FRONT_LEFT][SIDE_LEFT]   = 0.0f;
        Matrix[FRONT_RIGHT][SIDE_RIGHT] = 0.0f;
        Matrix[FRONT_LEFT][BACK_LEFT]   = 0.0f;
        Matrix[FRONT_RIGHT][BACK_RIGHT] = 0.0f;
    }

    /* Get current buffer queue item */
    BufferListItem = ALSource->queue;
    for(i = 0;i < BuffersPlayed && BufferListItem;i++)
        BufferListItem = BufferListItem->next;

    while(State == AL_PLAYING && j < SamplesToDo)
    {
        ALuint DataSize = 0;
        ALbuffer *ALBuffer;
        ALfloat *Data;
        ALuint BufferSize;

        /* Get buffer info */
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            Data      = ALBuffer->data;
            DataSize  = ALBuffer->size;
            DataSize /= Channels * Bytes;
        }
        if(DataPosInt >= DataSize)
            goto skipmix;

        if(BufferListItem->next)
        {
            ALbuffer *NextBuf = BufferListItem->next->buffer;
            if(NextBuf && NextBuf->data)
            {
                ALint ulExtraSamples = BUFFER_PADDING*Channels*Bytes;
                ulExtraSamples = min(NextBuf->size, ulExtraSamples);
                memcpy(&Data[DataSize*Channels], NextBuf->data, ulExtraSamples);
            }
        }
        else if(ALSource->bLooping)
        {
            ALbuffer *NextBuf = ALSource->queue->buffer;
            if(NextBuf && NextBuf->data)
            {
                ALint ulExtraSamples = BUFFER_PADDING*Channels*Bytes;
                ulExtraSamples = min(NextBuf->size, ulExtraSamples);
                memcpy(&Data[DataSize*Channels], NextBuf->data, ulExtraSamples);
            }
        }
        else
            memset(&Data[DataSize*Channels], 0, (BUFFER_PADDING*Channels*Bytes));

        /* Compute the gain steps for each output channel */
        for(i = 0;i < OUTPUTCHANNELS;i++)
            dryGainStep[i] = (ALSource->Params.DryGains[i]-
                              DrySend[i]) / rampLength;
        for(i = 0;i < MAX_SENDS;i++)
            wetGainStep[i] = (ALSource->Params.WetGains[i]-
                              WetSend[i]) / rampLength;

        /* Figure out how many samples we can mix. */
        DataSize64 = DataSize;
        DataSize64 <<= FRACTIONBITS;
        DataPos64 = DataPosInt;
        DataPos64 <<= FRACTIONBITS;
        DataPos64 += DataPosFrac;
        BufferSize = (ALuint)((DataSize64-DataPos64+(increment-1)) / increment);

        BufferSize = min(BufferSize, (SamplesToDo-j));

        /* Actual sample mixing loop */
        k = 0;
        Data += DataPosInt*Channels;

        if(Channels == 1) /* Mono */
        {
            while(BufferSize--)
            {
                for(i = 0;i < OUTPUTCHANNELS;i++)
                    DrySend[i] += dryGainStep[i];
                for(i = 0;i < MAX_SENDS;i++)
                    WetSend[i] += wetGainStep[i];

                /* First order interpolator */
                value = lerp(Data[k], Data[k+1], DataPosFrac);

                /* Direct path final mix buffer and panning */
                outsamp = lpFilter4P(DryFilter, 0, value);
                DryBuffer[j][FRONT_LEFT]   += outsamp*DrySend[FRONT_LEFT];
                DryBuffer[j][FRONT_RIGHT]  += outsamp*DrySend[FRONT_RIGHT];
                DryBuffer[j][SIDE_LEFT]    += outsamp*DrySend[SIDE_LEFT];
                DryBuffer[j][SIDE_RIGHT]   += outsamp*DrySend[SIDE_RIGHT];
                DryBuffer[j][BACK_LEFT]    += outsamp*DrySend[BACK_LEFT];
                DryBuffer[j][BACK_RIGHT]   += outsamp*DrySend[BACK_RIGHT];
                DryBuffer[j][FRONT_CENTER] += outsamp*DrySend[FRONT_CENTER];
                DryBuffer[j][BACK_CENTER]  += outsamp*DrySend[BACK_CENTER];

                /* Room path final mix buffer and panning */
                for(i = 0;i < MAX_SENDS;i++)
                {
                    outsamp = lpFilter2P(WetFilter[i], 0, value);
                    WetBuffer[i][j] += outsamp*WetSend[i];
                }

                DataPosFrac += increment;
                k += DataPosFrac>>FRACTIONBITS;
                DataPosFrac &= FRACTIONMASK;
                j++;
            }
        }
        else if(Channels == 2) /* Stereo */
        {
            const int chans[] = {
                FRONT_LEFT, FRONT_RIGHT
            };
            const ALfloat scaler = aluSqrt(1.0f/Channels);

#define DO_MIX() do { \
    while(BufferSize--) \
    { \
        for(i = 0;i < OUTPUTCHANNELS;i++) \
            DrySend[i] += dryGainStep[i]; \
        for(i = 0;i < MAX_SENDS;i++) \
            WetSend[i] += wetGainStep[i]; \
 \
        for(i = 0;i < Channels;i++) \
        { \
            value = lerp(Data[k*Channels + i], Data[(k+1)*Channels + i], DataPosFrac); \
            outsamp = lpFilter2P(DryFilter, chans[i]*2, value)*DrySend[chans[i]]; \
            for(out = 0;out < OUTPUTCHANNELS;out++) \
                DryBuffer[j][out] += outsamp*Matrix[chans[i]][out]; \
            for(out = 0;out < MAX_SENDS;out++) \
            { \
                outsamp = lpFilter1P(WetFilter[out], chans[i], value); \
                WetBuffer[out][j] += outsamp*WetSend[out]*scaler; \
            } \
        } \
 \
        DataPosFrac += increment; \
        k += DataPosFrac>>FRACTIONBITS; \
        DataPosFrac &= FRACTIONMASK; \
        j++; \
    } \
} while(0)

            DO_MIX();
        }
        else if(Channels == 4) /* Quad */
        {
            const int chans[] = {
                FRONT_LEFT, FRONT_RIGHT,
                BACK_LEFT,  BACK_RIGHT
            };
            const ALfloat scaler = aluSqrt(1.0f/Channels);

            DO_MIX();
        }
        else if(Channels == 6) /* 5.1 */
        {
            const int chans[] = {
                FRONT_LEFT,   FRONT_RIGHT,
                FRONT_CENTER, LFE,
                BACK_LEFT,    BACK_RIGHT
            };
            const ALfloat scaler = aluSqrt(1.0f/Channels);

            DO_MIX();
        }
        else if(Channels == 7) /* 6.1 */
        {
            const int chans[] = {
                FRONT_LEFT,   FRONT_RIGHT,
                FRONT_CENTER, LFE,
                BACK_CENTER,
                SIDE_LEFT,    SIDE_RIGHT
            };
            const ALfloat scaler = aluSqrt(1.0f/Channels);

            DO_MIX();
        }
        else if(Channels == 8) /* 7.1 */
        {
            const int chans[] = {
                FRONT_LEFT,   FRONT_RIGHT,
                FRONT_CENTER, LFE,
                BACK_LEFT,    BACK_RIGHT,
                SIDE_LEFT,    SIDE_RIGHT
            };
            const ALfloat scaler = aluSqrt(1.0f/Channels);

            DO_MIX();
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
                k += DataPosFrac>>FRACTIONBITS;
                DataPosFrac &= FRACTIONMASK;
                j++;
            }
        }
        DataPosInt += k;

    skipmix:
        /* Handle looping sources */
        if(DataPosInt >= DataSize)
        {
            if(BuffersPlayed < (ALSource->BuffersInQueue-1))
            {
                BufferListItem = BufferListItem->next;
                BuffersPlayed++;
                DataPosInt -= DataSize;
            }
            else
            {
                if(!ALSource->bLooping)
                {
                    State = AL_STOPPED;
                    BufferListItem = ALSource->queue;
                    BuffersPlayed = ALSource->BuffersInQueue;
                    DataPosInt = 0;
                    DataPosFrac = 0;
                }
                else
                {
                    BufferListItem = ALSource->queue;
                    BuffersPlayed = 0;
                    if(ALSource->BuffersInQueue == 1)
                        DataPosInt %= DataSize;
                    else
                        DataPosInt -= DataSize;
                }
            }
        }
    }

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

    if((ALSource=ALSource->next) != NULL)
        goto another_source;
}

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size)
{
    float (*DryBuffer)[OUTPUTCHANNELS];
    const Channel *ChanMap;
    ALuint SamplesToDo;
    ALeffectslot *ALEffectSlot;
    ALCcontext *ALContext;
    int fpuState;
    ALuint i, c;

    SuspendContext(NULL);

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

        for(c = 0;c < device->NumContexts;c++)
        {
            ALContext = device->Contexts[c];
            SuspendContext(ALContext);

            MixSomeSources(ALContext, DryBuffer, SamplesToDo);

            /* effect slot processing */
            ALEffectSlot = ALContext->AuxiliaryEffectSlot;
            while(ALEffectSlot)
            {
                if(ALEffectSlot->EffectState)
                    ALEffect_Process(ALEffectSlot->EffectState, ALEffectSlot, SamplesToDo, ALEffectSlot->WetBuffer, DryBuffer);

                for(i = 0;i < SamplesToDo;i++)
                    ALEffectSlot->WetBuffer[i] = 0.0f;
                ALEffectSlot = ALEffectSlot->next;
            }
            ProcessContext(ALContext);
        }

        //Post processing loop
        ChanMap = device->DevChannels;
        switch(device->Format)
        {
#define CHECK_WRITE_FORMAT(bits, type, func)                                  \
        case AL_FORMAT_MONO##bits:                                            \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                ((type*)buffer)[0] = (func)(DryBuffer[i][ChanMap[0]] +        \
                                            DryBuffer[i][ChanMap[1]]);        \
                buffer = ((type*)buffer) + 1;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_STEREO##bits:                                          \
            if(device->Bs2b)                                                  \
            {                                                                 \
                for(i = 0;i < SamplesToDo;i++)                                \
                {                                                             \
                    float samples[2];                                         \
                    samples[0] = DryBuffer[i][ChanMap[0]];                    \
                    samples[1] = DryBuffer[i][ChanMap[1]];                    \
                    bs2b_cross_feed(device->Bs2b, samples);                   \
                    ((type*)buffer)[0] = (func)(samples[0]);                  \
                    ((type*)buffer)[1] = (func)(samples[1]);                  \
                    buffer = ((type*)buffer) + 2;                             \
                }                                                             \
            }                                                                 \
            else                                                              \
            {                                                                 \
                for(i = 0;i < SamplesToDo;i++)                                \
                {                                                             \
                    ((type*)buffer)[0] = (func)(DryBuffer[i][ChanMap[0]]);    \
                    ((type*)buffer)[1] = (func)(DryBuffer[i][ChanMap[1]]);    \
                    buffer = ((type*)buffer) + 2;                             \
                }                                                             \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_QUAD##bits:                                            \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                ((type*)buffer)[0] = (func)(DryBuffer[i][ChanMap[0]]);        \
                ((type*)buffer)[1] = (func)(DryBuffer[i][ChanMap[1]]);        \
                ((type*)buffer)[2] = (func)(DryBuffer[i][ChanMap[2]]);        \
                ((type*)buffer)[3] = (func)(DryBuffer[i][ChanMap[3]]);        \
                buffer = ((type*)buffer) + 4;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_51CHN##bits:                                           \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                ((type*)buffer)[0] = (func)(DryBuffer[i][ChanMap[0]]);        \
                ((type*)buffer)[1] = (func)(DryBuffer[i][ChanMap[1]]);        \
                ((type*)buffer)[2] = (func)(DryBuffer[i][ChanMap[2]]);        \
                ((type*)buffer)[3] = (func)(DryBuffer[i][ChanMap[3]]);        \
                ((type*)buffer)[4] = (func)(DryBuffer[i][ChanMap[4]]);        \
                ((type*)buffer)[5] = (func)(DryBuffer[i][ChanMap[5]]);        \
                buffer = ((type*)buffer) + 6;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_61CHN##bits:                                           \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                ((type*)buffer)[0] = (func)(DryBuffer[i][ChanMap[0]]);        \
                ((type*)buffer)[1] = (func)(DryBuffer[i][ChanMap[1]]);        \
                ((type*)buffer)[2] = (func)(DryBuffer[i][ChanMap[2]]);        \
                ((type*)buffer)[3] = (func)(DryBuffer[i][ChanMap[3]]);        \
                ((type*)buffer)[4] = (func)(DryBuffer[i][ChanMap[4]]);        \
                ((type*)buffer)[5] = (func)(DryBuffer[i][ChanMap[5]]);        \
                ((type*)buffer)[6] = (func)(DryBuffer[i][ChanMap[6]]);        \
                buffer = ((type*)buffer) + 7;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_71CHN##bits:                                           \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                ((type*)buffer)[0] = (func)(DryBuffer[i][ChanMap[0]]);        \
                ((type*)buffer)[1] = (func)(DryBuffer[i][ChanMap[1]]);        \
                ((type*)buffer)[2] = (func)(DryBuffer[i][ChanMap[2]]);        \
                ((type*)buffer)[3] = (func)(DryBuffer[i][ChanMap[3]]);        \
                ((type*)buffer)[4] = (func)(DryBuffer[i][ChanMap[4]]);        \
                ((type*)buffer)[5] = (func)(DryBuffer[i][ChanMap[5]]);        \
                ((type*)buffer)[6] = (func)(DryBuffer[i][ChanMap[6]]);        \
                ((type*)buffer)[7] = (func)(DryBuffer[i][ChanMap[7]]);        \
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

    ProcessContext(NULL);
}

ALvoid aluHandleDisconnect(ALCdevice *device)
{
    ALuint i;

    SuspendContext(NULL);
    for(i = 0;i < device->NumContexts;i++)
    {
        ALsource *source;

        SuspendContext(device->Contexts[i]);

        source = device->Contexts[i]->Source;
        while(source)
        {
            if(source->state == AL_PLAYING)
            {
                source->state = AL_STOPPED;
                source->BuffersPlayed = source->BuffersInQueue;
                source->position = 0;
                source->position_fraction = 0;
            }
            source = source->next;
        }
        ProcessContext(device->Contexts[i]);
    }

    device->Connected = ALC_FALSE;
    ProcessContext(NULL);
}
