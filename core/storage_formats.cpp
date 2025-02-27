
#include "config.h"

#include "storage_formats.h"

#include <cstdint>
#include <string_view>

namespace {
using namespace std::string_view_literals;
} // namespace

auto NameFromFormat(FmtType type) noexcept -> std::string_view
{
    switch(type)
    {
    case FmtUByte: return "UInt8"sv;
    case FmtShort: return "Int16"sv;
    case FmtInt: return "Int32"sv;
    case FmtFloat: return "Float"sv;
    case FmtDouble: return "Double"sv;
    case FmtMulaw: return "muLaw"sv;
    case FmtAlaw: return "aLaw"sv;
    case FmtIMA4: return "IMA4 ADPCM"sv;
    case FmtMSADPCM: return "MS ADPCM"sv;
    }
    return "<internal error>"sv;
}

auto NameFromFormat(FmtChannels channels) noexcept -> std::string_view
{
    switch(channels)
    {
    case FmtMono: return "Mono"sv;
    case FmtStereo: return "Stereo"sv;
    case FmtRear: return "Rear"sv;
    case FmtQuad: return "Quadraphonic"sv;
    case FmtX51: return "Surround 5.1"sv;
    case FmtX61: return "Surround 6.1"sv;
    case FmtX71: return "Surround 7.1"sv;
    case FmtBFormat2D: return "B-Format 2D"sv;
    case FmtBFormat3D: return "B-Format 3D"sv;
    case FmtUHJ2: return "UHJ2"sv;
    case FmtUHJ3: return "UHJ3"sv;
    case FmtUHJ4: return "UHJ4"sv;
    case FmtSuperStereo: return "Super Stereo"sv;
    case FmtMonoDup: return "Mono (dup)"sv;
    }
    return "<internal error>"sv;
}

uint BytesFromFmt(FmtType type) noexcept
{
    switch(type)
    {
    case FmtUByte: return sizeof(std::uint8_t);
    case FmtShort: return sizeof(std::int16_t);
    case FmtInt: return sizeof(std::int32_t);
    case FmtFloat: return sizeof(float);
    case FmtDouble: return sizeof(double);
    case FmtMulaw: return sizeof(std::uint8_t);
    case FmtAlaw: return sizeof(std::uint8_t);
    case FmtIMA4: break;
    case FmtMSADPCM: break;
    }
    return 0;
}

uint ChannelsFromFmt(FmtChannels chans, uint ambiorder) noexcept
{
    switch(chans)
    {
    case FmtMono: return 1;
    case FmtStereo: return 2;
    case FmtRear: return 2;
    case FmtQuad: return 4;
    case FmtX51: return 6;
    case FmtX61: return 7;
    case FmtX71: return 8;
    case FmtBFormat2D: return (ambiorder*2) + 1;
    case FmtBFormat3D: return (ambiorder+1) * (ambiorder+1);
    case FmtUHJ2: return 2;
    case FmtUHJ3: return 3;
    case FmtUHJ4: return 4;
    case FmtSuperStereo: return 2;
    case FmtMonoDup: return 1;
    }
    return 0;
}
