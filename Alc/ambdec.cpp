
#include "config.h"

#include "ambdec.h"

#include <cstring>
#include <cctype>

#include <limits>
#include <string>
#include <fstream>
#include <sstream>

#include "compat.h"


namespace {

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


bool load_ambdec_speakers(AmbDecConf *conf, std::istream &f, std::string &buffer)
{
    ALsizei cur = 0;
    while(cur < conf->NumSpeakers)
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
            istr >> conf->Speakers[cur].Name;
            if(istr.fail()) WARN("Name not specified for speaker %u\n", cur+1);
            istr >> conf->Speakers[cur].Distance;
            if(istr.fail()) WARN("Distance not specified for speaker %u\n", cur+1);
            istr >> conf->Speakers[cur].Azimuth;
            if(istr.fail()) WARN("Azimuth not specified for speaker %u\n", cur+1);
            istr >> conf->Speakers[cur].Elevation;
            if(istr.fail()) WARN("Elevation not specified for speaker %u\n", cur+1);
            istr >> conf->Speakers[cur].Connection;
            if(istr.fail()) TRACE("Connection not specified for speaker %u\n", cur+1);

            cur++;
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

bool load_ambdec_matrix(ALfloat *gains, ALfloat (*matrix)[MAX_AMBI_COEFFS], ALsizei maxrow, std::istream &f, std::string &buffer)
{
    bool gotgains = false;
    ALsizei cur = 0;
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
            ALuint curgain = 0;
            float value;
            while(istr.good())
            {
                istr >> value;
                if(istr.fail()) break;
                if(!istr.eof() && !std::isspace(istr.peek()))
                {
                    ERR("Extra junk on gain %u: %s\n", curgain+1,
                        buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                    return false;
                }
                if(curgain < MAX_AMBI_ORDER+1)
                    gains[curgain++] = value;
            }
            while(curgain < MAX_AMBI_ORDER+1)
                gains[curgain++] = 0.0f;
            gotgains = true;
        }
        else if(cmd == "add_row")
        {
            ALuint curidx = 0;
            float value;
            while(istr.good())
            {
                istr >> value;
                if(istr.fail()) break;
                if(!istr.eof() && !std::isspace(istr.peek()))
                {
                    ERR("Extra junk on matrix element %ux%u: %s\n", cur, curidx,
                        buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                    return false;
                }
                if(curidx < MAX_AMBI_COEFFS)
                    matrix[cur][curidx++] = value;
            }
            while(curidx < MAX_AMBI_COEFFS)
                matrix[cur][curidx++] = 0.0f;
            cur++;
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
            istr >> std::hex >> ChanMask >> std::dec;
            if(!istr.eof() && !std::isspace(istr.peek()))
            {
                ERR("Extra junk after mask: %s\n",
                    buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                return 0;
            }
        }
        else if(command == "/dec/freq_bands")
        {
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
            istr >> NumSpeakers;
            if(!istr.eof() && !std::isspace(istr.peek()))
            {
                ERR("Extra junk after speakers: %s\n",
                    buffer.c_str()+static_cast<std::size_t>(istr.tellg()));
                return 0;
            }
            if(NumSpeakers > MAX_OUTPUT_CHANNELS)
            {
                ERR("Unsupported speaker count: %u\n", NumSpeakers);
                return 0;
            }
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
            const auto endpos = static_cast<std::size_t>(istr.tellg());
            if(!is_at_end(buffer, endpos))
            {
                ERR("Unexpected junk on line: %s\n", buffer.c_str()+endpos);
                return 0;
            }
            buffer.clear();

            if(!load_ambdec_speakers(this, f, buffer))
                return 0;

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
            const auto endpos = static_cast<std::size_t>(istr.tellg());
            if(!is_at_end(buffer, endpos))
            {
                ERR("Unexpected junk on line: %s\n", buffer.c_str()+endpos);
                return 0;
            }
            buffer.clear();

            if(FreqBands == 1)
            {
                if(command != "/matrix/{")
                {
                    ERR("Unexpected \"%s\" type for a single-band decoder\n", command.c_str());
                    return 0;
                }
                if(!load_ambdec_matrix(HFOrderGain, HFMatrix, NumSpeakers, f, buffer))
                    return 0;
            }
            else
            {
                if(command == "/lfmatrix/{")
                {
                    if(!load_ambdec_matrix(LFOrderGain, LFMatrix, NumSpeakers, f, buffer))
                        return 0;
                }
                else if(command == "/hfmatrix/{")
                {
                    if(!load_ambdec_matrix(HFOrderGain, HFMatrix, NumSpeakers, f, buffer))
                        return 0;
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
