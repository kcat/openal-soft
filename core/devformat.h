#ifndef CORE_DEVFORMAT_H
#define CORE_DEVFORMAT_H

#include <cstdint>
#include <string_view>


enum Channel : std::uint8_t {
    FrontLeft = 0,
    FrontRight,
    FrontCenter,
    LFE,
    BackLeft,
    BackRight,
    BackCenter,
    SideLeft,
    SideRight,

    TopCenter,
    TopFrontLeft,
    TopFrontCenter,
    TopFrontRight,
    TopBackLeft,
    TopBackCenter,
    TopBackRight,

    BottomFrontLeft,
    BottomFrontRight,
    BottomBackLeft,
    BottomBackRight,

    Aux0,
    Aux1,
    Aux2,
    Aux3,
    Aux4,
    Aux5,
    Aux6,
    Aux7,
    Aux8,
    Aux9,
    Aux10,
    Aux11,
    Aux12,
    Aux13,
    Aux14,
    Aux15,

    NumChannels
};

/* Device formats */
enum DevFmtType : std::uint8_t {
    DevFmtByte,
    DevFmtUByte,
    DevFmtShort,
    DevFmtUShort,
    DevFmtInt,
    DevFmtUInt,
    DevFmtFloat,

    DevFmtTypeDefault = DevFmtFloat
};
enum DevFmtChannels : std::uint8_t {
    DevFmtMono,
    DevFmtStereo,
    DevFmtQuad,
    DevFmtX51,
    DevFmtX61,
    DevFmtX71,
    DevFmtX714,
    DevFmtX7144,
    DevFmtX3D71,
    DevFmtAmbi3D,

    DevFmtChannelsDefault = DevFmtStereo
};
inline constexpr auto MaxOutputChannels = 32u;


[[nodiscard]]
auto BytesFromDevFmt(DevFmtType type) noexcept -> unsigned;
[[nodiscard]]
auto ChannelsFromDevFmt(DevFmtChannels chans, unsigned ambiorder) noexcept -> unsigned;
[[nodiscard]] inline
auto FrameSizeFromDevFmt(DevFmtChannels const chans, DevFmtType const type,
    unsigned const ambiorder) noexcept -> unsigned
{ return ChannelsFromDevFmt(chans, ambiorder) * BytesFromDevFmt(type); }

[[nodiscard]]
auto DevFmtTypeString(DevFmtType type) noexcept -> std::string_view;
[[nodiscard]]
auto DevFmtChannelsString(DevFmtChannels chans) noexcept -> std::string_view;

enum class DevAmbiLayout : bool {
    FuMa,
    ACN,

    Default = ACN
};

enum class DevAmbiScaling : std::uint8_t {
    FuMa,
    SN3D,
    N3D,

    Default = SN3D
};

#endif /* CORE_DEVFORMAT_H */
