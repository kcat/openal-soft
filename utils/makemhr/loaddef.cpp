/*
 * HRTF utility for producing and demonstrating the process of creating an
 * OpenAL Soft compatible HRIR data set.
 *
 * Copyright (C) 2011-2019  Christopher Fitzgerald
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Or visit:  http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

#include "config.h"

#include "loaddef.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "albit.h"
#include "alnumeric.h"
#include "alstring.h"
#include "filesystem.h"
#include "fmt/base.h"
#include "fmt/ostream.h"
#include "makemhr.h"
#include "polyphase_resampler.h"
#include "sofa-support.h"

#include "mysofa.h"

#if HAVE_CXXMODULES
import gsl;
#else
#include "gsl/gsl"
#endif

namespace {

using namespace std::string_view_literals;

// Constants for accessing the token reader's ring buffer.
constexpr auto TRRingBits = 16u;
constexpr auto TRRingSize = 1u << TRRingBits;
constexpr auto TRRingMask = TRRingSize - 1u;

// The token reader's load interval in bytes.
constexpr auto TRLoadSize = TRRingSize >> 2u;

// Token reader state for parsing the data set definition.
struct TokenReaderT {
    std::istream &mIStream;
    std::string mName;
    unsigned    mLine{};
    unsigned    mColumn{};
    std::array<char, TRRingSize> mRing{};
    std::streamsize mIn{};
    std::streamsize mOut{};

    explicit TokenReaderT(std::istream &istream) noexcept : mIStream{istream} { }
    TokenReaderT(const TokenReaderT&) = default;
};


// The limits for the listener's head 'radius' in the data set definition.
constexpr auto MinRadius = 0.05;
constexpr auto MaxRadius = 0.15;

// The maximum number of channels that can be addressed for a WAVE file
// source listed in the data set definition.
constexpr auto MaxWaveChannels = 65535u;

// The limits to the byte size for a binary source listed in the definition
// file.
enum : unsigned {
    MinBinSize = 2,
    MaxBinSize = 4
};

// The limits to the number of significant bits for an ASCII source listed in
// the data set definition.
enum : unsigned {
    MinASCIIBits = 16,
    MaxASCIIBits = 32
};

// The four-character-codes for RIFF/RIFX WAVE file chunks.
enum : unsigned {
    FOURCC_RIFF = 0x46464952, // 'RIFF'
    FOURCC_RIFX = 0x58464952, // 'RIFX'
    FOURCC_WAVE = 0x45564157, // 'WAVE'
    FOURCC_FMT  = 0x20746D66, // 'fmt '
    FOURCC_DATA = 0x61746164, // 'data'
    FOURCC_LIST = 0x5453494C, // 'LIST'
    FOURCC_WAVL = 0x6C766177, // 'wavl'
    FOURCC_SLNT = 0x746E6C73, // 'slnt'
};

// The supported wave formats.
enum : unsigned {
    WAVE_FORMAT_PCM        = 0x0001,
    WAVE_FORMAT_IEEE_FLOAT = 0x0003,
    WAVE_FORMAT_EXTENSIBLE = 0xFFFE,
};


// Source format for the references listed in the data set definition.
enum SourceFormatT {
    SF_NONE,
    SF_ASCII,  // ASCII text file.
    SF_BIN_LE, // Little-endian binary file.
    SF_BIN_BE, // Big-endian binary file.
    SF_WAVE,   // RIFF/RIFX WAVE file.
    SF_SOFA    // Spatially Oriented Format for Accoustics (SOFA) file.
};

// Element types for the references listed in the data set definition.
enum ElementTypeT {
    ET_NONE,
    ET_INT,  // Integer elements.
    ET_FP    // Floating-point elements.
};

// Source reference state used when loading sources.
struct SourceRefT {
    SourceFormatT mFormat;
    ElementTypeT  mType;
    unsigned mSize;
    int  mBits;
    unsigned mChannel;
    double mAzimuth;
    double mElevation;
    double mRadius;
    unsigned mSkip;
    unsigned mOffset;
    std::string mPath;
};


/* Whitespace is not significant. It can process tokens as identifiers, numbers
 * (integer and floating-point), strings, and operators. Strings must be
 * encapsulated by double-quotes and cannot span multiple lines.
 */

// Setup the reader on the given file.  The filename can be NULL if no error
// output is desired.
void TrSetup(const std::span<const char> startbytes, const std::string_view filename,
    TokenReaderT *tr)
{
    tr->mName = filename.substr(std::max(filename.rfind('/')+1, filename.rfind('\\')+1));
    tr->mLine = 1;
    tr->mColumn = 1;
    tr->mIn = 0;
    tr->mOut = 0;

    if(!startbytes.empty())
    {
        Expects(startbytes.size() <= tr->mRing.size());
        std::ranges::copy(startbytes, tr->mRing.begin());
        tr->mIn += std::ssize(startbytes);
    }
}

// Prime the reader's ring buffer, and return a result indicating that there
// is text to process.
auto TrLoad(TokenReaderT *tr) -> bool
{
    auto &istream = tr->mIStream;

    auto toLoad = std::streamsize{TRRingSize} - (tr->mIn - tr->mOut);
    if(toLoad >= TRLoadSize && istream.good())
    {
        // Load TRLoadSize (or less if at the end of the file) per read.
        toLoad = TRLoadSize;

        const auto in = tr->mIn & std::streamsize{TRRingMask};
        const auto count = std::streamsize{TRRingSize} - in;
        if(count < toLoad)
        {
            istream.read(std::to_address(tr->mRing.begin() + in), count);
            tr->mIn += istream.gcount();
            istream.read(tr->mRing.data(), toLoad-count);
            tr->mIn += istream.gcount();
        }
        else
        {
            istream.read(std::to_address(tr->mRing.begin() + in), toLoad);
            tr->mIn += istream.gcount();
        }

        if(tr->mOut >= TRRingSize)
        {
            tr->mOut -= TRRingSize;
            tr->mIn -= TRRingSize;
        }
    }
    if(tr->mIn > tr->mOut)
        return true;
    return false;
}

// Error display routine.  Only displays when the base name is not NULL.
// Used to display an error at a saved line/column.
template<typename ...Args>
void TrErrorAt(TokenReaderT const *tr, unsigned const line, unsigned const column,
    fmt::format_string<Args...> fmt, Args&& ...args)
{
    if(tr->mName.empty())
        return;
    fmt::print(std::cerr, "\nError ({}:{}:{}): ", tr->mName, line, column);
    fmt::println(std::cerr, fmt, std::forward<Args>(args)...);
}

// Used to display an error at the current line/column.
template<typename ...Args>
void TrError(const TokenReaderT *tr, fmt::format_string<Args...> fmt, Args&& ...args)
{
    TrErrorAt(tr, tr->mLine, tr->mColumn, fmt, std::forward<Args>(args)...);
}

// Skips to the next line.
void TrSkipLine(TokenReaderT *tr)
{
    while(TrLoad(tr))
    {
        auto ch = tr->mRing[tr->mOut&TRRingMask];
        tr->mOut++;
        if(ch == '\n')
        {
            tr->mLine += 1;
            tr->mColumn = 1;
            break;
        }
        tr->mColumn += 1;
    }
}

// Skips to the next token.
auto TrSkipWhitespace(TokenReaderT *tr) -> bool
{
    while(TrLoad(tr))
    {
        const auto ch = tr->mRing[tr->mOut&TRRingMask];
        if(isspace(ch))
        {
            tr->mOut++;
            if(ch == '\n')
            {
                tr->mLine += 1;
                tr->mColumn = 1;
            }
            else
                tr->mColumn += 1;
        }
        else if(ch == '#')
            TrSkipLine(tr);
        else
            return true;
    }
    return false;
}

// Get the line and/or column of the next token (or the end of input).
void TrIndication(TokenReaderT *tr, unsigned *line, unsigned *column)
{
    TrSkipWhitespace(tr);
    if(line) *line = tr->mLine;
    if(column) *column = tr->mColumn;
}

// Checks to see if a token is (likely to be) an identifier.  It does not
// display any errors and will not proceed to the next token.
auto TrIsIdent(TokenReaderT *tr) -> bool
{
    if(!TrSkipWhitespace(tr))
        return false;
    const auto ch = tr->mRing[tr->mOut&TRRingMask];
    return ch == '_' || isalpha(ch);
}


