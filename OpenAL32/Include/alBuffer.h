#ifndef _AL_BUFFER_H_
#define _AL_BUFFER_H_

#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User formats */
enum UserFmtType {
    UserFmtByte   = AL_BYTE,
    UserFmtUByte  = AL_UNSIGNED_BYTE,
    UserFmtShort  = AL_SHORT,
    UserFmtUShort = AL_UNSIGNED_SHORT,
    UserFmtInt    = AL_INT,
    UserFmtUInt   = AL_UNSIGNED_INT,
    UserFmtFloat  = AL_FLOAT,
    UserFmtDouble = AL_DOUBLE,
    UserFmtMulaw  = AL_MULAW,
    UserFmtIMA4   = AL_IMA4,
    UserFmtByte3  = AL_BYTE3,
    UserFmtUByte3 = AL_UNSIGNED_BYTE3,
};
enum UserFmtChannels {
    UserFmtMono   = AL_MONO,
    UserFmtStereo = AL_STEREO,
    UserFmtRear   = AL_REAR,
    UserFmtQuad   = AL_QUAD,
    UserFmtX51    = AL_5POINT1, /* (WFX order) */
    UserFmtX61    = AL_6POINT1, /* (WFX order) */
    UserFmtX71    = AL_7POINT1  /* (WFX order) */
};

ALboolean DecomposeUserFormat(ALenum format, enum UserFmtChannels *chans,
                              enum UserFmtType *type);
ALuint BytesFromUserFmt(enum UserFmtType type);
ALuint ChannelsFromUserFmt(enum UserFmtChannels chans);
static __inline ALuint FrameSizeFromUserFmt(enum UserFmtChannels chans,
                                            enum UserFmtType type)
{
    return ChannelsFromUserFmt(chans) * BytesFromUserFmt(type);
}


/* Storable formats */
enum FmtType {
    FmtByte  = UserFmtByte,
    FmtShort = UserFmtShort,
    FmtFloat = UserFmtFloat,
};
enum FmtChannels {
    FmtMono   = UserFmtMono,
    FmtStereo = UserFmtStereo,
    FmtRear   = UserFmtRear,
    FmtQuad   = UserFmtQuad,
    FmtX51    = UserFmtX51,
    FmtX61    = UserFmtX61,
    FmtX71    = UserFmtX71,
};

ALboolean DecomposeFormat(ALenum format, enum FmtChannels *chans, enum FmtType *type);
ALuint BytesFromFmt(enum FmtType type);
ALuint ChannelsFromFmt(enum FmtChannels chans);
static __inline ALuint FrameSizeFromFmt(enum FmtChannels chans, enum FmtType type)
{
    return ChannelsFromFmt(chans) * BytesFromFmt(type);
}


typedef struct ALbuffer
{
    ALvoid  *data;
    ALsizei  size;

    ALsizei          Frequency;
    enum FmtChannels FmtChannels;
    enum FmtType     FmtType;

    enum UserFmtChannels OriginalChannels;
    enum UserFmtType     OriginalType;
    ALsizei OriginalSize;
    ALsizei OriginalAlign;

    ALsizei  LoopStart;
    ALsizei  LoopEnd;

    RefCount ref; // Number of sources using this buffer (deletion can only occur when this is 0)

    // Index to itself
    ALuint buffer;
} ALbuffer;

ALvoid ReleaseALBuffers(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
