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

void DecomposeInputFormat(ALenum format, enum SrcFmtChannels *chans,
                          enum SrcFmtType *type);
ALuint BytesFromSrcFmt(enum SrcFmtType type);
ALuint ChannelsFromSrcFmt(enum SrcFmtChannels chans);
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

void DecomposeFormat(ALenum format, enum FmtChannels *chans, enum FmtType *type);
ALuint BytesFromFmt(enum FmtType type);
ALuint ChannelsFromFmt(enum FmtChannels chans);
static __inline ALuint FrameSizeFromFmt(enum FmtType type, enum FmtChannels chans)
{
    return BytesFromFmt(type) * ChannelsFromFmt(chans);
}


typedef struct ALbuffer
{
    ALvoid  *data;
    ALsizei  size;

    ALsizei          Frequency;
    enum FmtChannels FmtChannels;
    enum FmtType     FmtType;

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
