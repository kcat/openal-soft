#ifndef _AL_BUFFER_H_
#define _AL_BUFFER_H_

#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

enum FmtType {
    FmtUByte,
    FmtShort,
    FmtFloat,
    FmtDouble,
};

enum FmtChannels {
    FmtMono,
    FmtStereo,
    FmtQuad,
    Fmt51ChanWFX,
    Fmt61ChanWFX,
    Fmt71ChanWFX,
};


static __inline void DecomposeFormat(ALenum format, enum FmtType *type,
                                     enum FmtChannels *order)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
            *type  = FmtUByte;
            *order = FmtMono;
            break;
        case AL_FORMAT_MONO16:
            *type  = FmtShort;
            *order = FmtMono;
            break;
        case AL_FORMAT_MONO_FLOAT32:
            *type  = FmtFloat;
            *order = FmtMono;
            break;
        case AL_FORMAT_STEREO8:
            *type  = FmtUByte;
            *order = FmtStereo;
            break;
        case AL_FORMAT_STEREO16:
            *type  = FmtShort;
            *order = FmtStereo;
            break;
        case AL_FORMAT_STEREO_FLOAT32:
            *type  = FmtFloat;
            *order = FmtStereo;
            break;
        case AL_FORMAT_QUAD8_LOKI:
        case AL_FORMAT_QUAD8:
            *type  = FmtUByte;
            *order = FmtQuad;
            break;
        case AL_FORMAT_QUAD16_LOKI:
        case AL_FORMAT_QUAD16:
            *type  = FmtShort;
            *order = FmtQuad;
            break;
        case AL_FORMAT_QUAD32:
            *type  = FmtFloat;
            *order = FmtQuad;
            break;
        case AL_FORMAT_51CHN8:
            *type  = FmtUByte;
            *order = Fmt51ChanWFX;
            break;
        case AL_FORMAT_51CHN16:
            *type  = FmtShort;
            *order = Fmt51ChanWFX;
            break;
        case AL_FORMAT_51CHN32:
            *type  = FmtFloat;
            *order = Fmt51ChanWFX;
            break;
        case AL_FORMAT_61CHN8:
            *type  = FmtUByte;
            *order = Fmt61ChanWFX;
            break;
        case AL_FORMAT_61CHN16:
            *type  = FmtShort;
            *order = Fmt61ChanWFX;
            break;
        case AL_FORMAT_61CHN32:
            *type  = FmtFloat;
            *order = Fmt61ChanWFX;
            break;
        case AL_FORMAT_71CHN8:
            *type  = FmtUByte;
            *order = Fmt71ChanWFX;
            break;
        case AL_FORMAT_71CHN16:
            *type  = FmtShort;
            *order = Fmt71ChanWFX;
            break;
        case AL_FORMAT_71CHN32:
            *type  = FmtFloat;
            *order = Fmt71ChanWFX;
            break;

        default:
            AL_PRINT("Unhandled format specified: 0x%X\n", format);
            abort();
    }
}


typedef struct ALbuffer
{
    ALvoid  *data;
    ALsizei  size;

    ALenum   format;
    ALenum   eOriginalFormat;
    ALsizei  frequency;

    ALsizei  OriginalSize;
    ALsizei  OriginalAlign;

    ALsizei  LoopStart;
    ALsizei  LoopEnd;

    enum FmtType     FmtType;
    enum FmtChannels FmtChannels;

    ALuint   refcount; // Number of sources using this buffer (deletion can only occur when this is 0)

    // Index to itself
    ALuint buffer;
} ALbuffer;

ALvoid ReleaseALBuffers(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
