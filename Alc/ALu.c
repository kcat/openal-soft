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
    ALCdevice *Device = ALContext->Device;
    ALfloat SourceVolume,ListenerGain,MinVolume,MaxVolume;
    ALbufferlistitem *BufferListItem;
    enum DevFmtChannels DevChans;
    enum FmtChannels Channels;
    ALfloat (*SrcMatrix)[MAXCHANNELS];
    ALfloat DryGain, DryGainHF;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALint NumSends, Frequency;
    const ALfloat *SpeakerGain;
    ALfloat Pitch;
    ALfloat cw;
    ALuint pos;
    ALint i;

    /* Get device properties */
    DevChans  = ALContext->Device->FmtChans;
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

    SrcMatrix = ALSource->Params.DryGains;
    for(i = 0;i < MAXCHANNELS;i++)
    {
        ALuint i2;
        for(i2 = 0;i2 < MAXCHANNELS;i2++)
            SrcMatrix[i][i2] = 0.0f;
    }
    switch(Channels)
    {
    case FmtMono:
        if((ALContext->Device->Flags&DEVICE_USE_HRTF))
        {
            const ALshort *hrtf_left, *hrtf_right;

            GetHrtfCoeffs(0.0, 0.0, &hrtf_left, &hrtf_right);
            for(i = 0;i < HRTF_LENGTH;i++)
            {
                ALSource->Params.HrtfCoeffs[0][i][0] =
                               hrtf_left[i]*(1.0/32767.0)*DryGain*ListenerGain;
                ALSource->Params.HrtfCoeffs[0][i][1] =
                              hrtf_right[i]*(1.0/32767.0)*DryGain*ListenerGain;
            }
        }
        else
        {
            pos = aluCart2LUTpos(cos(0.0), sin(0.0));
            SpeakerGain = &Device->PanningLUT[MAXCHANNELS * pos];

            for(i = 0;i < (ALint)Device->NumChan;i++)
            {
                Channel chan = Device->Speaker2Chan[i];
                SrcMatrix[0][chan] = DryGain * ListenerGain * SpeakerGain[chan];
            }
        }
        break;
    case FmtStereo:
        if((ALContext->Device->Flags&DEVICE_USE_HRTF))
        {
            static const ALfloat angles[2] = { -30.0f, 30.0f };
            ALint c;

            for(c = 0;c < 2;c++)
            {
                const ALshort *hrtf_left, *hrtf_right;

                GetHrtfCoeffs(0.0, angles[c], &hrtf_left, &hrtf_right);
                for(i = 0;i < HRTF_LENGTH;i++)
                {
                    ALSource->Params.HrtfCoeffs[c][i][0] =
                               hrtf_left[i]*(1.0/32767.0)*DryGain*ListenerGain;
                    ALSource->Params.HrtfCoeffs[c][i][1] =
                              hrtf_right[i]*(1.0/32767.0)*DryGain*ListenerGain;
                }
            }
        }
        else
        {
            static const ALfloat angles[2] = { -30.0f, 30.0f };
            ALint c;

            for(c = 0;c < 2;c++)
            {
                pos = aluCart2LUTpos(cos(angles[c] * (M_PI/180.0)),
                                     sin(angles[c] * (M_PI/180.0)));
                SpeakerGain = &Device->PanningLUT[MAXCHANNELS * pos];

                for(i = 0;i < (ALint)Device->NumChan;i++)
                {
                    Channel chan = Device->Speaker2Chan[i];
                    SrcMatrix[c][chan] = DryGain * ListenerGain *
                                         SpeakerGain[chan];
                }
            }
        }
        break;

    case FmtRear:
        if((ALContext->Device->Flags&DEVICE_USE_HRTF))
        {
            static const ALfloat angles[2] = { -150.0f, 150.0f };
            ALint c;

            for(c = 0;c < 2;c++)
            {
                const ALshort *hrtf_left, *hrtf_right;

                GetHrtfCoeffs(0.0, angles[c], &hrtf_left, &hrtf_right);
                for(i = 0;i < HRTF_LENGTH;i++)
                {
                    ALSource->Params.HrtfCoeffs[c][i][0] =
                               hrtf_left[i]*(1.0/32767.0)*DryGain*ListenerGain;
                    ALSource->Params.HrtfCoeffs[c][i][1] =
                              hrtf_right[i]*(1.0/32767.0)*DryGain*ListenerGain;
                }
            }
        }
        else
        {
            static const ALfloat angles[2] = { -150.0f, 150.0f };
            ALint c;

            for(c = 0;c < 2;c++)
            {
                pos = aluCart2LUTpos(cos(angles[c] * (M_PI/180.0)),
                                     sin(angles[c] * (M_PI/180.0)));
                SpeakerGain = &Device->PanningLUT[MAXCHANNELS * pos];

                for(i = 0;i < (ALint)Device->NumChan;i++)
                {
                    Channel chan = Device->Speaker2Chan[i];
                    SrcMatrix[c][chan] = DryGain * ListenerGain *
                                         SpeakerGain[chan];
                }
            }
        }
        break;

    case FmtQuad:
        if((ALContext->Device->Flags&DEVICE_USE_HRTF))
        {
            static const ALfloat angles[4] = { -45.0f, 45.0f, -135.0f, 135.0f };
            ALint c;

            for(c = 0;c < 4;c++)
            {
                const ALshort *hrtf_left, *hrtf_right;

                GetHrtfCoeffs(0.0, angles[c], &hrtf_left, &hrtf_right);
                for(i = 0;i < HRTF_LENGTH;i++)
                {
                    ALSource->Params.HrtfCoeffs[c][i][0] =
                               hrtf_left[i]*(1.0/32767.0)*DryGain*ListenerGain;
                    ALSource->Params.HrtfCoeffs[c][i][1] =
                              hrtf_right[i]*(1.0/32767.0)*DryGain*ListenerGain;
                }
            }
        }
        else
        {
            static const ALfloat angles[4] = { -45.0f, 45.0f, -135.0f, 135.0f };
            ALint c;

            for(c = 0;c < 4;c++)
            {
                pos = aluCart2LUTpos(cos(angles[c] * (M_PI/180.0)),
                                     sin(angles[c] * (M_PI/180.0)));
                SpeakerGain = &Device->PanningLUT[MAXCHANNELS * pos];

                for(i = 0;i < (ALint)Device->NumChan;i++)
                {
                    Channel chan = Device->Speaker2Chan[i];
                    SrcMatrix[c][chan] = DryGain * ListenerGain *
                                         SpeakerGain[chan];
                }
            }
        }
        break;

    case FmtX51:
        if((ALContext->Device->Flags&DEVICE_USE_HRTF))
        {
            static const ALfloat angles[6] = { -30.0f, 30.0f, 0.0f, 0.0f,
                                               -110.0f, 110.0f };
            ALint c;

            for(c = 0;c < 6;c++)
            {
                const ALshort *hrtf_left, *hrtf_right;

                GetHrtfCoeffs(0.0, angles[c], &hrtf_left, &hrtf_right);
                for(i = 0;i < HRTF_LENGTH;i++)
                {
                    ALSource->Params.HrtfCoeffs[c][i][0] =
                               hrtf_left[i]*(1.0/32767.0)*DryGain*ListenerGain;
                    ALSource->Params.HrtfCoeffs[c][i][1] =
                              hrtf_right[i]*(1.0/32767.0)*DryGain*ListenerGain;
                }
            }
        }
        else
        {
            static const ALfloat angles[6] = { -30.0f, 30.0f, 0.0f, 0.0f,
                                               -110.0f, 110.0f };
            ALint c;

            for(c = 0;c < 6;c++)
            {
                if(c == 3) /* Special-case LFE */
                {
                    SrcMatrix[3][LFE] = DryGain * ListenerGain;
                    continue;
                }
                pos = aluCart2LUTpos(cos(angles[c] * (M_PI/180.0)),
                                     sin(angles[c] * (M_PI/180.0)));
                SpeakerGain = &Device->PanningLUT[MAXCHANNELS * pos];

                for(i = 0;i < (ALint)Device->NumChan;i++)
                {
                    Channel chan = Device->Speaker2Chan[i];
                    SrcMatrix[c][chan] = DryGain * ListenerGain *
                                         SpeakerGain[chan];
                }
            }
        }
        break;

    case FmtX61:
        if((ALContext->Device->Flags&DEVICE_USE_HRTF))
        {
            static const ALfloat angles[7] = { -30.0f, 30.0f, 0.0f, 0.0f,
                                               180.0f, -90.0f, 90.0f };
            ALint c;

            for(c = 0;c < 7;c++)
            {
                const ALshort *hrtf_left, *hrtf_right;

                GetHrtfCoeffs(0.0, angles[c], &hrtf_left, &hrtf_right);
                for(i = 0;i < HRTF_LENGTH;i++)
                {
                    ALSource->Params.HrtfCoeffs[c][i][0] =
                               hrtf_left[i]*(1.0/32767.0)*DryGain*ListenerGain;
                    ALSource->Params.HrtfCoeffs[c][i][1] =
                              hrtf_right[i]*(1.0/32767.0)*DryGain*ListenerGain;
                }
            }
        }
        else
        {
            static const ALfloat angles[7] = { -30.0f, 30.0f, 0.0f, 0.0f,
                                               180.0f, -90.0f, 90.0f };
            ALint c;

            for(c = 0;c < 7;c++)
            {
                if(c == 3) /* Special-case LFE */
                {
                    SrcMatrix[3][LFE] = DryGain * ListenerGain;
                    continue;
                }
                pos = aluCart2LUTpos(cos(angles[c] * (M_PI/180.0)),
                                     sin(angles[c] * (M_PI/180.0)));
                SpeakerGain = &Device->PanningLUT[MAXCHANNELS * pos];

                for(i = 0;i < (ALint)Device->NumChan;i++)
                {
                    Channel chan = Device->Speaker2Chan[i];
                    SrcMatrix[c][chan] = DryGain * ListenerGain *
                                         SpeakerGain[chan];
                }
            }
        }
        break;

    case FmtX71:
        if((ALContext->Device->Flags&DEVICE_USE_HRTF))
        {
            static const ALfloat angles[8] = {-30.0f, 30.0f, 0.0f, 0.0f,
                                              -110.0f, 110.0f, -90.0f, 90.0f};
            ALint c;

            for(c = 0;c < 8;c++)
            {
                const ALshort *hrtf_left, *hrtf_right;

                GetHrtfCoeffs(0.0, angles[c], &hrtf_left, &hrtf_right);
                for(i = 0;i < HRTF_LENGTH;i++)
                {
                    ALSource->Params.HrtfCoeffs[c][i][0] =
                               hrtf_left[i]*(1.0/32767.0)*DryGain*ListenerGain;
                    ALSource->Params.HrtfCoeffs[c][i][1] =
                              hrtf_right[i]*(1.0/32767.0)*DryGain*ListenerGain;
                }
            }
        }
        else
        {
            static const ALfloat angles[8] = {-30.0f, 30.0f, 0.0f, 0.0f,
                                              -110.0f, 110.0f, -90.0f, 90.0f};
            ALint c;

            for(c = 0;c < 8;c++)
            {
                if(c == 3) /* Special-case LFE */
                {
                    SrcMatrix[3][LFE] = DryGain * ListenerGain;
                    continue;
                }
                pos = aluCart2LUTpos(cos(angles[c] * (M_PI/180.0)),
                                     sin(angles[c] * (M_PI/180.0)));
                SpeakerGain = &Device->PanningLUT[MAXCHANNELS * pos];

                for(i = 0;i < (ALint)Device->NumChan;i++)
                {
                    Channel chan = Device->Speaker2Chan[i];
                    SrcMatrix[c][chan] = DryGain * ListenerGain *
                                         SpeakerGain[chan];
                }
            }
        }
        break;
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
    ALfloat MinVolume,MaxVolume,MinDist,MaxDist,Rolloff;
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
    InnerAngle   = ALSource->flInnerAngle * ConeScale;
    OuterAngle   = ALSource->flOuterAngle * ConeScale;
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
    Angle = aluAcos(aluDotproduct(Direction,SourceToListener)) * (180.0/M_PI);
    if(Angle >= InnerAngle && Angle <= OuterAngle)
    {
        ALfloat scale = (Angle-InnerAngle) / (OuterAngle-InnerAngle);
        ConeVolume = lerp(1.0, ALSource->flOuterGain, scale);
        ConeHF = lerp(1.0, ALSource->OuterGainHF, scale);
    }
    else if(Angle > OuterAngle)
    {
        ConeVolume = ALSource->flOuterGain;
        ConeHF = ALSource->OuterGainHF;
    }
    else
    {
        ConeVolume = 1.0f;
        ConeHF = 1.0f;
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
                                            (-60.0/20.0));

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

    if((Device->Flags&DEVICE_USE_HRTF))
    {
        const ALshort *hrtf_left, *hrtf_right;

        GetHrtfCoeffs(atan2(Position[1], -Position[2]) * (180.0/M_PI),
                      atan2(Position[0], -Position[2]) * (180.0/M_PI),
                      &hrtf_left, &hrtf_right);
        for(i = 0;i < HRTF_LENGTH;i++)
        {
            ALSource->Params.HrtfCoeffs[0][i][0] = hrtf_left[i]*(1.0/32767.0)*
                                                   DryGain;
            ALSource->Params.HrtfCoeffs[0][i][1] = hrtf_right[i]*(1.0/32767.0)*
                                                   DryGain;
        }
    }
    else
    {
        pos = aluCart2LUTpos(-Position[2], Position[0]);
        SpeakerGain = &Device->PanningLUT[MAXCHANNELS * pos];

        DirGain = aluSqrt(Position[0]*Position[0] + Position[2]*Position[2]);
        // elevation adjustment for directional gain. this sucks, but
        // has low complexity
        AmbientGain = aluSqrt(1.0/Device->NumChan);
        for(s = 0;s < MAXCHANNELS;s++)
        {
            ALuint s2;
            for(s2 = 0;s2 < MAXCHANNELS;s2++)
                ALSource->Params.DryGains[s][s2] = 0.0f;
        }
        for(s = 0;s < (ALsizei)Device->NumChan;s++)
        {
            Channel chan = Device->Speaker2Chan[s];
            ALfloat gain = lerp(AmbientGain, SpeakerGain[chan], DirGain);
            ALSource->Params.DryGains[0][chan] = DryGain * gain;
        }
    }

    /* Update filter coefficients. */
    cw = cos(2.0*M_PI * LOWPASSFREQCUTOFF / Frequency);

    ALSource->Params.iirFilter.coeff = lpCoeffCalc(DryGainHF, cw);
    for(i = 0;i < NumSends;i++)
    {
        ALfloat a = lpCoeffCalc(WetGainHF[i]*WetGainHF[i], cw);
        ALSource->Params.Send[i].iirFilter.coeff = a;
    }
}


