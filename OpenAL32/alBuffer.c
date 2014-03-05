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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "alMain.h"
#include "alu.h"
#include "alError.h"
#include "alBuffer.h"
#include "alThunk.h"


extern inline struct ALbuffer *LookupBuffer(ALCdevice *device, ALuint id);
extern inline struct ALbuffer *RemoveBuffer(ALCdevice *device, ALuint id);
extern inline ALuint FrameSizeFromUserFmt(enum UserFmtChannels chans, enum UserFmtType type);
extern inline ALuint FrameSizeFromFmt(enum FmtChannels chans, enum FmtType type);

static ALenum LoadData(ALbuffer *ALBuf, ALuint freq, ALenum NewFormat, ALsizei frames, enum UserFmtChannels chans, enum UserFmtType type, const ALvoid *data, ALsizei align, ALboolean storesrc);
static void ConvertData(ALvoid *dst, enum UserFmtType dstType, const ALvoid *src, enum UserFmtType srcType, ALsizei numchans, ALsizei len, ALsizei align);
static ALboolean IsValidType(ALenum type);
static ALboolean IsValidChannels(ALenum channels);
static ALboolean DecomposeUserFormat(ALenum format, enum UserFmtChannels *chans, enum UserFmtType *type);
static ALboolean DecomposeFormat(ALenum format, enum FmtChannels *chans, enum FmtType *type);
static ALboolean SanitizeAlignment(enum UserFmtType type, ALsizei *align);


/*
 * Global Variables
 */

/* IMA ADPCM Stepsize table */
static const int IMAStep_size[89] = {
       7,    8,    9,   10,   11,   12,   13,   14,   16,   17,   19,
      21,   23,   25,   28,   31,   34,   37,   41,   45,   50,   55,
      60,   66,   73,   80,   88,   97,  107,  118,  130,  143,  157,
     173,  190,  209,  230,  253,  279,  307,  337,  371,  408,  449,
     494,  544,  598,  658,  724,  796,  876,  963, 1060, 1166, 1282,
    1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,10442,
   11487,12635,13899,15289,16818,18500,20350,22358,24633,27086,29794,
   32767
};

/* IMA4 ADPCM Codeword decode table */
static const int IMA4Codeword[16] = {
    1, 3, 5, 7, 9, 11, 13, 15,
   -1,-3,-5,-7,-9,-11,-13,-15,
};

/* IMA4 ADPCM Step index adjust decode table */
static const int IMA4Index_adjust[16] = {
   -1,-1,-1,-1, 2, 4, 6, 8,
   -1,-1,-1,-1, 2, 4, 6, 8
};


/* MSADPCM Adaption table */
static const int MSADPCMAdaption[16] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};

/* MSADPCM Adaption Coefficient tables */
static const int MSADPCMAdaptionCoeff[7][2] = {
    { 256,    0 },
    { 512, -256 },
    {   0,    0 },
    { 192,   64 },
    { 240,    0 },
    { 460, -208 },
    { 392, -232 }
};


/* A quick'n'dirty lookup table to decode a muLaw-encoded byte sample into a
 * signed 16-bit sample */
static const ALshort muLawDecompressionTable[256] = {
    -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
    -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
    -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
    -11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
     -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
     -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
     -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
     -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
     -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
     -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
      -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,
      -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
      -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,
      -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
      -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,
       -56,   -48,   -40,   -32,   -24,   -16,    -8,     0,
     32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
     23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
     15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
     11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
      7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
      5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
      3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
      2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
      1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
      1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
       876,   844,   812,   780,   748,   716,   684,   652,
       620,   588,   556,   524,   492,   460,   428,   396,
       372,   356,   340,   324,   308,   292,   276,   260,
       244,   228,   212,   196,   180,   164,   148,   132,
       120,   112,   104,    96,    88,    80,    72,    64,
        56,    48,    40,    32,    24,    16,     8,     0
};

/* Values used when encoding a muLaw sample */
static const int muLawBias = 0x84;
static const int muLawClip = 32635;
static const char muLawCompressTable[256] = {
     0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
     4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
     5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
     5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
     6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
     6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
     6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
     6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};


/* A quick'n'dirty lookup table to decode an aLaw-encoded byte sample into a
 * signed 16-bit sample */
static const ALshort aLawDecompressionTable[256] = {
     -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
     -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
     -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
     -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
    -22016,-20992,-24064,-23040,-17920,-16896,-19968,-18944,
    -30208,-29184,-32256,-31232,-26112,-25088,-28160,-27136,
    -11008,-10496,-12032,-11520, -8960, -8448, -9984, -9472,
    -15104,-14592,-16128,-15616,-13056,-12544,-14080,-13568,
      -344,  -328,  -376,  -360,  -280,  -264,  -312,  -296,
      -472,  -456,  -504,  -488,  -408,  -392,  -440,  -424,
       -88,   -72,  -120,  -104,   -24,    -8,   -56,   -40,
      -216,  -200,  -248,  -232,  -152,  -136,  -184,  -168,
     -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
     -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
      -688,  -656,  -752,  -720,  -560,  -528,  -624,  -592,
      -944,  -912, -1008,  -976,  -816,  -784,  -880,  -848,
      5504,  5248,  6016,  5760,  4480,  4224,  4992,  4736,
      7552,  7296,  8064,  7808,  6528,  6272,  7040,  6784,
      2752,  2624,  3008,  2880,  2240,  2112,  2496,  2368,
      3776,  3648,  4032,  3904,  3264,  3136,  3520,  3392,
     22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
     30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
     11008, 10496, 12032, 11520,  8960,  8448,  9984,  9472,
     15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
       344,   328,   376,   360,   280,   264,   312,   296,
       472,   456,   504,   488,   408,   392,   440,   424,
        88,    72,   120,   104,    24,     8,    56,    40,
       216,   200,   248,   232,   152,   136,   184,   168,
      1376,  1312,  1504,  1440,  1120,  1056,  1248,  1184,
      1888,  1824,  2016,  1952,  1632,  1568,  1760,  1696,
       688,   656,   752,   720,   560,   528,   624,   592,
       944,   912,  1008,   976,   816,   784,   880,   848
};

/* Values used when encoding an aLaw sample */
static const int aLawClip = 32635;
static const char aLawCompressTable[128] = {
    1,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};


