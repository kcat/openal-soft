#ifndef CORE_STORAGE_FORMATS_H
#define CORE_STORAGE_FORMATS_H

#include <string_view>

#include "altypes.hpp"

/* Storable formats */
enum FmtType : u8::value_t {
    FmtUByte,
    FmtShort,
    FmtInt,
    FmtFloat,
    FmtDouble,
    FmtMulaw,
    FmtAlaw,
    FmtIMA4,
    FmtMSADPCM,
};
enum FmtChannels : u8::value_t {
    FmtMono,
    FmtStereo,
    FmtRear,
    FmtQuad,
    FmtX51, /* (WFX order) */
    FmtX61, /* (WFX order) */
    FmtX71, /* (WFX order) */
    FmtBFormat2D,
    FmtBFormat3D,
    FmtUHJ2, /* 2-channel UHJ, aka "BHJ", stereo-compatible */
    FmtUHJ3, /* 3-channel UHJ, aka "THJ" */
    FmtUHJ4, /* 4-channel UHJ, aka "PHJ" */
    FmtSuperStereo, /* Stereo processed with Super Stereo. */
};

enum class AmbiLayout : u8::value_t {
    FuMa,
    ACN,
};
enum class AmbiScaling : u8::value_t {
    FuMa,
    SN3D,
    N3D,
};

auto NameFromFormat(FmtType type) noexcept -> std::string_view;
auto NameFromFormat(FmtChannels channels) noexcept -> std::string_view;

[[nodiscard]]
auto BytesFromFmt(FmtType type) noexcept -> unsigned;
[[nodiscard]]
auto ChannelsFromFmt(FmtChannels chans, unsigned ambiorder) noexcept -> unsigned;
[[nodiscard]]
inline auto FrameSizeFromFmt(FmtChannels const chans, FmtType const type, unsigned const ambiorder)
    noexcept -> unsigned
{ return ChannelsFromFmt(chans, ambiorder) * BytesFromFmt(type); }

#endif /* CORE_STORAGE_FORMATS_H */