// Checks to see if a token is the given operator.  It does not display any
// errors and will not proceed to the next token.
auto TrIsOperator(TokenReaderT *tr, const std::string_view op) -> bool
{
    if(!TrSkipWhitespace(tr))
        return false;
    auto out = tr->mOut;
    auto len = 0_uz;
    while(len < op.size() && out < tr->mIn)
    {
        if(tr->mRing[out&TRRingMask] != op[len])
            break;
        ++len;
        ++out;
    }
    return len == op.size();
}

/* The TrRead*() routines obtain the value of a matching token type.  They
 * display type, form, and boundary errors and will proceed to the next
 * token.
 */

// Reads and validates an identifier token.
auto TrReadIdent(TokenReaderT *tr) -> std::string
{
    auto ret = std::string{};
    auto col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        auto ch = char{tr->mRing[tr->mOut&TRRingMask]};
        if(ch == '_' || std::isalpha(ch))
        {
            do {
                ret += ch;
                tr->mColumn += 1;
                tr->mOut += 1;
                if(!TrLoad(tr))
                    break;
                ch = tr->mRing[tr->mOut&TRRingMask];
            } while(ch == '_' || std::isdigit(ch) || std::isalpha(ch));

            return ret;
        }
    }
    TrErrorAt(tr, tr->mLine, col, "Expected an identifier.");
    ret.clear();
    return ret;
}

// Reads and validates (including bounds) an integer token.
auto TrReadInt(TokenReaderT *tr, const int loBound, const int hiBound, int *value) -> bool
{
    auto col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        auto len = 0u;
        auto temp = std::array<char,64+1>{};
        auto ch = tr->mRing[tr->mOut&TRRingMask];
        if(ch == '+' || ch == '-')
        {
            temp[len] = ch;
            len++;
            tr->mOut++;
        }
        auto digis = 0u;
        while(TrLoad(tr))
        {
            ch = tr->mRing[tr->mOut&TRRingMask];
            if(!isdigit(ch)) break;
            if(len < 64)
                temp[len] = ch;
            len++;
            digis++;
            tr->mOut++;
        }
        tr->mColumn += len;
        if(digis > 0 && ch != '.' && !isalpha(ch))
        {
            if(len > 64)
            {
                TrErrorAt(tr, tr->mLine, col, "Integer is too long.");
                return false;
            }
            temp[len] = '\0';
            *value = static_cast<int>(strtol(temp.data(), nullptr, 10));
            if(*value < loBound || *value > hiBound)
            {
                TrErrorAt(tr, tr->mLine, col, "Expected a value from {} to {}.", loBound, hiBound);
                return false;
            }
            return true;
        }
    }
    TrErrorAt(tr, tr->mLine, col, "Expected an integer.");
    return false;
}

// Reads and validates (including bounds) a float token.
auto TrReadFloat(TokenReaderT *tr, const double loBound, const double hiBound, double *value)
    -> bool
{
    auto col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        auto temp = std::array<char,64+1>{};
        auto len = 0u;
        auto ch = tr->mRing[tr->mOut&TRRingMask];
        if(ch == '+' || ch == '-')
        {
            temp[len] = ch;
            len++;
            tr->mOut++;
        }

        auto digis = 0u;
        while(TrLoad(tr))
        {
            ch = tr->mRing[tr->mOut&TRRingMask];
            if(!isdigit(ch)) break;
            if(len < 64)
                temp[len] = ch;
            len++;
            digis++;
            tr->mOut++;
        }
        if(ch == '.')
        {
            if(len < 64)
                temp[len] = ch;
            len++;
            tr->mOut++;
        }
        while(TrLoad(tr))
        {
            ch = tr->mRing[tr->mOut&TRRingMask];
            if(!isdigit(ch)) break;
            if(len < 64)
                temp[len] = ch;
            len++;
            digis++;
            tr->mOut++;
        }
        if(digis > 0)
        {
            if(ch == 'E' || ch == 'e')
            {
                if(len < 64)
                    temp[len] = ch;
                len++;
                digis = 0;
                tr->mOut++;
                if(ch == '+' || ch == '-')
                {
                    if(len < 64)
                        temp[len] = ch;
                    len++;
                    tr->mOut++;
                }
                while(TrLoad(tr))
                {
                    ch = tr->mRing[tr->mOut&TRRingMask];
                    if(!isdigit(ch)) break;
                    if(len < 64)
                        temp[len] = ch;
                    len++;
                    digis++;
                    tr->mOut++;
                }
            }
            tr->mColumn += len;
            if(digis > 0 && ch != '.' && !isalpha(ch))
            {
                if(len > 64)
                {
                    TrErrorAt(tr, tr->mLine, col, "Float is too long.");
                    return false;
                }
                temp[len] = '\0';
                *value = strtod(temp.data(), nullptr);
                if(*value < loBound || *value > hiBound)
                {
                    TrErrorAt(tr, tr->mLine, col, "Expected a value from {:f} to {:f}.", loBound,
                        hiBound);
                    return false;
                }
                return true;
            }
        }
        else
            tr->mColumn += len;
    }
    TrErrorAt(tr, tr->mLine, col, "Expected a float.");
    return false;
}

// Reads and validates a string token.
auto TrReadString(TokenReaderT *tr) -> std::optional<std::string>
{
    auto ret = std::string{};

    auto col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        if(char ch{tr->mRing[tr->mOut&TRRingMask]}; ch == '\"')
        {
            tr->mOut++;
            auto len = 0_uz;
            while(TrLoad(tr))
            {
                ch = tr->mRing[tr->mOut&TRRingMask];
                tr->mOut++;
                if(ch == '\"')
                    break;
                if(ch == '\n')
                {
                    TrErrorAt(tr, tr->mLine, col, "Unterminated string at end of line.");
                    return std::nullopt;
                }
                ret += ch;
                len++;
            }
            if(ch != '\"')
            {
                tr->mColumn += static_cast<unsigned>(1 + len);
                TrErrorAt(tr, tr->mLine, col, "Unterminated string at end of input.");
                return std::nullopt;
            }
            tr->mColumn += static_cast<unsigned>(2 + len);

            return std::optional{std::move(ret)};
        }
    }
    TrErrorAt(tr, tr->mLine, col, "Expected a string.");
    return std::nullopt;
}

// Reads and validates the given operator.
auto TrReadOperator(TokenReaderT *tr, const std::string_view op) -> bool
{
    auto col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        auto len = 0_uz;
        while(len < op.size() && TrLoad(tr))
        {
            if(tr->mRing[tr->mOut&TRRingMask] != op[len])
                break;
            ++len;
            tr->mOut += 1;
        }
        tr->mColumn += static_cast<unsigned>(len);
        if(len == op.size())
            return true;
    }
    TrErrorAt(tr, tr->mLine, col, "Expected '{}' operator.", op);
    return false;
}


/*************************
 *** File source input ***
 *************************/

// Read a binary value of the specified byte order and byte size from a file,
// storing it as a 32-bit unsigned integer.
auto ReadBin4(std::istream &istream, std::string_view const filename, std::endian const order,
    unsigned const bytes, uint32_t *out) -> bool
{
    auto in = std::array<char,4>{};
    istream.read(in.data(), static_cast<int>(bytes));
    if(istream.gcount() != static_cast<int>(bytes))
    {
        fmt::println(std::cerr, "\nError: Bad read from file '{}'.", filename);
        return false;
    }

    auto tmpval = std::bit_cast<uint32_t>(in);
    if constexpr(std::endian::native == std::endian::little)
    {
        if(order != std::endian::little)
            tmpval = al::byteswap(tmpval) >> ((4-bytes)*8);
    }
    else
    {
        if(order != std::endian::big)
            tmpval = al::byteswap(tmpval);
        else
            tmpval >>= ((4-bytes)*8);
    }
    *out = tmpval;
    return true;
}

