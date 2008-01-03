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

// fixes for mingw32.
#if defined(max) && !defined(__max)
#define __max max
#endif
#if defined(min) && !defined(__min)
#define __min min
#endif

#define BUFFERSIZE 48000
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

/* NOTE: The AL_FORMAT_REAR* enums aren't handled here be cause they're
 *       converted to AL_FORMAT_QUAD* when loaded */
__inline ALuint aluBytesFromFormat(ALenum format)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_STEREO8:
        case AL_FORMAT_QUAD8:
        case AL_FORMAT_51CHN8:
        case AL_FORMAT_61CHN8:
        case AL_FORMAT_71CHN8:
            return 1;

        case AL_FORMAT_MONO16:
        case AL_FORMAT_STEREO16:
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

static __inline ALint aluF2L(ALfloat Value)
{
    if(sizeof(ALint) == 4 && sizeof(double) == 8)
    {
        double temp;
        temp = Value + (((65536.0*65536.0*16.0)+(65536.0*65536.0*8.0))*65536.0);
        return *((ALint*)&temp);
    }
    return (ALint)Value;
}

static __inline ALshort aluF2S(ALfloat Value)
{
    ALint i;

    i = aluF2L(Value);
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

    length = (ALfloat)aluSqrt(aluDotproduct(inVector, inVector));
    if(length != 0)
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

static ALvoid CalcSourceParams(ALCcontext *ALContext, ALsource *ALSource,
                               ALenum isMono, ALenum OutputFormat,
                               ALfloat *drysend, ALfloat *wetsend,
                               ALfloat *pitch)
{
    ALfloat ListenerOrientation[6],ListenerPosition[3],ListenerVelocity[3];
    ALfloat InnerAngle,OuterAngle,OuterGain,Angle,Distance,DryMix,WetMix;
    ALfloat Direction[3],Position[3],Velocity[3],SourceToListener[3];
    ALfloat MinVolume,MaxVolume,MinDist,MaxDist,Rolloff;
    ALfloat Pitch,ConeVolume,SourceVolume,PanningFB,PanningLR,ListenerGain;
    ALfloat U[3],V[3],N[3];
    ALfloat DopplerFactor, DopplerVelocity, flSpeedOfSound, flMaxVelocity;
    ALfloat flVSS, flVLS;
    ALint DistanceModel;
    ALfloat Matrix[3][3];
    ALint HeadRelative;
    ALfloat flAttenuation;

    //Get context properties
    DopplerFactor   = ALContext->DopplerFactor;
    DistanceModel   = ALContext->DistanceModel;
    DopplerVelocity = ALContext->DopplerVelocity;
    flSpeedOfSound  = ALContext->flSpeedOfSound;

    //Get listener properties
    ListenerGain = ALContext->Listener.Gain;
    memcpy(ListenerPosition, ALContext->Listener.Position, sizeof(ALContext->Listener.Position));
    memcpy(ListenerVelocity, ALContext->Listener.Velocity, sizeof(ALContext->Listener.Velocity));
    memcpy(&ListenerOrientation[0], ALContext->Listener.Forward, sizeof(ALContext->Listener.Forward));
    memcpy(&ListenerOrientation[3], ALContext->Listener.Up, sizeof(ALContext->Listener.Up));

    //Get source properties
    Pitch        = ALSource->flPitch;
    SourceVolume = ALSource->flGain;
    memcpy(Position,  ALSource->vPosition,    sizeof(ALSource->vPosition));
    memcpy(Velocity,  ALSource->vVelocity,    sizeof(ALSource->vVelocity));
    memcpy(Direction, ALSource->vOrientation, sizeof(ALSource->vOrientation));
    MinVolume    = ALSource->flMinGain;
    MaxVolume    = ALSource->flMaxGain;
    MinDist      = ALSource->flRefDistance;
    MaxDist      = ALSource->flMaxDistance;
    Rolloff      = ALSource->flRollOffFactor;
    OuterGain    = ALSource->flOuterGain;
    InnerAngle   = ALSource->flInnerAngle;
    OuterAngle   = ALSource->flOuterAngle;
    HeadRelative = ALSource->bHeadRelative;

    //Set working variables
    DryMix = (ALfloat)(1.0f);
    WetMix = (ALfloat)(0.0f);

    //Only apply 3D calculations for mono buffers
    if(isMono != AL_FALSE)
    {
        //1. Translate Listener to origin (convert to head relative)
        if(HeadRelative==AL_FALSE)
        {
            Position[0] -= ListenerPosition[0];
            Position[1] -= ListenerPosition[1];
            Position[2] -= ListenerPosition[2];
        }

        //2. Calculate distance attenuation
        Distance = aluSqrt(aluDotproduct(Position, Position));

        flAttenuation = 1.0f;
        switch (DistanceModel)
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
                    flAttenuation = 1.0f - (Rolloff*(Distance-MinDist)/(MaxDist - MinDist));
                break;

            case AL_EXPONENT_DISTANCE_CLAMPED:
                Distance=__max(Distance,MinDist);
                Distance=__min(Distance,MaxDist);
                if (MaxDist < MinDist)
                    break;
                //fall-through
            case AL_EXPONENT_DISTANCE:
                if ((Distance > 0.0f) && (MinDist > 0.0f))
                    flAttenuation = (ALfloat)pow(Distance/MinDist, -Rolloff);
                break;

            case AL_NONE:
            default:
                flAttenuation = 1.0f;
                break;
        }

        // Source Gain + Attenuation
        DryMix = SourceVolume * flAttenuation;

        // Clamp to Min/Max Gain
        DryMix = __min(DryMix,MaxVolume);
        DryMix = __max(DryMix,MinVolume);
        WetMix = __min(WetMix,MaxVolume);
        WetMix = __max(WetMix,MinVolume);
        //3. Apply directional soundcones
        SourceToListener[0] = -Position[0];
        SourceToListener[1] = -Position[1];
        SourceToListener[2] = -Position[2];
        aluNormalize(Direction);
        aluNormalize(SourceToListener);
        Angle = (ALfloat)(180.0*acos(aluDotproduct(Direction,SourceToListener))/3.141592654f);
        if(Angle >= InnerAngle && Angle <= OuterAngle)
            ConeVolume = (1.0f+(OuterGain-1.0f)*(Angle-InnerAngle)/(OuterAngle-InnerAngle));
        else if(Angle > OuterAngle)
            ConeVolume = (1.0f+(OuterGain-1.0f)                                           );
        else
            ConeVolume = 1.0f;

        //4. Calculate Velocity
        if(DopplerFactor != 0.0f)
        {
            flVLS = aluDotproduct(ListenerVelocity, SourceToListener);
            flVSS = aluDotproduct(Velocity, SourceToListener);

            flMaxVelocity = (DopplerVelocity * flSpeedOfSound) / DopplerFactor;

            if (flVSS >= flMaxVelocity)
                flVSS = (flMaxVelocity - 1.0f);
            else if (flVSS <= -flMaxVelocity)
                flVSS = -flMaxVelocity + 1.0f;

            if (flVLS >= flMaxVelocity)
                flVLS = (flMaxVelocity - 1.0f);
            else if (flVLS <= -flMaxVelocity)
                flVLS = -flMaxVelocity + 1.0f;

            pitch[0] = Pitch * ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVLS)) /
                               ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVSS));
        }
        else
            pitch[0] = Pitch;

        //5. Align coordinate system axes
        aluCrossproduct(&ListenerOrientation[0], &ListenerOrientation[3], U); // Right-vector
        aluNormalize(U);                                // Normalized Right-vector
        memcpy(V, &ListenerOrientation[3], sizeof(V));  // Up-vector
        aluNormalize(V);                                // Normalized Up-vector
        memcpy(N, &ListenerOrientation[0], sizeof(N));  // At-vector
        aluNormalize(N);                                // Normalized At-vector
        Matrix[0][0] = U[0]; Matrix[0][1] = V[0]; Matrix[0][2] = -N[0];
        Matrix[1][0] = U[1]; Matrix[1][1] = V[1]; Matrix[1][2] = -N[1];
        Matrix[2][0] = U[2]; Matrix[2][1] = V[2]; Matrix[2][2] = -N[2];
        aluMatrixVector(Position, Matrix);

        //6. Convert normalized position into pannings, then into channel volumes
        aluNormalize(Position);
        switch(aluChannelsFromFormat(OutputFormat))
        {
            case 1:
                drysend[FRONT_LEFT]  = ConeVolume * ListenerGain * DryMix * aluSqrt(1.0f); //Direct
                drysend[FRONT_RIGHT] = ConeVolume * ListenerGain * DryMix * aluSqrt(1.0f); //Direct
                wetsend[FRONT_LEFT]  =              ListenerGain * WetMix * aluSqrt(1.0f); //Room
                wetsend[FRONT_RIGHT] =              ListenerGain * WetMix * aluSqrt(1.0f); //Room
                break;
            case 2:
                PanningLR = 0.5f + 0.5f*Position[0];
                drysend[FRONT_LEFT]  = ConeVolume * ListenerGain * DryMix * aluSqrt(1.0f-PanningLR);
                drysend[FRONT_RIGHT] = ConeVolume * ListenerGain * DryMix * aluSqrt(     PanningLR);
                wetsend[FRONT_LEFT]  =              ListenerGain * WetMix * aluSqrt(1.0f-PanningLR);
                wetsend[FRONT_RIGHT] =              ListenerGain * WetMix * aluSqrt(     PanningLR);
                break;
            case 4:
            /* TODO: Add center/lfe channel in spatial calculations? */
            case 6:
            /* TODO: Special paths for 6.1 and 7.1 output would be nice */
            case 7:
            case 8:
                // Apply a scalar so each individual speaker has more weight
                PanningLR = 0.5f + (0.5f*Position[0]*1.41421356f);
                PanningLR = __min(1.0f, PanningLR);
                PanningLR = __max(0.0f, PanningLR);
                PanningFB = 0.5f + (0.5f*Position[2]*1.41421356f);
                PanningFB = __min(1.0f, PanningFB);
                PanningFB = __max(0.0f, PanningFB);
                drysend[FRONT_LEFT]  = ConeVolume * ListenerGain * DryMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB));
                drysend[FRONT_RIGHT] = ConeVolume * ListenerGain * DryMix * aluSqrt((     PanningLR)*(1.0f-PanningFB));
                drysend[BACK_LEFT]   = ConeVolume * ListenerGain * DryMix * aluSqrt((1.0f-PanningLR)*(     PanningFB));
                drysend[BACK_RIGHT]  = ConeVolume * ListenerGain * DryMix * aluSqrt((     PanningLR)*(     PanningFB));
                drysend[SIDE_LEFT]   = 0.0f;
                drysend[SIDE_RIGHT]  = 0.0f;
                wetsend[FRONT_LEFT]  =              ListenerGain * WetMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB));
                wetsend[FRONT_RIGHT] =              ListenerGain * WetMix * aluSqrt((     PanningLR)*(1.0f-PanningFB));
                wetsend[BACK_LEFT]   =              ListenerGain * WetMix * aluSqrt((1.0f-PanningLR)*(     PanningFB));
                wetsend[BACK_RIGHT]  =              ListenerGain * WetMix * aluSqrt((     PanningLR)*(     PanningFB));
                wetsend[SIDE_LEFT]   = 0.0f;
                wetsend[SIDE_RIGHT]  = 0.0f;
                break;
            default:
                break;
        }
    }
    else
    {
        //1. Multi-channel buffers always play "normal"
        drysend[FRONT_LEFT]  = SourceVolume * 1.0f * ListenerGain;
        drysend[FRONT_RIGHT] = SourceVolume * 1.0f * ListenerGain;
        drysend[SIDE_LEFT]   = SourceVolume * 1.0f * ListenerGain;
        drysend[SIDE_RIGHT]  = SourceVolume * 1.0f * ListenerGain;
        drysend[BACK_LEFT]   = SourceVolume * 1.0f * ListenerGain;
        drysend[BACK_RIGHT]  = SourceVolume * 1.0f * ListenerGain;
        drysend[CENTER]      = SourceVolume * 1.0f * ListenerGain;
        drysend[LFE]         = SourceVolume * 1.0f * ListenerGain;
        wetsend[FRONT_LEFT]  = SourceVolume * 0.0f * ListenerGain;
        wetsend[FRONT_RIGHT] = SourceVolume * 0.0f * ListenerGain;
        wetsend[SIDE_LEFT]   = SourceVolume * 0.0f * ListenerGain;
        wetsend[SIDE_RIGHT]  = SourceVolume * 0.0f * ListenerGain;
        wetsend[BACK_LEFT]   = SourceVolume * 0.0f * ListenerGain;
        wetsend[BACK_RIGHT]  = SourceVolume * 0.0f * ListenerGain;
        wetsend[CENTER]      = SourceVolume * 0.0f * ListenerGain;
        wetsend[LFE]         = SourceVolume * 0.0f * ListenerGain;

        pitch[0] = Pitch;
    }
}

