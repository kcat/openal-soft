
#include "config.h"

#include "ambdec.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>

#include "alstring.h"
#include "filesystem.h"
#include "fmt/core.h"


namespace {

std::string read_word(std::istream &f)
{
    std::string ret;
    f >> ret;
    return ret;
}

bool is_at_end(const std::string &buffer, std::size_t endpos)
{
    while(endpos < buffer.length() && std::isspace(buffer[endpos]))
        ++endpos;
    return !(endpos < buffer.length() && buffer[endpos] != '#');
}


enum class ReaderScope {
    Global,
    Speakers,
    LFMatrix,
    HFMatrix,
};

template<typename ...Args>
auto make_error(size_t linenum, fmt::format_string<Args...> fmt, Args&& ...args)
    -> std::optional<std::string>
{
    std::optional<std::string> ret;
    auto &str = ret.emplace(fmt::format("Line {}: ", linenum));
    str += fmt::format(std::move(fmt), std::forward<Args>(args)...);
    return ret;
}

} // namespace

AmbDecConf::~AmbDecConf() = default;


std::optional<std::string> AmbDecConf::load(const char *fname) noexcept
{
    auto f = fs::ifstream{fs::path(al::char_as_u8(fname))};
    if(!f.is_open())
        return fmt::format("Failed to open file \"{}\"", fname);

    ReaderScope scope{ReaderScope::Global};
    size_t speaker_pos{0};
    size_t lfmatrix_pos{0};
    size_t hfmatrix_pos{0};
    size_t linenum{0};

    std::string buffer;
    while(f.good() && std::getline(f, buffer))
    {
        ++linenum;

        std::istringstream istr{buffer};
        std::string command{read_word(istr)};
        if(command.empty() || command[0] == '#')
            continue;

        if(command == "/}")
        {
            if(scope == ReaderScope::Global)
                return make_error(linenum, "Unexpected /}} in global scope");
            scope = ReaderScope::Global;
            continue;
        }

        if(scope == ReaderScope::Speakers)
        {
            if(command == "add_spkr")
            {
                if(speaker_pos == Speakers.size())
                    return make_error(linenum, "Too many speakers specified");

                AmbDecConf::SpeakerConf &spkr = Speakers[speaker_pos++];
                istr >> spkr.Name;
                istr >> spkr.Distance;
                istr >> spkr.Azimuth;
                istr >> spkr.Elevation;
                istr >> spkr.Connection;
            }
            else
                return make_error(linenum, "Unexpected speakers command: {}", command);
        }
        else if(scope == ReaderScope::LFMatrix || scope == ReaderScope::HFMatrix)
        {
            auto &gains = (scope == ReaderScope::LFMatrix) ? LFOrderGain : HFOrderGain;
            auto matrix = (scope == ReaderScope::LFMatrix) ? LFMatrix : HFMatrix;
            auto &pos = (scope == ReaderScope::LFMatrix) ? lfmatrix_pos : hfmatrix_pos;

            if(command == "order_gain")
            {
                size_t toread{(ChanMask > Ambi3OrderMask) ? 5u : 4u};
                std::size_t curgain{0u};
                float value{};
                while(toread)
                {
                    --toread;
                    istr >> value;
                    if(curgain < std::size(gains))
                        gains[curgain++] = value;
                }
            }
            else if(command == "add_row")
            {
                if(pos == Speakers.size())
                    return make_error(linenum, "Too many matrix rows specified");

                unsigned int mask{ChanMask};

                AmbDecConf::CoeffArray &mtxrow = matrix[pos++];
                mtxrow.fill(0.0f);

                float value{};
                while(mask)
                {
                    auto idx = static_cast<unsigned>(std::countr_zero(mask));
                    mask &= ~(1u << idx);

                    istr >> value;
                    if(idx < mtxrow.size())
                        mtxrow[idx] = value;
                }
            }
            else
                return make_error(linenum, "Unexpected matrix command: {}", command);
        }
        // Global scope commands
        else if(command == "/description")
        {
            while(istr.good() && std::isspace(istr.peek()))
                istr.ignore();
            std::getline(istr, Description);
            while(!Description.empty() && std::isspace(Description.back()))
                Description.pop_back();
        }
        else if(command == "/version")
        {
            if(Version)
                return make_error(linenum, "Duplicate version definition");
            istr >> Version;
            if(Version != 3)
                return make_error(linenum, "Unsupported version: {}", Version);
        }
        else if(command == "/dec/chan_mask")
        {
            if(ChanMask)
                return make_error(linenum, "Duplicate chan_mask definition");
            istr >> std::hex >> ChanMask >> std::dec;

            if(!ChanMask || ChanMask > Ambi4OrderMask)
                return make_error(linenum, "Invalid chan_mask: {:#x}", ChanMask);
            if(ChanMask > Ambi3OrderMask && CoeffScale == AmbDecScale::FuMa)
                return make_error(linenum, "FuMa not compatible with over third-order");
        }
        else if(command == "/dec/freq_bands")
        {
            if(FreqBands)
                return make_error(linenum, "Duplicate freq_bands");
            istr >> FreqBands;
            if(FreqBands != 1 && FreqBands != 2)
                return make_error(linenum, "Invalid freq_bands: {}", FreqBands);
        }
        else if(command == "/dec/speakers")
        {
            if(!Speakers.empty())
                return make_error(linenum, "Duplicate speakers");
            size_t numspeakers{};
            istr >> numspeakers;
            if(!numspeakers)
                return make_error(linenum, "Invalid speakers: {}", numspeakers);
            Speakers.resize(numspeakers);
        }
        else if(command == "/dec/coeff_scale")
        {
            if(CoeffScale != AmbDecScale::Unset)
                return make_error(linenum, "Duplicate coeff_scale");

            std::string scale{read_word(istr)};
            if(scale == "n3d") CoeffScale = AmbDecScale::N3D;
            else if(scale == "sn3d") CoeffScale = AmbDecScale::SN3D;
            else if(scale == "fuma") CoeffScale = AmbDecScale::FuMa;
            else
                return make_error(linenum, "Unexpected coeff_scale: {}", scale);

            if(ChanMask > Ambi3OrderMask && CoeffScale == AmbDecScale::FuMa)
                return make_error(linenum, "FuMa not compatible with over third-order");
        }
        else if(command == "/opt/xover_freq")
        {
            istr >> XOverFreq;
        }
        else if(command == "/opt/xover_ratio")
        {
            istr >> XOverRatio;
        }
        else if(command == "/opt/input_scale" || command == "/opt/nfeff_comp"
            || command == "/opt/delay_comp" || command == "/opt/level_comp")
        {
            /* Unused */
            read_word(istr);
        }
        else if(command == "/speakers/{")
        {
            if(Speakers.empty())
                return make_error(linenum, "Speakers defined without a count");
            scope = ReaderScope::Speakers;
        }
        else if(command == "/lfmatrix/{" || command == "/hfmatrix/{" || command == "/matrix/{")
        {
            if(Speakers.empty())
                return make_error(linenum, "Matrix defined without a speaker count");
            if(!ChanMask)
                return make_error(linenum, "Matrix defined without a channel mask");

            if(Matrix.empty())
            {
                Matrix.resize(Speakers.size() * FreqBands);
                LFMatrix = std::span{Matrix}.first(Speakers.size());
                HFMatrix = std::span{Matrix}.subspan(Speakers.size()*(FreqBands-1));
            }

            if(FreqBands == 1)
            {
                if(command != "/matrix/{")
                    return make_error(linenum, "Unexpected \"{}\" for a single-band decoder",
                        command);
                scope = ReaderScope::HFMatrix;
            }
            else
            {
                if(command == "/lfmatrix/{")
                    scope = ReaderScope::LFMatrix;
                else if(command == "/hfmatrix/{")
                    scope = ReaderScope::HFMatrix;
                else
                    return make_error(linenum, "Unexpected \"{}\" for a dual-band decoder",
                        command);
            }
        }
        else if(command == "/end")
        {
            const auto endpos = static_cast<std::size_t>(istr.tellg());
            if(!is_at_end(buffer, endpos))
                return make_error(linenum, "Extra junk on end: {}",
                    std::string_view{buffer}.substr(endpos));

            if(speaker_pos < Speakers.size() || hfmatrix_pos < Speakers.size()
                || (FreqBands == 2 && lfmatrix_pos < Speakers.size()))
                return make_error(linenum, "Incomplete decoder definition");
            if(CoeffScale == AmbDecScale::Unset)
                return make_error(linenum, "No coefficient scaling defined");

            return std::nullopt;
        }
        else
            return make_error(linenum, "Unexpected command: {}", command);

        istr.clear();
        const auto endpos = static_cast<std::size_t>(istr.tellg());
        if(!is_at_end(buffer, endpos))
            return make_error(linenum, "Extra junk on line: {}",
                std::string_view{buffer}.substr(endpos));
        buffer.clear();
    }
    return make_error(linenum, "Unexpected end of file");
}