// Read a binary value of the specified byte order from a file, storing it as
// a 64-bit unsigned integer.
auto ReadBin8(std::istream &istream, const std::string_view filename, const std::endian order,
    uint64_t *out) -> bool
{
    auto in = std::array<char,8>{};
    istream.read(in.data(), in.size());
    if(istream.gcount() != in.size())
    {
        fmt::println(std::cerr, "\nError: Bad read from file '{}'.", filename);
        return false;
    }

    auto tmpval = std::bit_cast<uint64_t>(in);
    if(order != std::endian::native)
        tmpval = al::byteswap(tmpval);
    *out = tmpval;
    return true;
}

/* Read a binary value of the specified type, byte order, and byte size from
 * a file, converting it to a double.  For integer types, the significant
 * bits are used to normalize the result.  The sign of bits determines
 * whether they are padded toward the MSB (negative) or LSB (positive).
 * Floating-point types are not normalized.
 */
auto ReadBinAsDouble(std::istream &istream, std::string_view const filename,
    std::endian const order, ElementTypeT const type, unsigned const bytes, int const bits,
    double *out) -> bool
{
    *out = 0.0;
    if(bytes > 4)
    {
        uint64_t val{};
        if(!ReadBin8(istream, filename, order, &val))
            return false;
        if(type == ET_FP)
            *out = std::bit_cast<double>(val);
    }
    else
    {
        uint32_t val{};
        if(!ReadBin4(istream, filename, order, bytes, &val))
            return false;
        if(type == ET_FP)
            *out = std::bit_cast<float>(val);
        else
        {
            if(bits > 0)
                val >>= (8*bytes) - static_cast<unsigned>(bits);
            else
                val &= (0xFFFFFFFF >> (32+bits));

            if(val&static_cast<unsigned>(1<<(std::abs(bits)-1)))
                val |= (0xFFFFFFFF << std::abs(bits));
            *out = static_cast<int32_t>(val) / static_cast<double>(1<<(std::abs(bits)-1));
        }
    }
    return true;
}

/* Read an ascii value of the specified type from a file, converting it to a
 * double.  For integer types, the significant bits are used to normalize the
 * result.  The sign of the bits should always be positive.  This also skips
 * up to one separator character before the element itself.
 */
auto ReadAsciiAsDouble(TokenReaderT *tr, std::string_view const filename, ElementTypeT const type,
    unsigned const bits, double *out) -> bool
{
    if(TrIsOperator(tr, ","))
        TrReadOperator(tr, ",");
    else if(TrIsOperator(tr, ":"))
        TrReadOperator(tr, ":");
    else if(TrIsOperator(tr, ";"))
        TrReadOperator(tr, ";");
    else if(TrIsOperator(tr, "|"))
        TrReadOperator(tr, "|");

    if(type == ET_FP)
    {
        if(!TrReadFloat(tr, -std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(), out))
        {
            fmt::println(std::cerr, "\nError: Bad read from file '{}'.", filename);
            return false;
        }
    }
    else
    {
        auto v = int{};
        if(!TrReadInt(tr, -(1<<(bits-1)), (1<<(bits-1))-1, &v))
        {
            fmt::println(std::cerr, "\nError: Bad read from file '{}'.", filename);
            return false;
        }
        *out = v / static_cast<double>((1<<(bits-1))-1);
    }
    return true;
}

// Read the RIFF/RIFX WAVE format chunk from a file, validating it against
// the source parameters and data set metrics.
auto ReadWaveFormat(std::istream &istream, std::endian const order, unsigned const hrirRate,
    SourceRefT *src) -> bool
{
    auto fourCC = uint32_t{};
    auto chunkSize = uint32_t{0u};
    do {
        if(chunkSize > 0)
            istream.seekg(static_cast<int>(chunkSize), std::ios::cur);
        if(!ReadBin4(istream, src->mPath, std::endian::little, 4, &fourCC)
            || !ReadBin4(istream, src->mPath, order, 4, &chunkSize))
            return false;
    } while(fourCC != FOURCC_FMT);

    auto format = uint32_t{};
    auto channels = uint32_t{};
    auto rate = uint32_t{};
    auto dummy = uint32_t{};
    auto block = uint32_t{};
    auto size = uint32_t{};
    auto bits = uint32_t{};
    if(!ReadBin4(istream, src->mPath, order, 2, &format)
        || !ReadBin4(istream, src->mPath, order, 2, &channels)
        || !ReadBin4(istream, src->mPath, order, 4, &rate)
        || !ReadBin4(istream, src->mPath, order, 4, &dummy)
        || !ReadBin4(istream, src->mPath, order, 2, &block))
        return false;
    block /= channels;
    if(chunkSize > 14)
    {
        if(!ReadBin4(istream, src->mPath, order, 2, &size))
            return false;
        size = std::max(size/8, block);
    }
    else
        size = block;
    if(format == WAVE_FORMAT_EXTENSIBLE)
    {
        istream.seekg(2, std::ios::cur);
        if(!ReadBin4(istream, src->mPath, order, 2, &bits))
            return false;
        if(bits == 0)
            bits = 8 * size;
        istream.seekg(4, std::ios::cur);
        if(!ReadBin4(istream, src->mPath, order, 2, &format))
            return false;
        istream.seekg(static_cast<int>(chunkSize - 26), std::ios::cur);
    }
    else
    {
        bits = 8 * size;
        if(chunkSize > 14)
            istream.seekg(static_cast<int>(chunkSize - 16), std::ios::cur);
        else
            istream.seekg(static_cast<int>(chunkSize - 14), std::ios::cur);
    }
    if(format != WAVE_FORMAT_PCM && format != WAVE_FORMAT_IEEE_FLOAT)
    {
        fmt::println(std::cerr, "\nError: Unsupported WAVE format in file '{}'.", src->mPath);
        return false;
    }
    if(src->mChannel >= channels)
    {
        fmt::println(std::cerr, "\nError: Missing source channel in WAVE file '{}'.", src->mPath);
        return false;
    }
    if(rate != hrirRate)
    {
        fmt::println(std::cerr, "\nError: Mismatched source sample rate in WAVE file '{}'.",
            src->mPath);
        return false;
    }
    if(format == WAVE_FORMAT_PCM)
    {
        if(size < 2 || size > 4)
        {
            fmt::println(std::cerr, "\nError: Unsupported sample size in WAVE file '{}'.",
                src->mPath);
            return false;
        }
        if(bits < 16 || bits > (8*size))
        {
            fmt::println(std::cerr, "\nError: Bad significant bits in WAVE file '{}'.",
                src->mPath);
            return false;
        }
        src->mType = ET_INT;
    }
    else
    {
        if(size != 4 && size != 8)
        {
            fmt::println(std::cerr, "\nError: Unsupported sample size in WAVE file '{}'.",
                src->mPath);
            return false;
        }
        src->mType = ET_FP;
    }
    src->mSize = size;
    src->mBits = static_cast<int>(bits);
    src->mSkip = channels;
    return true;
}

// Read a RIFF/RIFX WAVE data chunk, converting all elements to doubles.
auto ReadWaveData(std::istream &istream, const SourceRefT *src, const std::endian order,
    const std::span<double> hrir) -> bool
{
    auto pre = static_cast<int>(src->mSize * src->mChannel);
    auto post = static_cast<int>(src->mSize * (src->mSkip - src->mChannel - 1));
    auto skip = 0;
    for(size_t i{0};i < hrir.size();++i)
    {
        skip += pre;
        if(skip > 0)
            istream.seekg(skip, std::ios::cur);
        if(!ReadBinAsDouble(istream, src->mPath, order, src->mType, src->mSize, src->mBits,
            &hrir[i]))
            return false;
        skip = post;
    }
    if(skip > 0)
        istream.seekg(skip, std::ios::cur);
    return true;
}

