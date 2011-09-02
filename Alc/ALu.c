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
    static const ALfloat angles_Mono[1] = { 0.0f };
    static const ALfloat angles_Stereo[2] = { -30.0f, 30.0f };
    static const ALfloat angles_Rear[2] = { -150.0f, 150.0f };
    static const ALfloat angles_Quad[4] = { -45.0f, 45.0f, -135.0f, 135.0f };
    static const ALfloat angles_X51[6] = { -30.0f, 30.0f, 0.0f, 0.0f,
                                           -110.0f, 110.0f };
    static const ALfloat angles_X61[7] = { -30.0f, 30.0f, 0.0f, 0.0f,
                                           180.0f, -90.0f, 90.0f };
    static const ALfloat angles_X71[8] = { -30.0f, 30.0f, 0.0f, 0.0f,
                                           -110.0f, 110.0f, -90.0f, 90.0f };

    static const enum Channel chans_Mono[1] = { FRONT_CENTER };
    static const enum Channel chans_Stereo[2] = { FRONT_LEFT, FRONT_RIGHT };
    static const enum Channel chans_Rear[2] = { BACK_LEFT, BACK_RIGHT };
    static const enum Channel chans_Quad[4] = { FRONT_LEFT, FRONT_RIGHT,
                                                BACK_LEFT, BACK_RIGHT };
    static const enum Channel chans_X51[6] = { FRONT_LEFT, FRONT_RIGHT,
                                               FRONT_CENTER, LFE,
                                               BACK_LEFT, BACK_RIGHT };
    static const enum Channel chans_X61[7] = { FRONT_LEFT, FRONT_RIGHT,
                                               FRONT_CENTER, LFE, BACK_CENTER,
                                               SIDE_LEFT, SIDE_RIGHT };
    static const enum Channel chans_X71[8] = { FRONT_LEFT, FRONT_RIGHT,
                                               FRONT_CENTER, LFE,
                                               BACK_LEFT, BACK_RIGHT,
                                               SIDE_LEFT, SIDE_RIGHT };

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
    const ALfloat *angles = NULL;
    const enum Channel *chans = NULL;
    enum Resampler Resampler;
    ALint num_channels = 0;
    ALboolean VirtualChannels;
    ALfloat Pitch;
    ALfloat cw;
    ALuint pos;
    ALint i, c;

    /* Get device properties */
    DevChans  = ALContext->Device->FmtChans;
    NumSends  = ALContext->Device->NumAuxSends;
    Frequency = ALContext->Device->Frequency;

    /* Get listener properties */
    ListenerGain = ALContext->Listener.Gain;

    /* Get source properties */
    SourceVolume    = ALSource->flGain;
    MinVolume       = ALSource->flMinGain;
    MaxVolume       = ALSource->flMaxGain;
    Pitch           = ALSource->flPitch;
    Resampler       = ALSource->Resampler;
    VirtualChannels = ALSource->VirtualChannels;

    /* Calculate the stepping value */
    Channels = FmtMono;
    BufferListItem = ALSource->queue;
    while(BufferListItem != NULL)
    {
        ALbuffer *ALBuffer;
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            ALint maxstep = STACK_DATA_SIZE / ALSource->NumChannels /
                                              ALSource->SampleSize;
            maxstep -= ResamplerPadding[Resampler] +
                       ResamplerPrePadding[Resampler] + 1;
            maxstep = mini(maxstep, INT_MAX>>FRACTIONBITS);

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

            if(ALSource->VirtualChannels && (Device->Flags&DEVICE_USE_HRTF))
                ALSource->Params.DoMix = SelectHrtfMixer(ALBuffer,
                       (ALSource->Params.Step==FRACTIONONE) ? POINT_RESAMPLER :
                                                              Resampler);
            else
                ALSource->Params.DoMix = SelectMixer(ALBuffer,
                       (ALSource->Params.Step==FRACTIONONE) ? POINT_RESAMPLER :
                                                              Resampler);
            break;
        }
        BufferListItem = BufferListItem->next;
    }

    /* Calculate gains */
    DryGain  = clampf(SourceVolume, MinVolume, MaxVolume);
    DryGain *= ALSource->DirectGain;
    DryGainHF = ALSource->DirectGainHF;
    for(i = 0;i < NumSends;i++)
    {
        WetGain[i]  = clampf(SourceVolume, MinVolume, MaxVolume);
        WetGain[i] *= ALSource->Send[i].WetGain;
        WetGainHF[i] = ALSource->Send[i].WetGainHF;
    }

    SrcMatrix = ALSource->Params.DryGains;
    for(i = 0;i < MAXCHANNELS;i++)
    {
        for(c = 0;c < MAXCHANNELS;c++)
            SrcMatrix[i][c] = 0.0f;
    }
    switch(Channels)
    {
    case FmtMono:
        angles = angles_Mono;
        chans = chans_Mono;
        num_channels = 1;
        break;
    case FmtStereo:
        if(VirtualChannels && (ALContext->Device->Flags&DEVICE_DUPLICATE_STEREO))
        {
            DryGain *= aluSqrt(2.0f/4.0f);
            for(c = 0;c < 2;c++)
            {
                pos = aluCart2LUTpos(cos(angles_Rear[c] * (M_PI/180.0)),
                                     sin(angles_Rear[c] * (M_PI/180.0)));
                SpeakerGain = Device->PanningLUT[pos];

                for(i = 0;i < (ALint)Device->NumChan;i++)
                {
                    enum Channel chan = Device->Speaker2Chan[i];
                    SrcMatrix[c][chan] += DryGain * ListenerGain *
                                          SpeakerGain[chan];
                }
            }
        }
        angles = angles_Stereo;
        chans = chans_Stereo;
        num_channels = 2;
        break;

    case FmtRear:
        angles = angles_Rear;
        chans = chans_Rear;
        num_channels = 2;
        break;

    case FmtQuad:
        angles = angles_Quad;
        chans = chans_Quad;
        num_channels = 4;
        break;

    case FmtX51:
        angles = angles_X51;
        chans = chans_X51;
        num_channels = 6;
        break;

    case FmtX61:
        angles = angles_X61;
        chans = chans_X61;
        num_channels = 7;
        break;

    case FmtX71:
        angles = angles_X71;
        chans = chans_X71;
        num_channels = 8;
        break;
    }

    if(VirtualChannels == AL_FALSE)
    {
        for(c = 0;c < num_channels;c++)
            SrcMatrix[c][chans[c]] += DryGain * ListenerGain;
    }
    else if((Device->Flags&DEVICE_USE_HRTF))
    {
        for(c = 0;c < num_channels;c++)
        {
            if(chans[c] == LFE)
            {
                /* Skip LFE */
                ALSource->Params.HrtfDelay[c][0] = 0;
                ALSource->Params.HrtfDelay[c][1] = 0;
                for(i = 0;i < HRIR_LENGTH;i++)
                {
                    ALSource->Params.HrtfCoeffs[c][i][0] = 0.0f;
                    ALSource->Params.HrtfCoeffs[c][i][1] = 0.0f;
                }
            }
            else
            {
                /* Get the static HRIR coefficients and delays for this
                 * channel. */
                GetLerpedHrtfCoeffs(0.0, angles[c] * (M_PI/180.0),
                                    DryGain*ListenerGain,
                                    ALSource->Params.HrtfCoeffs[c],
                                    ALSource->Params.HrtfDelay[c]);
            }
            ALSource->HrtfCounter = 0;
        }
    }
    else
    {
        for(c = 0;c < num_channels;c++)
        {
            if(chans[c] == LFE) /* Special-case LFE */
            {
                SrcMatrix[c][LFE] += DryGain * ListenerGain;
                continue;
            }
            pos = aluCart2LUTpos(cos(angles[c] * (M_PI/180.0)),
                                 sin(angles[c] * (M_PI/180.0)));
            SpeakerGain = Device->PanningLUT[pos];

            for(i = 0;i < (ALint)Device->NumChan;i++)
            {
                enum Channel chan = Device->Speaker2Chan[i];
                SrcMatrix[c][chan] += DryGain * ListenerGain *
                                      SpeakerGain[chan];
            }
        }
    }
    for(i = 0;i < NumSends;i++)
    {
        ALSource->Params.Send[i].Slot = ALSource->Send[i].Slot;
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
    ALfloat InnerAngle,OuterAngle,Angle,Distance,ClampedDist;
    ALfloat Direction[3],Position[3],SourceToListener[3];
    ALfloat Velocity[3],ListenerVel[3];
    ALfloat MinVolume,MaxVolume,MinDist,MaxDist,Rolloff;
    ALfloat ConeVolume,ConeHF,SourceVolume,ListenerGain;
    ALfloat DopplerFactor, DopplerVelocity, SpeedOfSound;
    ALfloat AirAbsorptionFactor;
    ALfloat RoomAirAbsorption[MAX_SENDS];
    ALbufferlistitem *BufferListItem;
    ALfloat Attenuation, EffectiveDist;
    ALfloat RoomAttenuation[MAX_SENDS];
    ALfloat MetersPerUnit;
    ALfloat RoomRolloffBase;
    ALfloat RoomRolloff[MAX_SENDS];
    ALfloat DecayDistance[MAX_SENDS];
    ALfloat DryGain;
    ALfloat DryGainHF;
    ALboolean DryGainHFAuto;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALboolean WetGainAuto;
    ALboolean WetGainHFAuto;
    enum Resampler Resampler;
    ALfloat Pitch;
    ALuint Frequency;
    ALint NumSends;
    ALfloat cw;
    ALint i;

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
    MinVolume    = ALSource->flMinGain;
    MaxVolume    = ALSource->flMaxGain;
    Pitch        = ALSource->flPitch;
    Resampler    = ALSource->Resampler;
    memcpy(Position,  ALSource->vPosition,    sizeof(ALSource->vPosition));
    memcpy(Direction, ALSource->vOrientation, sizeof(ALSource->vOrientation));
    memcpy(Velocity,  ALSource->vVelocity,    sizeof(ALSource->vVelocity));
    MinDist = ALSource->flRefDistance;
    MaxDist = ALSource->flMaxDistance;
    Rolloff = ALSource->flRollOffFactor;
    InnerAngle = ALSource->flInnerAngle * ConeScale;
    OuterAngle = ALSource->flOuterAngle * ConeScale;
    AirAbsorptionFactor = ALSource->AirAbsorptionFactor;
    DryGainHFAuto = ALSource->DryGainHFAuto;
    WetGainAuto   = ALSource->WetGainAuto;
    WetGainHFAuto = ALSource->WetGainHFAuto;
    RoomRolloffBase = ALSource->RoomRolloffFactor;
    for(i = 0;i < NumSends;i++)
    {
        ALeffectslot *Slot = ALSource->Send[i].Slot;

        if(!Slot || Slot->effect.type == AL_EFFECT_NULL)
        {
            RoomRolloff[i] = 0.0f;
            DecayDistance[i] = 0.0f;
            RoomAirAbsorption[i] = 1.0f;
        }
        else if(Slot->AuxSendAuto)
        {
            RoomRolloff[i] = RoomRolloffBase;
            if(IsReverbEffect(Slot->effect.type))
            {
                RoomRolloff[i] += Slot->effect.Params.Reverb.RoomRolloffFactor;
                DecayDistance[i] = Slot->effect.Params.Reverb.DecayTime *
                                   SPEEDOFSOUNDMETRESPERSEC;
                RoomAirAbsorption[i] = Slot->effect.Params.Reverb.AirAbsorptionGainHF;
            }
            else
            {
                DecayDistance[i] = 0.0f;
                RoomAirAbsorption[i] = 1.0f;
            }
        }
        else
        {
            /* If the slot's auxiliary send auto is off, the data sent to the
             * effect slot is the same as the dry path, sans filter effects */
            RoomRolloff[i] = Rolloff;
            DecayDistance[i] = 0.0f;
            RoomAirAbsorption[i] = AIRABSORBGAINHF;
        }

        ALSource->Params.Send[i].Slot = Slot;
    }

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
    ClampedDist = Distance;

    Attenuation = 1.0f;
    for(i = 0;i < NumSends;i++)
        RoomAttenuation[i] = 1.0f;
    switch(ALContext->SourceDistanceModel ? ALSource->DistanceModel :
                                            ALContext->DistanceModel)
    {
        case InverseDistanceClamped:
            ClampedDist = clampf(ClampedDist, MinDist, MaxDist);
            if(MaxDist < MinDist)
                break;
            //fall-through
        case InverseDistance:
            if(MinDist > 0.0f)
            {
                if((MinDist + (Rolloff * (ClampedDist - MinDist))) > 0.0f)
                    Attenuation = MinDist / (MinDist + (Rolloff * (ClampedDist - MinDist)));
                for(i = 0;i < NumSends;i++)
                {
                    if((MinDist + (RoomRolloff[i] * (ClampedDist - MinDist))) > 0.0f)
                        RoomAttenuation[i] = MinDist / (MinDist + (RoomRolloff[i] * (ClampedDist - MinDist)));
                }
            }
            break;

        case LinearDistanceClamped:
            ClampedDist = clampf(ClampedDist, MinDist, MaxDist);
            if(MaxDist < MinDist)
                break;
            //fall-through
        case LinearDistance:
            if(MaxDist != MinDist)
            {
                Attenuation = 1.0f - (Rolloff*(ClampedDist-MinDist)/(MaxDist - MinDist));
                Attenuation = maxf(Attenuation, 0.0f);
                for(i = 0;i < NumSends;i++)
                {
                    RoomAttenuation[i] = 1.0f - (RoomRolloff[i]*(ClampedDist-MinDist)/(MaxDist - MinDist));
                    RoomAttenuation[i] = maxf(RoomAttenuation[i], 0.0f);
                }
            }
            break;

        case ExponentDistanceClamped:
            ClampedDist = clampf(ClampedDist, MinDist, MaxDist);
            if(MaxDist < MinDist)
                break;
            //fall-through
        case ExponentDistance:
            if(ClampedDist > 0.0f && MinDist > 0.0f)
            {
                Attenuation = aluPow(ClampedDist/MinDist, -Rolloff);
                for(i = 0;i < NumSends;i++)
                    RoomAttenuation[i] = aluPow(ClampedDist/MinDist, -RoomRolloff[i]);
            }
            break;

        case DisableDistance:
            break;
    }

    // Source Gain + Attenuation
    DryGain = SourceVolume * Attenuation;
    for(i = 0;i < NumSends;i++)
        WetGain[i] = SourceVolume * RoomAttenuation[i];

    // Distance-based air absorption
    EffectiveDist = 0.0f;
    if(MinDist > 0.0f && Attenuation < 1.0f)
        EffectiveDist = (MinDist/Attenuation - MinDist)*MetersPerUnit;
    if(AirAbsorptionFactor > 0.0f && EffectiveDist > 0.0f)
    {
        DryGainHF *= aluPow(AIRABSORBGAINHF, AirAbsorptionFactor*EffectiveDist);
        for(i = 0;i < NumSends;i++)
            WetGainHF[i] *= aluPow(RoomAirAbsorption[i],
                                   AirAbsorptionFactor*EffectiveDist);
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
    if(WetGainAuto)
    {
        for(i = 0;i < NumSends;i++)
            WetGain[i] *= ConeVolume;
    }
    if(DryGainHFAuto)
        DryGainHF *= ConeHF;
    if(WetGainHFAuto)
    {
        for(i = 0;i < NumSends;i++)
            WetGainHF[i] *= ConeHF;
    }

    // Clamp to Min/Max Gain
    DryGain = clampf(DryGain, MinVolume, MaxVolume);
    for(i = 0;i < NumSends;i++)
        WetGain[i] = clampf(WetGain[i], MinVolume, MaxVolume);

    // Apply filter gains and filters
    DryGain   *= ALSource->DirectGain * ListenerGain;
    DryGainHF *= ALSource->DirectGainHF;
    for(i = 0;i < NumSends;i++)
    {
        WetGain[i]   *= ALSource->Send[i].WetGain * ListenerGain;
        WetGainHF[i] *= ALSource->Send[i].WetGainHF;
    }

    if(WetGainAuto)
    {
        /* Apply a decay-time transformation to the wet path, based on the
         * attenuation of the dry path.
         *
         * Using the approximate (effective) source to listener distance, the
         * initial decay of the reverb effect is calculated and applied to the
         * wet path.
         */
        for(i = 0;i < NumSends;i++)
        {
            if(DecayDistance[i] > 0.0f)
                WetGain[i] *= aluPow(0.001f /* -60dB */,
                                     EffectiveDist / DecayDistance[i]);
        }
    }

    // Calculate Velocity
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
            ALint maxstep = STACK_DATA_SIZE / ALSource->NumChannels /
                                              ALSource->SampleSize;
            maxstep -= ResamplerPadding[Resampler] +
                       ResamplerPrePadding[Resampler] + 1;
            maxstep = mini(maxstep, INT_MAX>>FRACTIONBITS);

            Pitch = Pitch * ALBuffer->Frequency / Frequency;
            if(Pitch > (ALfloat)maxstep)
                ALSource->Params.Step = maxstep<<FRACTIONBITS;
            else
            {
                ALSource->Params.Step = Pitch*FRACTIONONE;
                if(ALSource->Params.Step == 0)
                    ALSource->Params.Step = 1;
            }

            if((Device->Flags&DEVICE_USE_HRTF))
                ALSource->Params.DoMix = SelectHrtfMixer(ALBuffer,
                       (ALSource->Params.Step==FRACTIONONE) ? POINT_RESAMPLER :
                                                              Resampler);
            else
                ALSource->Params.DoMix = SelectMixer(ALBuffer,
                       (ALSource->Params.Step==FRACTIONONE) ? POINT_RESAMPLER :
                                                              Resampler);
            break;
        }
        BufferListItem = BufferListItem->next;
    }

    if((Device->Flags&DEVICE_USE_HRTF))
    {
        // Use a binaural HRTF algorithm for stereo headphone playback
        ALfloat delta, ev = 0.0f, az = 0.0f;

        if(Distance > 0.0f)
        {
            ALfloat invlen = 1.0f/Distance;
            Position[0] *= invlen;
            Position[1] *= invlen;
            Position[2] *= invlen;

            // Calculate elevation and azimuth only when the source is not at
            // the listener.  This prevents +0 and -0 Z from producing
            // inconsistent panning.
            ev = asin(Position[1]);
            az = atan2(Position[0], -Position[2]*ZScale);
        }

        // Check to see if the HRIR is already moving.
        if(ALSource->HrtfMoving)
        {
            // Calculate the normalized HRTF transition factor (delta).
            delta = CalcHrtfDelta(ALSource->Params.HrtfGain, DryGain,
                                  ALSource->Params.HrtfDir, Position);
            // If the delta is large enough, get the moving HRIR target
            // coefficients, target delays, steppping values, and counter.
            if(delta > 0.001f)
            {
                ALSource->HrtfCounter = GetMovingHrtfCoeffs(ev, az, DryGain,
                                          delta, ALSource->HrtfCounter,
                                          ALSource->Params.HrtfCoeffs[0],
                                          ALSource->Params.HrtfDelay[0],
                                          ALSource->Params.HrtfCoeffStep,
                                          ALSource->Params.HrtfDelayStep);
                ALSource->Params.HrtfGain = DryGain;
                ALSource->Params.HrtfDir[0] = Position[0];
                ALSource->Params.HrtfDir[1] = Position[1];
                ALSource->Params.HrtfDir[2] = Position[2];
            }
        }
        else
        {
            // Get the initial (static) HRIR coefficients and delays.
            GetLerpedHrtfCoeffs(ev, az, DryGain,
                                ALSource->Params.HrtfCoeffs[0],
                                ALSource->Params.HrtfDelay[0]);
            ALSource->HrtfCounter = 0;
            ALSource->Params.HrtfGain = DryGain;
            ALSource->Params.HrtfDir[0] = Position[0];
            ALSource->Params.HrtfDir[1] = Position[1];
            ALSource->Params.HrtfDir[2] = Position[2];
        }
    }
    else
    {
        // Use energy-preserving panning algorithm for multi-speaker playback
        ALfloat DirGain, AmbientGain;
        const ALfloat *SpeakerGain;
        ALfloat length;
        ALint pos;

        length = maxf(Distance, MinDist);
        if(length > 0.0f)
        {
            ALfloat invlen = 1.0f/length;
            Position[0] *= invlen;
            Position[1] *= invlen;
            Position[2] *= invlen;
        }

        pos = aluCart2LUTpos(-Position[2]*ZScale, Position[0]);
        SpeakerGain = Device->PanningLUT[pos];

        DirGain = aluSqrt(Position[0]*Position[0] + Position[2]*Position[2]);
        // elevation adjustment for directional gain. this sucks, but
        // has low complexity
        AmbientGain = aluSqrt(1.0/Device->NumChan);
        for(i = 0;i < MAXCHANNELS;i++)
        {
            ALuint i2;
            for(i2 = 0;i2 < MAXCHANNELS;i2++)
                ALSource->Params.DryGains[i][i2] = 0.0f;
        }
        for(i = 0;i < (ALint)Device->NumChan;i++)
        {
            enum Channel chan = Device->Speaker2Chan[i];
            ALfloat gain = lerp(AmbientGain, SpeakerGain[chan], DirGain);
            ALSource->Params.DryGains[0][chan] = DryGain * gain;
        }
    }
    for(i = 0;i < NumSends;i++)
        ALSource->Params.Send[i].WetGain = WetGain[i];

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
{ return val; }
static __inline ALshort aluF2S(ALfloat val)
{
    if(val > 1.0f) return 32767;
    if(val < -1.0f) return -32768;
    return (ALint)(val*32767.0f);
}
static __inline ALushort aluF2US(ALfloat val)
{ return aluF2S(val)+32768; }
static __inline ALbyte aluF2B(ALfloat val)
{ return aluF2S(val)>>8; }
static __inline ALubyte aluF2UB(ALfloat val)
{ return aluF2US(val)>>8; }