AL_API ALvoid AL_APIENTRY alGenBuffers(ALsizei n, ALuint *buffers)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsizei cur = 0;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    for(cur = 0;cur < n;cur++)
    {
        ALbuffer *buffer = calloc(1, sizeof(ALbuffer));
        if(!buffer)
        {
            alDeleteBuffers(cur, buffers);
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, done);
        }
        RWLockInit(&buffer->lock);

        err = NewThunkEntry(&buffer->id);
        if(err == AL_NO_ERROR)
            err = InsertUIntMapEntry(&device->BufferMap, buffer->id, buffer);
        if(err != AL_NO_ERROR)
        {
            FreeThunkEntry(buffer->id);
            memset(buffer, 0, sizeof(ALbuffer));
            free(buffer);

            alDeleteBuffers(cur, buffers);
            SET_ERROR_AND_GOTO(context, err, done);
        }

        buffers[cur] = buffer->id;
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDeleteBuffers(ALsizei n, const ALuint *buffers)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *ALBuf;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    for(i = 0;i < n;i++)
    {
        if(!buffers[i])
            continue;

        /* Check for valid Buffer ID */
        if((ALBuf=LookupBuffer(device, buffers[i])) == NULL)
            SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
        if(ALBuf->ref != 0)
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    for(i = 0;i < n;i++)
    {
        if((ALBuf=RemoveBuffer(device, buffers[i])) == NULL)
            continue;
        FreeThunkEntry(ALBuf->id);

        free(ALBuf->data);

        memset(ALBuf, 0, sizeof(*ALBuf));
        free(ALBuf);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsBuffer(ALuint buffer)
{
    ALCcontext *context;
    ALboolean ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    ret = ((!buffer || LookupBuffer(context->Device, buffer)) ?
           AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(context);

    return ret;
}


AL_API ALvoid AL_APIENTRY alBufferData(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
{
    enum UserFmtChannels srcchannels;
    enum UserFmtType srctype;
    ALCdevice *device;
    ALCcontext *context;
    ALuint framesize;
    ALenum newformat;
    ALbuffer *albuf;
    ALsizei align;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(!(size >= 0 && freq > 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(DecomposeUserFormat(format, &srcchannels, &srctype) == AL_FALSE)
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);

    align = albuf->UnpackAlign;
    if(SanitizeAlignment(srctype, &align) == AL_FALSE)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(srctype)
    {
        case UserFmtByte:
        case UserFmtUByte:
        case UserFmtShort:
        case UserFmtUShort:
        case UserFmtFloat:
            framesize = FrameSizeFromUserFmt(srcchannels, srctype) * align;
            if((size%framesize) != 0)
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

            err = LoadData(albuf, freq, format, size/framesize*align,
                           srcchannels, srctype, data, align, AL_TRUE);
            if(err != AL_NO_ERROR)
                SET_ERROR_AND_GOTO(context, err, done);
            break;

        case UserFmtInt:
        case UserFmtUInt:
        case UserFmtByte3:
        case UserFmtUByte3:
        case UserFmtDouble:
            framesize = FrameSizeFromUserFmt(srcchannels, srctype) * align;
            if((size%framesize) != 0)
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

            newformat = AL_FORMAT_MONO_FLOAT32;
            switch(srcchannels)
            {
                case UserFmtMono: newformat = AL_FORMAT_MONO_FLOAT32; break;
                case UserFmtStereo: newformat = AL_FORMAT_STEREO_FLOAT32; break;
                case UserFmtRear: newformat = AL_FORMAT_REAR32; break;
                case UserFmtQuad: newformat = AL_FORMAT_QUAD32; break;
                case UserFmtX51: newformat = AL_FORMAT_51CHN32; break;
                case UserFmtX61: newformat = AL_FORMAT_61CHN32; break;
                case UserFmtX71: newformat = AL_FORMAT_71CHN32; break;
            }
            err = LoadData(albuf, freq, newformat, size/framesize*align,
                           srcchannels, srctype, data, align, AL_TRUE);
            if(err != AL_NO_ERROR)
                SET_ERROR_AND_GOTO(context, err, done);
            break;

        case UserFmtMulaw:
        case UserFmtAlaw:
            framesize = FrameSizeFromUserFmt(srcchannels, srctype) * align;
            if((size%framesize) != 0)
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

            newformat = AL_FORMAT_MONO16;
            switch(srcchannels)
            {
                case UserFmtMono: newformat = AL_FORMAT_MONO16; break;
                case UserFmtStereo: newformat = AL_FORMAT_STEREO16; break;
                case UserFmtRear: newformat = AL_FORMAT_REAR16; break;
                case UserFmtQuad: newformat = AL_FORMAT_QUAD16; break;
                case UserFmtX51: newformat = AL_FORMAT_51CHN16; break;
                case UserFmtX61: newformat = AL_FORMAT_61CHN16; break;
                case UserFmtX71: newformat = AL_FORMAT_71CHN16; break;
            }
            err = LoadData(albuf, freq, newformat, size/framesize*align,
                           srcchannels, srctype, data, align, AL_TRUE);
            if(err != AL_NO_ERROR)
                SET_ERROR_AND_GOTO(context, err, done);
            break;

        case UserFmtIMA4:
            framesize  = (align-1)/2 + 4;
            framesize *= ChannelsFromUserFmt(srcchannels);
            if((size%framesize) != 0)
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

            newformat = AL_FORMAT_MONO16;
            switch(srcchannels)
            {
                case UserFmtMono: newformat = AL_FORMAT_MONO16; break;
                case UserFmtStereo: newformat = AL_FORMAT_STEREO16; break;
                case UserFmtRear: newformat = AL_FORMAT_REAR16; break;
                case UserFmtQuad: newformat = AL_FORMAT_QUAD16; break;
                case UserFmtX51: newformat = AL_FORMAT_51CHN16; break;
                case UserFmtX61: newformat = AL_FORMAT_61CHN16; break;
                case UserFmtX71: newformat = AL_FORMAT_71CHN16; break;
            }
            err = LoadData(albuf, freq, newformat, size/framesize*align,
                           srcchannels, srctype, data, align, AL_TRUE);
            if(err != AL_NO_ERROR)
                SET_ERROR_AND_GOTO(context, err, done);
            break;

        case UserFmtMSADPCM:
            framesize  = (align-2)/2 + 7;
            framesize *= ChannelsFromUserFmt(srcchannels);
            if((size%framesize) != 0)
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

            newformat = AL_FORMAT_MONO16;
            switch(srcchannels)
            {
                case UserFmtMono: newformat = AL_FORMAT_MONO16; break;
                case UserFmtStereo: newformat = AL_FORMAT_STEREO16; break;
                case UserFmtRear: newformat = AL_FORMAT_REAR16; break;
                case UserFmtQuad: newformat = AL_FORMAT_QUAD16; break;
                case UserFmtX51: newformat = AL_FORMAT_51CHN16; break;
                case UserFmtX61: newformat = AL_FORMAT_61CHN16; break;
                case UserFmtX71: newformat = AL_FORMAT_71CHN16; break;
            }
            err = LoadData(albuf, freq, newformat, size/framesize*align,
                           srcchannels, srctype, data, align, AL_TRUE);
            if(err != AL_NO_ERROR)
                SET_ERROR_AND_GOTO(context, err, done);
            break;
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alBufferSubDataSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length)
{
    enum UserFmtChannels srcchannels;
    enum UserFmtType srctype;
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;
    ALuint byte_align;
    ALuint channels;
    ALuint bytes;
    ALsizei align;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(!(length >= 0 && offset >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(DecomposeUserFormat(format, &srcchannels, &srctype) == AL_FALSE)
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);

    WriteLock(&albuf->lock);
    align = albuf->UnpackAlign;
    if(SanitizeAlignment(srctype, &align) == AL_FALSE)
    {
        WriteUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    }
    if(srcchannels != albuf->OriginalChannels || srctype != albuf->OriginalType)
    {
        WriteUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }
    if(align != albuf->OriginalAlign)
    {
        WriteUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

    if(albuf->OriginalType == UserFmtIMA4)
    {
        byte_align  = (albuf->OriginalAlign-1)/2 + 4;
        byte_align *= ChannelsFromUserFmt(albuf->OriginalChannels);
    }
    else if(albuf->OriginalType == UserFmtMSADPCM)
    {
        byte_align  = (albuf->OriginalAlign-2)/2 + 7;
        byte_align *= ChannelsFromUserFmt(albuf->OriginalChannels);
    }
    else
    {
        byte_align  = albuf->OriginalAlign;
        byte_align *= FrameSizeFromUserFmt(albuf->OriginalChannels,
                                           albuf->OriginalType);
    }

    if(offset > albuf->OriginalSize || length > albuf->OriginalSize-offset ||
       (offset%byte_align) != 0 || (length%byte_align) != 0)
    {
        WriteUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    }

    channels = ChannelsFromFmt(albuf->FmtChannels);
    bytes = BytesFromFmt(albuf->FmtType);
    /* offset -> byte offset, length -> sample count */
    offset = offset/byte_align * channels*bytes;
    length = length/byte_align * albuf->OriginalAlign;

    ConvertData((char*)albuf->data+offset, (enum UserFmtType)albuf->FmtType,
                data, srctype, channels, length, align);
    WriteUnlock(&albuf->lock);

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBufferSamplesSOFT(ALuint buffer,
  ALuint samplerate, ALenum internalformat, ALsizei samples,
  ALenum channels, ALenum type, const ALvoid *data)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;
    ALsizei align;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(!(samples >= 0 && samplerate != 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(IsValidType(type) == AL_FALSE || IsValidChannels(channels) == AL_FALSE)
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);

    align = albuf->UnpackAlign;
    if(SanitizeAlignment(type, &align) == AL_FALSE)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if((samples%align) != 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    err = LoadData(albuf, samplerate, internalformat, samples,
                   channels, type, data, align, AL_FALSE);
    if(err != AL_NO_ERROR)
        SET_ERROR_AND_GOTO(context, err, done);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alBufferSubSamplesSOFT(ALuint buffer,
  ALsizei offset, ALsizei samples,
  ALenum channels, ALenum type, const ALvoid *data)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;
    ALsizei align;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(!(samples >= 0 && offset >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(IsValidType(type) == AL_FALSE)
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);

    WriteLock(&albuf->lock);
    align = albuf->UnpackAlign;
    if(SanitizeAlignment(type, &align) == AL_FALSE)
    {
        WriteUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    }
    if(channels != (ALenum)albuf->FmtChannels)
    {
        WriteUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }
    if(offset > albuf->SampleLen || samples > albuf->SampleLen-offset)
    {
        WriteUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    }
    if((samples%align) != 0)
    {
        WriteUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    }

    /* offset -> byte offset */
    offset *= FrameSizeFromFmt(albuf->FmtChannels, albuf->FmtType);
    ConvertData((char*)albuf->data+offset, (enum UserFmtType)albuf->FmtType,
                data, type, ChannelsFromFmt(albuf->FmtChannels), samples, align);
    WriteUnlock(&albuf->lock);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alGetBufferSamplesSOFT(ALuint buffer,
  ALsizei offset, ALsizei samples,
  ALenum channels, ALenum type, ALvoid *data)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;
    ALsizei align;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(!(samples >= 0 && offset >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(IsValidType(type) == AL_FALSE)
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);

    ReadLock(&albuf->lock);
    align = albuf->PackAlign;
    if(SanitizeAlignment(type, &align) == AL_FALSE)
    {
        ReadUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    }
    if(channels != (ALenum)albuf->FmtChannels)
    {
        ReadUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }
    if(offset > albuf->SampleLen || samples > albuf->SampleLen-offset)
    {
        ReadUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    }
    if((samples%align) != 0)
    {
        ReadUnlock(&albuf->lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    }

    /* offset -> byte offset */
    offset *= FrameSizeFromFmt(albuf->FmtChannels, albuf->FmtType);
    ConvertData(data, type, (char*)albuf->data+offset, (enum UserFmtType)albuf->FmtType,
                ChannelsFromFmt(albuf->FmtChannels), samples, align);
    ReadUnlock(&albuf->lock);

done:
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsBufferFormatSupportedSOFT(ALenum format)
{
    enum FmtChannels dstchannels;
    enum FmtType dsttype;
    ALCcontext *context;
    ALboolean ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    ret = DecomposeFormat(format, &dstchannels, &dsttype);

    ALCcontext_DecRef(context);

    return ret;
}


AL_API void AL_APIENTRY alBufferf(ALuint buffer, ALenum param, ALfloat UNUSED(value))
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(LookupBuffer(device, buffer) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBuffer3f(ALuint buffer, ALenum param, ALfloat UNUSED(value1), ALfloat UNUSED(value2), ALfloat UNUSED(value3))
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(LookupBuffer(device, buffer) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBufferfv(ALuint buffer, ALenum param, const ALfloat *values)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(LookupBuffer(device, buffer) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBufferi(ALuint buffer, ALenum param, ALint value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    switch(param)
    {
    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        if(!(value >= 0))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
        ExchangeInt(&albuf->UnpackAlign, value);
        break;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        if(!(value >= 0))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
        ExchangeInt(&albuf->PackAlign, value);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBuffer3i(ALuint buffer, ALenum param, ALint UNUSED(value1), ALint UNUSED(value2), ALint UNUSED(value3))
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(LookupBuffer(device, buffer) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBufferiv(ALuint buffer, ALenum param, const ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    if(values)
    {
        switch(param)
        {
            case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
            case AL_PACK_BLOCK_ALIGNMENT_SOFT:
                alBufferi(buffer, param, values[0]);
                return;
        }
    }

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        WriteLock(&albuf->lock);
        if(albuf->ref != 0)
        {
            WriteUnlock(&albuf->lock);
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
        }
        if(values[0] >= values[1] || values[0] < 0 ||
           values[1] > albuf->SampleLen)
        {
            WriteUnlock(&albuf->lock);
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
        }

        albuf->LoopStart = values[0];
        albuf->LoopEnd = values[1];
        WriteUnlock(&albuf->lock);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alGetBufferf(ALuint buffer, ALenum param, ALfloat *value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    if(!(value))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    case AL_SEC_LENGTH_SOFT:
        ReadLock(&albuf->lock);
        if(albuf->SampleLen != 0)
            *value = albuf->SampleLen / (ALfloat)albuf->Frequency;
        else
            *value = 0.0f;
        ReadUnlock(&albuf->lock);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetBuffer3f(ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(LookupBuffer(device, buffer) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    if(!(value1 && value2 && value3))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetBufferfv(ALuint buffer, ALenum param, ALfloat *values)
{
    ALCdevice *device;
    ALCcontext *context;

    switch(param)
    {
    case AL_SEC_LENGTH_SOFT:
        alGetBufferf(buffer, param, values);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(LookupBuffer(device, buffer) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alGetBufferi(ALuint buffer, ALenum param, ALint *value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    if(!(value))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    case AL_FREQUENCY:
        *value = albuf->Frequency;
        break;

    case AL_BITS:
        *value = BytesFromFmt(albuf->FmtType) * 8;
        break;

    case AL_CHANNELS:
        *value = ChannelsFromFmt(albuf->FmtChannels);
        break;

    case AL_SIZE:
        ReadLock(&albuf->lock);
        *value = albuf->SampleLen * FrameSizeFromFmt(albuf->FmtChannels,
                                                     albuf->FmtType);
        ReadUnlock(&albuf->lock);
        break;

    case AL_INTERNAL_FORMAT_SOFT:
        *value = albuf->Format;
        break;

    case AL_BYTE_LENGTH_SOFT:
        *value = albuf->OriginalSize;
        break;

    case AL_SAMPLE_LENGTH_SOFT:
        *value = albuf->SampleLen;
        break;

    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        *value = albuf->UnpackAlign;
        break;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        *value = albuf->PackAlign;
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetBuffer3i(ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(LookupBuffer(device, buffer) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    if(!(value1 && value2 && value3))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetBufferiv(ALuint buffer, ALenum param, ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer   *albuf;

    switch(param)
    {
    case AL_FREQUENCY:
    case AL_BITS:
    case AL_CHANNELS:
    case AL_SIZE:
    case AL_INTERNAL_FORMAT_SOFT:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        alGetBufferi(buffer, param, values);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        ReadLock(&albuf->lock);
        values[0] = albuf->LoopStart;
        values[1] = albuf->LoopEnd;
        ReadUnlock(&albuf->lock);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


typedef ALubyte ALmulaw;
typedef ALubyte ALalaw;
typedef ALubyte ALima4;
typedef ALubyte ALmsadpcm;
typedef struct {
    ALbyte b[3];
} ALbyte3;
extern ALbyte ALbyte3_size_is_not_3[(sizeof(ALbyte3)==sizeof(ALbyte[3]))?1:-1];
typedef struct {
    ALubyte b[3];
} ALubyte3;
extern ALbyte ALubyte3_size_is_not_3[(sizeof(ALubyte3)==sizeof(ALubyte[3]))?1:-1];

static inline ALshort DecodeMuLaw(ALmulaw val)
{ return muLawDecompressionTable[val]; }

static ALmulaw EncodeMuLaw(ALshort val)
{
    ALint mant, exp, sign;

    sign = (val>>8) & 0x80;
    if(sign)
    {
        /* -32768 doesn't properly negate on a short; it results in itself.
         * So clamp to -32767 */
        val = maxi(val, -32767);
        val = -val;
    }

    val = mini(val, muLawClip);
    val += muLawBias;

    exp = muLawCompressTable[(val>>7) & 0xff];
    mant = (val >> (exp+3)) & 0x0f;

    return ~(sign | (exp<<4) | mant);
}

static inline ALshort DecodeALaw(ALalaw val)
{ return aLawDecompressionTable[val]; }

static ALalaw EncodeALaw(ALshort val)
{
    ALint mant, exp, sign;

    sign = ((~val) >> 8) & 0x80;
    if(!sign)
    {
        val = maxi(val, -32767);
        val = -val;
    }
    val = mini(val, aLawClip);

    if(val >= 256)
    {
        exp = aLawCompressTable[(val>>8) & 0x7f];
        mant = (val >> (exp+3)) & 0x0f;
    }
    else
    {
        exp = 0;
        mant = val >> 4;
    }

    return ((exp<<4) | mant) ^ (sign^0x55);
}

static void DecodeIMA4Block(ALshort *dst, const ALima4 *src, ALint numchans, ALsizei align)
{
    ALint sample[MAX_INPUT_CHANNELS], index[MAX_INPUT_CHANNELS];
    ALuint code[MAX_INPUT_CHANNELS];
    ALsizei j,k,c;

    for(c = 0;c < numchans;c++)
    {
        sample[c]  = *(src++);
        sample[c] |= *(src++) << 8;
        sample[c]  = (sample[c]^0x8000) - 32768;
        index[c]  = *(src++);
        index[c] |= *(src++) << 8;
        index[c]  = (index[c]^0x8000) - 32768;

        index[c] = clampi(index[c], 0, 88);

        dst[c] = sample[c];
    }

    for(j = 1;j < align;j += 8)
    {
        for(c = 0;c < numchans;c++)
        {
            code[c]  = *(src++);
            code[c] |= *(src++) << 8;
            code[c] |= *(src++) << 16;
            code[c] |= *(src++) << 24;
        }

        for(k = 0;k < 8;k++)
        {
            for(c = 0;c < numchans;c++)
            {
                int nibble = code[c]&0xf;
                code[c] >>= 4;

                sample[c] += IMA4Codeword[nibble] * IMAStep_size[index[c]] / 8;
                sample[c] = clampi(sample[c], -32768, 32767);

                index[c] += IMA4Index_adjust[nibble];
                index[c] = clampi(index[c], 0, 88);

                dst[(j+k)*numchans + c] = sample[c];
            }
        }
    }
}

static void EncodeIMA4Block(ALima4 *dst, const ALshort *src, ALint *sample, ALint *index, ALint numchans, ALsizei align)
{
    ALsizei j,k,c;

    for(c = 0;c < numchans;c++)
    {
        int diff = src[c] - sample[c];
        int step = IMAStep_size[index[c]];
        int nibble;

        nibble = 0;
        if(diff < 0)
        {
            nibble = 0x8;
            diff = -diff;
        }

        diff = mini(step*2, diff);
        nibble |= (diff*8/step - 1) / 2;

        sample[c] += IMA4Codeword[nibble] * step / 8;
        sample[c] = clampi(sample[c], -32768, 32767);

        index[c] += IMA4Index_adjust[nibble];
        index[c] = clampi(index[c], 0, 88);

        *(dst++) = sample[c] & 0xff;
        *(dst++) = (sample[c]>>8) & 0xff;
        *(dst++) = index[c] & 0xff;
        *(dst++) = (index[c]>>8) & 0xff;
    }

    for(j = 1;j < align;j += 8)
    {
        for(c = 0;c < numchans;c++)
        {
            for(k = 0;k < 8;k++)
            {
                int diff = src[(j+k)*numchans + c] - sample[c];
                int step = IMAStep_size[index[c]];
                int nibble;

                nibble = 0;
                if(diff < 0)
                {
                    nibble = 0x8;
                    diff = -diff;
                }

                diff = mini(step*2, diff);
                nibble |= (diff*8/step - 1) / 2;

                sample[c] += IMA4Codeword[nibble] * step / 8;
                sample[c] = clampi(sample[c], -32768, 32767);

                index[c] += IMA4Index_adjust[nibble];
                index[c] = clampi(index[c], 0, 88);

                if(!(k&1)) *dst = nibble;
                else *(dst++) |= nibble<<4;
            }
        }
    }
}


static void DecodeMSADPCMBlock(ALshort *dst, const ALmsadpcm *src, ALint numchans, ALsizei align)
{
    ALubyte blockpred[MAX_INPUT_CHANNELS];
    ALint delta[MAX_INPUT_CHANNELS];
    ALshort samples[MAX_INPUT_CHANNELS][2];
    ALint i, j;

    for(i = 0;i < numchans;i++)
    {
        blockpred[i] = *(src++);
        blockpred[i] = minu(blockpred[i], 6);
    }
    for(i = 0;i < numchans;i++)
    {
        delta[i]  = *(src++);
        delta[i] |= *(src++) << 8;
        delta[i]  = (delta[i]^0x8000) - 0x8000;
    }
    for(i = 0;i < numchans;i++)
    {
        samples[i][0]  = *(src++);
        samples[i][0] |= *(src++) << 8;
        samples[i][0]  = (samples[i][0]^0x8000) - 0x8000;
    }
    for(i = 0;i < numchans;i++)
    {
        samples[i][1]  = *(src++);
        samples[i][1] |= *(src++) << 8;
        samples[i][1]  = (samples[i][1]^0x8000) - 0x8000;
    }

    /* Second sample is written first. */
    for(i = 0;i < numchans;i++)
        *(dst++) = samples[i][1];
    for(i = 0;i < numchans;i++)
        *(dst++) = samples[i][0];

    for(j = 2;j < align;j++)
    {
        for(i = 0;i < numchans;i++)
        {
            const ALint num = (j*numchans) + i;
            ALint nibble, pred;

            /* Read the nibble and sign-expand it. */
            if(!(num&1))
                nibble = (*src>>4)&0x0f;
            else
                nibble = (*(src++))&0x0f;
            nibble = (nibble^0x08) - 0x08;

            pred  = (samples[i][0]*MSADPCMAdaptionCoeff[blockpred[i]][0] +
                     samples[i][1]*MSADPCMAdaptionCoeff[blockpred[i]][1]) / 256;
            pred += nibble * delta[i];
            pred  = clampi(pred, -32768, 32767);

            samples[i][1] = samples[i][0];
            samples[i][0] = pred;

            delta[i] = (MSADPCMAdaption[nibble&0x0f] * delta[i]) / 256;
            delta[i] = maxi(16, delta[i]);

            *(dst++) = pred;
        }
    }
}

/* NOTE: This encoder is pretty dumb/simplistic. Some kind of pre-processing
 * that tries to find the optimal block predictors would be nice, at least. A
 * multi-pass method that can generate better deltas would be good, too. */
static void EncodeMSADPCMBlock(ALmsadpcm *dst, const ALshort *src, ALint *sample, ALint numchans, ALsizei align)
{
    ALubyte blockpred[MAX_INPUT_CHANNELS];
    ALint delta[MAX_INPUT_CHANNELS];
    ALshort samples[MAX_INPUT_CHANNELS][2];
    ALint i, j;

    /* Block predictor */
    for(i = 0;i < numchans;i++)
    {
        /* FIXME: Calculate something better. */
        blockpred[i] = 0;
        *(dst++) = blockpred[i];
    }
    /* Initial delta */
    for(i = 0;i < numchans;i++)
    {
        delta[i] = 16;
        *(dst++) = (delta[i]   ) & 0xff;
        *(dst++) = (delta[i]>>8) & 0xff;
    }
    /* Initial sample 1 */
    for(i = 0;i < numchans;i++)
    {
        samples[i][0] = src[1*numchans + i];
        *(dst++) = (samples[i][0]   ) & 0xff;
        *(dst++) = (samples[i][0]>>8) & 0xff;
    }
    /* Initial sample 2 */
    for(i = 0;i < numchans;i++)
    {
        samples[i][1] = src[i];
        *(dst++) = (samples[i][1]   ) & 0xff;
        *(dst++) = (samples[i][1]>>8) & 0xff;
    }

    for(j = 2;j < align;j++)
    {
        for(i = 0;i < numchans;i++)
        {
            const ALint num = (j*numchans) + i;
            ALint nibble = 0;
            ALint bias;

            sample[i] = (samples[i][0]*MSADPCMAdaptionCoeff[blockpred[i]][0] +
                         samples[i][1]*MSADPCMAdaptionCoeff[blockpred[i]][1]) / 256;

            nibble = src[num] - sample[i];
            if(nibble >= 0)
                bias = delta[i] / 2;
            else
                bias = -delta[i] / 2;

            nibble = (nibble + bias) / delta[i];
            nibble = clampi(nibble, -8, 7)&0x0f;

            sample[i] += ((nibble^0x08)-0x08) * delta[i];
            sample[i]  = clampi(sample[i], -32768, 32767);

            samples[i][1] = samples[i][0];
            samples[i][0] = sample[i];

            delta[i] = (MSADPCMAdaption[nibble] * delta[i]) / 256;
            delta[i] = maxi(16, delta[i]);

            if(!(num&1))
                *dst = nibble << 4;
            else
            {
                *dst |= nibble;
                dst++;
            }
        }
    }
}


static inline ALint DecodeByte3(ALbyte3 val)
{
    if(IS_LITTLE_ENDIAN)
        return (val.b[2]<<16) | (((ALubyte)val.b[1])<<8) | ((ALubyte)val.b[0]);
    return (val.b[0]<<16) | (((ALubyte)val.b[1])<<8) | ((ALubyte)val.b[2]);
}

static inline ALbyte3 EncodeByte3(ALint val)
{
    if(IS_LITTLE_ENDIAN)
    {
        ALbyte3 ret = {{ val, val>>8, val>>16 }};
        return ret;
    }
    else
    {
        ALbyte3 ret = {{ val>>16, val>>8, val }};
        return ret;
    }
}

static inline ALint DecodeUByte3(ALubyte3 val)
{
    if(IS_LITTLE_ENDIAN)
        return (val.b[2]<<16) | (val.b[1]<<8) | (val.b[0]);
    return (val.b[0]<<16) | (val.b[1]<<8) | val.b[2];
}

static inline ALubyte3 EncodeUByte3(ALint val)
{
    if(IS_LITTLE_ENDIAN)
    {
        ALubyte3 ret = {{ val, val>>8, val>>16 }};
        return ret;
    }
    else
    {
        ALubyte3 ret = {{ val>>16, val>>8, val }};
        return ret;
    }
}


static inline ALbyte Conv_ALbyte_ALbyte(ALbyte val)
{ return val; }
static inline ALbyte Conv_ALbyte_ALubyte(ALubyte val)
{ return val-128; }
static inline ALbyte Conv_ALbyte_ALshort(ALshort val)
{ return val>>8; }
static inline ALbyte Conv_ALbyte_ALushort(ALushort val)
{ return (val>>8)-128; }
static inline ALbyte Conv_ALbyte_ALint(ALint val)
{ return val>>24; }
static inline ALbyte Conv_ALbyte_ALuint(ALuint val)
{ return (val>>24)-128; }
static inline ALbyte Conv_ALbyte_ALfloat(ALfloat val)
{
    if(val > 1.0f) return 127;
    if(val < -1.0f) return -128;
    return (ALint)(val * 127.0f);
}
static inline ALbyte Conv_ALbyte_ALdouble(ALdouble val)
{
    if(val > 1.0) return 127;
    if(val < -1.0) return -128;
    return (ALint)(val * 127.0);
}
static inline ALbyte Conv_ALbyte_ALmulaw(ALmulaw val)
{ return Conv_ALbyte_ALshort(DecodeMuLaw(val)); }
static inline ALbyte Conv_ALbyte_ALalaw(ALalaw val)
{ return Conv_ALbyte_ALshort(DecodeALaw(val)); }
static inline ALbyte Conv_ALbyte_ALbyte3(ALbyte3 val)
{ return DecodeByte3(val)>>16; }
static inline ALbyte Conv_ALbyte_ALubyte3(ALubyte3 val)
{ return (DecodeUByte3(val)>>16)-128; }

static inline ALubyte Conv_ALubyte_ALbyte(ALbyte val)
{ return val+128; }
static inline ALubyte Conv_ALubyte_ALubyte(ALubyte val)
{ return val; }
static inline ALubyte Conv_ALubyte_ALshort(ALshort val)
{ return (val>>8)+128; }
static inline ALubyte Conv_ALubyte_ALushort(ALushort val)
{ return val>>8; }
static inline ALubyte Conv_ALubyte_ALint(ALint val)
{ return (val>>24)+128; }
static inline ALubyte Conv_ALubyte_ALuint(ALuint val)
{ return val>>24; }
static inline ALubyte Conv_ALubyte_ALfloat(ALfloat val)
{
    if(val > 1.0f) return 255;
    if(val < -1.0f) return 0;
    return (ALint)(val * 127.0f) + 128;
}
static inline ALubyte Conv_ALubyte_ALdouble(ALdouble val)
{
    if(val > 1.0) return 255;
    if(val < -1.0) return 0;
    return (ALint)(val * 127.0) + 128;
}
static inline ALubyte Conv_ALubyte_ALmulaw(ALmulaw val)
{ return Conv_ALubyte_ALshort(DecodeMuLaw(val)); }
static inline ALubyte Conv_ALubyte_ALalaw(ALalaw val)
{ return Conv_ALubyte_ALshort(DecodeALaw(val)); }
static inline ALubyte Conv_ALubyte_ALbyte3(ALbyte3 val)
{ return (DecodeByte3(val)>>16)+128; }
static inline ALubyte Conv_ALubyte_ALubyte3(ALubyte3 val)
{ return DecodeUByte3(val)>>16; }

static inline ALshort Conv_ALshort_ALbyte(ALbyte val)
{ return val<<8; }
static inline ALshort Conv_ALshort_ALubyte(ALubyte val)
{ return (val-128)<<8; }
static inline ALshort Conv_ALshort_ALshort(ALshort val)
{ return val; }
static inline ALshort Conv_ALshort_ALushort(ALushort val)
{ return val-32768; }
static inline ALshort Conv_ALshort_ALint(ALint val)
{ return val>>16; }
static inline ALshort Conv_ALshort_ALuint(ALuint val)
{ return (val>>16)-32768; }
static inline ALshort Conv_ALshort_ALfloat(ALfloat val)
{
    if(val > 1.0f) return 32767;
    if(val < -1.0f) return -32768;
    return (ALint)(val * 32767.0f);
}
static inline ALshort Conv_ALshort_ALdouble(ALdouble val)
{
    if(val > 1.0) return 32767;
    if(val < -1.0) return -32768;
    return (ALint)(val * 32767.0);
}
static inline ALshort Conv_ALshort_ALmulaw(ALmulaw val)
{ return Conv_ALshort_ALshort(DecodeMuLaw(val)); }
static inline ALshort Conv_ALshort_ALalaw(ALalaw val)
{ return Conv_ALshort_ALshort(DecodeALaw(val)); }
static inline ALshort Conv_ALshort_ALbyte3(ALbyte3 val)
{ return DecodeByte3(val)>>8; }
static inline ALshort Conv_ALshort_ALubyte3(ALubyte3 val)
{ return (DecodeUByte3(val)>>8)-32768; }

static inline ALushort Conv_ALushort_ALbyte(ALbyte val)
{ return (val+128)<<8; }
static inline ALushort Conv_ALushort_ALubyte(ALubyte val)
{ return val<<8; }
static inline ALushort Conv_ALushort_ALshort(ALshort val)
{ return val+32768; }
static inline ALushort Conv_ALushort_ALushort(ALushort val)
{ return val; }
static inline ALushort Conv_ALushort_ALint(ALint val)
{ return (val>>16)+32768; }
static inline ALushort Conv_ALushort_ALuint(ALuint val)
{ return val>>16; }
static inline ALushort Conv_ALushort_ALfloat(ALfloat val)
{
    if(val > 1.0f) return 65535;
    if(val < -1.0f) return 0;
    return (ALint)(val * 32767.0f) + 32768;
}
static inline ALushort Conv_ALushort_ALdouble(ALdouble val)
{
    if(val > 1.0) return 65535;
    if(val < -1.0) return 0;
    return (ALint)(val * 32767.0) + 32768;
}
static inline ALushort Conv_ALushort_ALmulaw(ALmulaw val)
{ return Conv_ALushort_ALshort(DecodeMuLaw(val)); }
static inline ALushort Conv_ALushort_ALalaw(ALalaw val)
{ return Conv_ALushort_ALshort(DecodeALaw(val)); }
static inline ALushort Conv_ALushort_ALbyte3(ALbyte3 val)
{ return (DecodeByte3(val)>>8)+32768; }
static inline ALushort Conv_ALushort_ALubyte3(ALubyte3 val)
{ return DecodeUByte3(val)>>8; }

static inline ALint Conv_ALint_ALbyte(ALbyte val)
{ return val<<24; }
static inline ALint Conv_ALint_ALubyte(ALubyte val)
{ return (val-128)<<24; }
static inline ALint Conv_ALint_ALshort(ALshort val)
{ return val<<16; }
static inline ALint Conv_ALint_ALushort(ALushort val)
{ return (val-32768)<<16; }
static inline ALint Conv_ALint_ALint(ALint val)
{ return val; }
static inline ALint Conv_ALint_ALuint(ALuint val)
{ return val-2147483648u; }
static inline ALint Conv_ALint_ALfloat(ALfloat val)
{
    if(val > 1.0f) return 2147483647;
    if(val < -1.0f) return -2147483647-1;
    return (ALint)(val*16777215.0f) << 7;
}
static inline ALint Conv_ALint_ALdouble(ALdouble val)
{
    if(val > 1.0) return 2147483647;
    if(val < -1.0) return -2147483647-1;
    return (ALint)(val * 2147483647.0);
}
static inline ALint Conv_ALint_ALmulaw(ALmulaw val)
{ return Conv_ALint_ALshort(DecodeMuLaw(val)); }
static inline ALint Conv_ALint_ALalaw(ALalaw val)
{ return Conv_ALint_ALshort(DecodeALaw(val)); }
static inline ALint Conv_ALint_ALbyte3(ALbyte3 val)
{ return DecodeByte3(val)<<8; }
static inline ALint Conv_ALint_ALubyte3(ALubyte3 val)
{ return (DecodeUByte3(val)-8388608)<<8; }

static inline ALuint Conv_ALuint_ALbyte(ALbyte val)
{ return (val+128)<<24; }
static inline ALuint Conv_ALuint_ALubyte(ALubyte val)
{ return val<<24; }
static inline ALuint Conv_ALuint_ALshort(ALshort val)
{ return (val+32768)<<16; }
static inline ALuint Conv_ALuint_ALushort(ALushort val)
{ return val<<16; }
static inline ALuint Conv_ALuint_ALint(ALint val)
{ return val+2147483648u; }
static inline ALuint Conv_ALuint_ALuint(ALuint val)
{ return val; }
static inline ALuint Conv_ALuint_ALfloat(ALfloat val)
{
    if(val > 1.0f) return 4294967295u;
    if(val < -1.0f) return 0;
    return ((ALint)(val*16777215.0f)<<7) + 2147483648u;
}
static inline ALuint Conv_ALuint_ALdouble(ALdouble val)
{
    if(val > 1.0) return 4294967295u;
    if(val < -1.0) return 0;
    return (ALint)(val * 2147483647.0) + 2147483648u;
}
static inline ALuint Conv_ALuint_ALmulaw(ALmulaw val)
{ return Conv_ALuint_ALshort(DecodeMuLaw(val)); }
static inline ALuint Conv_ALuint_ALalaw(ALalaw val)
{ return Conv_ALuint_ALshort(DecodeALaw(val)); }
static inline ALuint Conv_ALuint_ALbyte3(ALbyte3 val)
{ return (DecodeByte3(val)+8388608)<<8; }
static inline ALuint Conv_ALuint_ALubyte3(ALubyte3 val)
{ return DecodeUByte3(val)<<8; }

static inline ALfloat Conv_ALfloat_ALbyte(ALbyte val)
{ return val * (1.0f/127.0f); }
static inline ALfloat Conv_ALfloat_ALubyte(ALubyte val)
{ return (val-128) * (1.0f/127.0f); }
static inline ALfloat Conv_ALfloat_ALshort(ALshort val)
{ return val * (1.0f/32767.0f); }
static inline ALfloat Conv_ALfloat_ALushort(ALushort val)
{ return (val-32768) * (1.0f/32767.0f); }
static inline ALfloat Conv_ALfloat_ALint(ALint val)
{ return (ALfloat)(val * (1.0/2147483647.0)); }
static inline ALfloat Conv_ALfloat_ALuint(ALuint val)
{ return (ALfloat)((ALint)(val-2147483648u) * (1.0/2147483647.0)); }
static inline ALfloat Conv_ALfloat_ALfloat(ALfloat val)
{ return (val==val) ? val : 0.0f; }
static inline ALfloat Conv_ALfloat_ALdouble(ALdouble val)
{ return (val==val) ? (ALfloat)val : 0.0f; }
static inline ALfloat Conv_ALfloat_ALmulaw(ALmulaw val)
{ return Conv_ALfloat_ALshort(DecodeMuLaw(val)); }
static inline ALfloat Conv_ALfloat_ALalaw(ALalaw val)
{ return Conv_ALfloat_ALshort(DecodeALaw(val)); }
static inline ALfloat Conv_ALfloat_ALbyte3(ALbyte3 val)
{ return (ALfloat)(DecodeByte3(val) * (1.0/8388607.0)); }
static inline ALfloat Conv_ALfloat_ALubyte3(ALubyte3 val)
{ return (ALfloat)((DecodeUByte3(val)-8388608) * (1.0/8388607.0)); }

static inline ALdouble Conv_ALdouble_ALbyte(ALbyte val)
{ return val * (1.0/127.0); }
static inline ALdouble Conv_ALdouble_ALubyte(ALubyte val)
{ return (val-128) * (1.0/127.0); }
static inline ALdouble Conv_ALdouble_ALshort(ALshort val)
{ return val * (1.0/32767.0); }
static inline ALdouble Conv_ALdouble_ALushort(ALushort val)
{ return (val-32768) * (1.0/32767.0); }
static inline ALdouble Conv_ALdouble_ALint(ALint val)
{ return val * (1.0/2147483647.0); }
static inline ALdouble Conv_ALdouble_ALuint(ALuint val)
{ return (ALint)(val-2147483648u) * (1.0/2147483647.0); }
static inline ALdouble Conv_ALdouble_ALfloat(ALfloat val)
{ return (val==val) ? val : 0.0f; }
static inline ALdouble Conv_ALdouble_ALdouble(ALdouble val)
{ return (val==val) ? val : 0.0; }
static inline ALdouble Conv_ALdouble_ALmulaw(ALmulaw val)
{ return Conv_ALdouble_ALshort(DecodeMuLaw(val)); }
static inline ALdouble Conv_ALdouble_ALalaw(ALalaw val)
{ return Conv_ALdouble_ALshort(DecodeALaw(val)); }
static inline ALdouble Conv_ALdouble_ALbyte3(ALbyte3 val)
{ return DecodeByte3(val) * (1.0/8388607.0); }
static inline ALdouble Conv_ALdouble_ALubyte3(ALubyte3 val)
{ return (DecodeUByte3(val)-8388608) * (1.0/8388607.0); }

#define DECL_TEMPLATE(T)                                                      \
static inline ALmulaw Conv_ALmulaw_##T(T val)                                 \
{ return EncodeMuLaw(Conv_ALshort_##T(val)); }

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)
static inline ALmulaw Conv_ALmulaw_ALmulaw(ALmulaw val)
{ return val; }
DECL_TEMPLATE(ALalaw)
DECL_TEMPLATE(ALbyte3)
DECL_TEMPLATE(ALubyte3)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T)                                                      \
static inline ALalaw Conv_ALalaw_##T(T val)                                   \
{ return EncodeALaw(Conv_ALshort_##T(val)); }

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)
DECL_TEMPLATE(ALmulaw)
static inline ALalaw Conv_ALalaw_ALalaw(ALalaw val)
{ return val; }
DECL_TEMPLATE(ALbyte3)
DECL_TEMPLATE(ALubyte3)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T)                                                      \
static inline ALbyte3 Conv_ALbyte3_##T(T val)                                 \
{ return EncodeByte3(Conv_ALint_##T(val)>>8); }

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)
DECL_TEMPLATE(ALmulaw)
DECL_TEMPLATE(ALalaw)
static inline ALbyte3 Conv_ALbyte3_ALbyte3(ALbyte3 val)
{ return val; }
DECL_TEMPLATE(ALubyte3)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T)                                                      \
static inline ALubyte3 Conv_ALubyte3_##T(T val)                               \
{ return EncodeUByte3(Conv_ALuint_##T(val)>>8); }

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)
DECL_TEMPLATE(ALmulaw)
DECL_TEMPLATE(ALalaw)
DECL_TEMPLATE(ALbyte3)
static inline ALubyte3 Conv_ALubyte3_ALubyte3(ALubyte3 val)
{ return val; }

#undef DECL_TEMPLATE


#define DECL_TEMPLATE(T1, T2)                                                 \
static void Convert_##T1##_##T2(T1 *dst, const T2 *src, ALuint numchans,      \
                                ALuint len, ALsizei UNUSED(align))            \
{                                                                             \
    ALuint i, j;                                                              \
    for(i = 0;i < len;i++)                                                    \
    {                                                                         \
        for(j = 0;j < numchans;j++)                                           \
            *(dst++) = Conv_##T1##_##T2(*(src++));                            \
    }                                                                         \
}

#define DECL_TEMPLATE2(T)  \
DECL_TEMPLATE(T, ALbyte)   \
DECL_TEMPLATE(T, ALubyte)  \
DECL_TEMPLATE(T, ALshort)  \
DECL_TEMPLATE(T, ALushort) \
DECL_TEMPLATE(T, ALint)    \
DECL_TEMPLATE(T, ALuint)   \
DECL_TEMPLATE(T, ALfloat)  \
DECL_TEMPLATE(T, ALdouble) \
DECL_TEMPLATE(T, ALmulaw)  \
DECL_TEMPLATE(T, ALalaw)   \
DECL_TEMPLATE(T, ALbyte3)  \
DECL_TEMPLATE(T, ALubyte3)

DECL_TEMPLATE2(ALbyte)
DECL_TEMPLATE2(ALubyte)
DECL_TEMPLATE2(ALshort)
DECL_TEMPLATE2(ALushort)
DECL_TEMPLATE2(ALint)
DECL_TEMPLATE2(ALuint)
DECL_TEMPLATE2(ALfloat)
DECL_TEMPLATE2(ALdouble)
DECL_TEMPLATE2(ALmulaw)
DECL_TEMPLATE2(ALalaw)
DECL_TEMPLATE2(ALbyte3)
DECL_TEMPLATE2(ALubyte3)

#undef DECL_TEMPLATE2
#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T)                                                      \
static void Convert_##T##_ALima4(T *dst, const ALima4 *src, ALuint numchans,  \
                                 ALuint len, ALuint align)                    \
{                                                                             \
    ALsizei byte_align = ((align-1)/2 + 4) * numchans;                        \
    ALuint i, j, k;                                                           \
    ALshort *tmp;                                                             \
                                                                              \
    tmp = alloca(align*numchans*sizeof(*tmp));                                \
    for(i = 0;i < len;i += align)                                             \
    {                                                                         \
        DecodeIMA4Block(tmp, src, numchans, align);                           \
        src += byte_align;                                                    \
                                                                              \
        for(j = 0;j < align;j++)                                              \
        {                                                                     \
            for(k = 0;k < numchans;k++)                                       \
                *(dst++) = Conv_##T##_ALshort(tmp[j*numchans + k]);           \
        }                                                                     \
    }                                                                         \
}

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
static void Convert_ALshort_ALima4(ALshort *dst, const ALima4 *src, ALuint numchans,
                                   ALuint len, ALuint align)
{
    ALsizei byte_align = ((align-1)/2 + 4) * numchans;
    ALuint i;

    for(i = 0;i < len;i += align)
    {
        DecodeIMA4Block(dst, src, numchans, align);
        src += byte_align;
        dst += align*numchans;
    }
}
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)
DECL_TEMPLATE(ALmulaw)
DECL_TEMPLATE(ALalaw)
DECL_TEMPLATE(ALbyte3)
DECL_TEMPLATE(ALubyte3)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T)                                                      \
static void Convert_ALima4_##T(ALima4 *dst, const T *src, ALuint numchans,    \
                               ALuint len, ALuint align)                      \
{                                                                             \
    ALint sample[MaxChannels] = {0,0,0,0,0,0,0,0};                            \
    ALint index[MaxChannels] = {0,0,0,0,0,0,0,0};                             \
    ALsizei byte_align = ((align-1)/2 + 4) * numchans;                        \
    ALuint i, j, k;                                                           \
    ALshort *tmp;                                                             \
                                                                              \
    tmp = alloca(align*numchans*sizeof(*tmp));                                \
    for(i = 0;i < len;i += align)                                             \
    {                                                                         \
        for(j = 0;j < align;j++)                                              \
        {                                                                     \
            for(k = 0;k < numchans;k++)                                       \
                tmp[j*numchans + k] = Conv_ALshort_##T(*(src++));             \
        }                                                                     \
        EncodeIMA4Block(dst, tmp, sample, index, numchans, align);            \
        dst += byte_align;                                                    \
    }                                                                         \
}

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
static void Convert_ALima4_ALshort(ALima4 *dst, const ALshort *src,
                                   ALuint numchans, ALuint len, ALuint align)
{
    ALint sample[MaxChannels] = {0,0,0,0,0,0,0,0};
    ALint index[MaxChannels] = {0,0,0,0,0,0,0,0};
    ALsizei byte_align = ((align-1)/2 + 4) * numchans;
    ALuint i;

    for(i = 0;i < len;i += align)
    {
        EncodeIMA4Block(dst, src, sample, index, numchans, align);
        src += align*numchans;
        dst += byte_align;
    }
}
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)
DECL_TEMPLATE(ALmulaw)
DECL_TEMPLATE(ALalaw)
DECL_TEMPLATE(ALbyte3)
DECL_TEMPLATE(ALubyte3)

#undef DECL_TEMPLATE


#define DECL_TEMPLATE(T)                                                      \
static void Convert_##T##_ALmsadpcm(T *dst, const ALmsadpcm *src,             \
                                    ALuint numchans, ALuint len,              \
                                    ALuint align)                             \
{                                                                             \
    ALsizei byte_align = ((align-2)/2 + 7) * numchans;                        \
    ALuint i, j, k;                                                           \
    ALshort *tmp;                                                             \
                                                                              \
    tmp = alloca(align*numchans*sizeof(*tmp));                                \
    for(i = 0;i < len;i += align)                                             \
    {                                                                         \
        DecodeMSADPCMBlock(tmp, src, numchans, align);                        \
        src += byte_align;                                                    \
                                                                              \
        for(j = 0;j < align;j++)                                              \
        {                                                                     \
            for(k = 0;k < numchans;k++)                                       \
                *(dst++) = Conv_##T##_ALshort(tmp[j*numchans + k]);           \
        }                                                                     \
    }                                                                         \
}

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
static void Convert_ALshort_ALmsadpcm(ALshort *dst, const ALmsadpcm *src,
                                      ALuint numchans, ALuint len,
                                      ALuint align)
{
    ALsizei byte_align = ((align-2)/2 + 7) * numchans;
    ALuint i;

    for(i = 0;i < len;i += align)
    {
        DecodeMSADPCMBlock(dst, src, numchans, align);
        src += byte_align;
        dst += align*numchans;
    }
}
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)
DECL_TEMPLATE(ALmulaw)
DECL_TEMPLATE(ALalaw)
DECL_TEMPLATE(ALbyte3)
DECL_TEMPLATE(ALubyte3)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T)                                                      \
static void Convert_ALmsadpcm_##T(ALmsadpcm *dst, const T *src,               \
                                  ALuint numchans, ALuint len, ALuint align)  \
{                                                                             \
    ALint sample[MaxChannels] = {0,0,0,0,0,0,0,0};                            \
    ALsizei byte_align = ((align-2)/2 + 7) * numchans;                        \
    ALuint i, j, k;                                                           \
    ALshort *tmp;                                                             \
                                                                              \
    tmp = alloca(align*numchans*sizeof(*tmp));                                \
    for(i = 0;i < len;i += align)                                             \
    {                                                                         \
        for(j = 0;j < align;j++)                                              \
        {                                                                     \
            for(k = 0;k < numchans;k++)                                       \
                tmp[j*numchans + k] = Conv_ALshort_##T(*(src++));             \
        }                                                                     \
        EncodeMSADPCMBlock(dst, tmp, sample, numchans, align);                \
        dst += byte_align;                                                    \
    }                                                                         \
}

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
static void Convert_ALmsadpcm_ALshort(ALmsadpcm *dst, const ALshort *src,
                                      ALuint numchans, ALuint len, ALuint align)
{
    ALint sample[MaxChannels] = {0,0,0,0,0,0,0,0};
    ALsizei byte_align = ((align-2)/2 + 7) * numchans;
    ALuint i;

    for(i = 0;i < len;i += align)
    {
        EncodeMSADPCMBlock(dst, src, sample, numchans, align);
        src += align*numchans;
        dst += byte_align;
    }
}
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)
DECL_TEMPLATE(ALmulaw)
DECL_TEMPLATE(ALalaw)
DECL_TEMPLATE(ALbyte3)
DECL_TEMPLATE(ALubyte3)

#undef DECL_TEMPLATE

/* NOTE: We don't store compressed samples internally, so these conversions
 * should never happen. */
static void Convert_ALima4_ALima4(ALima4* UNUSED(dst), const ALima4* UNUSED(src),
                                  ALuint UNUSED(numchans), ALuint UNUSED(len),
                                  ALuint UNUSED(align))
{
    ERR("Unexpected IMA4-to-IMA4 conversion!\n");
}

static void Convert_ALmsadpcm_ALmsadpcm(ALmsadpcm* UNUSED(dst), const ALmsadpcm* UNUSED(src),
                                        ALuint UNUSED(numchans), ALuint UNUSED(len),
                                        ALuint UNUSED(align))
{
    ERR("Unexpected MSADPCM-to-MSADPCM conversion!\n");
}

static void Convert_ALmsadpcm_ALima4(ALmsadpcm* UNUSED(dst), const ALima4* UNUSED(src),
                                     ALuint UNUSED(numchans), ALuint UNUSED(len),
                                     ALuint UNUSED(align))
{
    ERR("Unexpected IMA4-to-MSADPCM conversion!\n");
}

static void Convert_ALima4_ALmsadpcm(ALima4* UNUSED(dst), const ALmsadpcm* UNUSED(src),
                                     ALuint UNUSED(numchans), ALuint UNUSED(len),
                                     ALuint UNUSED(align))
{
    ERR("Unexpected MSADPCM-to-IMA4 conversion!\n");
}


#define DECL_TEMPLATE(T)                                                      \
static void Convert_##T(T *dst, const ALvoid *src, enum UserFmtType srcType,  \
                        ALsizei numchans, ALsizei len, ALsizei align)         \
{                                                                             \
    switch(srcType)                                                           \
    {                                                                         \
        case UserFmtByte:                                                     \
            Convert_##T##_ALbyte(dst, src, numchans, len, align);             \
            break;                                                            \
        case UserFmtUByte:                                                    \
            Convert_##T##_ALubyte(dst, src, numchans, len, align);            \
            break;                                                            \
        case UserFmtShort:                                                    \
            Convert_##T##_ALshort(dst, src, numchans, len, align);            \
            break;                                                            \
        case UserFmtUShort:                                                   \
            Convert_##T##_ALushort(dst, src, numchans, len, align);           \
            break;                                                            \
        case UserFmtInt:                                                      \
            Convert_##T##_ALint(dst, src, numchans, len, align);              \
            break;                                                            \
        case UserFmtUInt:                                                     \
            Convert_##T##_ALuint(dst, src, numchans, len, align);             \
            break;                                                            \
        case UserFmtFloat:                                                    \
            Convert_##T##_ALfloat(dst, src, numchans, len, align);            \
            break;                                                            \
        case UserFmtDouble:                                                   \
            Convert_##T##_ALdouble(dst, src, numchans, len, align);           \
            break;                                                            \
        case UserFmtMulaw:                                                    \
            Convert_##T##_ALmulaw(dst, src, numchans, len, align);            \
            break;                                                            \
        case UserFmtAlaw:                                                     \
            Convert_##T##_ALalaw(dst, src, numchans, len, align);             \
            break;                                                            \
        case UserFmtIMA4:                                                     \
            Convert_##T##_ALima4(dst, src, numchans, len, align);             \
            break;                                                            \
        case UserFmtMSADPCM:                                                  \
            Convert_##T##_ALmsadpcm(dst, src, numchans, len, align);          \
            break;                                                            \
        case UserFmtByte3:                                                    \
            Convert_##T##_ALbyte3(dst, src, numchans, len, align);            \
            break;                                                            \
        case UserFmtUByte3:                                                   \
            Convert_##T##_ALubyte3(dst, src, numchans, len, align);           \
            break;                                                            \
    }                                                                         \
}

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)
DECL_TEMPLATE(ALmulaw)
DECL_TEMPLATE(ALalaw)
DECL_TEMPLATE(ALima4)
DECL_TEMPLATE(ALmsadpcm)
DECL_TEMPLATE(ALbyte3)
DECL_TEMPLATE(ALubyte3)

#undef DECL_TEMPLATE


static void ConvertData(ALvoid *dst, enum UserFmtType dstType, const ALvoid *src, enum UserFmtType srcType, ALsizei numchans, ALsizei len, ALsizei align)
{
    switch(dstType)
    {
        case UserFmtByte:
            Convert_ALbyte(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtUByte:
            Convert_ALubyte(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtShort:
            Convert_ALshort(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtUShort:
            Convert_ALushort(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtInt:
            Convert_ALint(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtUInt:
            Convert_ALuint(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtFloat:
            Convert_ALfloat(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtDouble:
            Convert_ALdouble(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtMulaw:
            Convert_ALmulaw(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtAlaw:
            Convert_ALalaw(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtIMA4:
            Convert_ALima4(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtMSADPCM:
            Convert_ALmsadpcm(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtByte3:
            Convert_ALbyte3(dst, src, srcType, numchans, len, align);
            break;
        case UserFmtUByte3:
            Convert_ALubyte3(dst, src, srcType, numchans, len, align);
            break;
    }
}


/*
 * LoadData
 *
 * Loads the specified data into the buffer, using the specified formats.
 * Currently, the new format must have the same channel configuration as the
 * original format.
 */
static ALenum LoadData(ALbuffer *ALBuf, ALuint freq, ALenum NewFormat, ALsizei frames, enum UserFmtChannels SrcChannels, enum UserFmtType SrcType, const ALvoid *data, ALsizei align, ALboolean storesrc)
{
    ALuint NewChannels, NewBytes;
    enum FmtChannels DstChannels;
    enum FmtType DstType;
    ALuint64 newsize;
    ALvoid *temp;

    if(DecomposeFormat(NewFormat, &DstChannels, &DstType) == AL_FALSE ||
       (long)SrcChannels != (long)DstChannels)
        return AL_INVALID_ENUM;

    NewChannels = ChannelsFromFmt(DstChannels);
    NewBytes = BytesFromFmt(DstType);

    newsize = frames;
    newsize *= NewBytes;
    newsize *= NewChannels;
    if(newsize > INT_MAX)
        return AL_OUT_OF_MEMORY;

    WriteLock(&ALBuf->lock);
    if(ALBuf->ref != 0)
    {
        WriteUnlock(&ALBuf->lock);
        return AL_INVALID_OPERATION;
    }

    temp = realloc(ALBuf->data, (size_t)newsize);
    if(!temp && newsize)
    {
        WriteUnlock(&ALBuf->lock);
        return AL_OUT_OF_MEMORY;
    }
    ALBuf->data = temp;

    if(data != NULL)
        ConvertData(ALBuf->data, (enum UserFmtType)DstType, data, SrcType, NewChannels, frames, align);

    if(storesrc)
    {
        ALBuf->OriginalChannels = SrcChannels;
        ALBuf->OriginalType     = SrcType;
        if(SrcType == UserFmtIMA4)
        {
            ALsizei byte_align = ((align-1)/2 + 4) * ChannelsFromUserFmt(SrcChannels);
            ALBuf->OriginalSize  = frames / align * byte_align;
            ALBuf->OriginalAlign = align;
        }
        else if(SrcType == UserFmtMSADPCM)
        {
            ALsizei byte_align = ((align-2)/2 + 7) * ChannelsFromUserFmt(SrcChannels);
            ALBuf->OriginalSize  = frames / align * byte_align;
            ALBuf->OriginalAlign = align;
        }
        else
        {
            ALBuf->OriginalSize  = frames * FrameSizeFromUserFmt(SrcChannels, SrcType);
            ALBuf->OriginalAlign = 1;
        }
    }
    else
    {
        ALBuf->OriginalChannels = (enum UserFmtChannels)DstChannels;
        ALBuf->OriginalType     = (enum UserFmtType)DstType;
        ALBuf->OriginalSize     = frames * NewBytes * NewChannels;
        ALBuf->OriginalAlign    = 1;
    }

    ALBuf->Frequency = freq;
    ALBuf->FmtChannels = DstChannels;
    ALBuf->FmtType = DstType;
    ALBuf->Format = NewFormat;

    ALBuf->SampleLen = frames;
    ALBuf->LoopStart = 0;
    ALBuf->LoopEnd = ALBuf->SampleLen;

    WriteUnlock(&ALBuf->lock);
    return AL_NO_ERROR;
}


ALuint BytesFromUserFmt(enum UserFmtType type)
{
    switch(type)
    {
    case UserFmtByte: return sizeof(ALbyte);
    case UserFmtUByte: return sizeof(ALubyte);
    case UserFmtShort: return sizeof(ALshort);
    case UserFmtUShort: return sizeof(ALushort);
    case UserFmtInt: return sizeof(ALint);
    case UserFmtUInt: return sizeof(ALuint);
    case UserFmtFloat: return sizeof(ALfloat);
    case UserFmtDouble: return sizeof(ALdouble);
    case UserFmtByte3: return sizeof(ALbyte3);
    case UserFmtUByte3: return sizeof(ALubyte3);
    case UserFmtMulaw: return sizeof(ALubyte);
    case UserFmtAlaw: return sizeof(ALubyte);
    case UserFmtIMA4: break; /* not handled here */
    case UserFmtMSADPCM: break; /* not handled here */
    }
    return 0;
}
ALuint ChannelsFromUserFmt(enum UserFmtChannels chans)
{
    switch(chans)
    {
    case UserFmtMono: return 1;
    case UserFmtStereo: return 2;
    case UserFmtRear: return 2;
    case UserFmtQuad: return 4;
    case UserFmtX51: return 6;
    case UserFmtX61: return 7;
    case UserFmtX71: return 8;
    }
    return 0;
}
static ALboolean DecomposeUserFormat(ALenum format, enum UserFmtChannels *chans,
                                     enum UserFmtType *type)
{
    static const struct {
        ALenum format;
        enum UserFmtChannels channels;
        enum UserFmtType type;
    } list[] = {
        { AL_FORMAT_MONO8,             UserFmtMono, UserFmtUByte   },
        { AL_FORMAT_MONO16,            UserFmtMono, UserFmtShort   },
        { AL_FORMAT_MONO_FLOAT32,      UserFmtMono, UserFmtFloat   },
        { AL_FORMAT_MONO_DOUBLE_EXT,   UserFmtMono, UserFmtDouble  },
        { AL_FORMAT_MONO_IMA4,         UserFmtMono, UserFmtIMA4    },
        { AL_FORMAT_MONO_MSADPCM_SOFT, UserFmtMono, UserFmtMSADPCM },
        { AL_FORMAT_MONO_MULAW,        UserFmtMono, UserFmtMulaw   },
        { AL_FORMAT_MONO_ALAW_EXT,     UserFmtMono, UserFmtAlaw    },

        { AL_FORMAT_STEREO8,             UserFmtStereo, UserFmtUByte   },
        { AL_FORMAT_STEREO16,            UserFmtStereo, UserFmtShort   },
        { AL_FORMAT_STEREO_FLOAT32,      UserFmtStereo, UserFmtFloat   },
        { AL_FORMAT_STEREO_DOUBLE_EXT,   UserFmtStereo, UserFmtDouble  },
        { AL_FORMAT_STEREO_IMA4,         UserFmtStereo, UserFmtIMA4    },
        { AL_FORMAT_STEREO_MSADPCM_SOFT, UserFmtStereo, UserFmtMSADPCM },
        { AL_FORMAT_STEREO_MULAW,        UserFmtStereo, UserFmtMulaw   },
        { AL_FORMAT_STEREO_ALAW_EXT,     UserFmtStereo, UserFmtAlaw    },

        { AL_FORMAT_REAR8,      UserFmtRear, UserFmtUByte },
        { AL_FORMAT_REAR16,     UserFmtRear, UserFmtShort },
        { AL_FORMAT_REAR32,     UserFmtRear, UserFmtFloat },
        { AL_FORMAT_REAR_MULAW, UserFmtRear, UserFmtMulaw },

        { AL_FORMAT_QUAD8_LOKI,  UserFmtQuad, UserFmtUByte },
        { AL_FORMAT_QUAD16_LOKI, UserFmtQuad, UserFmtShort },

        { AL_FORMAT_QUAD8,      UserFmtQuad, UserFmtUByte },
        { AL_FORMAT_QUAD16,     UserFmtQuad, UserFmtShort },
        { AL_FORMAT_QUAD32,     UserFmtQuad, UserFmtFloat },
        { AL_FORMAT_QUAD_MULAW, UserFmtQuad, UserFmtMulaw },

        { AL_FORMAT_51CHN8,      UserFmtX51, UserFmtUByte },
        { AL_FORMAT_51CHN16,     UserFmtX51, UserFmtShort },
        { AL_FORMAT_51CHN32,     UserFmtX51, UserFmtFloat },
        { AL_FORMAT_51CHN_MULAW, UserFmtX51, UserFmtMulaw },

        { AL_FORMAT_61CHN8,      UserFmtX61, UserFmtUByte },
        { AL_FORMAT_61CHN16,     UserFmtX61, UserFmtShort },
        { AL_FORMAT_61CHN32,     UserFmtX61, UserFmtFloat },
        { AL_FORMAT_61CHN_MULAW, UserFmtX61, UserFmtMulaw },

        { AL_FORMAT_71CHN8,      UserFmtX71, UserFmtUByte },
        { AL_FORMAT_71CHN16,     UserFmtX71, UserFmtShort },
        { AL_FORMAT_71CHN32,     UserFmtX71, UserFmtFloat },
        { AL_FORMAT_71CHN_MULAW, UserFmtX71, UserFmtMulaw },
    };
    ALuint i;

    for(i = 0;i < COUNTOF(list);i++)
    {
        if(list[i].format == format)
        {
            *chans = list[i].channels;
            *type  = list[i].type;
            return AL_TRUE;
        }
    }

    return AL_FALSE;
}

ALuint BytesFromFmt(enum FmtType type)
{
    switch(type)
    {
    case FmtByte: return sizeof(ALbyte);
    case FmtShort: return sizeof(ALshort);
    case FmtFloat: return sizeof(ALfloat);
    }
    return 0;
}
ALuint ChannelsFromFmt(enum FmtChannels chans)
{
    switch(chans)
    {
    case FmtMono: return 1;
    case FmtStereo: return 2;
    case FmtRear: return 2;
    case FmtQuad: return 4;
    case FmtX51: return 6;
    case FmtX61: return 7;
    case FmtX71: return 8;
    }
    return 0;
}
static ALboolean DecomposeFormat(ALenum format, enum FmtChannels *chans, enum FmtType *type)
{
    static const struct {
        ALenum format;
        enum FmtChannels channels;
        enum FmtType type;
    } list[] = {
        { AL_MONO8_SOFT,   FmtMono, FmtByte  },
        { AL_MONO16_SOFT,  FmtMono, FmtShort },
        { AL_MONO32F_SOFT, FmtMono, FmtFloat },

        { AL_STEREO8_SOFT,   FmtStereo, FmtByte  },
        { AL_STEREO16_SOFT,  FmtStereo, FmtShort },
        { AL_STEREO32F_SOFT, FmtStereo, FmtFloat },

        { AL_REAR8_SOFT,   FmtRear, FmtByte  },
        { AL_REAR16_SOFT,  FmtRear, FmtShort },
        { AL_REAR32F_SOFT, FmtRear, FmtFloat },

        { AL_FORMAT_QUAD8_LOKI,  FmtQuad, FmtByte  },
        { AL_FORMAT_QUAD16_LOKI, FmtQuad, FmtShort },

        { AL_QUAD8_SOFT,   FmtQuad, FmtByte  },
        { AL_QUAD16_SOFT,  FmtQuad, FmtShort },
        { AL_QUAD32F_SOFT, FmtQuad, FmtFloat },

        { AL_5POINT1_8_SOFT,   FmtX51, FmtByte  },
        { AL_5POINT1_16_SOFT,  FmtX51, FmtShort },
        { AL_5POINT1_32F_SOFT, FmtX51, FmtFloat },

        { AL_6POINT1_8_SOFT,   FmtX61, FmtByte  },
        { AL_6POINT1_16_SOFT,  FmtX61, FmtShort },
        { AL_6POINT1_32F_SOFT, FmtX61, FmtFloat },

        { AL_7POINT1_8_SOFT,   FmtX71, FmtByte  },
        { AL_7POINT1_16_SOFT,  FmtX71, FmtShort },
        { AL_7POINT1_32F_SOFT, FmtX71, FmtFloat },
    };
    ALuint i;

    for(i = 0;i < COUNTOF(list);i++)
    {
        if(list[i].format == format)
        {
            *chans = list[i].channels;
            *type  = list[i].type;
            return AL_TRUE;
        }
    }

    return AL_FALSE;
}

static ALboolean SanitizeAlignment(enum UserFmtType type, ALsizei *align)
{
    if(*align < 0)
        return AL_FALSE;

    if(*align == 0)
    {
        if(type == UserFmtIMA4)
        {
            /* Here is where things vary:
             * nVidia and Apple use 64+1 sample frames per block -> block_size=36 bytes per channel
             * Most PC sound software uses 2040+1 sample frames per block -> block_size=1024 bytes per channel
             */
            *align = 65;
        }
        else if(type == UserFmtMSADPCM)
            *align = 64;
        else
            *align = 1;
        return AL_TRUE;
    }

    if(type == UserFmtIMA4)
    {
        /* IMA4 block alignment must be a multiple of 8, plus 1. */
        return ((*align)&7) == 1;
    }
    if(type == UserFmtMSADPCM)
    {
        /* MSADPCM block alignment must be a multiple of 8. */
        /* FIXME: Too strict? Might only require align*channels to be a
         * multiple of 2. */
        return ((*align)&7) == 0;
    }

    return AL_TRUE;
}


static ALboolean IsValidType(ALenum type)
{
    switch(type)
    {
        case AL_BYTE_SOFT:
        case AL_UNSIGNED_BYTE_SOFT:
        case AL_SHORT_SOFT:
        case AL_UNSIGNED_SHORT_SOFT:
        case AL_INT_SOFT:
        case AL_UNSIGNED_INT_SOFT:
        case AL_FLOAT_SOFT:
        case AL_DOUBLE_SOFT:
        case AL_BYTE3_SOFT:
        case AL_UNSIGNED_BYTE3_SOFT:
            return AL_TRUE;
    }
    return AL_FALSE;
}

static ALboolean IsValidChannels(ALenum channels)
{
    switch(channels)
    {
        case AL_MONO_SOFT:
        case AL_STEREO_SOFT:
        case AL_REAR_SOFT:
        case AL_QUAD_SOFT:
        case AL_5POINT1_SOFT:
        case AL_6POINT1_SOFT:
        case AL_7POINT1_SOFT:
            return AL_TRUE;
    }
    return AL_FALSE;
}


/*
 *    ReleaseALBuffers()
 *
 *    INTERNAL: Called to destroy any buffers that still exist on the device
 */
ALvoid ReleaseALBuffers(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->BufferMap.size;i++)
    {
        ALbuffer *temp = device->BufferMap.array[i].value;
        device->BufferMap.array[i].value = NULL;

        free(temp->data);

        FreeThunkEntry(temp->id);
        memset(temp, 0, sizeof(ALbuffer));
        free(temp);
    }
}