// Read the RIFF/RIFX WAVE list or data chunk, converting all elements to
// doubles.
auto ReadWaveList(std::istream &istream, const SourceRefT *src, const std::endian order,
    const std::span<double> hrir) -> int
{
    auto chunkSize = uint32_t{};
    for(;;)
    {
        auto fourCC = uint32_t{};
        if(!ReadBin4(istream, src->mPath, std::endian::little, 4, &fourCC)
            || !ReadBin4(istream, src->mPath, order, 4, &chunkSize))
            return false;

        if(fourCC == FOURCC_DATA)
        {
            const auto block = src->mSize * src->mSkip;
            if(chunkSize / block < (src->mOffset + hrir.size()))
            {
                fmt::println(std::cerr, "\nError: Bad read from file '{}'.", src->mPath);
                return false;
            }
            using off_type = std::istream::off_type;
            istream.seekg(gsl::narrow_cast<off_type>(size_t{src->mOffset} * block), std::ios::cur);
            if(!ReadWaveData(istream, src, order, hrir))
                return false;
            return true;
        }
        if(fourCC == FOURCC_LIST)
        {
            if(!ReadBin4(istream, src->mPath, std::endian::little, 4, &fourCC))
                return false;
            chunkSize -= 4;
            if(fourCC == FOURCC_WAVL)
                break;
        }
        if(chunkSize > 0)
            istream.seekg(static_cast<long>(chunkSize), std::ios::cur);
    }
    auto listSize = chunkSize;
    auto const block = src->mSize * src->mSkip;
    auto skip = src->mOffset;
    auto offset = 0u;
    auto lastSample = 0.0;
    while(offset < hrir.size() && listSize > 8)
    {
        auto fourCC = uint32_t{};
        if(!ReadBin4(istream, src->mPath, std::endian::little, 4, &fourCC)
            || !ReadBin4(istream, src->mPath, order, 4, &chunkSize))
            return false;
        listSize -= 8 + chunkSize;
        if(fourCC == FOURCC_DATA)
        {
            auto count = chunkSize / block;
            if(count > skip)
            {
                using off_type = std::istream::off_type;
                istream.seekg(gsl::narrow_cast<off_type>(size_t{skip} * block), std::ios::cur);
                chunkSize -= skip * block;
                count -= skip;
                skip = 0;
                if(count > (hrir.size() - offset))
                    count = static_cast<unsigned>(hrir.size() - offset);
                if(!ReadWaveData(istream, src, order, hrir.subspan(offset, count)))
                    return 0;
                chunkSize -= count * block;
                offset += count;
                lastSample = hrir[offset - 1];
            }
            else
                skip -= count;
        }
        else if(fourCC == FOURCC_SLNT)
        {
            auto count = uint32_t{};
            if(!ReadBin4(istream, src->mPath, order, 4, &count))
                return 0;
            chunkSize -= 4;
            if(count > skip)
            {
                count -= skip;
                skip = 0;
                if(count > (hrir.size() - offset))
                    count = static_cast<unsigned>(hrir.size() - offset);
                std::ranges::fill(hrir | std::views::drop(offset) | std::views::take(count),
                    lastSample);
                offset += count;
            }
            else
                skip -= count;
        }
        if(chunkSize > 0)
            istream.seekg(static_cast<long>(chunkSize), std::ios::cur);
    }
    if(offset < hrir.size())
    {
        fmt::println(std::cerr, "\nError: Bad read from file '{}'.", src->mPath);
        return false;
    }
    return true;
}

// Load a source HRIR from an ASCII text file containing a list of elements
// separated by whitespace or common list operators (',', ';', ':', '|').
auto LoadAsciiSource(std::istream &istream, const SourceRefT *src, const std::span<double> hrir)
    -> bool
{
    auto tr = TokenReaderT{istream};

    TrSetup({}, {}, &tr);
    for(auto i=0u;i < src->mOffset;++i)
    {
        auto dummy = double{};
        if(!ReadAsciiAsDouble(&tr, src->mPath, src->mType, static_cast<unsigned>(src->mBits),
            &dummy))
            return false;
    }
    for(size_t i{0_uz};i < hrir.size();++i)
    {
        if(!ReadAsciiAsDouble(&tr, src->mPath, src->mType, static_cast<unsigned>(src->mBits),
            &hrir[i]))
            return false;
        for(auto j=0u;j < src->mSkip;++j)
        {
            auto dummy = double{};
            if(!ReadAsciiAsDouble(&tr, src->mPath, src->mType, static_cast<unsigned>(src->mBits),
                &dummy))
                return false;
        }
    }
    return true;
}

// Load a source HRIR from a binary file.
auto LoadBinarySource(std::istream &istream, const SourceRefT *src, const std::endian order,
    const std::span<double> hrir) -> bool
{
    istream.seekg(static_cast<long>(src->mOffset), std::ios::beg);
    for(size_t i{0_uz};i < hrir.size();++i)
    {
        if(!ReadBinAsDouble(istream, src->mPath, order, src->mType, src->mSize, src->mBits,
            &hrir[i]))
            return false;
        if(src->mSkip > 0)
            istream.seekg(static_cast<long>(src->mSkip), std::ios::cur);
    }
    return true;
}

// Load a source HRIR from a RIFF/RIFX WAVE file.
auto LoadWaveSource(std::istream &istream, SourceRefT *src, unsigned const hrirRate,
    std::span<double> const hrir) -> bool
{
    auto fourCC = uint32_t{};
    auto dummy = uint32_t{};
    auto order = std::endian{};

    if(!ReadBin4(istream, src->mPath, std::endian::little, 4, &fourCC)
        || !ReadBin4(istream, src->mPath, std::endian::little, 4, &dummy))
        return false;
    if(fourCC == FOURCC_RIFF)
        order = std::endian::little;
    else if(fourCC == FOURCC_RIFX)
        order = std::endian::big;
    else
    {
        fmt::println(std::cerr, "\nError: No RIFF/RIFX chunk in file '{}'.", src->mPath);
        return false;
    }

    if(!ReadBin4(istream, src->mPath, std::endian::little, 4, &fourCC))
        return false;
    if(fourCC != FOURCC_WAVE)
    {
        fmt::println(std::cerr, "\nError: Not a RIFF/RIFX WAVE file '{}'.", src->mPath);
        return false;
    }
    if(!ReadWaveFormat(istream, order, hrirRate, src))
        return false;
    if(!ReadWaveList(istream, src, order, hrir))
        return false;
    return true;
}


struct SofaEasyDeleter {
    void operator()(gsl::owner<MYSOFA_EASY*> sofa) const
    {
        if(sofa->neighborhood) mysofa_neighborhood_free(sofa->neighborhood);
        if(sofa->lookup) mysofa_lookup_free(sofa->lookup);
        if(sofa->hrtf) mysofa_free(sofa->hrtf);
        delete sofa;
    }
};
using SofaEasyPtr = std::unique_ptr<MYSOFA_EASY,SofaEasyDeleter>;

struct SofaCacheEntry {
    std::string mName;
    unsigned mSampleRate{};
    SofaEasyPtr mSofa;
};
std::vector<SofaCacheEntry> gSofaCache;

// Load a Spatially Oriented Format for Accoustics (SOFA) file.
auto LoadSofaFile(SourceRefT *src, unsigned const hrirRate, unsigned const n) -> MYSOFA_EASY*
{
    const auto srcname = std::string_view{src->mPath};
    auto iter = std::ranges::find_if(gSofaCache, [srcname,hrirRate](SofaCacheEntry &entry) -> bool
        { return entry.mName == srcname && entry.mSampleRate == hrirRate; });
    if(iter != gSofaCache.end()) return iter->mSofa.get();

    auto sofa = SofaEasyPtr{new(std::nothrow) MYSOFA_EASY{}};
    if(!sofa)
    {
        fmt::println(std::cerr, "\nError:  Out of memory.");
        return nullptr;
    }
    sofa->lookup = nullptr;
    sofa->neighborhood = nullptr;

    auto err = int{};
    sofa->hrtf = mysofa_load(src->mPath.c_str(), &err);
    if(!sofa->hrtf)
    {
        fmt::println(std::cerr, "\nError: Could not load source file '{}': {} ({}).", src->mPath,
            SofaErrorStr(err), err);
        return nullptr;
    }
    /* NOTE: Some valid SOFA files are failing this check. */
    err = mysofa_check(sofa->hrtf);
    if(err != MYSOFA_OK)
        fmt::println(std::cerr, "\nWarning: Supposedly malformed source file '{}': {} ({}).",
            src->mPath, SofaErrorStr(err), err);
    if((src->mOffset + n) > sofa->hrtf->N)
    {
        fmt::println(std::cerr, "\nError: Not enough samples in SOFA file '{}'.", src->mPath);
        return nullptr;
    }
    if(src->mChannel >= sofa->hrtf->R)
    {
        fmt::println(std::cerr, "\nError: Missing source receiver in SOFA file '{}'.", src->mPath);
        return nullptr;
    }
    mysofa_tocartesian(sofa->hrtf);
    sofa->lookup = mysofa_lookup_init(sofa->hrtf);
    if(sofa->lookup == nullptr)
    {
        fmt::println(std::cerr, "\nError:  Out of memory.");
        return nullptr;
    }
    gSofaCache.emplace_back(SofaCacheEntry{std::string{srcname}, hrirRate, std::move(sofa)});
    return gSofaCache.back().mSofa.get();
}