#define DECL_TEMPLATE(T, N, func)                                             \
static void Write_##T##_##N(ALCdevice *device, T *RESTRICT buffer,            \
                            ALuint SamplesToDo)                               \
{                                                                             \
    ALfloat (*RESTRICT DryBuffer)[MAXCHANNELS] = device->DryBuffer;           \
    const enum Channel *ChanMap = device->DevChannels;                        \
    ALuint i, j;                                                              \
                                                                              \
    for(i = 0;i < SamplesToDo;i++)                                            \
    {                                                                         \
        for(j = 0;j < N;j++)                                                  \
            *(buffer++) = func(DryBuffer[i][ChanMap[j]]);                     \
    }                                                                         \
}

DECL_TEMPLATE(ALfloat, 1, aluF2F)
DECL_TEMPLATE(ALfloat, 4, aluF2F)
DECL_TEMPLATE(ALfloat, 6, aluF2F)
DECL_TEMPLATE(ALfloat, 7, aluF2F)
DECL_TEMPLATE(ALfloat, 8, aluF2F)

DECL_TEMPLATE(ALushort, 1, aluF2US)
DECL_TEMPLATE(ALushort, 4, aluF2US)
DECL_TEMPLATE(ALushort, 6, aluF2US)
DECL_TEMPLATE(ALushort, 7, aluF2US)
DECL_TEMPLATE(ALushort, 8, aluF2US)

