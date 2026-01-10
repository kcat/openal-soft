#ifndef CORE_DEVFORMAT_H
#define CORE_DEVFORMAT_H

#include <cstdint>
#include <cstddef>
#include <string_view>

#include "altypes.hpp"

enum Channel : u8 {
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

    MaxChannels
};


/* Device formats */
enum DevFmtType : u8 {
    DevFmtByte,
    DevFmtUByte,
    DevFmtShort,
    DevFmtUShort,
    DevFmtInt,
    DevFmtUInt,
    DevFmtFloat,

    DevFmtTypeDefault = DevFmtFloat
};
enum DevFmtChannels : u8 {
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
inline constexpr auto MaxOutputChannels = 32_uz;

/* DevFmtType traits, providing the type, etc given a DevFmtType. */
template<DevFmtType>
struct DevFmtTypeTraits { };

template<>
struct DevFmtTypeTraits<DevFmtByte> { using Type = int8_t; };
template<>
struct DevFmtTypeTraits<DevFmtUByte> { using Type = uint8_t; };
template<>
struct DevFmtTypeTraits<DevFmtShort> { using Type = int16_t; };
template<>
struct DevFmtTypeTraits<DevFmtUShort> { using Type = uint16_t; };
template<>
struct DevFmtTypeTraits<DevFmtInt> { using Type = int32_t; };
template<>
struct DevFmtTypeTraits<DevFmtUInt> { using Type = uint32_t; };
template<>
struct DevFmtTypeTraits<DevFmtFloat> { using Type = float; };

template<DevFmtType T>
using DevFmtType_t = DevFmtTypeTraits<T>::Type;


[[nodiscard]]
auto BytesFromDevFmt(DevFmtType type) noexcept -> u32;
[[nodiscard]]
auto ChannelsFromDevFmt(DevFmtChannels chans, u32 ambiorder) noexcept -> u32;
[[nodiscard]]
inline auto FrameSizeFromDevFmt(DevFmtChannels const chans, DevFmtType const type,
    u32 const ambiorder) noexcept -> u32
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

enum class DevAmbiScaling : u8 {
    FuMa,
    SN3D,
    N3D,

    Default = SN3D
};

#endif /* CORE_DEVFORMAT_H */
