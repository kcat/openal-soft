#ifndef AL_BUFFER_H
#define AL_BUFFER_H

#include <atomic>

#include "AL/al.h"

#include "albyte.h"
#include "almalloc.h"
#include "atomic.h"
#include "inprogext.h"
#include "vector.h"


/* User formats */
enum UserFmtType : unsigned char {
    UserFmtUByte,
    UserFmtShort,
    UserFmtFloat,
    UserFmtDouble,
    UserFmtMulaw,
    UserFmtAlaw,
    UserFmtIMA4,
    UserFmtMSADPCM,
};
enum UserFmtChannels : unsigned char {
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


/* Storable formats */
enum FmtType : unsigned char {
    FmtUByte  = UserFmtUByte,
    FmtShort  = UserFmtShort,
    FmtFloat  = UserFmtFloat,
    FmtDouble = UserFmtDouble,
    FmtMulaw  = UserFmtMulaw,
    FmtAlaw   = UserFmtAlaw,
};
enum FmtChannels : unsigned char {
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


ALuint BytesFromFmt(FmtType type) noexcept;
ALuint ChannelsFromFmt(FmtChannels chans) noexcept;


struct ALbuffer {
    al::vector<al::byte,16> mData;

    ALuint Frequency{0u};
    ALbitfieldSOFT Access{0u};
    ALuint SampleLen{0u};

    FmtChannels mFmtChannels{};
    FmtType     mFmtType{};

    UserFmtType OriginalType{};
    ALuint OriginalSize{0};
    ALuint OriginalAlign{0};

    ALenum AmbiLayout{AL_FUMA_SOFT};
    ALenum AmbiScaling{AL_FUMA_SOFT};

    ALuint LoopStart{0u};
    ALuint LoopEnd{0u};

    ALuint UnpackAlign{0};
    ALuint PackAlign{0};

    ALbitfieldSOFT MappedAccess{0u};
    ALsizei MappedOffset{0};
    ALsizei MappedSize{0};

    /* Number of times buffer was attached to a source (deletion can only occur when 0) */
    RefCount ref{0u};

    /* Self ID */
    ALuint id{0};

    inline ALuint bytesFromFmt() const noexcept { return BytesFromFmt(mFmtType); }
    inline ALuint channelsFromFmt() const noexcept { return ChannelsFromFmt(mFmtChannels); }
    inline ALuint frameSizeFromFmt() const noexcept { return channelsFromFmt() * bytesFromFmt(); }
};

#endif