// Copies the HRIR data from a particular SOFA measurement.
void ExtractSofaHrir(const MYSOFA_HRTF *hrtf, const size_t index, const size_t channel,
    const size_t offset, const std::span<double> hrir)
{
    const auto irValues = std::span{hrtf->DataIR.values, hrtf->DataIR.elements}
        .subspan((index*hrtf->R + channel)*hrtf->N + offset);
    std::ranges::copy(irValues | std::views::take(hrir.size()), hrir.begin());
}

// Load a source HRIR from a Spatially Oriented Format for Accoustics (SOFA)
// file.
auto LoadSofaSource(SourceRefT *src, unsigned const hrirRate, std::span<double> const hrir) -> bool
{
    auto *sofa = LoadSofaFile(src, hrirRate, static_cast<unsigned>(hrir.size()));
    if(sofa == nullptr) return false;

    /* NOTE: At some point it may be beneficial or necessary to consider the
             various coordinate systems, listener/source orientations, and
             directional vectors defined in the SOFA file.
    */
    auto target = std::array{
        static_cast<float>(src->mAzimuth),
        static_cast<float>(src->mElevation),
        static_cast<float>(src->mRadius)
    };
    mysofa_s2c(target.data());

    auto nearest = mysofa_lookup(sofa->lookup, target.data());
    if(nearest < 0)
    {
        fmt::println(std::cerr, "\nError: Lookup failed in source file '{}'.", src->mPath);
        return false;
    }

    auto coords = std::span{sofa->hrtf->SourcePosition.values, sofa->hrtf->M*3_uz}
        .subspan(static_cast<unsigned>(nearest)*3_uz).first<3>();
    if(std::abs(coords[0] - target[0]) > 0.001 || std::abs(coords[1] - target[1]) > 0.001
        || std::abs(coords[2] - target[2]) > 0.001)
    {
        fmt::println(std::cerr,
            "\nError: No impulse response at coordinates ({:.3f}r, {:.1f}ev, {:.1f}az) in file '{}'.",
            src->mRadius, src->mElevation, src->mAzimuth, src->mPath);
        target[0] = coords[0];
        target[1] = coords[1];
        target[2] = coords[2];
        mysofa_c2s(target.data());
        fmt::println(std::cerr, "       Nearest candidate at ({:.3f}r, {:.1f}ev, {:.1f}az).",
            target[2], target[1], target[0]);
        return false;
    }

    ExtractSofaHrir(sofa->hrtf, static_cast<unsigned>(nearest), src->mChannel, src->mOffset, hrir);

    return true;
}

// Load a source HRIR from a supported file type.
auto LoadSource(SourceRefT *src, unsigned const hrirRate, std::span<double> const hrir) -> bool
{
    auto istream = fs::ifstream{};
    if(src->mFormat != SF_SOFA)
    {
        if(src->mFormat == SF_ASCII)
            istream.open(fs::path(al::char_as_u8(src->mPath)));
        else
            istream.open(fs::path(al::char_as_u8(src->mPath)), std::ios::binary);
        if(!istream.good())
        {
            fmt::println(std::cerr, "\nError: Could not open source file '{}'.", src->mPath);
            return false;
        }
    }

    switch(src->mFormat)
    {
        case SF_ASCII: return LoadAsciiSource(istream, src, hrir);
        case SF_BIN_LE: return LoadBinarySource(istream, src, std::endian::little, hrir);
        case SF_BIN_BE: return LoadBinarySource(istream, src, std::endian::big, hrir);
        case SF_WAVE: return LoadWaveSource(istream, src, hrirRate, hrir);
        case SF_SOFA: return LoadSofaSource(src, hrirRate, hrir);
        case SF_NONE: break;
    }
    return false;
}


// Match the channel type from a given identifier.
auto MatchChannelType(const std::string_view ident) -> ChannelTypeT
{
    if(al::case_compare(ident, "mono"sv) == 0)
        return CT_MONO;
    if(al::case_compare(ident, "stereo"sv) == 0)
        return CT_STEREO;
    return CT_NONE;
}


