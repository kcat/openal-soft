#ifndef _AL_BUFFER_H_
#define _AL_BUFFER_H_

#include "alMain.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User formats */
enum UserFmtType {
    UserFmtUByte,
    UserFmtShort,
    UserFmtFloat,
    UserFmtDouble,
    UserFmtMulaw,
    UserFmtAlaw,
    UserFmtIMA4,
    UserFmtMSADPCM,
};
enum UserFmtChannels {
    UserFmtMono,
    UserFmtStereo,
    UserFmtRear,
    UserFmtQuad,
    UserFmtX51, /* (WFX order) */
    UserFmtX61, /* (WFX order) */
    UserFmtX71, /* (WFX order) */
    UserFmtBFormat2D, /* WXY */
    UserFmtBFormat3D, /* WXYZ */
};

ALsizei BytesFromUserFmt(enum UserFmtType type);
ALsizei ChannelsFromUserFmt(enum UserFmtChannels chans);
inline ALsizei FrameSizeFromUserFmt(enum UserFmtChannels chans, enum UserFmtType type)
{
    return ChannelsFromUserFmt(chans) * BytesFromUserFmt(type);
}


/* Storable formats */
enum FmtType {
    FmtUByte  = UserFmtUByte,
    FmtShort  = UserFmtShort,
    FmtFloat  = UserFmtFloat,
    FmtDouble = UserFmtDouble,
    FmtMulaw  = UserFmtMulaw,
    FmtAlaw   = UserFmtAlaw,
};
enum FmtChannels {
    FmtMono   = UserFmtMono,
    FmtStereo = UserFmtStereo,
    FmtRear   = UserFmtRear,
    FmtQuad   = UserFmtQuad,
    FmtX51    = UserFmtX51,
    FmtX61    = UserFmtX61,
    FmtX71    = UserFmtX71,
    FmtBFormat2D = UserFmtBFormat2D,
    FmtBFormat3D = UserFmtBFormat3D,
};
#define MAX_INPUT_CHANNELS  (8)

ALsizei BytesFromFmt(enum FmtType type);
ALsizei ChannelsFromFmt(enum FmtChannels chans);
inline ALsizei FrameSizeFromFmt(enum FmtChannels chans, enum FmtType type)
{
    return ChannelsFromFmt(chans) * BytesFromFmt(type);
}


typedef struct ALbuffer {
    ALvoid  *data;

    ALsizei  Frequency;
    ALenum   Format;
    ALbitfieldSOFT Access;
    ALsizei  SampleLen;

    enum FmtChannels FmtChannels;
    enum FmtType     FmtType;
    ALuint BytesAlloc;

    enum UserFmtChannels OriginalChannels;
    enum UserFmtType     OriginalType;
    ALsizei              OriginalSize;
    ALsizei              OriginalAlign;

    ALsizei LoopStart;
    ALsizei LoopEnd;

    ATOMIC(ALsizei) UnpackAlign;
    ATOMIC(ALsizei) PackAlign;

    ALbitfieldSOFT MappedAccess;

    /* Number of times buffer was attached to a source (deletion can only occur when 0) */
    RefCount ref;

    RWLock lock;

    /* Self ID */
    ALuint id;
} ALbuffer;

ALbuffer *NewBuffer(ALCcontext *context);
void DeleteBuffer(ALCdevice *device, ALbuffer *buffer);

inline void LockBuffersRead(ALCdevice *device)
{ LockUIntMapRead(&device->BufferMap); }
inline void UnlockBuffersRead(ALCdevice *device)
{ UnlockUIntMapRead(&device->BufferMap); }
inline void LockBuffersWrite(ALCdevice *device)
{ LockUIntMapWrite(&device->BufferMap); }
inline void UnlockBuffersWrite(ALCdevice *device)
{ UnlockUIntMapWrite(&device->BufferMap); }

inline struct ALbuffer *LookupBuffer(ALCdevice *device, ALuint id)
{ return (struct ALbuffer*)LookupUIntMapKeyNoLock(&device->BufferMap, id); }
inline struct ALbuffer *RemoveBuffer(ALCdevice *device, ALuint id)
{ return (struct ALbuffer*)RemoveUIntMapKeyNoLock(&device->BufferMap, id); }

ALvoid ReleaseALBuffers(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