DECL_TEMPLATE(ALshort, 1, aluF2S)
DECL_TEMPLATE(ALshort, 4, aluF2S)
DECL_TEMPLATE(ALshort, 6, aluF2S)
DECL_TEMPLATE(ALshort, 7, aluF2S)
DECL_TEMPLATE(ALshort, 8, aluF2S)

DECL_TEMPLATE(ALubyte, 1, aluF2UB)
DECL_TEMPLATE(ALubyte, 4, aluF2UB)
DECL_TEMPLATE(ALubyte, 6, aluF2UB)
DECL_TEMPLATE(ALubyte, 7, aluF2UB)
DECL_TEMPLATE(ALubyte, 8, aluF2UB)

DECL_TEMPLATE(ALbyte, 1, aluF2B)
DECL_TEMPLATE(ALbyte, 4, aluF2B)
DECL_TEMPLATE(ALbyte, 6, aluF2B)
DECL_TEMPLATE(ALbyte, 7, aluF2B)
DECL_TEMPLATE(ALbyte, 8, aluF2B)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T, N, func)                                             \
static void Write_##T##_##N(ALCdevice *device, T *RESTRICT buffer,            \
                            ALuint SamplesToDo)                               \
{                                                                             \
    ALfloat (*RESTRICT DryBuffer)[MAXCHANNELS] = device->DryBuffer;           \
    const enum Channel *ChanMap = device->DevChannels;                        \
    ALuint i, j;                                                              \
                                                                              \
    if(device->Bs2b)                                                          \
    {                                                                         \
        for(i = 0;i < SamplesToDo;i++)                                        \
        {                                                                     \
            float samples[2];                                                 \
            samples[0] = DryBuffer[i][ChanMap[0]];                            \
            samples[1] = DryBuffer[i][ChanMap[1]];                            \
            bs2b_cross_feed(device->Bs2b, samples);                           \
            *(buffer++) = func(samples[0]);                                   \
            *(buffer++) = func(samples[1]);                                   \
        }                                                                     \
    }                                                                         \
    else                                                                      \
    {                                                                         \
        for(i = 0;i < SamplesToDo;i++)                                        \
        {                                                                     \
            for(j = 0;j < N;j++)                                              \
                *(buffer++) = func(DryBuffer[i][ChanMap[j]]);                 \
        }                                                                     \
    }                                                                         \
}

