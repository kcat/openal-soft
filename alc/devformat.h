#ifndef ALC_DEVFORMAT_H
#define ALC_DEVFORMAT_H

#include <cstdint>


using uint = unsigned int;

enum Channel {
    FrontLeft = 0,
    FrontRight,
    FrontCenter,
    LFE,
    BackLeft,
    BackRight,
    BackCenter,
    SideLeft,
    SideRight,

    TopFrontLeft,
    TopFrontCenter,
    TopFrontRight,
    TopCenter,
    TopBackLeft,
    TopBackCenter,
    TopBackRight,

    MaxChannels
};


/* Device formats */
enum DevFmtType : unsigned char {
    DevFmtByte,
    DevFmtUByte,
    DevFmtShort,
    DevFmtUShort,
    DevFmtInt,
    DevFmtUInt,
    DevFmtFloat,

    DevFmtTypeDefault = DevFmtFloat
};
enum DevFmtChannels : unsigned char {
    DevFmtMono,
    DevFmtStereo,
    DevFmtQuad,
    DevFmtX51,
    DevFmtX61,
    DevFmtX71,
    DevFmtAmbi3D,

    /* Similar to 5.1, except using rear channels instead of sides */
    DevFmtX51Rear,

    DevFmtChannelsDefault = DevFmtStereo
};
#define MAX_OUTPUT_CHANNELS  16

/* DevFmtType traits, providing the type, etc given a DevFmtType. */
template<DevFmtType T>
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


uint BytesFromDevFmt(DevFmtType type) noexcept;
uint ChannelsFromDevFmt(DevFmtChannels chans, uint ambiorder) noexcept;
inline uint FrameSizeFromDevFmt(DevFmtChannels chans, DevFmtType type, uint ambiorder) noexcept
{ return ChannelsFromDevFmt(chans, ambiorder) * BytesFromDevFmt(type); }

enum class DevAmbiLayout : bool {
    FuMa,
    ACN,

    Default = ACN
};

enum class DevAmbiScaling : unsigned char {
    FuMa,
    SN3D,
    N3D,

    Default = SN3D
};

#endif /* ALC_DEVFORMAT_H */
