#ifndef ALC_BUFFER_FORMATS_H
#define ALC_BUFFER_FORMATS_H

#include "AL/al.h"


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

ALuint BytesFromFmt(FmtType type) noexcept;
ALuint ChannelsFromFmt(FmtChannels chans, ALuint ambiorder) noexcept;
inline ALuint FrameSizeFromFmt(FmtChannels chans, FmtType type, ALuint ambiorder) noexcept
{ return ChannelsFromFmt(chans, ambiorder) * BytesFromFmt(type); }

#endif /* ALC_BUFFER_FORMATS_H */
