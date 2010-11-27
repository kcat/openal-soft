#ifndef _AL_BUFFER_H_
#define _AL_BUFFER_H_

#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Input formats (some are currently theoretical) */
enum SrcFmtType {
    SrcFmtByte,   /* AL_BYTE */
    SrcFmtUByte,  /* AL_UNSIGNED_BYTE */
    SrcFmtShort,  /* AL_SHORT */
    SrcFmtUShort, /* AL_UNSIGNED_SHORT */
    SrcFmtFloat,  /* AL_FLOAT */
    SrcFmtDouble, /* AL_DOUBLE */
};
enum SrcFmtChannels {
    SrcFmtMono,   /* AL_MONO */
    SrcFmtStereo, /* AL_STEREO */
    SrcFmtRear,   /* AL_REAR */
    SrcFmtQuad,   /* AL_QUAD */
    SrcFmtX51,    /* AL_5POINT1 (WFX order) */
    SrcFmtX61,    /* AL_6POINT1 (WFX order) */
    SrcFmtX71,    /* AL_7POINT1 (WFX order) */
};

void DecomposeInputFormat(ALenum format, enum SrcFmtType *type,
                          enum SrcFmtChannels *order);

/* Storable formats */
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

void DecomposeFormat(ALenum format, enum FmtType *type, enum FmtChannels *order);


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