ALvoid aluMixData(ALCcontext *ALContext,ALvoid *buffer,ALsizei size,ALenum format)
{
    static float DryBuffer[BUFFERSIZE][OUTPUTCHANNELS];
    static float WetBuffer[BUFFERSIZE][OUTPUTCHANNELS];
    ALfloat DrySend[OUTPUTCHANNELS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    ALfloat WetSend[OUTPUTCHANNELS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    ALuint BlockAlign,BufferSize;
    ALuint DataSize=0,DataPosInt=0,DataPosFrac=0;
    ALuint Channels,Bits,Frequency,ulExtraSamples;
    ALfloat Pitch;
    ALint Looping,increment,State;
    ALuint Buffer,fraction;
    ALuint SamplesToDo;
    ALsource *ALSource;
    ALbuffer *ALBuffer;
    ALfloat value;
    ALshort *Data;
    ALuint i,j,k;
    ALbufferlistitem *BufferListItem;
    ALuint loop;
    ALint64 DataSize64,DataPos64;

    SuspendContext(ALContext);

    if(buffer)
    {
        //Figure output format variables
        BlockAlign  = aluChannelsFromFormat(format);
        BlockAlign *= aluBytesFromFormat(format);

        size /= BlockAlign;
        while(size > 0)
        {
            //Setup variables
            ALSource = (ALContext ? ALContext->Source : NULL);
            SamplesToDo = min(size, BUFFERSIZE);

            //Clear mixing buffer
            memset(DryBuffer, 0, SamplesToDo*OUTPUTCHANNELS*sizeof(ALfloat));
            memset(WetBuffer, 0, SamplesToDo*OUTPUTCHANNELS*sizeof(ALfloat));

            //Actual mixing loop
            while(ALSource)
            {
                j = 0;
                State = ALSource->state;
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
                        Bits      = aluBytesFromFormat(ALBuffer->format) * 8;
                        Channels  = aluChannelsFromFormat(ALBuffer->format);
                        DataSize  = ALBuffer->size;
                        Frequency = ALBuffer->frequency;

                        CalcSourceParams(ALContext, ALSource,
                                         (Channels==1) ? AL_TRUE : AL_FALSE,
                                         format, DrySend, WetSend, &Pitch);


                        Pitch = (Pitch*Frequency) / ALContext->Frequency;
                        DataSize = DataSize / (Bits*Channels/8);

                        //Get source info
                        DataPosInt = ALSource->position;
                        DataPosFrac = ALSource->position_fraction;

                        //Compute 18.14 fixed point step
                        increment = aluF2L(Pitch*(1L<<FRACTIONBITS));
                        if(increment > (MAX_PITCH<<FRACTIONBITS))
                            increment = (MAX_PITCH<<FRACTIONBITS);

                        //Figure out how many samples we can mix.
                        //Pitch must be <= 4 (the number below !)
                        DataSize64 = DataSize+MAX_PITCH;
                        DataSize64 <<= FRACTIONBITS;
                        DataPos64 = DataPosInt;
                        DataPos64 <<= FRACTIONBITS;
                        DataPos64 += DataPosFrac;
                        BufferSize = (ALuint)((DataSize64-DataPos64) / increment);
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
                                if(BufferListItem->next->buffer &&
                                   ((ALbuffer*)ALTHUNK_LOOKUPENTRY(BufferListItem->next->buffer))->data)
                                {
                                    ulExtraSamples = min(((ALbuffer*)ALTHUNK_LOOKUPENTRY(BufferListItem->next->buffer))->size, (ALint)(16*Channels));
                                    memcpy(&Data[DataSize*Channels], ((ALbuffer*)ALTHUNK_LOOKUPENTRY(BufferListItem->next->buffer))->data, ulExtraSamples);
                                }
                            }
                            else if (ALSource->bLooping)
                            {
                                if (ALSource->queue->buffer)
                                {
                                    if(((ALbuffer*)ALTHUNK_LOOKUPENTRY(ALSource->queue->buffer))->data)
                                    {
                                        ulExtraSamples = min(((ALbuffer*)ALTHUNK_LOOKUPENTRY(ALSource->queue->buffer))->size, (ALint)(16*Channels));
                                        memcpy(&Data[DataSize*Channels], ((ALbuffer*)ALTHUNK_LOOKUPENTRY(ALSource->queue->buffer))->data, ulExtraSamples);
                                    }
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
                                //First order interpolator
                                value = (ALfloat)((ALshort)(((Data[k]*((1L<<FRACTIONBITS)-fraction))+(Data[k+1]*(fraction)))>>FRACTIONBITS));
                                //Direct path final mix buffer and panning
                                DryBuffer[j][FRONT_LEFT]  += value*DrySend[FRONT_LEFT];
                                DryBuffer[j][FRONT_RIGHT] += value*DrySend[FRONT_RIGHT];
#if 0 /* FIXME: Re-enable when proper 6-channel spatialization is used */
                                DryBuffer[j][SIDE_LEFT]   += value*DrySend[SIDE_LEFT];
                                DryBuffer[j][SIDE_RIGHT]  += value*DrySend[SIDE_RIGHT];
#endif
                                DryBuffer[j][BACK_LEFT]   += value*DrySend[BACK_LEFT];
                                DryBuffer[j][BACK_RIGHT]  += value*DrySend[BACK_RIGHT];
                                //Room path final mix buffer and panning
                                WetBuffer[j][FRONT_LEFT]  += value*WetSend[FRONT_LEFT];
                                WetBuffer[j][FRONT_RIGHT] += value*WetSend[FRONT_RIGHT];
#if 0 /* FIXME: Re-enable when proper 6-channel spatialization is used */
                                WetBuffer[j][SIDE_LEFT]   += value*WetSend[SIDE_LEFT];
                                WetBuffer[j][SIDE_RIGHT]  += value*WetSend[SIDE_RIGHT];
#endif
                                WetBuffer[j][BACK_LEFT]   += value*WetSend[BACK_LEFT];
                                WetBuffer[j][BACK_RIGHT]  += value*WetSend[BACK_RIGHT];
                            }
                            else
                            {
                                //First order interpolator (front left)
                                value = (ALfloat)((ALshort)(((Data[k*Channels  ]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels  ]*(fraction)))>>FRACTIONBITS));
                                DryBuffer[j][FRONT_LEFT] += value*DrySend[FRONT_LEFT];
                                WetBuffer[j][FRONT_LEFT] += value*WetSend[FRONT_LEFT];
                                //First order interpolator (front right)
                                value = (ALfloat)((ALshort)(((Data[k*Channels+1]*((1L<<FRACTIONBITS)-fraction))+(Data[(k+1)*Channels+1]*(fraction)))>>FRACTIONBITS));
                                DryBuffer[j][FRONT_RIGHT] += value*DrySend[FRONT_RIGHT];
                                WetBuffer[j][FRONT_RIGHT] += value*WetSend[FRONT_RIGHT];
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
                    if(ALContext->bs2b)
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
                        ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                        ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
                        ((ALubyte*)buffer)[4] = (ALubyte)((aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER])>>8)+128);
                        ((ALubyte*)buffer)[5] = (ALubyte)((aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE])>>8)+128);
                        buffer = ((ALubyte*)buffer) + 6;
                    }
                    break;
                case AL_FORMAT_61CHN8:
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        ((ALubyte*)buffer)[0] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT])>>8)+128);
                        ((ALubyte*)buffer)[1] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT])>>8)+128);
                        ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][SIDE_LEFT]  +WetBuffer[i][SIDE_LEFT])>>8)+128);
                        ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][SIDE_RIGHT] +WetBuffer[i][SIDE_RIGHT])>>8)+128);
                        ((ALubyte*)buffer)[4] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                        ((ALubyte*)buffer)[5] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
                        ((ALubyte*)buffer)[6] = (ALubyte)((aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE])>>8)+128);
                        buffer = ((ALubyte*)buffer) + 7;
                    }
                    break;
                case AL_FORMAT_71CHN8:
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        ((ALubyte*)buffer)[0] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT])>>8)+128);
                        ((ALubyte*)buffer)[1] = (ALubyte)((aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT])>>8)+128);
                        ((ALubyte*)buffer)[2] = (ALubyte)((aluF2S(DryBuffer[i][SIDE_LEFT]  +WetBuffer[i][SIDE_LEFT])>>8)+128);
                        ((ALubyte*)buffer)[3] = (ALubyte)((aluF2S(DryBuffer[i][SIDE_RIGHT] +WetBuffer[i][SIDE_RIGHT])>>8)+128);
                        ((ALubyte*)buffer)[4] = (ALubyte)((aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT])>>8)+128);
                        ((ALubyte*)buffer)[5] = (ALubyte)((aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT])>>8)+128);
                        ((ALubyte*)buffer)[6] = (ALubyte)((aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER])>>8)+128);
                        ((ALubyte*)buffer)[7] = (ALubyte)((aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE])>>8)+128);
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
                    if(ALContext->bs2b)
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
                        ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                        ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
                        ((ALshort*)buffer)[4] = aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER]);
                        ((ALshort*)buffer)[5] = aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE]);
                        buffer = ((ALshort*)buffer) + 6;
                    }
                    break;
                case AL_FORMAT_61CHN16:
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        ((ALshort*)buffer)[0] = aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT]);
                        ((ALshort*)buffer)[1] = aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT]);
                        ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][SIDE_LEFT]  +WetBuffer[i][SIDE_LEFT]);
                        ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][SIDE_RIGHT] +WetBuffer[i][SIDE_RIGHT]);
                        ((ALshort*)buffer)[4] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                        ((ALshort*)buffer)[5] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
                        ((ALshort*)buffer)[6] = aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE]);
                        buffer = ((ALshort*)buffer) + 7;
                    }
                    break;
                case AL_FORMAT_71CHN16:
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        ((ALshort*)buffer)[0] = aluF2S(DryBuffer[i][FRONT_LEFT] +WetBuffer[i][FRONT_LEFT]);
                        ((ALshort*)buffer)[1] = aluF2S(DryBuffer[i][FRONT_RIGHT]+WetBuffer[i][FRONT_RIGHT]);
                        ((ALshort*)buffer)[2] = aluF2S(DryBuffer[i][SIDE_LEFT]  +WetBuffer[i][SIDE_LEFT]);
                        ((ALshort*)buffer)[3] = aluF2S(DryBuffer[i][SIDE_RIGHT] +WetBuffer[i][SIDE_RIGHT]);
                        ((ALshort*)buffer)[4] = aluF2S(DryBuffer[i][BACK_LEFT]  +WetBuffer[i][BACK_LEFT]);
                        ((ALshort*)buffer)[5] = aluF2S(DryBuffer[i][BACK_RIGHT] +WetBuffer[i][BACK_RIGHT]);
                        ((ALshort*)buffer)[6] = aluF2S(DryBuffer[i][CENTER]     +WetBuffer[i][CENTER]);
                        ((ALshort*)buffer)[7] = aluF2S(DryBuffer[i][LFE]        +WetBuffer[i][LFE]);
                        buffer = ((ALshort*)buffer) + 8;
                    }
                    break;

                default:
                    break;
            }

            size -= SamplesToDo;
        }
    }

    ProcessContext(ALContext);
}
