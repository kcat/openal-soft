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
    SrcFmtMulaw,  /* AL_MULAW */
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

static __inline ALuint BytesFromSrcFmt(enum SrcFmtType type)
{
    switch(type)
    {
    case SrcFmtByte: return sizeof(ALbyte);
    case SrcFmtUByte: return sizeof(ALubyte);
    case SrcFmtShort: return sizeof(ALshort);
    case SrcFmtUShort: return sizeof(ALushort);
    case SrcFmtFloat: return sizeof(ALfloat);
    case SrcFmtDouble: return sizeof(ALdouble);
    case SrcFmtMulaw: return sizeof(ALubyte);
    }
    return 0;
}
static __inline ALuint ChannelsFromSrcFmt(enum SrcFmtChannels chans)
{
    switch(chans)
    {
    case SrcFmtMono: return 1;
    case SrcFmtStereo: return 2;
    case SrcFmtRear: return 2;
    case SrcFmtQuad: return 4;
    case SrcFmtX51: return 6;
    case SrcFmtX61: return 7;
    case SrcFmtX71: return 8;
    }
    return 0;
}
static __inline ALuint FrameSizeFromSrcFmt(enum SrcFmtType type,
                                           enum SrcFmtChannels chans)
{
    return BytesFromSrcFmt(type) * ChannelsFromSrcFmt(chans);
}


/* Storable formats */
enum FmtType {
    FmtUByte,
    FmtShort,
    FmtFloat,
};
enum FmtChannels {
    FmtMono,
    FmtStereo,
    FmtRear,
    FmtQuad,
    FmtX51,
    FmtX61,
    FmtX71,
};

void DecomposeFormat(ALenum format, enum FmtType *type, enum FmtChannels *order);

static __inline ALuint BytesFromFmt(enum FmtType type)
{
    switch(type)
    {
    case FmtUByte: return sizeof(ALubyte);
    case FmtShort: return sizeof(ALshort);
    case FmtFloat: return sizeof(ALfloat);
    }
    return 0;
}
static __inline ALuint ChannelsFromFmt(enum FmtChannels chans)
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
static __inline ALuint FrameSizeFromFmt(enum FmtType type, enum FmtChannels chans)
{
    return BytesFromFmt(type) * ChannelsFromFmt(chans);
}


typedef struct ALbuffer
{
    ALvoid  *data;
    ALsizei  size;

    ALsizei          Frequency;
    enum FmtType     FmtType;
    enum FmtChannels FmtChannels;

    ALenum   OriginalFormat;
    ALsizei  OriginalSize;
    ALsizei  OriginalAlign;

    ALsizei  LoopStart;
    ALsizei  LoopEnd;

    ALuint   refcount; // Number of sources using this buffer (deletion can only occur when this is 0)

    // Index to itself
    ALuint buffer;
} ALbuffer;

ALvoid ReleaseALBuffers(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