// Process the data set definition to read and validate the data set metrics.
auto ProcessMetrics(TokenReaderT *tr, unsigned const fftSize, unsigned const truncSize,
    ChannelModeT const chanMode, HrirDataT *hData) -> bool
{
    auto hasRate = 0;
    auto hasType = 0;
    auto hasPoints = 0;
    auto hasRadius = 0;
    auto hasDistance = 0;
    auto hasAzimuths = 0;
    auto line = unsigned{};
    auto col = unsigned{};
    auto fpVal = double{};
    auto points = unsigned{};
    auto intVal = int{};
    auto distances = std::array<double, MAX_FD_COUNT>{};
    auto fdCount = 0u;
    auto evCounts = std::array<unsigned, MAX_FD_COUNT>{};
    auto azCounts = std::vector<std::array<unsigned, MAX_EV_COUNT>>(MAX_FD_COUNT);
    std::ranges::fill(azCounts | std::views::join, 0u);

    TrIndication(tr, &line, &col);
    while(TrIsIdent(tr))
    {
        TrIndication(tr, &line, &col);
        const auto ident = TrReadIdent(tr);
        if(ident.empty())
            return false;
        if(al::case_compare(ident, "rate"sv) == 0)
        {
            if(hasRate)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'rate'.");
                return false;
            }
            if(!TrReadOperator(tr, "="))
                return false;
            if(!TrReadInt(tr, MIN_RATE, MAX_RATE, &intVal))
                return false;
            hData->mIrRate = static_cast<unsigned>(intVal);
            hasRate = 1;
        }
        else if(al::case_compare(ident, "type"sv) == 0)
        {
            if(hasType)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'type'.");
                return false;
            }
            if(!TrReadOperator(tr, "="))
                return false;

            const auto type = TrReadIdent(tr);
            if(type.empty())
                return false;
            hData->mChannelType = MatchChannelType(type);
            if(hData->mChannelType == CT_NONE)
            {
                TrErrorAt(tr, line, col, "Expected a channel type.");
                return false;
            }
            if(hData->mChannelType == CT_STEREO)
            {
                if(chanMode == CM_ForceMono)
                    hData->mChannelType = CT_MONO;
            }
            hasType = 1;
        }
        else if(al::case_compare(ident, "points"sv) == 0)
        {
            if(hasPoints)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'points'.");
                return false;
            }
            if(!TrReadOperator(tr, "="))
                return false;
            TrIndication(tr, &line, &col);
            if(!TrReadInt(tr, MIN_POINTS, MAX_POINTS, &intVal))
                return false;
            points = static_cast<unsigned>(intVal);
            if(fftSize > 0 && points > fftSize)
            {
                TrErrorAt(tr, line, col, "Value exceeds the overridden FFT size.");
                return false;
            }
            if(points < truncSize)
            {
                TrErrorAt(tr, line, col, "Value is below the truncation size.");
                return false;
            }
            hData->mIrPoints = points;
            hData->mFftSize = fftSize;
            hData->mIrSize = std::max(points, 1u + (fftSize/2u));
            hasPoints = 1;
        }
        else if(al::case_compare(ident, "radius"sv) == 0)
        {
            if(hasRadius)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'radius'.");
                return false;
            }
            if(!TrReadOperator(tr, "="))
                return false;
            if(!TrReadFloat(tr, MinRadius, MaxRadius, &fpVal))
                return false;
            hData->mRadius = fpVal;
            hasRadius = 1;
        }
        else if(al::case_compare(ident, "distance"sv) == 0)
        {
            auto count = 0u;

            if(hasDistance)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'distance'.");
                return false;
            }
            if(!TrReadOperator(tr, "="))
                return false;

            for(;;)
            {
                if(!TrReadFloat(tr, MIN_DISTANCE, MAX_DISTANCE, &fpVal))
                    return false;
                if(count > 0 && fpVal <= distances[count - 1])
                {
                    TrError(tr, "Distances are not ascending.");
                    return false;
                }
                distances[count++] = fpVal;
                if(!TrIsOperator(tr, ","))
                    break;
                if(count >= MAX_FD_COUNT)
                {
                    TrError(tr, "Exceeded the maximum of {} fields.", MAX_FD_COUNT);
                    return false;
                }
                TrReadOperator(tr, ",");
            }
            if(fdCount != 0 && count != fdCount)
            {
                TrError(tr, "Did not match the specified number of {} fields.", fdCount);
                return false;
            }
            fdCount = count;
            hasDistance = 1;
        }
        else if(al::case_compare(ident, "azimuths"sv) == 0)
        {
            auto count = 0u;

            if(hasAzimuths)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'azimuths'.");
                return false;
            }
            if(!TrReadOperator(tr, "="))
                return false;

            evCounts[0] = 0;
            for(;;)
            {
                if(!TrReadInt(tr, MIN_AZ_COUNT, MAX_AZ_COUNT, &intVal))
                    return false;
                azCounts[count][evCounts[count]++] = static_cast<unsigned>(intVal);
                if(TrIsOperator(tr, ","))
                {
                    if(evCounts[count] >= MAX_EV_COUNT)
                    {
                        TrError(tr, "Exceeded the maximum of {} elevations.", MAX_EV_COUNT);
                        return false;
                    }
                    TrReadOperator(tr, ",");
                }
                else
                {
                    if(evCounts[count] < MIN_EV_COUNT)
                    {
                        TrErrorAt(tr, line, col, "Did not reach the minimum of {} azimuth counts.",
                            MIN_EV_COUNT);
                        return false;
                    }
                    if(azCounts[count][0] != 1 || azCounts[count][evCounts[count] - 1] != 1)
                    {
                        TrError(tr, "Poles are not singular for field {}.", count - 1);
                        return false;
                    }
                    count++;
                    if(!TrIsOperator(tr, ";"))
                        break;

                    if(count >= MAX_FD_COUNT)
                    {
                        TrError(tr, "Exceeded the maximum number of %d fields.", MAX_FD_COUNT);
                        return false;
                    }
                    evCounts[count] = 0;
                    TrReadOperator(tr, ";");
                }
            }
            if(fdCount != 0 && count != fdCount)
            {
                TrError(tr, "Did not match the specified number of %d fields.", fdCount);
                return false;
            }
            fdCount = count;
            hasAzimuths = 1;
        }
        else
        {
            TrErrorAt(tr, line, col, "Expected a metric name.");
            return false;
        }
        TrSkipWhitespace(tr);
    }
    if(!(hasRate && hasPoints && hasRadius && hasDistance && hasAzimuths))
    {
        TrErrorAt(tr, line, col, "Expected a metric name.");
        return false;
    }
    if(distances[0] < hData->mRadius)
    {
        TrError(tr, "Distance cannot start below head radius.");
        return false;
    }
    if(hData->mChannelType == CT_NONE)
        hData->mChannelType = CT_MONO;
    const auto azs = std::span{azCounts}.first<MAX_FD_COUNT>();
    if(!PrepareHrirData(std::span{distances}.first(fdCount), evCounts, azs, hData))
    {
        fmt::println(std::cerr, "Error:  Out of memory.");
        exit(-1);
    }
    return true;
}

// Parse an index triplet from the data set definition.
auto ReadIndexTriplet(TokenReaderT *tr, HrirDataT const *hData, unsigned *fi, unsigned *ei,
    unsigned *ai) -> bool
{
    auto intVal = int{};

    if(hData->mFds.size() > 1)
    {
        if(!TrReadInt(tr, 0, static_cast<int>(hData->mFds.size()-1), &intVal))
            return false;
        *fi = static_cast<unsigned>(intVal);
        if(!TrReadOperator(tr, ","))
            return false;
    }
    else
    {
        *fi = 0;
    }
    if(!TrReadInt(tr, 0, static_cast<int>(hData->mFds[*fi].mEvs.size()-1), &intVal))
        return false;
    *ei = static_cast<unsigned>(intVal);
    if(!TrReadOperator(tr, ","))
        return false;
    if(!TrReadInt(tr, 0, static_cast<int>(hData->mFds[*fi].mEvs[*ei].mAzs.size()-1), &intVal))
        return false;
    *ai = static_cast<unsigned>(intVal);
    return true;
}

// Match the source format from a given identifier.
auto MatchSourceFormat(const std::string_view ident) -> SourceFormatT
{
    if(al::case_compare(ident, "ascii"sv) == 0)
        return SF_ASCII;
    if(al::case_compare(ident, "bin_le"sv) == 0)
        return SF_BIN_LE;
    if(al::case_compare(ident, "bin_be"sv) == 0)
        return SF_BIN_BE;
    if(al::case_compare(ident, "wave"sv) == 0)
        return SF_WAVE;
    if(al::case_compare(ident, "sofa"sv) == 0)
        return SF_SOFA;
    return SF_NONE;
}

// Match the source element type from a given identifier.
auto MatchElementType(const std::string_view ident) -> ElementTypeT
{
    if(al::case_compare(ident, "int"sv) == 0)
        return ET_INT;
    if(al::case_compare(ident, "fp"sv) == 0)
        return ET_FP;
    return ET_NONE;
}

// Parse and validate a source reference from the data set definition.
auto ReadSourceRef(TokenReaderT *tr, SourceRefT *src) -> bool
{
    auto line = unsigned{};
    auto col = unsigned{};
    auto fpVal = double{};
    auto intVal = int{};

    TrIndication(tr, &line, &col);
    auto ident = TrReadIdent(tr);
    if(ident.empty())
        return false;
    src->mFormat = MatchSourceFormat(ident);
    if(src->mFormat == SF_NONE)
    {
        TrErrorAt(tr, line, col, "Expected a source format.");
        return false;
    }
    if(!TrReadOperator(tr, "("))
        return false;
    if(src->mFormat == SF_SOFA)
    {
        if(!TrReadFloat(tr, MIN_DISTANCE, MAX_DISTANCE, &fpVal))
            return false;
        src->mRadius = fpVal;
        if(!TrReadOperator(tr, ","))
            return false;
        if(!TrReadFloat(tr, -90.0, 90.0, &fpVal))
            return false;
        src->mElevation = fpVal;
        if(!TrReadOperator(tr, ","))
            return false;
        if(!TrReadFloat(tr, -360.0, 360.0, &fpVal))
            return false;
        src->mAzimuth = fpVal;
        if(!TrReadOperator(tr, ":"))
            return false;
        if(!TrReadInt(tr, 0, MaxWaveChannels, &intVal))
            return false;
        src->mType = ET_NONE;
        src->mSize = 0;
        src->mBits = 0;
        src->mChannel = static_cast<unsigned>(intVal);
        src->mSkip = 0;
    }
    else if(src->mFormat == SF_WAVE)
    {
        if(!TrReadInt(tr, 0, MaxWaveChannels, &intVal))
            return false;
        src->mType = ET_NONE;
        src->mSize = 0;
        src->mBits = 0;
        src->mChannel = static_cast<unsigned>(intVal);
        src->mSkip = 0;
    }
    else
    {
        TrIndication(tr, &line, &col);
        ident = TrReadIdent(tr);
        if(ident.empty())
            return false;
        src->mType = MatchElementType(ident);
        if(src->mType == ET_NONE)
        {
            TrErrorAt(tr, line, col, "Expected a source element type.");
            return false;
        }
        if(src->mFormat == SF_BIN_LE || src->mFormat == SF_BIN_BE)
        {
            if(!TrReadOperator(tr, ","))
                return false;
            if(src->mType == ET_INT)
            {
                if(!TrReadInt(tr, MinBinSize, MaxBinSize, &intVal))
                    return false;
                src->mSize = static_cast<unsigned>(intVal);
                if(!TrIsOperator(tr, ","))
                    src->mBits = static_cast<int>(8*src->mSize);
                else
                {
                    TrReadOperator(tr, ",");
                    TrIndication(tr, &line, &col);
                    if(!TrReadInt(tr, -2147483647-1, 2147483647, &intVal))
                        return false;
                    if(std::abs(intVal) < int{MinBinSize}*8 || static_cast<unsigned>(std::abs(intVal)) > (8*src->mSize))
                    {
                        TrErrorAt(tr, line, col, "Expected a value of (+/-) {} to {}.", MinBinSize*8, 8*src->mSize);
                        return false;
                    }
                    src->mBits = intVal;
                }
            }
            else
            {
                TrIndication(tr, &line, &col);
                if(!TrReadInt(tr, -2147483647-1, 2147483647, &intVal))
                    return false;
                if(intVal != 4 && intVal != 8)
                {
                    TrErrorAt(tr, line, col, "Expected a value of 4 or 8.");
                    return false;
                }
                src->mSize = static_cast<unsigned>(intVal);
                src->mBits = 0;
            }
        }
        else if(src->mFormat == SF_ASCII && src->mType == ET_INT)
        {
            if(!TrReadOperator(tr, ","))
                return false;
            if(!TrReadInt(tr, MinASCIIBits, MaxASCIIBits, &intVal))
                return false;
            src->mSize = 0;
            src->mBits = intVal;
        }
        else
        {
            src->mSize = 0;
            src->mBits = 0;
        }

        if(!TrIsOperator(tr, ";"))
            src->mSkip = 0;
        else
        {
            TrReadOperator(tr, ";");
            if(!TrReadInt(tr, 0, 0x7FFFFFFF, &intVal))
                return false;
            src->mSkip = static_cast<unsigned>(intVal);
        }
    }
    if(!TrReadOperator(tr, ")"))
        return false;
    if(TrIsOperator(tr, "@"))
    {
        TrReadOperator(tr, "@");
        if(!TrReadInt(tr, 0, 0x7FFFFFFF, &intVal))
            return false;
        src->mOffset = static_cast<unsigned>(intVal);
    }
    else
        src->mOffset = 0;
    if(!TrReadOperator(tr, ":"))
        return false;

    auto srcpath = TrReadString(tr);
    if(!srcpath)
        return false;
    src->mPath = std::move(*srcpath);
    return true;
}

