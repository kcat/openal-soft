#ifndef CORE_DEVFORMAT_H
#define CORE_DEVFORMAT_H

#include <cstdint>
#include <cstddef>
#include <string_view>

#include "altypes.hpp"

enum Channel : u8::value_t {
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
enum DevFmtType : u8::value_t {
    DevFmtByte,
    DevFmtUByte,
    DevFmtShort,
    DevFmtUShort,
    DevFmtInt,
    DevFmtUInt,
    DevFmtFloat,

    DevFmtTypeDefault = DevFmtFloat
};
enum DevFmtChannels : u8::value_t {
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
struct DevFmtTypeTraits<DevFmtByte> { using Type = i8; };
template<>
struct DevFmtTypeTraits<DevFmtUByte> { using Type = u8; };
template<>
struct DevFmtTypeTraits<DevFmtShort> { using Type = i16; };
template<>
struct DevFmtTypeTraits<DevFmtUShort> { using Type = u16; };
template<>
struct DevFmtTypeTraits<DevFmtInt> { using Type = i32; };
template<>
struct DevFmtTypeTraits<DevFmtUInt> { using Type = u32; };
template<>
struct DevFmtTypeTraits<DevFmtFloat> { using Type = f32; };

template<DevFmtType T>
using DevFmtType_t = typename DevFmtTypeTraits<T>::Type;


[[nodiscard]]
auto BytesFromDevFmt(DevFmtType type) noexcept -> unsigned;
[[nodiscard]]
auto ChannelsFromDevFmt(DevFmtChannels chans, unsigned ambiorder) noexcept -> unsigned;
[[nodiscard]]
inline auto FrameSizeFromDevFmt(DevFmtChannels const chans, DevFmtType const type,
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

enum class DevAmbiScaling : u8::value_t {
    FuMa,
    SN3D,
    N3D,

    Default = SN3D
};

#endif /* CORE_DEVFORMAT_H */
