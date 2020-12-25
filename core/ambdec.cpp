
#include "config.h"

#include "ambdec.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <sstream>
#include <string>

#include "alfstream.h"
#include "core/logging.h"


namespace {

template<typename T, std::size_t N>
constexpr inline std::size_t size(const T(&)[N]) noexcept
{ return N; }

int readline(std::istream &f, std::string &output)
{
    while(f.good() && f.peek() == '\n')
        f.ignore();

    return std::getline(f, output) && !output.empty();
}

bool read_clipped_line(std::istream &f, std::string &buffer)
{
    while(readline(f, buffer))
    {
        std::size_t pos{0};
        while(pos < buffer.length() && std::isspace(buffer[pos]))
            pos++;
        buffer.erase(0, pos);

        std::size_t cmtpos{buffer.find_first_of('#')};
        if(cmtpos < buffer.length())
            buffer.resize(cmtpos);
        while(!buffer.empty() && std::isspace(buffer.back()))
            buffer.pop_back();

        if(!buffer.empty())
            return true;
    }
    return false;
}


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
    return !(endpos < buffer.length());
}


bool load_ambdec_speakers(AmbDecConf::SpeakerConf *spkrs, const std::size_t num_speakers,
    std::istream &f, std::string &buffer)
{
    size_t cur_speaker{0};
    while(cur_speaker < num_speakers)
    {
        std::istringstream istr{buffer};

        std::string cmd{read_word(istr)};
        if(cmd.empty())
        {
            if(!read_clipped_line(f, buffer))
            {
                ERR("Unexpected end of file\n");
                return false;
            }
            continue;
        }

        if(cmd == "add_spkr")
        {
            AmbDecConf::SpeakerConf &spkr = spkrs[cur_speaker++];
            const size_t spkr_num{cur_speaker};

            istr >> spkr.Name;
            if(istr.fail()) WARN("Name not specified for speaker %zu\n", spkr_num);
            istr >> spkr.Distance;
            if(istr.fail()) WARN("Distance not specified for speaker %zu\n", spkr_num);
            istr >> spkr.Azimuth;
            if(istr.fail()) WARN("Azimuth not specified for speaker %zu\n", spkr_num);
            istr >> spkr.Elevation;
            if(istr.fail()) WARN("Elevation not specified for speaker %zu\n", spkr_num);
            istr >> spkr.Connection;
            if(istr.fail()) TRACE("Connection not specified for speaker %zu\n", spkr_num);
        }
        else
        {
            ERR("Unexpected speakers command: %s\n", cmd.c_str());
            return false;
        }

        istr.clear();
        const auto endpos = static_cast<std::size_t>(istr.tellg());
        if(!is_at_end(buffer, endpos))
        {
            ERR("Unexpected junk on line: %s\n", buffer.c_str()+endpos);
            return false;
        }
        buffer.clear();
    }

    return true;
}

