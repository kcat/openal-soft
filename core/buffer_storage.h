#ifndef CORE_BUFFER_STORAGE_H
#define CORE_BUFFER_STORAGE_H

#include <span>
#include <variant>

#include "alnumeric.h"
#include "fmt_traits.h"
#include "storage_formats.h"


constexpr auto IsBFormat(FmtChannels const chans) noexcept -> bool
{ return chans == FmtBFormat2D || chans == FmtBFormat3D; }

/* Super Stereo is considered part of the UHJ family here, since it goes
 * through similar processing as UHJ, both result in a B-Format signal, and
 * needs the same consideration as BHJ (three channel result with only two
 * channel input).
 */
constexpr auto IsUHJ(FmtChannels const chans) noexcept -> bool
{ return chans == FmtUHJ2 || chans == FmtUHJ3 || chans == FmtUHJ4 || chans == FmtSuperStereo; }

/** Ambisonic formats are either B-Format or UHJ formats. */
constexpr auto IsAmbisonic(FmtChannels const chans) noexcept -> bool
{ return IsBFormat(chans) || IsUHJ(chans); }

constexpr auto Is2DAmbisonic(FmtChannels const chans) noexcept -> bool
{
    return chans == FmtBFormat2D || chans == FmtUHJ2 || chans == FmtUHJ3
        || chans == FmtSuperStereo;
}


using CallbackType = auto(*)(void*, void*, int) noexcept -> int;

using SampleVariant = std::variant<std::span<u8>,
    std::span<i16>,
    std::span<i32>,
    std::span<f32>,
    std::span<f64>,
    std::span<MulawSample>,
    std::span<AlawSample>,
    std::span<IMA4Data>,
    std::span<MSADPCMData>>;

struct BufferStorage {
    CallbackType mCallback{nullptr};
    void *mUserData{nullptr};

    SampleVariant mData;

    unsigned mSampleRate{0u};
    FmtChannels mChannels{FmtMono};
    FmtType mType{FmtShort};
    unsigned mSampleLen{0u};
    unsigned mBlockAlign{0u};

    AmbiLayout mAmbiLayout{AmbiLayout::FuMa};
    AmbiScaling mAmbiScaling{AmbiScaling::FuMa};
    unsigned mAmbiOrder{0u};

    [[nodiscard]] auto bytesFromFmt() const noexcept -> unsigned { return BytesFromFmt(mType); }
    [[nodiscard]] auto channelsFromFmt() const noexcept -> unsigned
    { return ChannelsFromFmt(mChannels, mAmbiOrder); }
    [[nodiscard]] auto frameSizeFromFmt() const noexcept -> unsigned
    { return channelsFromFmt() * bytesFromFmt(); }

    [[nodiscard]] auto blockSizeFromFmt() const noexcept -> unsigned
    {
        if(mType == FmtIMA4) return ((mBlockAlign-1)/2 + 4) * channelsFromFmt();
        if(mType == FmtMSADPCM) return ((mBlockAlign-2)/2 + 7) * channelsFromFmt();
        return frameSizeFromFmt();
    }

    [[nodiscard]] auto isBFormat() const noexcept -> bool { return IsBFormat(mChannels); }
};

#endif /* CORE_BUFFER_STORAGE_H */