DECL_TEMPLATE(ALfloat, 2, aluF2F)
DECL_TEMPLATE(ALushort, 2, aluF2US)
DECL_TEMPLATE(ALshort, 2, aluF2S)
DECL_TEMPLATE(ALubyte, 2, aluF2UB)
DECL_TEMPLATE(ALbyte, 2, aluF2B)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T)                                                      \
static void Write_##T(ALCdevice *device, T *buffer, ALuint SamplesToDo)       \
{                                                                             \
    switch(device->FmtChans)                                                  \
    {                                                                         \
        case DevFmtMono:                                                      \
            Write_##T##_1(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtStereo:                                                    \
            Write_##T##_2(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtQuad:                                                      \
            Write_##T##_4(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtX51:                                                       \
        case DevFmtX51Side:                                                   \
            Write_##T##_6(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtX61:                                                       \
            Write_##T##_7(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtX71:                                                       \
            Write_##T##_8(device, buffer, SamplesToDo);                       \
            break;                                                            \
    }                                                                         \
}

DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALubyte)
DECL_TEMPLATE(ALbyte)

#undef DECL_TEMPLATE

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size)
{
    ALuint SamplesToDo;
    ALeffectslot **slot, **slot_end;
    ALsource **src, **src_end;
    ALCcontext *ctx;
    int fpuState;
    ALuint i, c;

#if defined(HAVE_FESETROUND)
    fpuState = fegetround();
    fesetround(FE_TOWARDZERO);
#elif defined(HAVE__CONTROLFP)
    fpuState = _controlfp(0, 0);
    (void)_controlfp(_RC_CHOP, _MCW_RC);
#else
    (void)fpuState;
#endif

    while(size > 0)
    {
        /* Setup variables */
        SamplesToDo = minu(size, BUFFERSIZE);

        /* Clear mixing buffer */
        memset(device->DryBuffer, 0, SamplesToDo*MAXCHANNELS*sizeof(ALfloat));

        LockDevice(device);
        ctx = device->ContextList;
        while(ctx)
        {
            ALenum DeferUpdates = ctx->DeferUpdates;
            ALenum UpdateSources = AL_FALSE;

            if(!DeferUpdates)
                UpdateSources = ExchangeInt(&ctx->UpdateSources, AL_FALSE);

            src = ctx->ActiveSources;
            src_end = src + ctx->ActiveSourceCount;
            while(src != src_end)
            {
                if((*src)->state != AL_PLAYING)
                {
                    --(ctx->ActiveSourceCount);
                    *src = *(--src_end);
                    continue;
                }

                if(!DeferUpdates && (ExchangeInt(&(*src)->NeedsUpdate, AL_FALSE) ||
                                     UpdateSources))
                    ALsource_Update(*src, ctx);

                MixSource(*src, device, SamplesToDo);
                src++;
            }

            /* effect slot processing */
            slot = ctx->ActiveEffectSlots;
            slot_end = slot + ctx->ActiveEffectSlotCount;
            while(slot != slot_end)
            {
                for(i = 0;i < SamplesToDo;i++)
                {
                    (*slot)->WetBuffer[i] += (*slot)->ClickRemoval[0];
                    (*slot)->ClickRemoval[0] -= (*slot)->ClickRemoval[0] / 256.0f;
                }
                for(i = 0;i < 1;i++)
                {
                    (*slot)->ClickRemoval[i] += (*slot)->PendingClicks[i];
                    (*slot)->PendingClicks[i] = 0.0f;
                }

                if(!DeferUpdates && ExchangeInt(&(*slot)->NeedsUpdate, AL_FALSE))
                    ALEffect_Update((*slot)->EffectState, ctx, *slot);

                ALEffect_Process((*slot)->EffectState, *slot, SamplesToDo,
                                 (*slot)->WetBuffer, device->DryBuffer);

                for(i = 0;i < SamplesToDo;i++)
                    (*slot)->WetBuffer[i] = 0.0f;

                slot++;
            }

            ctx = ctx->next;
        }
        UnlockDevice(device);

        //Post processing loop
        if(device->FmtChans == DevFmtMono)
        {
            for(i = 0;i < SamplesToDo;i++)
            {
                device->DryBuffer[i][FRONT_CENTER] += device->ClickRemoval[FRONT_CENTER];
                device->ClickRemoval[FRONT_CENTER] -= device->ClickRemoval[FRONT_CENTER] / 256.0f;
            }
            device->ClickRemoval[FRONT_CENTER] += device->PendingClicks[FRONT_CENTER];
            device->PendingClicks[FRONT_CENTER] = 0.0f;
        }
        else if(device->FmtChans == DevFmtStereo)
        {
            /* Assumes the first two channels are FRONT_LEFT and FRONT_RIGHT */
            for(i = 0;i < SamplesToDo;i++)
            {
                for(c = 0;c < 2;c++)
                {
                    device->DryBuffer[i][c] += device->ClickRemoval[c];
                    device->ClickRemoval[c] -= device->ClickRemoval[c] / 256.0f;
                }
            }
            for(c = 0;c < 2;c++)
            {
                device->ClickRemoval[c] += device->PendingClicks[c];
                device->PendingClicks[c] = 0.0f;
            }
        }
        else
        {
            for(i = 0;i < SamplesToDo;i++)
            {
                for(c = 0;c < MAXCHANNELS;c++)
                {
                    device->DryBuffer[i][c] += device->ClickRemoval[c];
                    device->ClickRemoval[c] -= device->ClickRemoval[c] / 256.0f;
                }
            }
            for(c = 0;c < MAXCHANNELS;c++)
            {
                device->ClickRemoval[c] += device->PendingClicks[c];
                device->PendingClicks[c] = 0.0f;
            }
        }

        if(buffer)
        {
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
    ALCcontext *Context;

    LockDevice(device);
    Context = device->ContextList;
    while(Context)
    {
        ALsource *source;
        ALsizei pos;

        LockUIntMapRead(&Context->SourceMap);
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
        UnlockUIntMapRead(&Context->SourceMap);

        Context = Context->next;
    }

    device->Connected = ALC_FALSE;
    UnlockDevice(device);
}