bool load_ambdec_matrix(float (&gains)[MaxAmbiOrder+1], AmbDecConf::CoeffArray *matrix,
    const std::size_t maxrow, std::istream &f, std::string &buffer)
{
    bool gotgains{false};
    std::size_t cur{0u};
    while(cur < maxrow)
    {
        std::istringstream istr{buffer};

        std::string cmd{read_word(istr)};
        if(cmd.empty())
        {
            if(!read_clipped_line(f, buffer))
            {
                ERR("Unexpected end of file\n");
                return false;
            }
            continue;
        }

        if(cmd == "order_gain")
        {
            std::size_t curgain{0u};
            float value;
            while(istr.good())
            {
                istr >> value;
                if(istr.fail()) break;
                if(!istr.eof() && !std::isspace(istr.peek()))
                {
                    ERR("Extra junk on gain %zu: %s\n", curgain+1,
                        buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                    return false;
                }
                if(curgain < size(gains))
                    gains[curgain++] = value;
            }
            std::fill(std::begin(gains)+curgain, std::end(gains), 0.0f);
            gotgains = true;
        }
        else if(cmd == "add_row")
        {
            AmbDecConf::CoeffArray &mtxrow = matrix[cur++];
            std::size_t curidx{0u};
            float value{};
            while(istr.good())
            {
                istr >> value;
                if(istr.fail()) break;
                if(!istr.eof() && !std::isspace(istr.peek()))
                {
                    ERR("Extra junk on matrix element %zux%zu: %s\n", curidx,
                        cur, buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                    return false;
                }
                if(curidx < mtxrow.size())
                    mtxrow[curidx++] = value;
            }
            std::fill(mtxrow.begin()+curidx, mtxrow.end(), 0.0f);
        }
        else
        {
            ERR("Unexpected matrix command: %s\n", cmd.c_str());
            return false;
        }

        istr.clear();
        const auto endpos = static_cast<std::size_t>(istr.tellg());
        if(!is_at_end(buffer, endpos))
        {
            ERR("Unexpected junk on line: %s\n", buffer.c_str()+endpos);
            return false;
        }
        buffer.clear();
    }

    if(!gotgains)
    {
        ERR("Matrix order_gain not specified\n");
        return false;
    }

    return true;
}

} // namespace

int AmbDecConf::load(const char *fname) noexcept
{
    al::ifstream f{fname};
    if(!f.is_open())
    {
        ERR("Failed to open: %s\n", fname);
        return 0;
    }

    bool speakers_loaded{false};
    bool matrix_loaded{false};
    bool lfmatrix_loaded{false};
    std::size_t num_speakers{0u};
    std::string buffer;
    while(read_clipped_line(f, buffer))
    {
        std::istringstream istr{buffer};

        std::string command{read_word(istr)};
        if(command.empty())
        {
            ERR("Malformed line: %s\n", buffer.c_str());
            return 0;
        }

        if(command == "/description")
            istr >> Description;
        else if(command == "/version")
        {
            istr >> Version;
            if(!istr.eof() && !std::isspace(istr.peek()))
            {
                ERR("Extra junk after version: %s\n",
                    buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                return 0;
            }
            if(Version != 3)
            {
                ERR("Unsupported version: %u\n", Version);
                return 0;
            }
        }
        else if(command == "/dec/chan_mask")
        {
            if(ChanMask)
            {
                ERR("Duplicate chan_mask\n");
                return 0;
            }
            istr >> std::hex >> ChanMask >> std::dec;
            if(!istr.eof() && !std::isspace(istr.peek()))
            {
                ERR("Extra junk after mask: %s\n",
                    buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                return 0;
            }
            if(!ChanMask)
            {
                ERR("Invalid chan_mask: 0x%08x\n", ChanMask);
                return 0;
            }
        }
        else if(command == "/dec/freq_bands")
        {
            if(FreqBands)
            {
                ERR("Duplicate freq_bands\n");
                return 0;
            }
            istr >> FreqBands;
            if(!istr.eof() && !std::isspace(istr.peek()))
            {
                ERR("Extra junk after freq_bands: %s\n",
                    buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                return 0;
            }
            if(FreqBands != 1 && FreqBands != 2)
            {
                ERR("Invalid freq_bands value: %u\n", FreqBands);
                return 0;
            }
        }
        else if(command == "/dec/speakers")
        {
            if(num_speakers)
            {
                ERR("Duplicate speakers\n");
                return 0;
            }
            istr >> num_speakers;
            if(!istr.eof() && !std::isspace(istr.peek()))
            {
                ERR("Extra junk after speakers: %s\n",
                    buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                return 0;
            }
            if(!num_speakers)
            {
                ERR("Invalid speakers: %zu\n", num_speakers);
                return 0;
            }
            NumSpeakers = num_speakers;
            Speakers = std::make_unique<SpeakerConf[]>(num_speakers);
        }
        else if(command == "/dec/coeff_scale")
        {
            std::string scale = read_word(istr);
            if(scale == "n3d") CoeffScale = AmbDecScale::N3D;
            else if(scale == "sn3d") CoeffScale = AmbDecScale::SN3D;
            else if(scale == "fuma") CoeffScale = AmbDecScale::FuMa;
            else
            {
                ERR("Unsupported coeff scale: %s\n", scale.c_str());
                return 0;
            }
        }
        else if(command == "/opt/xover_freq")
        {
            istr >> XOverFreq;
            if(!istr.eof() && !std::isspace(istr.peek()))
            {
                ERR("Extra junk after xover_freq: %s\n",
                    buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                return 0;
            }
        }
        else if(command == "/opt/xover_ratio")
        {
            istr >> XOverRatio;
            if(!istr.eof() && !std::isspace(istr.peek()))
            {
                ERR("Extra junk after xover_ratio: %s\n",
                    buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                return 0;
            }
        }
        else if(command == "/opt/input_scale" || command == "/opt/nfeff_comp" ||
                command == "/opt/delay_comp" || command == "/opt/level_comp")
        {
            /* Unused */
            read_word(istr);
        }
        else if(command == "/speakers/{")
        {
            if(!num_speakers)
            {
                ERR("AmbDec speakers defined without a count\n");
                return 0;
            }
            const auto endpos = static_cast<std::size_t>(istr.tellg());
            if(!is_at_end(buffer, endpos))
            {
                ERR("Unexpected junk on line: %s\n", buffer.c_str()+endpos);
                return 0;
            }
            buffer.clear();

            if(!load_ambdec_speakers(Speakers.get(), num_speakers, f, buffer))
                return 0;
            speakers_loaded = true;

            if(!read_clipped_line(f, buffer))
            {
                ERR("Unexpected end of file\n");
                return 0;
            }
            std::istringstream istr2{buffer};
            std::string endmark{read_word(istr2)};
            if(endmark != "/}")
            {
                ERR("Expected /} after speaker definitions, got %s\n", endmark.c_str());
                return 0;
            }
            istr.swap(istr2);
        }
        else if(command == "/lfmatrix/{" || command == "/hfmatrix/{" || command == "/matrix/{")
        {
            if(!NumSpeakers)
            {
                ERR("AmbDec matrix defined without a count\n");
                return 0;
            }
            const auto endpos = static_cast<std::size_t>(istr.tellg());
            if(!is_at_end(buffer, endpos))
            {
                ERR("Unexpected junk on line: %s\n", buffer.c_str()+endpos);
                return 0;
            }
            buffer.clear();

            if(!Matrix)
            {
                Matrix = std::make_unique<CoeffArray[]>(NumSpeakers * FreqBands);
                LFMatrix = Matrix.get();
                HFMatrix = LFMatrix + NumSpeakers*(FreqBands-1);
            }

            if(FreqBands == 1)
            {
                if(command != "/matrix/{")
                {
                    ERR("Unexpected \"%s\" type for a single-band decoder\n", command.c_str());
                    return 0;
                }
                if(!load_ambdec_matrix(HFOrderGain, HFMatrix, num_speakers, f, buffer))
                    return 0;
                matrix_loaded = true;
            }
            else
            {
                if(command == "/lfmatrix/{")
                {
                    if(!load_ambdec_matrix(LFOrderGain, LFMatrix, num_speakers, f, buffer))
                        return 0;
                    lfmatrix_loaded = true;
                }
                else if(command == "/hfmatrix/{")
                {
                    if(!load_ambdec_matrix(HFOrderGain, HFMatrix, num_speakers, f, buffer))
                        return 0;
                    matrix_loaded = true;
                }
                else
                {
                    ERR("Unexpected \"%s\" type for a dual-band decoder\n", command.c_str());
                    return 0;
                }
            }

            if(!read_clipped_line(f, buffer))
            {
                ERR("Unexpected end of file\n");
                return 0;
            }
            std::istringstream istr2{buffer};
            std::string endmark{read_word(istr2)};
            if(endmark != "/}")
            {
                ERR("Expected /} after matrix definitions, got %s\n", endmark.c_str());
                return 0;
            }
            istr.swap(istr2);
        }
        else if(command == "/end")
        {
            const auto endpos = static_cast<std::size_t>(istr.tellg());
            if(!is_at_end(buffer, endpos))
            {
                ERR("Unexpected junk on end: %s\n", buffer.c_str()+endpos);
                return 0;
            }

            if(!speakers_loaded || !matrix_loaded || (FreqBands == 2 && !lfmatrix_loaded))
            {
                ERR("No decoder defined\n");
                return 0;
            }

            return 1;
        }
        else
        {
            ERR("Unexpected command: %s\n", command.c_str());
            return 0;
        }

        istr.clear();
        const auto endpos = static_cast<std::size_t>(istr.tellg());
        if(!is_at_end(buffer, endpos))
        {
            ERR("Unexpected junk on line: %s\n", buffer.c_str()+endpos);
            return 0;
        }
        buffer.clear();
    }
    ERR("Unexpected end of file\n");

    return 0;
}
