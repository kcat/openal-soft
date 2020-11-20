#ifndef ALC_BUFFER_STORAGE_H
#define ALC_BUFFER_STORAGE_H

#include <atomic>

#include "albyte.h"
#include "almalloc.h"
#include "vector.h"


using uint = unsigned int;

/* Storable formats */
enum FmtType : unsigned char {
    FmtUByte,
    FmtShort,
    FmtFloat,
    FmtDouble,
    FmtMulaw,
    FmtAlaw,
};
enum FmtChannels : unsigned char {
    FmtMono,
    FmtStereo,
    FmtRear,
    FmtQuad,
    FmtX51, /* (WFX order) */
    FmtX61, /* (WFX order) */
    FmtX71, /* (WFX order) */
    FmtBFormat2D,
    FmtBFormat3D,
};

enum class AmbiLayout : unsigned char {
    FuMa,
    ACN,
};
enum class AmbiScaling : unsigned char {
    FuMa,
    SN3D,
    N3D,
};

uint BytesFromFmt(FmtType type) noexcept;
uint ChannelsFromFmt(FmtChannels chans, uint ambiorder) noexcept;
inline uint FrameSizeFromFmt(FmtChannels chans, FmtType type, uint ambiorder) noexcept
{ return ChannelsFromFmt(chans, ambiorder) * BytesFromFmt(type); }


using CallbackType = int(*)(void*, void*, int);

struct BufferStorage {
    al::vector<al::byte,16> mData;

    CallbackType mCallback{nullptr};
    void *mUserData{nullptr};

    uint mSampleRate{0u};
    FmtChannels mChannels{};
    FmtType mType{};
    uint mSampleLen{0u};

    AmbiLayout mAmbiLayout{AmbiLayout::FuMa};
    AmbiScaling mAmbiScaling{AmbiScaling::FuMa};
    uint mAmbiOrder{0u};

    uint mLoopStart{0u};
    uint mLoopEnd{0u};

    inline uint bytesFromFmt() const noexcept { return BytesFromFmt(mType); }
    inline uint channelsFromFmt() const noexcept
    { return ChannelsFromFmt(mChannels, mAmbiOrder); }
    inline uint frameSizeFromFmt() const noexcept { return channelsFromFmt() * bytesFromFmt(); }

    inline bool isBFormat() const noexcept
    { return mChannels == FmtBFormat2D || mChannels == FmtBFormat3D; }
};


struct BufferlistItem {
    std::atomic<BufferlistItem*> mNext{nullptr};
    uint mSampleLen{0u};
    BufferStorage *mBuffer{nullptr};

    DEF_NEWDEL(BufferlistItem)
};

#endif /* ALC_BUFFER_STORAGE_H */
