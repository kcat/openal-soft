#ifndef ALC_DEVFORMAT_H
#define ALC_DEVFORMAT_H

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "inprogext.h"


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

    UpperFrontLeft,
    UpperFrontRight,
    UpperBackLeft,
    UpperBackRight,
    LowerFrontLeft,
    LowerFrontRight,
    LowerBackLeft,
    LowerBackRight,

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
enum DevFmtType : ALenum {
    DevFmtByte   = ALC_BYTE_SOFT,
    DevFmtUByte  = ALC_UNSIGNED_BYTE_SOFT,
    DevFmtShort  = ALC_SHORT_SOFT,
    DevFmtUShort = ALC_UNSIGNED_SHORT_SOFT,
    DevFmtInt    = ALC_INT_SOFT,
    DevFmtUInt   = ALC_UNSIGNED_INT_SOFT,
    DevFmtFloat  = ALC_FLOAT_SOFT,

    DevFmtTypeDefault = DevFmtFloat
};
enum DevFmtChannels : ALenum {
    DevFmtMono   = ALC_MONO_SOFT,
    DevFmtStereo = ALC_STEREO_SOFT,
    DevFmtQuad   = ALC_QUAD_SOFT,
    DevFmtX51    = ALC_5POINT1_SOFT,
    DevFmtX61    = ALC_6POINT1_SOFT,
    DevFmtX71    = ALC_7POINT1_SOFT,
    DevFmtAmbi3D = ALC_BFORMAT3D_SOFT,

    /* Similar to 5.1, except using rear channels instead of sides */
    DevFmtX51Rear = 0x70000000,

    DevFmtChannelsDefault = DevFmtStereo
};
#define MAX_OUTPUT_CHANNELS  (16)

/* DevFmtType traits, providing the type, etc given a DevFmtType. */
template<DevFmtType T>
struct DevFmtTypeTraits { };

template<>
struct DevFmtTypeTraits<DevFmtByte> { using Type = ALbyte; };
template<>
struct DevFmtTypeTraits<DevFmtUByte> { using Type = ALubyte; };
template<>
struct DevFmtTypeTraits<DevFmtShort> { using Type = ALshort; };
template<>
struct DevFmtTypeTraits<DevFmtUShort> { using Type = ALushort; };
template<>
struct DevFmtTypeTraits<DevFmtInt> { using Type = ALint; };
template<>
struct DevFmtTypeTraits<DevFmtUInt> { using Type = ALuint; };
template<>
struct DevFmtTypeTraits<DevFmtFloat> { using Type = ALfloat; };


ALuint BytesFromDevFmt(DevFmtType type) noexcept;
ALuint ChannelsFromDevFmt(DevFmtChannels chans, ALsizei ambiorder) noexcept;
inline ALuint FrameSizeFromDevFmt(DevFmtChannels chans, DevFmtType type, ALsizei ambiorder) noexcept
{ return ChannelsFromDevFmt(chans, ambiorder) * BytesFromDevFmt(type); }

enum class AmbiLayout {
    FuMa = ALC_FUMA_SOFT, /* FuMa channel order */
    ACN = ALC_ACN_SOFT,   /* ACN channel order */

    Default = ACN
};

enum class AmbiNorm {
    FuMa = ALC_FUMA_SOFT, /* FuMa normalization */
    SN3D = ALC_SN3D_SOFT, /* SN3D normalization */
    N3D = ALC_N3D_SOFT,   /* N3D normalization */

    Default = SN3D
};

#endif /* ALC_DEVFORMAT_H */