// Parse and validate a SOFA source reference from the data set definition.
auto ReadSofaRef(TokenReaderT *tr, SourceRefT *src) -> bool
{
    auto line = unsigned{};
    auto col = unsigned{};
    auto intVal = int{};

    TrIndication(tr, &line, &col);
    const auto ident = TrReadIdent(tr);
    if(ident.empty())
        return false;
    src->mFormat = MatchSourceFormat(ident);
    if(src->mFormat != SF_SOFA)
    {
        TrErrorAt(tr, line, col, "Expected the SOFA source format.");
        return false;
    }

    src->mType = ET_NONE;
    src->mSize = 0;
    src->mBits = 0;
    src->mChannel = 0;
    src->mSkip = 0;

    if(TrIsOperator(tr, "@"))
    {
        TrReadOperator(tr, "@");
        if(!TrReadInt(tr, 0, 0x7FFFFFFF, &intVal))
            return false;
        src->mOffset = static_cast<unsigned>(intVal);
    }
    else
        src->mOffset = 0;
    if(!TrReadOperator(tr, ":"))
        return false;

    auto srcpath = TrReadString(tr);
    if(!srcpath)
        return false;
    src->mPath = std::move(*srcpath);
    return true;
}

// Match the target ear (index) from a given identifier.
auto MatchTargetEar(const std::string_view ident) -> std::optional<uint8_t>
{
    if(al::case_compare(ident, "left"sv) == 0)
        return 0u;
    if(al::case_compare(ident, "right"sv) == 0)
        return 1u;
    return std::nullopt;
}

// Calculate the onset time of an HRIR and average it with any existing
// timing for its field, elevation, azimuth, and ear.
constexpr auto OnsetRateMultiple = 10;
auto AverageHrirOnset(PPhaseResampler &rs, std::span<double> upsampled, unsigned const rate,
    std::span<double const> const hrir, double const f, double const onset) -> double
{
    rs.process(hrir, upsampled);

    const auto iter = std::ranges::max_element(upsampled, std::less{},
        [](const double value) -> double { return std::abs(value); });
    return std::lerp(onset, gsl::narrow_cast<double>(std::distance(upsampled.begin(), iter))
        / (10*rate), f);
}

// Calculate the magnitude response of an HRIR and average it with any
// existing responses for its field, elevation, azimuth, and ear.
void AverageHrirMagnitude(unsigned const fftSize, std::span<double const> const hrir,
    double const f, std::span<double> const mag)
{
    auto const m = 1u + (fftSize/2u);
    auto h = std::vector<complex_d>(fftSize);
    auto r = std::vector<double>(m);

    auto const hiter = std::ranges::copy(hrir, h.begin()).out;
    std::fill(hiter, h.end(), 0.0);
    forward_fft(h);
    MagnitudeResponse(h, r);
    for(auto i=0u;i < m;++i)
        mag[i] = std::lerp(mag[i], r[i], f);
}

