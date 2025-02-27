
#include "config.h"

#include "devformat.h"

#include <string_view>

namespace {
using namespace std::string_view_literals;
} // namespace

uint BytesFromDevFmt(DevFmtType type) noexcept
{
    switch(type)
    {
    case DevFmtByte: return sizeof(int8_t);
    case DevFmtUByte: return sizeof(uint8_t);
    case DevFmtShort: return sizeof(int16_t);
    case DevFmtUShort: return sizeof(uint16_t);
    case DevFmtInt: return sizeof(int32_t);
    case DevFmtUInt: return sizeof(uint32_t);
    case DevFmtFloat: return sizeof(float);
    }
    return 0;
}
uint ChannelsFromDevFmt(DevFmtChannels chans, uint ambiorder) noexcept
{
    switch(chans)
    {
    case DevFmtMono: return 1;
    case DevFmtStereo: return 2;
    case DevFmtQuad: return 4;
    case DevFmtX51: return 6;
    case DevFmtX61: return 7;
    case DevFmtX71: return 8;
    case DevFmtX714: return 12;
    case DevFmtX7144: return 16;
    case DevFmtX3D71: return 8;
    case DevFmtAmbi3D: return (ambiorder+1) * (ambiorder+1);
    }
    return 0;
}

auto DevFmtTypeString(DevFmtType type) noexcept -> std::string_view
{
    switch(type)
    {
    case DevFmtByte: return "Int8"sv;
    case DevFmtUByte: return "UInt8"sv;
    case DevFmtShort: return "Int16"sv;
    case DevFmtUShort: return "UInt16"sv;
    case DevFmtInt: return "Int32"sv;
    case DevFmtUInt: return "UInt32"sv;
    case DevFmtFloat: return "Float32"sv;
    }
    return "(unknown type)"sv;
}
auto DevFmtChannelsString(DevFmtChannels chans) noexcept -> std::string_view
{
    switch(chans)
    {
    case DevFmtMono: return "Mono"sv;
    case DevFmtStereo: return "Stereo"sv;
    case DevFmtQuad: return "Quadraphonic"sv;
    case DevFmtX51: return "5.1 Surround"sv;
    case DevFmtX61: return "6.1 Surround"sv;
    case DevFmtX71: return "7.1 Surround"sv;
    case DevFmtX714: return "7.1.4 Surround"sv;
    case DevFmtX7144: return "7.1.4.4 Surround"sv;
    case DevFmtX3D71: return "3D7.1 Surround"sv;
    case DevFmtAmbi3D: return "Ambisonic 3D"sv;
    }
    return "(unknown channels)"sv;
}
