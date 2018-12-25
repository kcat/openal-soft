#ifndef _AL_BUFFER_H_
#define _AL_BUFFER_H_

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"

#include "inprogext.h"
#include "atomic.h"
#include "vector.h"


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

ALsizei BytesFromUserFmt(UserFmtType type);
ALsizei ChannelsFromUserFmt(UserFmtChannels chans);
inline ALsizei FrameSizeFromUserFmt(UserFmtChannels chans, UserFmtType type)
{ return ChannelsFromUserFmt(chans) * BytesFromUserFmt(type); }


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

/* DevFmtType traits, providing the type, etc given a DevFmtType. */
template<FmtType T>
struct FmtTypeTraits { };

template<>
struct FmtTypeTraits<FmtUByte> { using Type = ALubyte; };
template<>
struct FmtTypeTraits<FmtShort> { using Type = ALshort; };
template<>
struct FmtTypeTraits<FmtFloat> { using Type = ALfloat; };
template<>
struct FmtTypeTraits<FmtDouble> { using Type = ALdouble; };
template<>
struct FmtTypeTraits<FmtMulaw> { using Type = ALubyte; };
template<>
struct FmtTypeTraits<FmtAlaw> { using Type = ALubyte; };


ALsizei BytesFromFmt(FmtType type);
ALsizei ChannelsFromFmt(FmtChannels chans);
inline ALsizei FrameSizeFromFmt(FmtChannels chans, FmtType type)
{ return ChannelsFromFmt(chans) * BytesFromFmt(type); }


struct ALbuffer {
    al::vector<ALbyte,16> mData;

    ALsizei Frequency{0};
    ALbitfieldSOFT Access{0u};
    ALsizei SampleLen{0};

    FmtChannels mFmtChannels{};
    FmtType     mFmtType{};
    ALsizei BytesAlloc{0};

    UserFmtType OriginalType{};
    ALsizei OriginalSize{0};
    ALsizei OriginalAlign{0};

    ALsizei LoopStart{0};
    ALsizei LoopEnd{0};

    std::atomic<ALsizei> UnpackAlign{0};
    std::atomic<ALsizei> PackAlign{0};

    ALbitfieldSOFT MappedAccess{0u};
    ALsizei MappedOffset{0};
    ALsizei MappedSize{0};

    /* Number of times buffer was attached to a source (deletion can only occur when 0) */
    RefCount ref{0u};

    /* Self ID */
    ALuint id{0};
};

#endif