// Process the list of sources in the data set definition.
auto ProcessSources(TokenReaderT *tr, HrirDataT *hData, unsigned const outRate) -> bool
{
    const auto channels = (hData->mChannelType == CT_STEREO) ? 2u : 1u;
    hData->mHrirsBase.resize(size_t{channels} * hData->mIrCount * hData->mIrSize);
    const auto hrirs = std::span{hData->mHrirsBase};
    auto hrir = std::vector(hData->mIrSize, 0.0);
    auto line = unsigned{};
    auto col = unsigned{};
    auto fi = unsigned{};
    auto ei = unsigned{};
    auto ai = unsigned{};

    auto onsetSamples = std::vector(size_t{OnsetRateMultiple}*hData->mIrPoints, 0.0);
    auto onsetResampler = PPhaseResampler{};
    onsetResampler.init(hData->mIrRate, OnsetRateMultiple*hData->mIrRate);

    auto resampler = std::optional<PPhaseResampler>{};
    if(outRate && outRate != hData->mIrRate)
        resampler.emplace().init(hData->mIrRate, outRate);
    const auto rateScale = outRate ? static_cast<double>(outRate) / hData->mIrRate : 1.0;
    const auto irPoints{outRate
        ? std::min(static_cast<unsigned>(std::ceil(hData->mIrPoints*rateScale)), hData->mIrPoints)
        : hData->mIrPoints};

    fmt::print("Loading sources...");
    std::cout.flush();
    auto count = 0;
    while(TrIsOperator(tr, "["))
    {
        auto factor = std::array{1.0, 1.0};

        TrIndication(tr, &line, &col);
        TrReadOperator(tr, "[");

        if(TrIsOperator(tr, "*"))
        {
            TrReadOperator(tr, "*");
            if(!TrReadOperator(tr, "]") || !TrReadOperator(tr, "="))
                return false;

            TrIndication(tr, &line, &col);
            SourceRefT src{};
            if(!ReadSofaRef(tr, &src))
                return false;

            if(hData->mChannelType == CT_STEREO)
            {
                const auto type = TrReadIdent(tr);
                if(type.empty())
                    return false;

                switch(MatchChannelType(type))
                {
                case CT_NONE:
                    TrErrorAt(tr, line, col, "Expected a channel type.");
                    return false;
                case CT_MONO:
                    src.mChannel = 0;
                    break;
                case CT_STEREO:
                    src.mChannel = 1;
                    break;
                }
            }
            else
            {
                const auto type = TrReadIdent(tr);
                if(type.empty())
                    return false;

                if(MatchChannelType(type) != CT_MONO)
                {
                    TrErrorAt(tr, line, col, "Expected a mono channel type.");
                    return false;
                }
                src.mChannel = 0;
            }

            auto const *const sofa = LoadSofaFile(&src, hData->mIrRate, hData->mIrPoints);
            if(!sofa) return false;

            const auto srcPosValues = std::span{sofa->hrtf->SourcePosition.values,
                sofa->hrtf->M*3_uz};
            for(auto const si : std::views::iota(0u, sofa->hrtf->M))
            {
                fmt::print("\rLoading sources... {} of {}", si+1, sofa->hrtf->M);
                std::cout.flush();

                std::array aer{srcPosValues[3_uz*si], srcPosValues[3_uz*si + 1],
                    srcPosValues[3_uz*si + 2]};
                mysofa_c2s(aer.data());

                if(std::fabs(aer[1]) >= 89.999f)
                    aer[0] = 0.0f;
                else
                    aer[0] = std::fmod(360.0f - aer[0], 360.0f);

                auto field = std::ranges::find_if(hData->mFds, [&aer](const HrirFdT &fld) -> bool
                { return (std::abs(aer[2] - fld.mDistance) < 0.001); });
                if(field == hData->mFds.end())
                    continue;
                fi = static_cast<unsigned>(std::distance(hData->mFds.begin(), field));

                const auto evscale = 180.0 / static_cast<double>(field->mEvs.size()-1);
                auto ef = (90.0 + aer[1]) / evscale;
                ei = static_cast<unsigned>(std::round(ef));
                ef = (ef - ei) * evscale;
                if(std::abs(ef) >= 0.1)
                    continue;

                const auto azscale = 360.0 / static_cast<double>(field->mEvs[ei].mAzs.size());
                auto af = aer[0] / azscale;
                ai = static_cast<unsigned>(std::round(af));
                af = (af - ai) * azscale;
                ai %= static_cast<unsigned>(field->mEvs[ei].mAzs.size());
                if(std::abs(af) >= 0.1)
                    continue;

                HrirAzT *azd = &field->mEvs[ei].mAzs[ai];
                if(!azd->mIrs[0].empty())
                {
                    TrErrorAt(tr, line, col, "Redefinition of source [ {}, {}, {} ].", fi, ei, ai);
                    return false;
                }

                const auto hrirPoints = std::span{hrir}.first(hData->mIrPoints);
                ExtractSofaHrir(sofa->hrtf, si, 0, src.mOffset, hrirPoints);
                azd->mIrs[0] = hrirs.subspan(size_t{hData->mIrSize}*azd->mIndex, hData->mIrSize);
                azd->mDelays[0] = AverageHrirOnset(onsetResampler, onsetSamples, hData->mIrRate,
                    hrirPoints, 1.0, azd->mDelays[0]);
                if(resampler)
                    resampler->process(hrirPoints, hrir);
                AverageHrirMagnitude(hData->mFftSize, std::span{hrir}.first(irPoints), 1.0,
                    azd->mIrs[0]);

                if(src.mChannel == 1)
                {
                    ExtractSofaHrir(sofa->hrtf, si, 1, src.mOffset, hrirPoints);
                    azd->mIrs[1] = hrirs.subspan(
                        (size_t{hData->mIrCount}+azd->mIndex) * hData->mIrSize, hData->mIrSize);
                    azd->mDelays[1] = AverageHrirOnset(onsetResampler, onsetSamples,
                        hData->mIrRate, hrirPoints, 1.0, azd->mDelays[1]);
                    if(resampler)
                        resampler->process(hrirPoints, hrir);
                    AverageHrirMagnitude(hData->mFftSize, std::span{hrir}.first(irPoints), 1.0,
                        azd->mIrs[1]);
                }

                // TODO: Since some SOFA files contain minimum phase HRIRs,
                // it would be beneficial to check for per-measurement delays
                // (when available) to reconstruct the HRTDs.
            }

            continue;
        }

        if(!ReadIndexTriplet(tr, hData, &fi, &ei, &ai))
            return false;
        if(!TrReadOperator(tr, "]"))
            return false;
        HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

        if(!azd->mIrs[0].empty())
        {
            TrErrorAt(tr, line, col, "Redefinition of source.");
            return false;
        }
        if(!TrReadOperator(tr, "="))
            return false;

        while(true)
        {
            SourceRefT src{};
            if(!ReadSourceRef(tr, &src))
                return false;

            // TODO: Would be nice to display 'x of y files', but that would
            // require preparing the source refs first to get a total count
            // before loading them.
            ++count;
            fmt::print("\rLoading sources... {} file{}", count, (count==1)?"":"s");
            std::cout.flush();

            if(!LoadSource(&src, hData->mIrRate, std::span{hrir}.first(hData->mIrPoints)))
                return false;

            auto ti = 0u;
            if(hData->mChannelType == CT_STEREO)
            {
                const auto ident = TrReadIdent(tr);
                if(ident.empty())
                    return false;

                if(auto earopt = MatchTargetEar(ident))
                    ti = *earopt;
                else
                {
                    TrErrorAt(tr, line, col, "Expected a target ear.");
                    return false;
                }
            }
            const auto hrirPoints = std::span{hrir}.first(hData->mIrPoints);
            azd->mIrs[ti] = hrirs.subspan((ti*size_t{hData->mIrCount}+azd->mIndex)*hData->mIrSize,
                hData->mIrSize);
            azd->mDelays[ti] = AverageHrirOnset(onsetResampler, onsetSamples, hData->mIrRate,
                hrirPoints, 1.0/factor[ti], azd->mDelays[ti]);
            if(resampler)
                resampler->process(hrirPoints, hrir);
            AverageHrirMagnitude(hData->mFftSize, std::span{hrir}.first(irPoints), 1.0/factor[ti],
                azd->mIrs[ti]);
            factor[ti] += 1.0;
            if(!TrIsOperator(tr, "+"))
                break;
            TrReadOperator(tr, "+");
        }
        if(hData->mChannelType == CT_STEREO)
        {
            if(azd->mIrs[0].empty())
            {
                TrErrorAt(tr, line, col, "Missing left ear source reference(s).");
                return false;
            }
            if(azd->mIrs[1].empty())
            {
                TrErrorAt(tr, line, col, "Missing right ear source reference(s).");
                return false;
            }
        }
    }
    fmt::println("");
    hrir.clear();
    if(resampler)
    {
        hData->mIrRate = outRate;
        hData->mIrPoints = irPoints;
        resampler.reset();
    }
    for(fi = 0;fi < hData->mFds.size();fi++)
    {
        for(ei = 0;ei < hData->mFds[fi].mEvs.size();ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzs.size();ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];
                if(!azd->mIrs[0].empty())
                    break;
            }
            if(ai < hData->mFds[fi].mEvs[ei].mAzs.size())
                break;
        }
        if(ei >= hData->mFds[fi].mEvs.size())
        {
            TrError(tr, "Missing source references [ {}, *, * ].", fi);
            return false;
        }
        hData->mFds[fi].mEvStart = ei;
        for(;ei < hData->mFds[fi].mEvs.size();ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzs.size();ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                if(azd->mIrs[0].empty())
                {
                    TrError(tr, "Missing source reference [ {}, {}, {} ].", fi, ei, ai);
                    return false;
                }
            }
        }
    }
    for(auto ti=0u;ti < channels;++ti)
    {
        for(fi = 0;fi < hData->mFds.size();++fi)
        {
            for(ei = 0;ei < hData->mFds[fi].mEvs.size();++ei)
            {
                for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzs.size();++ai)
                {
                    HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];
                    azd->mIrs[ti] = hrirs.subspan(
                        (ti*size_t{hData->mIrCount} + azd->mIndex) * hData->mIrSize,
                        hData->mIrSize);
                }
            }
        }
    }
    if(!TrLoad(tr))
    {
        gSofaCache.clear();
        return true;
    }

    TrError(tr, "Errant data at end of source list.");
    gSofaCache.clear();
    return false;
}

} /* namespace */

auto LoadDefInput(std::istream &istream, std::span<char const> const startbytes,
    std::string_view const filename, unsigned const fftSize, unsigned const truncSize,
    unsigned const outRate, ChannelModeT const chanMode, HrirDataT *hData) -> bool
{
    auto tr = TokenReaderT{istream};

    TrSetup(startbytes, filename, &tr);
    if(!ProcessMetrics(&tr, fftSize, truncSize, chanMode, hData)
        || !ProcessSources(&tr, hData, outRate))
        return false;

    return true;
}