static __inline ALfloat aluF2F(ALfloat val)
{
    return val;
}
static __inline ALushort aluF2US(ALfloat val)
{
    if(val > 1.0f) return 65535;
    if(val < -1.0f) return 0;
    return (ALint)(val*32767.0f) + 32768;
}
static __inline ALshort aluF2S(ALfloat val)
{
    if(val > 1.0f) return 32767;
    if(val < -1.0f) return -32768;
    return (ALint)(val*32767.0f);
}
static __inline ALubyte aluF2UB(ALfloat val)
{
    ALushort i = aluF2US(val);
    return i>>8;
}
static __inline ALbyte aluF2B(ALfloat val)
{
    ALshort i = aluF2S(val);
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
static void Write_##T##_##chans(ALCdevice *device, T *RESTRICT buffer,        \
                                ALuint SamplesToDo)                           \
{                                                                             \
    ALfloat (*RESTRICT DryBuffer)[MAXCHANNELS] = device->DryBuffer;           \
    const ALuint *ChanMap = device->DevChannels;                              \
    ALuint i, j;                                                              \
                                                                              \
    for(i = 0;i < SamplesToDo;i++)                                            \
    {                                                                         \
        for(j = 0;j < N;j++)                                                  \
            buffer[ChanMap[chans[j]]] = func(DryBuffer[i][chans[j]]);         \
        buffer += N;                                                          \
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
    const ALuint *ChanMap = device->DevChannels;                              \
    ALuint i, j;                                                              \
                                                                              \
    if(device->Bs2b)                                                          \
    {                                                                         \
        for(i = 0;i < SamplesToDo;i++)                                        \
        {                                                                     \
            float samples[2];                                                 \
            samples[0] = DryBuffer[i][chans[0]];                              \
            samples[1] = DryBuffer[i][chans[1]];                              \
            bs2b_cross_feed(device->Bs2b, samples);                           \
            buffer[ChanMap[chans[0]]]  = func(samples[0]);                    \
            buffer[ChanMap[chans[1]]] = func(samples[1]);                     \
            buffer += 2;                                                      \
        }                                                                     \
    }                                                                         \
    else                                                                      \
    {                                                                         \
        for(i = 0;i < SamplesToDo;i++)                                        \
        {                                                                     \
            for(j = 0;j < N;j++)                                              \
                buffer[ChanMap[chans[j]]] = func(DryBuffer[i][chans[j]]);     \
            buffer += N;                                                      \
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
