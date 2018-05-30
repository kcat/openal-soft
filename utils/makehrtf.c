/*
 * HRTF utility for producing and demonstrating the process of creating an
 * OpenAL Soft compatible HRIR data set.
 *
 * Copyright (C) 2011-2017  Christopher Fitzgerald
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
 *
 * --------------------------------------------------------------------------
 *
 * A big thanks goes out to all those whose work done in the field of
 * binaural sound synthesis using measured HRTFs makes this utility and the
 * OpenAL Soft implementation possible.
 *
 * The algorithm for diffuse-field equalization was adapted from the work
 * done by Rio Emmanuel and Larcher Veronique of IRCAM and Bill Gardner of
 * MIT Media Laboratory.  It operates as follows:
 *
 *  1.  Take the FFT of each HRIR and only keep the magnitude responses.
 *  2.  Calculate the diffuse-field power-average of all HRIRs weighted by
 *      their contribution to the total surface area covered by their
 *      measurement.
 *  3.  Take the diffuse-field average and limit its magnitude range.
 *  4.  Equalize the responses by using the inverse of the diffuse-field
 *      average.
 *  5.  Reconstruct the minimum-phase responses.
 *  5.  Zero the DC component.
 *  6.  IFFT the result and truncate to the desired-length minimum-phase FIR.
 *
 * The spherical head algorithm for calculating propagation delay was adapted
 * from the paper:
 *
 *  Modeling Interaural Time Difference Assuming a Spherical Head
 *  Joel David Miller
 *  Music 150, Musical Acoustics, Stanford University
 *  December 2, 2001
 *
 * The formulae for calculating the Kaiser window metrics are from the
 * the textbook:
 *
 *  Discrete-Time Signal Processing
 *  Alan V. Oppenheim and Ronald W. Schafer
 *  Prentice-Hall Signal Processing Series
 *  1999
 */

#include "config.h"

#define _UNICODE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_GETOPT
#include <unistd.h>
#else
#include "getopt.h"
#endif

#include "win_main_utf8.h"

/* Define int64_t and uint64_t types */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#include <inttypes.h>
#elif defined(_WIN32) && defined(__GNUC__)
#include <stdint.h>
#elif defined(_WIN32)
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
/* Fallback if nothing above works */
#include <inttypes.h>
#endif

#ifndef M_PI
#define M_PI                         (3.14159265358979323846)
#endif

#ifndef HUGE_VAL
#define HUGE_VAL                     (1.0 / 0.0)
#endif


// The epsilon used to maintain signal stability.
#define EPSILON                      (1e-9)

// Constants for accessing the token reader's ring buffer.
#define TR_RING_BITS                 (16)
#define TR_RING_SIZE                 (1 << TR_RING_BITS)
#define TR_RING_MASK                 (TR_RING_SIZE - 1)

// The token reader's load interval in bytes.
#define TR_LOAD_SIZE                 (TR_RING_SIZE >> 2)

// The maximum identifier length used when processing the data set
// definition.
#define MAX_IDENT_LEN                (16)

// The maximum path length used when processing filenames.
#define MAX_PATH_LEN                 (256)

// The limits for the sample 'rate' metric in the data set definition and for
// resampling.
#define MIN_RATE                     (32000)
#define MAX_RATE                     (96000)

// The limits for the HRIR 'points' metric in the data set definition.
#define MIN_POINTS                   (16)
#define MAX_POINTS                   (8192)

// The limit to the number of 'distances' listed in the data set definition.
#define MAX_FD_COUNT                 (16)

// The limits to the number of 'azimuths' listed in the data set definition.
#define MIN_EV_COUNT                 (5)
#define MAX_EV_COUNT                 (128)

// The limits for each of the 'azimuths' listed in the data set definition.
#define MIN_AZ_COUNT                 (1)
#define MAX_AZ_COUNT                 (128)

// The limits for the listener's head 'radius' in the data set definition.
#define MIN_RADIUS                   (0.05)
#define MAX_RADIUS                   (0.15)

// The limits for the 'distance' from source to listener for each field in
// the definition file.
#define MIN_DISTANCE                 (0.05)
#define MAX_DISTANCE                 (2.50)

// The maximum number of channels that can be addressed for a WAVE file
// source listed in the data set definition.
#define MAX_WAVE_CHANNELS            (65535)

// The limits to the byte size for a binary source listed in the definition
// file.
#define MIN_BIN_SIZE                 (2)
#define MAX_BIN_SIZE                 (4)

// The minimum number of significant bits for binary sources listed in the
// data set definition.  The maximum is calculated from the byte size.
#define MIN_BIN_BITS                 (16)

// The limits to the number of significant bits for an ASCII source listed in
// the data set definition.
#define MIN_ASCII_BITS               (16)
#define MAX_ASCII_BITS               (32)

// The limits to the FFT window size override on the command line.
#define MIN_FFTSIZE                  (65536)
#define MAX_FFTSIZE                  (131072)

// The limits to the equalization range limit on the command line.
#define MIN_LIMIT                    (2.0)
#define MAX_LIMIT                    (120.0)

// The limits to the truncation window size on the command line.
#define MIN_TRUNCSIZE                (16)
#define MAX_TRUNCSIZE                (512)

// The limits to the custom head radius on the command line.
#define MIN_CUSTOM_RADIUS            (0.05)
#define MAX_CUSTOM_RADIUS            (0.15)

// The truncation window size must be a multiple of the below value to allow
// for vectorized convolution.
#define MOD_TRUNCSIZE                (8)

// The defaults for the command line options.
#define DEFAULT_FFTSIZE              (65536)
#define DEFAULT_EQUALIZE             (1)
#define DEFAULT_SURFACE              (1)
#define DEFAULT_LIMIT                (24.0)
#define DEFAULT_TRUNCSIZE            (32)
#define DEFAULT_HEAD_MODEL           (HM_DATASET)
#define DEFAULT_CUSTOM_RADIUS        (0.0)

// The four-character-codes for RIFF/RIFX WAVE file chunks.
#define FOURCC_RIFF                  (0x46464952) // 'RIFF'
#define FOURCC_RIFX                  (0x58464952) // 'RIFX'
#define FOURCC_WAVE                  (0x45564157) // 'WAVE'
#define FOURCC_FMT                   (0x20746D66) // 'fmt '
#define FOURCC_DATA                  (0x61746164) // 'data'
#define FOURCC_LIST                  (0x5453494C) // 'LIST'
#define FOURCC_WAVL                  (0x6C766177) // 'wavl'
#define FOURCC_SLNT                  (0x746E6C73) // 'slnt'

// The supported wave formats.
#define WAVE_FORMAT_PCM              (0x0001)
#define WAVE_FORMAT_IEEE_FLOAT       (0x0003)
#define WAVE_FORMAT_EXTENSIBLE       (0xFFFE)

// The maximum propagation delay value supported by OpenAL Soft.
#define MAX_HRTD                     (63.0)

// The OpenAL Soft HRTF format marker.  It stands for minimum-phase head
// response protocol 02.
#define MHR_FORMAT                   ("MinPHR02")

// Sample and channel type enum values.
typedef enum SampleTypeT {
    ST_S16 = 0,
    ST_S24 = 1
} SampleTypeT;

// Certain iterations rely on these integer enum values.
typedef enum ChannelTypeT {
    CT_NONE   = -1,
    CT_MONO   = 0,
    CT_STEREO = 1
} ChannelTypeT;

// Byte order for the serialization routines.
typedef enum ByteOrderT {
    BO_NONE,
    BO_LITTLE,
    BO_BIG
} ByteOrderT;

// Source format for the references listed in the data set definition.
typedef enum SourceFormatT {
    SF_NONE,
    SF_WAVE,   // RIFF/RIFX WAVE file.
    SF_BIN_LE, // Little-endian binary file.
    SF_BIN_BE, // Big-endian binary file.
    SF_ASCII   // ASCII text file.
} SourceFormatT;

// Element types for the references listed in the data set definition.
typedef enum ElementTypeT {
    ET_NONE,
    ET_INT,   // Integer elements.
    ET_FP    // Floating-point elements.
} ElementTypeT;

// Head model used for calculating the impulse delays.
typedef enum HeadModelT {
    HM_NONE,
    HM_DATASET, // Measure the onset from the dataset.
    HM_SPHERE   // Calculate the onset using a spherical head model.
} HeadModelT;

// Unsigned integer type.
typedef unsigned int uint;

// Serialization types.  The trailing digit indicates the number of bits.
typedef unsigned char uint8;
typedef int           int32;
typedef unsigned int  uint32;
typedef uint64_t      uint64;

// Token reader state for parsing the data set definition.
typedef struct TokenReaderT {
    FILE *mFile;
    const char *mName;
    uint        mLine;
    uint        mColumn;
    char   mRing[TR_RING_SIZE];
    size_t mIn;
    size_t mOut;
} TokenReaderT;

// Source reference state used when loading sources.
typedef struct SourceRefT {
    SourceFormatT mFormat;
    ElementTypeT  mType;
    uint mSize;
    int  mBits;
    uint mChannel;
    uint mSkip;
    uint mOffset;
    char mPath[MAX_PATH_LEN+1];
} SourceRefT;

// Structured HRIR storage for stereo azimuth pairs, elevations, and fields.
typedef struct HrirAzT {
    double mAzimuth;
    uint mIndex;
    double mDelays[2];
    double *mIrs[2];
} HrirAzT;

typedef struct HrirEvT {
    double mElevation;
    uint mIrCount;
    uint mAzCount;
    HrirAzT *mAzs;
} HrirEvT;

typedef struct HrirFdT {
    double mDistance;
    uint mIrCount;
    uint mEvCount;
    uint mEvStart;
    HrirEvT *mEvs;
} HrirFdT;

// The HRIR metrics and data set used when loading, processing, and storing
// the resulting HRTF.
typedef struct HrirDataT {
    uint mIrRate;
    SampleTypeT mSampleType;
    ChannelTypeT mChannelType;
    uint mIrPoints;
    uint mFftSize;
    uint mIrSize;
    double mRadius;
    uint mIrCount;
    uint mFdCount;
    HrirFdT *mFds;
} HrirDataT;

// The resampler metrics and FIR filter.
typedef struct ResamplerT {
    uint mP, mQ, mM, mL;
    double *mF;
} ResamplerT;


/****************************************
 *** Complex number type and routines ***
 ****************************************/

typedef struct {
    double Real, Imag;
} Complex;

static Complex MakeComplex(double r, double i)
{
    Complex c = { r, i };
    return c;
}

static Complex c_add(Complex a, Complex b)
{
    Complex r;
    r.Real = a.Real + b.Real;
    r.Imag = a.Imag + b.Imag;
    return r;
}

static Complex c_sub(Complex a, Complex b)
{
    Complex r;
    r.Real = a.Real - b.Real;
    r.Imag = a.Imag - b.Imag;
    return r;
}

static Complex c_mul(Complex a, Complex b)
{
    Complex r;
    r.Real = a.Real*b.Real - a.Imag*b.Imag;
    r.Imag = a.Imag*b.Real + a.Real*b.Imag;
    return r;
}

static Complex c_muls(Complex a, double s)
{
    Complex r;
    r.Real = a.Real * s;
    r.Imag = a.Imag * s;
    return r;
}

static double c_abs(Complex a)
{
    return sqrt(a.Real*a.Real + a.Imag*a.Imag);
}

static Complex c_exp(Complex a)
{
    Complex r;
    double e = exp(a.Real);
    r.Real = e * cos(a.Imag);
    r.Imag = e * sin(a.Imag);
    return r;
}

/*****************************
 *** Token reader routines ***
 *****************************/

/* Whitespace is not significant. It can process tokens as identifiers, numbers
 * (integer and floating-point), strings, and operators. Strings must be
 * encapsulated by double-quotes and cannot span multiple lines.
 */

// Setup the reader on the given file.  The filename can be NULL if no error
// output is desired.
static void TrSetup(FILE *fp, const char *filename, TokenReaderT *tr)
{
    const char *name = NULL;

    if(filename)
    {
        const char *slash = strrchr(filename, '/');
        if(slash)
        {
            const char *bslash = strrchr(slash+1, '\\');
            if(bslash) name = bslash+1;
            else name = slash+1;
        }
        else
        {
            const char *bslash = strrchr(filename, '\\');
            if(bslash) name = bslash+1;
            else name = filename;
        }
    }

    tr->mFile = fp;
    tr->mName = name;
    tr->mLine = 1;
    tr->mColumn = 1;
    tr->mIn = 0;
    tr->mOut = 0;
}

// Prime the reader's ring buffer, and return a result indicating that there
// is text to process.
static int TrLoad(TokenReaderT *tr)
{
    size_t toLoad, in, count;

    toLoad = TR_RING_SIZE - (tr->mIn - tr->mOut);
    if(toLoad >= TR_LOAD_SIZE && !feof(tr->mFile))
    {
        // Load TR_LOAD_SIZE (or less if at the end of the file) per read.
        toLoad = TR_LOAD_SIZE;
        in = tr->mIn&TR_RING_MASK;
        count = TR_RING_SIZE - in;
        if(count < toLoad)
        {
            tr->mIn += fread(&tr->mRing[in], 1, count, tr->mFile);
            tr->mIn += fread(&tr->mRing[0], 1, toLoad-count, tr->mFile);
        }
        else
            tr->mIn += fread(&tr->mRing[in], 1, toLoad, tr->mFile);

        if(tr->mOut >= TR_RING_SIZE)
        {
            tr->mOut -= TR_RING_SIZE;
            tr->mIn -= TR_RING_SIZE;
        }
    }
    if(tr->mIn > tr->mOut)
        return 1;
    return 0;
}

// Error display routine.  Only displays when the base name is not NULL.
static void TrErrorVA(const TokenReaderT *tr, uint line, uint column, const char *format, va_list argPtr)
{
    if(!tr->mName)
        return;
    fprintf(stderr, "Error (%s:%u:%u): ", tr->mName, line, column);
    vfprintf(stderr, format, argPtr);
}

// Used to display an error at a saved line/column.
static void TrErrorAt(const TokenReaderT *tr, uint line, uint column, const char *format, ...)
{
    va_list argPtr;

    va_start(argPtr, format);
    TrErrorVA(tr, line, column, format, argPtr);
    va_end(argPtr);
}

// Used to display an error at the current line/column.
static void TrError(const TokenReaderT *tr, const char *format, ...)
{
    va_list argPtr;

    va_start(argPtr, format);
    TrErrorVA(tr, tr->mLine, tr->mColumn, format, argPtr);
    va_end(argPtr);
}

// Skips to the next line.
static void TrSkipLine(TokenReaderT *tr)
{
    char ch;

    while(TrLoad(tr))
    {
        ch = tr->mRing[tr->mOut&TR_RING_MASK];
        tr->mOut++;
        if(ch == '\n')
        {
            tr->mLine++;
            tr->mColumn = 1;
            break;
        }
        tr->mColumn ++;
    }
}

// Skips to the next token.
static int TrSkipWhitespace(TokenReaderT *tr)
{
    char ch;

    while(TrLoad(tr))
    {
        ch = tr->mRing[tr->mOut&TR_RING_MASK];
        if(isspace(ch))
        {
            tr->mOut++;
            if(ch == '\n')
            {
                tr->mLine++;
                tr->mColumn = 1;
            }
            else
                tr->mColumn++;
        }
        else if(ch == '#')
            TrSkipLine(tr);
        else
            return 1;
    }
    return 0;
}

// Get the line and/or column of the next token (or the end of input).
static void TrIndication(TokenReaderT *tr, uint *line, uint *column)
{
    TrSkipWhitespace(tr);
    if(line) *line = tr->mLine;
    if(column) *column = tr->mColumn;
}

// Checks to see if a token is (likely to be) an identifier.  It does not
// display any errors and will not proceed to the next token.
static int TrIsIdent(TokenReaderT *tr)
{
    char ch;

    if(!TrSkipWhitespace(tr))
        return 0;
    ch = tr->mRing[tr->mOut&TR_RING_MASK];
    return ch == '_' || isalpha(ch);
}


// Checks to see if a token is the given operator.  It does not display any
// errors and will not proceed to the next token.
static int TrIsOperator(TokenReaderT *tr, const char *op)
{
    size_t out, len;
    char ch;

    if(!TrSkipWhitespace(tr))
        return 0;
    out = tr->mOut;
    len = 0;
    while(op[len] != '\0' && out < tr->mIn)
    {
        ch = tr->mRing[out&TR_RING_MASK];
        if(ch != op[len]) break;
        len++;
        out++;
    }
    if(op[len] == '\0')
        return 1;
    return 0;
}

/* The TrRead*() routines obtain the value of a matching token type.  They
 * display type, form, and boundary errors and will proceed to the next
 * token.
 */

// Reads and validates an identifier token.
static int TrReadIdent(TokenReaderT *tr, const uint maxLen, char *ident)
{
    uint col, len;
    char ch;

    col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        ch = tr->mRing[tr->mOut&TR_RING_MASK];
        if(ch == '_' || isalpha(ch))
        {
            len = 0;
            do {
                if(len < maxLen)
                    ident[len] = ch;
                len++;
                tr->mOut++;
                if(!TrLoad(tr))
                    break;
                ch = tr->mRing[tr->mOut&TR_RING_MASK];
            } while(ch == '_' || isdigit(ch) || isalpha(ch));

            tr->mColumn += len;
            if(len < maxLen)
            {
                ident[len] = '\0';
                return 1;
            }
            TrErrorAt(tr, tr->mLine, col, "Identifier is too long.\n");
            return 0;
        }
    }
    TrErrorAt(tr, tr->mLine, col, "Expected an identifier.\n");
    return 0;
}

// Reads and validates (including bounds) an integer token.
static int TrReadInt(TokenReaderT *tr, const int loBound, const int hiBound, int *value)
{
    uint col, digis, len;
    char ch, temp[64+1];

    col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        len = 0;
        ch = tr->mRing[tr->mOut&TR_RING_MASK];
        if(ch == '+' || ch == '-')
        {
            temp[len] = ch;
            len++;
            tr->mOut++;
        }
        digis = 0;
        while(TrLoad(tr))
        {
            ch = tr->mRing[tr->mOut&TR_RING_MASK];
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
                return 0;
            }
            temp[len] = '\0';
            *value = strtol(temp, NULL, 10);
            if(*value < loBound || *value > hiBound)
            {
                TrErrorAt(tr, tr->mLine, col, "Expected a value from %d to %d.\n", loBound, hiBound);
                return 0;
            }
            return 1;
        }
    }
    TrErrorAt(tr, tr->mLine, col, "Expected an integer.\n");
    return 0;
}

// Reads and validates (including bounds) a float token.
static int TrReadFloat(TokenReaderT *tr, const double loBound, const double hiBound, double *value)
{
    uint col, digis, len;
    char ch, temp[64+1];

    col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        len = 0;
        ch = tr->mRing[tr->mOut&TR_RING_MASK];
        if(ch == '+' || ch == '-')
        {
            temp[len] = ch;
            len++;
            tr->mOut++;
        }

        digis = 0;
        while(TrLoad(tr))
        {
            ch = tr->mRing[tr->mOut&TR_RING_MASK];
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
            ch = tr->mRing[tr->mOut&TR_RING_MASK];
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
                    ch = tr->mRing[tr->mOut&TR_RING_MASK];
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
                    return 0;
                }
                temp[len] = '\0';
                *value = strtod(temp, NULL);
                if(*value < loBound || *value > hiBound)
                {
                    TrErrorAt(tr, tr->mLine, col, "Expected a value from %f to %f.\n", loBound, hiBound);
                    return 0;
                }
                return 1;
            }
        }
        else
            tr->mColumn += len;
    }
    TrErrorAt(tr, tr->mLine, col, "Expected a float.\n");
    return 0;
}

// Reads and validates a string token.
static int TrReadString(TokenReaderT *tr, const uint maxLen, char *text)
{
    uint col, len;
    char ch;

    col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        ch = tr->mRing[tr->mOut&TR_RING_MASK];
        if(ch == '\"')
        {
            tr->mOut++;
            len = 0;
            while(TrLoad(tr))
            {
                ch = tr->mRing[tr->mOut&TR_RING_MASK];
                tr->mOut++;
                if(ch == '\"')
                    break;
                if(ch == '\n')
                {
                    TrErrorAt(tr, tr->mLine, col, "Unterminated string at end of line.\n");
                    return 0;
                }
                if(len < maxLen)
                    text[len] = ch;
                len++;
            }
            if(ch != '\"')
            {
                tr->mColumn += 1 + len;
                TrErrorAt(tr, tr->mLine, col, "Unterminated string at end of input.\n");
                return 0;
            }
            tr->mColumn += 2 + len;
            if(len > maxLen)
            {
                TrErrorAt(tr, tr->mLine, col, "String is too long.\n");
                return 0;
            }
            text[len] = '\0';
            return 1;
        }
    }
    TrErrorAt(tr, tr->mLine, col, "Expected a string.\n");
    return 0;
}

// Reads and validates the given operator.
static int TrReadOperator(TokenReaderT *tr, const char *op)
{
    uint col, len;
    char ch;

    col = tr->mColumn;
    if(TrSkipWhitespace(tr))
    {
        col = tr->mColumn;
        len = 0;
        while(op[len] != '\0' && TrLoad(tr))
        {
            ch = tr->mRing[tr->mOut&TR_RING_MASK];
            if(ch != op[len]) break;
            len++;
            tr->mOut++;
        }
        tr->mColumn += len;
        if(op[len] == '\0')
            return 1;
    }
    TrErrorAt(tr, tr->mLine, col, "Expected '%s' operator.\n", op);
    return 0;
}

/* Performs a string substitution.  Any case-insensitive occurrences of the
 * pattern string are replaced with the replacement string.  The result is
 * truncated if necessary.
 */
static int StrSubst(const char *in, const char *pat, const char *rep, const size_t maxLen, char *out)
{
    size_t inLen, patLen, repLen;
    size_t si, di;
    int truncated;

    inLen = strlen(in);
    patLen = strlen(pat);
    repLen = strlen(rep);
    si = 0;
    di = 0;
    truncated = 0;
    while(si < inLen && di < maxLen)
    {
        if(patLen <= inLen-si)
        {
            if(strncasecmp(&in[si], pat, patLen) == 0)
            {
                if(repLen > maxLen-di)
                {
                    repLen = maxLen - di;
                    truncated = 1;
                }
                strncpy(&out[di], rep, repLen);
                si += patLen;
                di += repLen;
            }
        }
        out[di] = in[si];
        si++;
        di++;
    }
    if(si < inLen)
        truncated = 1;
    out[di] = '\0';
    return !truncated;
}


/*********************
 *** Math routines ***
 *********************/

// Provide missing math routines for MSVC versions < 1800 (Visual Studio 2013).
#if defined(_MSC_VER) && _MSC_VER < 1800
static double round(double val)
{
    if(val < 0.0)
        return ceil(val-0.5);
    return floor(val+0.5);
}

static double fmin(double a, double b)
{
    return (a<b) ? a : b;
}

static double fmax(double a, double b)
{
    return (a>b) ? a : b;
}
#endif

// Simple clamp routine.
static double Clamp(const double val, const double lower, const double upper)
{
    return fmin(fmax(val, lower), upper);
}

// Performs linear interpolation.
static double Lerp(const double a, const double b, const double f)
{
    return a + f * (b - a);
}

static inline uint dither_rng(uint *seed)
{
    *seed = *seed * 96314165 + 907633515;
    return *seed;
}

// Performs a triangular probability density function dither. The input samples
// should be normalized (-1 to +1).
static void TpdfDither(double *restrict out, const double *restrict in, const double scale,
                       const int count, const int step, uint *seed)
{
    static const double PRNG_SCALE = 1.0 / UINT_MAX;
    uint prn0, prn1;
    int i;

    for(i = 0;i < count;i++)
    {
        prn0 = dither_rng(seed);
        prn1 = dither_rng(seed);
        out[i*step] = round(in[i]*scale + (prn0*PRNG_SCALE - prn1*PRNG_SCALE));
    }
}

// Allocates an array of doubles.
static double *CreateDoubles(size_t n)
{
    double *a;

    a = calloc(n?n:1, sizeof(*a));
    if(a == NULL)
    {
        fprintf(stderr, "Error:  Out of memory.\n");
        exit(-1);
    }
    return a;
}

// Allocates an array of complex numbers.
static Complex *CreateComplexes(size_t n)
{
    Complex *a;

    a = calloc(n?n:1, sizeof(*a));
    if(a == NULL)
    {
        fprintf(stderr, "Error:  Out of memory.\n");
        exit(-1);
    }
    return a;
}

/* Fast Fourier transform routines. The number of points must be a power of
 * two.
 */

// Performs bit-reversal ordering.
static void FftArrange(const uint n, Complex *inout)
{
    uint rk, k, m;

    // Handle in-place arrangement.
    rk = 0;
    for(k = 0;k < n;k++)
    {
        if(rk > k)
        {
            Complex temp = inout[rk];
            inout[rk] = inout[k];
            inout[k] = temp;
        }

        m = n;
        while(rk&(m >>= 1))
            rk &= ~m;
        rk |= m;
    }
}

// Performs the summation.
static void FftSummation(const int n, const double s, Complex *cplx)
{
    double pi;
    int m, m2;
    int i, k, mk;

    pi = s * M_PI;
    for(m = 1, m2 = 2;m < n; m <<= 1, m2 <<= 1)
    {
        // v = Complex (-2.0 * sin (0.5 * pi / m) * sin (0.5 * pi / m), -sin (pi / m))
        double sm = sin(0.5 * pi / m);
        Complex v = MakeComplex(-2.0*sm*sm, -sin(pi / m));
        Complex w = MakeComplex(1.0, 0.0);
        for(i = 0;i < m;i++)
        {
            for(k = i;k < n;k += m2)
            {
                Complex t;
                mk = k + m;
                t = c_mul(w, cplx[mk]);
                cplx[mk] = c_sub(cplx[k], t);
                cplx[k] = c_add(cplx[k], t);
            }
            w = c_add(w, c_mul(v, w));
        }
    }
}

// Performs a forward FFT.
static void FftForward(const uint n, Complex *inout)
{
    FftArrange(n, inout);
    FftSummation(n, 1.0, inout);
}

// Performs an inverse FFT.
static void FftInverse(const uint n, Complex *inout)
{
    double f;
    uint i;

    FftArrange(n, inout);
    FftSummation(n, -1.0, inout);
    f = 1.0 / n;
    for(i = 0;i < n;i++)
        inout[i] = c_muls(inout[i], f);
}

/* Calculate the complex helical sequence (or discrete-time analytical signal)
 * of the given input using the Hilbert transform. Given the natural logarithm
 * of a signal's magnitude response, the imaginary components can be used as
 * the angles for minimum-phase reconstruction.
 */
static void Hilbert(const uint n, Complex *inout)
{
    uint i;

    // Handle in-place operation.
    for(i = 0;i < n;i++)
        inout[i].Imag = 0.0;

    FftInverse(n, inout);
    for(i = 1;i < (n+1)/2;i++)
        inout[i] = c_muls(inout[i], 2.0);
    /* Increment i if n is even. */
    i += (n&1)^1;
    for(;i < n;i++)
        inout[i] = MakeComplex(0.0, 0.0);
    FftForward(n, inout);
}

/* Calculate the magnitude response of the given input.  This is used in
 * place of phase decomposition, since the phase residuals are discarded for
 * minimum phase reconstruction.  The mirrored half of the response is also
 * discarded.
 */
static void MagnitudeResponse(const uint n, const Complex *in, double *out)
{
    const uint m = 1 + (n / 2);
    uint i;
    for(i = 0;i < m;i++)
        out[i] = fmax(c_abs(in[i]), EPSILON);
}

/* Apply a range limit (in dB) to the given magnitude response.  This is used
 * to adjust the effects of the diffuse-field average on the equalization
 * process.
 */
static void LimitMagnitudeResponse(const uint n, const uint m, const double limit, const double *in, double *out)
{
    double halfLim;
    uint i, lower, upper;
    double ave;

    halfLim = limit / 2.0;
    // Convert the response to dB.
    for(i = 0;i < m;i++)
        out[i] = 20.0 * log10(in[i]);
    // Use six octaves to calculate the average magnitude of the signal.
    lower = ((uint)ceil(n / pow(2.0, 8.0))) - 1;
    upper = ((uint)floor(n / pow(2.0, 2.0))) - 1;
    ave = 0.0;
    for(i = lower;i <= upper;i++)
        ave += out[i];
    ave /= upper - lower + 1;
    // Keep the response within range of the average magnitude.
    for(i = 0;i < m;i++)
        out[i] = Clamp(out[i], ave - halfLim, ave + halfLim);
    // Convert the response back to linear magnitude.
    for(i = 0;i < m;i++)
        out[i] = pow(10.0, out[i] / 20.0);
}

/* Reconstructs the minimum-phase component for the given magnitude response
 * of a signal.  This is equivalent to phase recomposition, sans the missing
 * residuals (which were discarded).  The mirrored half of the response is
 * reconstructed.
 */
static void MinimumPhase(const uint n, const double *in, Complex *out)
{
    const uint m = 1 + (n / 2);
    double *mags;
    uint i;

    mags = CreateDoubles(n);
    for(i = 0;i < m;i++)
    {
        mags[i] = fmax(EPSILON, in[i]);
        out[i] = MakeComplex(log(mags[i]), 0.0);
    }
    for(;i < n;i++)
    {
        mags[i] = mags[n - i];
        out[i] = out[n - i];
    }
    Hilbert(n, out);
    // Remove any DC offset the filter has.
    mags[0] = EPSILON;
    for(i = 0;i < n;i++)
    {
        Complex a = c_exp(MakeComplex(0.0, out[i].Imag));
        out[i] = c_mul(MakeComplex(mags[i], 0.0), a);
    }
    free(mags);
}


/***************************
 *** Resampler functions ***
 ***************************/

/* This is the normalized cardinal sine (sinc) function.
 *
 *   sinc(x) = { 1,                   x = 0
 *             { sin(pi x) / (pi x),  otherwise.
 */
static double Sinc(const double x)
{
    if(fabs(x) < EPSILON)
        return 1.0;
    return sin(M_PI * x) / (M_PI * x);
}

/* The zero-order modified Bessel function of the first kind, used for the
 * Kaiser window.
 *
 *   I_0(x) = sum_{k=0}^inf (1 / k!)^2 (x / 2)^(2 k)
 *          = sum_{k=0}^inf ((x / 2)^k / k!)^2
 */
static double BesselI_0(const double x)
{
    double term, sum, x2, y, last_sum;
    int k;

    // Start at k=1 since k=0 is trivial.
    term = 1.0;
    sum = 1.0;
    x2 = x/2.0;
    k = 1;

    // Let the integration converge until the term of the sum is no longer
    // significant.
    do {
        y = x2 / k;
        k++;
        last_sum = sum;
        term *= y * y;
        sum += term;
    } while(sum != last_sum);
    return sum;
}

/* Calculate a Kaiser window from the given beta value and a normalized k
 * [-1, 1].
 *
 *   w(k) = { I_0(B sqrt(1 - k^2)) / I_0(B),  -1 <= k <= 1
 *          { 0,                              elsewhere.
 *
 * Where k can be calculated as:
 *
 *   k = i / l,         where -l <= i <= l.
 *
 * or:
 *
 *   k = 2 i / M - 1,   where 0 <= i <= M.
 */
static double Kaiser(const double b, const double k)
{
    if(!(k >= -1.0 && k <= 1.0))
        return 0.0;
    return BesselI_0(b * sqrt(1.0 - k*k)) / BesselI_0(b);
}

// Calculates the greatest common divisor of a and b.
static uint Gcd(uint x, uint y)
{
    while(y > 0)
    {
        uint z = y;
        y = x % y;
        x = z;
    }
    return x;
}

/* Calculates the size (order) of the Kaiser window.  Rejection is in dB and
 * the transition width is normalized frequency (0.5 is nyquist).
 *
 *   M = { ceil((r - 7.95) / (2.285 2 pi f_t)),  r > 21
 *       { ceil(5.79 / 2 pi f_t),                r <= 21.
 *
 */
static uint CalcKaiserOrder(const double rejection, const double transition)
{
    double w_t = 2.0 * M_PI * transition;
    if(rejection > 21.0)
        return (uint)ceil((rejection - 7.95) / (2.285 * w_t));
    return (uint)ceil(5.79 / w_t);
}

// Calculates the beta value of the Kaiser window.  Rejection is in dB.
static double CalcKaiserBeta(const double rejection)
{
    if(rejection > 50.0)
        return 0.1102 * (rejection - 8.7);
    if(rejection >= 21.0)
        return (0.5842 * pow(rejection - 21.0, 0.4)) +
               (0.07886 * (rejection - 21.0));
    return 0.0;
}

/* Calculates a point on the Kaiser-windowed sinc filter for the given half-
 * width, beta, gain, and cutoff.  The point is specified in non-normalized
 * samples, from 0 to M, where M = (2 l + 1).
 *
 *   w(k) 2 p f_t sinc(2 f_t x)
 *
 *   x    -- centered sample index (i - l)
 *   k    -- normalized and centered window index (x / l)
 *   w(k) -- window function (Kaiser)
 *   p    -- gain compensation factor when sampling
 *   f_t  -- normalized center frequency (or cutoff; 0.5 is nyquist)
 */
static double SincFilter(const int l, const double b, const double gain, const double cutoff, const int i)
{
    return Kaiser(b, (double)(i - l) / l) * 2.0 * gain * cutoff * Sinc(2.0 * cutoff * (i - l));
}

/* This is a polyphase sinc-filtered resampler.
 *
 *              Upsample                      Downsample
 *
 *              p/q = 3/2                     p/q = 3/5
 *
 *          M-+-+-+->                     M-+-+-+->
 *         -------------------+          ---------------------+
 *   p  s * f f f f|f|        |    p  s * f f f f f           |
 *   |  0 *   0 0 0|0|0       |    |  0 *   0 0 0 0|0|        |
 *   v  0 *     0 0|0|0 0     |    v  0 *     0 0 0|0|0       |
 *      s *       f|f|f f f   |       s *       f f|f|f f     |
 *      0 *        |0|0 0 0 0 |       0 *         0|0|0 0 0   |
 *         --------+=+--------+       0 *          |0|0 0 0 0 |
 *          d . d .|d|. d . d            ----------+=+--------+
 *                                        d . . . .|d|. . . .
 *          q->
 *                                        q-+-+-+->
 *
 *   P_f(i,j) = q i mod p + pj
 *   P_s(i,j) = floor(q i / p) - j
 *   d[i=0..N-1] = sum_{j=0}^{floor((M - 1) / p)} {
 *                   { f[P_f(i,j)] s[P_s(i,j)],  P_f(i,j) < M
 *                   { 0,                        P_f(i,j) >= M. }
 */

// Calculate the resampling metrics and build the Kaiser-windowed sinc filter
// that's used to cut frequencies above the destination nyquist.
static void ResamplerSetup(ResamplerT *rs, const uint srcRate, const uint dstRate)
{
    double cutoff, width, beta;
    uint gcd, l;
    int i;

    gcd = Gcd(srcRate, dstRate);
    rs->mP = dstRate / gcd;
    rs->mQ = srcRate / gcd;
    /* The cutoff is adjusted by half the transition width, so the transition
     * ends before the nyquist (0.5).  Both are scaled by the downsampling
     * factor.
     */
    if(rs->mP > rs->mQ)
    {
        cutoff = 0.475 / rs->mP;
        width = 0.05 / rs->mP;
    }
    else
    {
        cutoff = 0.475 / rs->mQ;
        width = 0.05 / rs->mQ;
    }
    // A rejection of -180 dB is used for the stop band. Round up when
    // calculating the left offset to avoid increasing the transition width.
    l = (CalcKaiserOrder(180.0, width)+1) / 2;
    beta = CalcKaiserBeta(180.0);
    rs->mM = l*2 + 1;
    rs->mL = l;
    rs->mF = CreateDoubles(rs->mM);
    for(i = 0;i < ((int)rs->mM);i++)
        rs->mF[i] = SincFilter((int)l, beta, rs->mP, cutoff, i);
}

// Clean up after the resampler.
static void ResamplerClear(ResamplerT *rs)
{
    free(rs->mF);
    rs->mF = NULL;
}

// Perform the upsample-filter-downsample resampling operation using a
// polyphase filter implementation.
static void ResamplerRun(ResamplerT *rs, const uint inN, const double *in, const uint outN, double *out)
{
    const uint p = rs->mP, q = rs->mQ, m = rs->mM, l = rs->mL;
    const double *f = rs->mF;
    uint j_f, j_s;
    double *work;
    uint i;

    if(outN == 0)
        return;

    // Handle in-place operation.
    if(in == out)
        work = CreateDoubles(outN);
    else
        work = out;
    // Resample the input.
    for(i = 0;i < outN;i++)
    {
        double r = 0.0;
        // Input starts at l to compensate for the filter delay.  This will
        // drop any build-up from the first half of the filter.
        j_f = (l + (q * i)) % p;
        j_s = (l + (q * i)) / p;
        while(j_f < m)
        {
            // Only take input when 0 <= j_s < inN.  This single unsigned
            // comparison catches both cases.
            if(j_s < inN)
                r += f[j_f] * in[j_s];
            j_f += p;
            j_s--;
        }
        work[i] = r;
    }
    // Clean up after in-place operation.
    if(work != out)
    {
        for(i = 0;i < outN;i++)
            out[i] = work[i];
        free(work);
    }
}

/*************************
 *** File source input ***
 *************************/

// Read a binary value of the specified byte order and byte size from a file,
// storing it as a 32-bit unsigned integer.
static int ReadBin4(FILE *fp, const char *filename, const ByteOrderT order, const uint bytes, uint32 *out)
{
    uint8 in[4];
    uint32 accum;
    uint i;

    if(fread(in, 1, bytes, fp) != bytes)
    {
        fprintf(stderr, "Error: Bad read from file '%s'.\n", filename);
        return 0;
    }
    accum = 0;
    switch(order)
    {
        case BO_LITTLE:
            for(i = 0;i < bytes;i++)
                accum = (accum<<8) | in[bytes - i - 1];
            break;
        case BO_BIG:
            for(i = 0;i < bytes;i++)
                accum = (accum<<8) | in[i];
            break;
        default:
            break;
    }
    *out = accum;
    return 1;
}

// Read a binary value of the specified byte order from a file, storing it as
// a 64-bit unsigned integer.
static int ReadBin8(FILE *fp, const char *filename, const ByteOrderT order, uint64 *out)
{
    uint8 in [8];
    uint64 accum;
    uint i;

    if(fread(in, 1, 8, fp) != 8)
    {
        fprintf(stderr, "Error: Bad read from file '%s'.\n", filename);
        return 0;
    }
    accum = 0ULL;
    switch(order)
    {
        case BO_LITTLE:
            for(i = 0;i < 8;i++)
                accum = (accum<<8) | in[8 - i - 1];
            break;
        case BO_BIG:
            for(i = 0;i < 8;i++)
                accum = (accum<<8) | in[i];
            break;
        default:
            break;
    }
    *out = accum;
    return 1;
}

/* Read a binary value of the specified type, byte order, and byte size from
 * a file, converting it to a double.  For integer types, the significant
 * bits are used to normalize the result.  The sign of bits determines
 * whether they are padded toward the MSB (negative) or LSB (positive).
 * Floating-point types are not normalized.
 */
static int ReadBinAsDouble(FILE *fp, const char *filename, const ByteOrderT order, const ElementTypeT type, const uint bytes, const int bits, double *out)
{
    union {
        uint32 ui;
        int32 i;
        float f;
    } v4;
    union {
        uint64 ui;
        double f;
    } v8;

    *out = 0.0;
    if(bytes > 4)
    {
        if(!ReadBin8(fp, filename, order, &v8.ui))
            return 0;
        if(type == ET_FP)
            *out = v8.f;
    }
    else
    {
        if(!ReadBin4(fp, filename, order, bytes, &v4.ui))
            return 0;
        if(type == ET_FP)
            *out = v4.f;
        else
        {
            if(bits > 0)
                v4.ui >>= (8*bytes) - ((uint)bits);
            else
                v4.ui &= (0xFFFFFFFF >> (32+bits));

            if(v4.ui&(uint)(1<<(abs(bits)-1)))
                v4.ui |= (0xFFFFFFFF << abs (bits));
            *out = v4.i / (double)(1<<(abs(bits)-1));
        }
    }
    return 1;
}

/* Read an ascii value of the specified type from a file, converting it to a
 * double.  For integer types, the significant bits are used to normalize the
 * result.  The sign of the bits should always be positive.  This also skips
 * up to one separator character before the element itself.
 */
static int ReadAsciiAsDouble(TokenReaderT *tr, const char *filename, const ElementTypeT type, const uint bits, double *out)
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
        if(!TrReadFloat(tr, -HUGE_VAL, HUGE_VAL, out))
        {
            fprintf(stderr, "Error: Bad read from file '%s'.\n", filename);
            return 0;
        }
    }
    else
    {
        int v;
        if(!TrReadInt(tr, -(1<<(bits-1)), (1<<(bits-1))-1, &v))
        {
            fprintf(stderr, "Error: Bad read from file '%s'.\n", filename);
            return 0;
        }
        *out = v / (double)((1<<(bits-1))-1);
    }
    return 1;
}

// Read the RIFF/RIFX WAVE format chunk from a file, validating it against
// the source parameters and data set metrics.
static int ReadWaveFormat(FILE *fp, const ByteOrderT order, const uint hrirRate, SourceRefT *src)
{
    uint32 fourCC, chunkSize;
    uint32 format, channels, rate, dummy, block, size, bits;

    chunkSize = 0;
    do {
        if(chunkSize > 0)
            fseek (fp, (long) chunkSize, SEEK_CUR);
        if(!ReadBin4(fp, src->mPath, BO_LITTLE, 4, &fourCC) ||
           !ReadBin4(fp, src->mPath, order, 4, &chunkSize))
            return 0;
    } while(fourCC != FOURCC_FMT);
    if(!ReadBin4(fp, src->mPath, order, 2, &format) ||
       !ReadBin4(fp, src->mPath, order, 2, &channels) ||
       !ReadBin4(fp, src->mPath, order, 4, &rate) ||
       !ReadBin4(fp, src->mPath, order, 4, &dummy) ||
       !ReadBin4(fp, src->mPath, order, 2, &block))
        return 0;
    block /= channels;
    if(chunkSize > 14)
    {
        if(!ReadBin4(fp, src->mPath, order, 2, &size))
            return 0;
        size /= 8;
        if(block > size)
            size = block;
    }
    else
        size = block;
    if(format == WAVE_FORMAT_EXTENSIBLE)
    {
        fseek(fp, 2, SEEK_CUR);
        if(!ReadBin4(fp, src->mPath, order, 2, &bits))
            return 0;
        if(bits == 0)
            bits = 8 * size;
        fseek(fp, 4, SEEK_CUR);
        if(!ReadBin4(fp, src->mPath, order, 2, &format))
            return 0;
        fseek(fp, (long)(chunkSize - 26), SEEK_CUR);
    }
    else
    {
        bits = 8 * size;
        if(chunkSize > 14)
            fseek(fp, (long)(chunkSize - 16), SEEK_CUR);
        else
            fseek(fp, (long)(chunkSize - 14), SEEK_CUR);
    }
    if(format != WAVE_FORMAT_PCM && format != WAVE_FORMAT_IEEE_FLOAT)
    {
        fprintf(stderr, "Error: Unsupported WAVE format in file '%s'.\n", src->mPath);
        return 0;
    }
    if(src->mChannel >= channels)
    {
        fprintf(stderr, "Error: Missing source channel in WAVE file '%s'.\n", src->mPath);
        return 0;
    }
    if(rate != hrirRate)
    {
        fprintf(stderr, "Error: Mismatched source sample rate in WAVE file '%s'.\n", src->mPath);
        return 0;
    }
    if(format == WAVE_FORMAT_PCM)
    {
        if(size < 2 || size > 4)
        {
            fprintf(stderr, "Error: Unsupported sample size in WAVE file '%s'.\n", src->mPath);
            return 0;
        }
        if(bits < 16 || bits > (8*size))
        {
            fprintf (stderr, "Error:  Bad significant bits in WAVE file '%s'.\n", src->mPath);
            return 0;
        }
        src->mType = ET_INT;
    }
    else
    {
        if(size != 4 && size != 8)
        {
            fprintf(stderr, "Error: Unsupported sample size in WAVE file '%s'.\n", src->mPath);
            return 0;
        }
        src->mType = ET_FP;
    }
    src->mSize = size;
    src->mBits = (int)bits;
    src->mSkip = channels;
    return 1;
}

// Read a RIFF/RIFX WAVE data chunk, converting all elements to doubles.
static int ReadWaveData(FILE *fp, const SourceRefT *src, const ByteOrderT order, const uint n, double *hrir)
{
    int pre, post, skip;
    uint i;

    pre = (int)(src->mSize * src->mChannel);
    post = (int)(src->mSize * (src->mSkip - src->mChannel - 1));
    skip = 0;
    for(i = 0;i < n;i++)
    {
        skip += pre;
        if(skip > 0)
            fseek(fp, skip, SEEK_CUR);
        if(!ReadBinAsDouble(fp, src->mPath, order, src->mType, src->mSize, src->mBits, &hrir[i]))
            return 0;
        skip = post;
    }
    if(skip > 0)
        fseek(fp, skip, SEEK_CUR);
    return 1;
}

// Read the RIFF/RIFX WAVE list or data chunk, converting all elements to
// doubles.
static int ReadWaveList(FILE *fp, const SourceRefT *src, const ByteOrderT order, const uint n, double *hrir)
{
    uint32 fourCC, chunkSize, listSize, count;
    uint block, skip, offset, i;
    double lastSample;

    for(;;)
    {
        if(!ReadBin4(fp, src->mPath, BO_LITTLE, 4, &fourCC) ||
           !ReadBin4(fp, src->mPath, order, 4, &chunkSize))
            return 0;

        if(fourCC == FOURCC_DATA)
        {
            block = src->mSize * src->mSkip;
            count = chunkSize / block;
            if(count < (src->mOffset + n))
            {
                fprintf(stderr, "Error: Bad read from file '%s'.\n", src->mPath);
                return 0;
            }
            fseek(fp, (long)(src->mOffset * block), SEEK_CUR);
            if(!ReadWaveData(fp, src, order, n, &hrir[0]))
                return 0;
            return 1;
        }
        else if(fourCC == FOURCC_LIST)
        {
            if(!ReadBin4(fp, src->mPath, BO_LITTLE, 4, &fourCC))
                return 0;
            chunkSize -= 4;
            if(fourCC == FOURCC_WAVL)
                break;
        }
        if(chunkSize > 0)
            fseek(fp, (long)chunkSize, SEEK_CUR);
    }
    listSize = chunkSize;
    block = src->mSize * src->mSkip;
    skip = src->mOffset;
    offset = 0;
    lastSample = 0.0;
    while(offset < n && listSize > 8)
    {
        if(!ReadBin4(fp, src->mPath, BO_LITTLE, 4, &fourCC) ||
           !ReadBin4(fp, src->mPath, order, 4, &chunkSize))
            return 0;
        listSize -= 8 + chunkSize;
        if(fourCC == FOURCC_DATA)
        {
            count = chunkSize / block;
            if(count > skip)
            {
                fseek(fp, (long)(skip * block), SEEK_CUR);
                chunkSize -= skip * block;
                count -= skip;
                skip = 0;
                if(count > (n - offset))
                    count = n - offset;
                if(!ReadWaveData(fp, src, order, count, &hrir[offset]))
                    return 0;
                chunkSize -= count * block;
                offset += count;
                lastSample = hrir [offset - 1];
            }
            else
            {
                skip -= count;
                count = 0;
            }
        }
        else if(fourCC == FOURCC_SLNT)
        {
            if(!ReadBin4(fp, src->mPath, order, 4, &count))
                return 0;
            chunkSize -= 4;
            if(count > skip)
            {
                count -= skip;
                skip = 0;
                if(count > (n - offset))
                    count = n - offset;
                for(i = 0; i < count; i ++)
                    hrir[offset + i] = lastSample;
                offset += count;
            }
            else
            {
                skip -= count;
                count = 0;
            }
        }
        if(chunkSize > 0)
            fseek(fp, (long)chunkSize, SEEK_CUR);
    }
    if(offset < n)
    {
        fprintf(stderr, "Error: Bad read from file '%s'.\n", src->mPath);
        return 0;
    }
    return 1;
}

// Load a source HRIR from a RIFF/RIFX WAVE file.
static int LoadWaveSource(FILE *fp, SourceRefT *src, const uint hrirRate, const uint n, double *hrir)
{
    uint32 fourCC, dummy;
    ByteOrderT order;

    if(!ReadBin4(fp, src->mPath, BO_LITTLE, 4, &fourCC) ||
       !ReadBin4(fp, src->mPath, BO_LITTLE, 4, &dummy))
        return 0;
    if(fourCC == FOURCC_RIFF)
        order = BO_LITTLE;
    else if(fourCC == FOURCC_RIFX)
        order = BO_BIG;
    else
    {
        fprintf(stderr, "Error: No RIFF/RIFX chunk in file '%s'.\n", src->mPath);
        return 0;
    }

    if(!ReadBin4(fp, src->mPath, BO_LITTLE, 4, &fourCC))
        return 0;
    if(fourCC != FOURCC_WAVE)
    {
        fprintf(stderr, "Error: Not a RIFF/RIFX WAVE file '%s'.\n", src->mPath);
        return 0;
    }
    if(!ReadWaveFormat(fp, order, hrirRate, src))
        return 0;
    if(!ReadWaveList(fp, src, order, n, hrir))
        return 0;
    return 1;
}

// Load a source HRIR from a binary file.
static int LoadBinarySource(FILE *fp, const SourceRefT *src, const ByteOrderT order, const uint n, double *hrir)
{
    uint i;

    fseek(fp, (long)src->mOffset, SEEK_SET);
    for(i = 0;i < n;i++)
    {
        if(!ReadBinAsDouble(fp, src->mPath, order, src->mType, src->mSize, src->mBits, &hrir[i]))
            return 0;
        if(src->mSkip > 0)
            fseek(fp, (long)src->mSkip, SEEK_CUR);
    }
    return 1;
}

// Load a source HRIR from an ASCII text file containing a list of elements
// separated by whitespace or common list operators (',', ';', ':', '|').
static int LoadAsciiSource(FILE *fp, const SourceRefT *src, const uint n, double *hrir)
{
    TokenReaderT tr;
    uint i, j;
    double dummy;

    TrSetup(fp, NULL, &tr);
    for(i = 0;i < src->mOffset;i++)
    {
        if(!ReadAsciiAsDouble(&tr, src->mPath, src->mType, (uint)src->mBits, &dummy))
            return 0;
    }
    for(i = 0;i < n;i++)
    {
        if(!ReadAsciiAsDouble(&tr, src->mPath, src->mType, (uint)src->mBits, &hrir[i]))
            return 0;
        for(j = 0;j < src->mSkip;j++)
        {
            if(!ReadAsciiAsDouble(&tr, src->mPath, src->mType, (uint)src->mBits, &dummy))
                return 0;
        }
    }
    return 1;
}

// Load a source HRIR from a supported file type.
static int LoadSource(SourceRefT *src, const uint hrirRate, const uint n, double *hrir)
{
    int result;
    FILE *fp;

    if(src->mFormat == SF_ASCII)
        fp = fopen(src->mPath, "r");
    else
        fp = fopen(src->mPath, "rb");
    if(fp == NULL)
    {
        fprintf(stderr, "Error: Could not open source file '%s'.\n", src->mPath);
        return 0;
    }
    if(src->mFormat == SF_WAVE)
        result = LoadWaveSource(fp, src, hrirRate, n, hrir);
    else if(src->mFormat == SF_BIN_LE)
        result = LoadBinarySource(fp, src, BO_LITTLE, n, hrir);
    else if(src->mFormat == SF_BIN_BE)
        result = LoadBinarySource(fp, src, BO_BIG, n, hrir);
    else
        result = LoadAsciiSource(fp, src, n, hrir);
    fclose(fp);
    return result;
}


/***************************
 *** File storage output ***
 ***************************/

// Write an ASCII string to a file.
static int WriteAscii(const char *out, FILE *fp, const char *filename)
{
    size_t len;

    len = strlen(out);
    if(fwrite(out, 1, len, fp) != len)
    {
        fclose(fp);
        fprintf(stderr, "Error: Bad write to file '%s'.\n", filename);
        return 0;
    }
    return 1;
}

// Write a binary value of the given byte order and byte size to a file,
// loading it from a 32-bit unsigned integer.
static int WriteBin4(const ByteOrderT order, const uint bytes, const uint32 in, FILE *fp, const char *filename)
{
    uint8 out[4];
    uint i;

    switch(order)
    {
        case BO_LITTLE:
            for(i = 0;i < bytes;i++)
                out[i] = (in>>(i*8)) & 0x000000FF;
            break;
        case BO_BIG:
            for(i = 0;i < bytes;i++)
                out[bytes - i - 1] = (in>>(i*8)) & 0x000000FF;
            break;
        default:
            break;
    }
    if(fwrite(out, 1, bytes, fp) != bytes)
    {
        fprintf(stderr, "Error: Bad write to file '%s'.\n", filename);
        return 0;
    }
    return 1;
}

// Store the OpenAL Soft HRTF data set.
static int StoreMhr(const HrirDataT *hData, const char *filename)
{
    uint channels = (hData->mChannelType == CT_STEREO) ? 2 : 1;
    uint n = hData->mIrPoints;
    FILE *fp;
    uint fi, ei, ai, i;
    uint dither_seed = 22222;

    if((fp=fopen(filename, "wb")) == NULL)
    {
        fprintf(stderr, "Error: Could not open MHR file '%s'.\n", filename);
        return 0;
    }
    if(!WriteAscii(MHR_FORMAT, fp, filename))
        return 0;
    if(!WriteBin4(BO_LITTLE, 4, (uint32)hData->mIrRate, fp, filename))
        return 0;
    if(!WriteBin4(BO_LITTLE, 1, (uint32)hData->mSampleType, fp, filename))
        return 0;
    if(!WriteBin4(BO_LITTLE, 1, (uint32)hData->mChannelType, fp, filename))
        return 0;
    if(!WriteBin4(BO_LITTLE, 1, (uint32)hData->mIrPoints, fp, filename))
        return 0;
    if(!WriteBin4(BO_LITTLE, 1, (uint32)hData->mFdCount, fp, filename))
        return 0;
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        if(!WriteBin4(BO_LITTLE, 2, (uint32)(1000.0 * hData->mFds[fi].mDistance), fp, filename))
            return 0;
        if(!WriteBin4(BO_LITTLE, 1, (uint32)hData->mFds[fi].mEvCount, fp, filename))
            return 0;
        for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
        {
            if(!WriteBin4(BO_LITTLE, 1, (uint32)hData->mFds[fi].mEvs[ei].mAzCount, fp, filename))
                return 0;
        }
    }

    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        const double scale = (hData->mSampleType == ST_S16) ? 32767.0 :
                             ((hData->mSampleType == ST_S24) ? 8388607.0 : 0.0);
        const int bps = (hData->mSampleType == ST_S16) ? 2 :
                        ((hData->mSampleType == ST_S24) ? 3 : 0);

        for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];
                double out[2 * MAX_TRUNCSIZE];

                TpdfDither(out, azd->mIrs[0], scale, n, channels, &dither_seed);
                if(hData->mChannelType == CT_STEREO)
                    TpdfDither(out+1, azd->mIrs[1], scale, n, channels, &dither_seed);
                for(i = 0;i < (channels * n);i++)
                {
                    int v = (int)Clamp(out[i], -scale-1.0, scale);
                    if(!WriteBin4(BO_LITTLE, bps, (uint32)v, fp, filename))
                        return 0;
                }
            }
        }
    }
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];
                int v = (int)fmin(round(hData->mIrRate * azd->mDelays[0]), MAX_HRTD);

                if(!WriteBin4(BO_LITTLE, 1, (uint32)v, fp, filename))
                    return 0;
                if(hData->mChannelType == CT_STEREO)
                {
                    v = (int)fmin(round(hData->mIrRate * azd->mDelays[1]), MAX_HRTD);

                    if(!WriteBin4(BO_LITTLE, 1, (uint32)v, fp, filename))
                        return 0;
                }
            }
        }
    }
    fclose(fp);
    return 1;
}


/***********************
 *** HRTF processing ***
 ***********************/

// Calculate the onset time of an HRIR and average it with any existing
// timing for its field, elevation, azimuth, and ear.
static double AverageHrirOnset(const uint rate, const uint n, const double *hrir, const double f, const double onset)
{
    double mag = 0.0;
    uint i;

    for(i = 0;i < n;i++)
        mag = fmax(fabs(hrir[i]), mag);
    mag *= 0.15;
    for(i = 0;i < n;i++)
    {
        if(fabs(hrir[i]) >= mag)
            break;
    }
    return Lerp(onset, (double)i / rate, f);
}

// Calculate the magnitude response of an HRIR and average it with any
// existing responses for its field, elevation, azimuth, and ear.
static void AverageHrirMagnitude(const uint points, const uint n, const double *hrir, const double f, double *mag)
{
    uint m = 1 + (n / 2), i;
    Complex *h = CreateComplexes(n);
    double *r = CreateDoubles(n);

    for(i = 0;i < points;i++)
        h[i] = MakeComplex(hrir[i], 0.0);
    for(;i < n;i++)
        h[i] = MakeComplex(0.0, 0.0);
    FftForward(n, h);
    MagnitudeResponse(n, h, r);
    for(i = 0;i < m;i++)
        mag[i] = Lerp(mag[i], r[i], f);
    free(r);
    free(h);
}

/* Calculate the contribution of each HRIR to the diffuse-field average based
 * on the area of its surface patch.  All patches are centered at the HRIR
 * coordinates on the unit sphere and are measured by solid angle.
 */
static void CalculateDfWeights(const HrirDataT *hData, double *weights)
{
    double sum, evs, ev, upperEv, lowerEv, solidAngle;
    uint fi, ei;

    sum = 0.0;
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        evs = M_PI / 2.0 / (hData->mFds[fi].mEvCount - 1);
        for(ei = hData->mFds[fi].mEvStart;ei < hData->mFds[fi].mEvCount;ei++)
        {
            // For each elevation, calculate the upper and lower limits of
            // the patch band.
            ev = hData->mFds[fi].mEvs[ei].mElevation;
            lowerEv = fmax(-M_PI / 2.0, ev - evs);
            upperEv = fmin(M_PI / 2.0, ev + evs);
            // Calculate the area of the patch band.
            solidAngle = 2.0 * M_PI * (sin(upperEv) - sin(lowerEv));
            // Each weight is the area of one patch.
            weights[(fi * MAX_EV_COUNT) + ei] = solidAngle / hData->mFds[fi].mEvs[ei].mAzCount;
            // Sum the total surface area covered by the HRIRs of all fields.
            sum += solidAngle;
        }
    }
    /* TODO: It may be interesting to experiment with how a volume-based
             weighting performs compared to the existing distance-indepenent
             surface patches.
     */
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        // Normalize the weights given the total surface coverage for all
        // fields.
        for(ei = hData->mFds[fi].mEvStart;ei < hData->mFds[fi].mEvCount;ei++)
            weights[(fi * MAX_EV_COUNT) + ei] /= sum;
    }
}

/* Calculate the diffuse-field average from the given magnitude responses of
 * the HRIR set.  Weighting can be applied to compensate for the varying
 * surface area covered by each HRIR.  The final average can then be limited
 * by the specified magnitude range (in positive dB; 0.0 to skip).
 */
static void CalculateDiffuseFieldAverage(const HrirDataT *hData, const uint channels, const uint m, const int weighted, const double limit, double *dfa)
{
    double *weights = CreateDoubles(hData->mFdCount * MAX_EV_COUNT);
    uint count, ti, fi, ei, i, ai;

    if(weighted)
    {
        // Use coverage weighting to calculate the average.
        CalculateDfWeights(hData, weights);
    }
    else
    {
        double weight;

        // If coverage weighting is not used, the weights still need to be
        // averaged by the number of existing HRIRs.
        count = hData->mIrCount;
        for(fi = 0;fi < hData->mFdCount;fi++)
        {
            for(ei = 0;ei < hData->mFds[fi].mEvStart;ei++)
                count -= hData->mFds[fi].mEvs[ei].mAzCount;
        }
        weight = 1.0 / count;

        for(fi = 0;fi < hData->mFdCount;fi++)
        {
            for(ei = hData->mFds[fi].mEvStart;ei < hData->mFds[fi].mEvCount;ei++)
                weights[(fi * MAX_EV_COUNT) + ei] = weight;
        }
    }
    for(ti = 0;ti < channels;ti++)
    {
        for(i = 0;i < m;i++)
            dfa[(ti * m) + i] = 0.0;
        for(fi = 0;fi < hData->mFdCount;fi++)
        {
            for(ei = hData->mFds[fi].mEvStart;ei < hData->mFds[fi].mEvCount;ei++)
            {
                for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
                {
                    HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];
                    // Get the weight for this HRIR's contribution.
                    double weight = weights[(fi * MAX_EV_COUNT) + ei];

                    // Add this HRIR's weighted power average to the total.
                    for(i = 0;i < m;i++)
                        dfa[(ti * m) + i] += weight * azd->mIrs[ti][i] * azd->mIrs[ti][i];
                }
            }
        }
        // Finish the average calculation and keep it from being too small.
        for(i = 0;i < m;i++)
            dfa[(ti * m) + i] = fmax(sqrt(dfa[(ti * m) + i]), EPSILON);
        // Apply a limit to the magnitude range of the diffuse-field average
        // if desired.
        if(limit > 0.0)
            LimitMagnitudeResponse(hData->mFftSize, m, limit, &dfa[ti * m], &dfa[ti * m]);
    }
    free(weights);
}

// Perform diffuse-field equalization on the magnitude responses of the HRIR
// set using the given average response.
static void DiffuseFieldEqualize(const uint channels, const uint m, const double *dfa, const HrirDataT *hData)
{
    uint ti, fi, ei, ai, i;

    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        for(ei = hData->mFds[fi].mEvStart;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                for(ti = 0;ti < channels;ti++)
                {
                    for(i = 0;i < m;i++)
                        azd->mIrs[ti][i] /= dfa[(ti * m) + i];
                }
            }
        }
    }
}

// Perform minimum-phase reconstruction using the magnitude responses of the
// HRIR set.
static void ReconstructHrirs(const HrirDataT *hData)
{
    uint channels = (hData->mChannelType == CT_STEREO) ? 2 : 1;
    uint n = hData->mFftSize;
    uint ti, fi, ei, ai, i;
    Complex *h = CreateComplexes(n);
    uint total, count, pcdone, lastpc;

    total = hData->mIrCount;
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        for(ei = 0;ei < hData->mFds[fi].mEvStart;ei++)
            total -= hData->mFds[fi].mEvs[ei].mAzCount;
    }
    total *= channels;
    count = pcdone = lastpc = 0;
    printf("%3d%% done.", pcdone);
    fflush(stdout);
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        for(ei = hData->mFds[fi].mEvStart;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                for(ti = 0;ti < channels;ti++)
                {
                    MinimumPhase(n, azd->mIrs[ti], h);
                    FftInverse(n, h);
                    for(i = 0;i < hData->mIrPoints;i++)
                        azd->mIrs[ti][i] = h[i].Real;
                    pcdone = ++count * 100 / total;
                    if(pcdone != lastpc)
                    {
                        lastpc = pcdone;
                        printf("\r%3d%% done.", pcdone);
                        fflush(stdout);
                    }
                }
            }
        }
    }
    printf("\n");
    free(h);
}

// Resamples the HRIRs for use at the given sampling rate.
static void ResampleHrirs(const uint rate, HrirDataT *hData)
{
    uint channels = (hData->mChannelType == CT_STEREO) ? 2 : 1;
    uint n = hData->mIrPoints;
    uint ti, fi, ei, ai;
    ResamplerT rs;

    ResamplerSetup(&rs, hData->mIrRate, rate);
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        for(ei = hData->mFds[fi].mEvStart;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                for(ti = 0;ti < channels;ti++)
                    ResamplerRun(&rs, n, azd->mIrs[ti], n, azd->mIrs[ti]);
            }
        }
    }
    hData->mIrRate = rate;
    ResamplerClear(&rs);
}

/* Given field and elevation indices and an azimuth, calculate the indices of
 * the two HRIRs that bound the coordinate along with a factor for
 * calculating the continuous HRIR using interpolation.
 */
static void CalcAzIndices(const HrirDataT *hData, const uint fi, const uint ei, const double az, uint *a0, uint *a1, double *af)
{
    double f = (2.0*M_PI + az) * hData->mFds[fi].mEvs[ei].mAzCount / (2.0*M_PI);
    uint i = (uint)f % hData->mFds[fi].mEvs[ei].mAzCount;

    f -= floor(f);
    *a0 = i;
    *a1 = (i + 1) % hData->mFds[fi].mEvs[ei].mAzCount;
    *af = f;
}

// Synthesize any missing onset timings at the bottom elevations of each
// field.  This just blends between slightly exaggerated known onsets (not
// an accurate model).
static void SynthesizeOnsets(HrirDataT *hData)
{
    uint channels = (hData->mChannelType == CT_STEREO) ? 2 : 1;
    uint ti, fi, oi, ai, ei, a0, a1;
    double t, of, af;

    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        if(hData->mFds[fi].mEvStart <= 0)
            continue;
        oi = hData->mFds[fi].mEvStart;

        for(ti = 0;ti < channels;ti++)
        {
            t = 0.0;
            for(ai = 0;ai < hData->mFds[fi].mEvs[oi].mAzCount;ai++)
                t += hData->mFds[fi].mEvs[oi].mAzs[ai].mDelays[ti];
            hData->mFds[fi].mEvs[0].mAzs[0].mDelays[ti] = 1.32e-4 + (t / hData->mFds[fi].mEvs[oi].mAzCount);
            for(ei = 1;ei < hData->mFds[fi].mEvStart;ei++)
            {
                of = (double)ei / hData->mFds[fi].mEvStart;
                for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
                {
                    CalcAzIndices(hData, fi, oi, hData->mFds[fi].mEvs[ei].mAzs[ai].mAzimuth, &a0, &a1, &af);
                    hData->mFds[fi].mEvs[ei].mAzs[ai].mDelays[ti] = Lerp(
                        hData->mFds[fi].mEvs[0].mAzs[0].mDelays[ti],
                        Lerp(hData->mFds[fi].mEvs[oi].mAzs[a0].mDelays[ti],
                             hData->mFds[fi].mEvs[oi].mAzs[a1].mDelays[ti], af),
                        of
                    );
                }
            }
        }
    }
}

/* Attempt to synthesize any missing HRIRs at the bottom elevations of each
 * field.  Right now this just blends the lowest elevation HRIRs together and
 * applies some attenuation and high frequency damping.  It is a simple, if
 * inaccurate model.
 */
static void SynthesizeHrirs(HrirDataT *hData)
{
    uint channels = (hData->mChannelType == CT_STEREO) ? 2 : 1;
    uint n = hData->mIrPoints;
    uint ti, fi, ai, ei, i;
    double lp[4], s0, s1;
    double of, b;
    uint a0, a1;
    double af;

    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        const uint oi = hData->mFds[fi].mEvStart;
        if(oi <= 0) continue;

        for(ti = 0;ti < channels;ti++)
        {
            for(i = 0;i < n;i++)
                hData->mFds[fi].mEvs[0].mAzs[0].mIrs[ti][i] = 0.0;
            for(ai = 0;ai < hData->mFds[fi].mEvs[oi].mAzCount;ai++)
            {
                for(i = 0;i < n;i++)
                    hData->mFds[fi].mEvs[0].mAzs[0].mIrs[ti][i] += hData->mFds[fi].mEvs[oi].mAzs[ai].mIrs[ti][i] /
                                                                   hData->mFds[fi].mEvs[oi].mAzCount;
            }
            for(ei = 1;ei < hData->mFds[fi].mEvStart;ei++)
            {
                of = (double)ei / hData->mFds[fi].mEvStart;
                b = (1.0 - of) * (3.5e-6 * hData->mIrRate);
                for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
                {
                    CalcAzIndices(hData, fi, oi, hData->mFds[fi].mEvs[ei].mAzs[ai].mAzimuth, &a0, &a1, &af);
                    lp[0] = 0.0;
                    lp[1] = 0.0;
                    lp[2] = 0.0;
                    lp[3] = 0.0;
                    for(i = 0;i < n;i++)
                    {
                        s0 = hData->mFds[fi].mEvs[0].mAzs[0].mIrs[ti][i];
                        s1 = Lerp(hData->mFds[fi].mEvs[oi].mAzs[a0].mIrs[ti][i],
                                  hData->mFds[fi].mEvs[oi].mAzs[a1].mIrs[ti][i], af);
                        s0 = Lerp(s0, s1, of);
                        lp[0] = Lerp(s0, lp[0], b);
                        lp[1] = Lerp(lp[0], lp[1], b);
                        lp[2] = Lerp(lp[1], lp[2], b);
                        lp[3] = Lerp(lp[2], lp[3], b);
                        hData->mFds[fi].mEvs[ei].mAzs[ai].mIrs[ti][i] = lp[3];
                    }
                }
            }
            b = 3.5e-6 * hData->mIrRate;
            lp[0] = 0.0;
            lp[1] = 0.0;
            lp[2] = 0.0;
            lp[3] = 0.0;
            for(i = 0;i < n;i++)
            {
                s0 = hData->mFds[fi].mEvs[0].mAzs[0].mIrs[ti][i];
                lp[0] = Lerp(s0, lp[0], b);
                lp[1] = Lerp(lp[0], lp[1], b);
                lp[2] = Lerp(lp[1], lp[2], b);
                lp[3] = Lerp(lp[2], lp[3], b);
                hData->mFds[fi].mEvs[0].mAzs[0].mIrs[ti][i] = lp[3];
            }
        }
        hData->mFds[fi].mEvStart = 0;
    }
}

// The following routines assume a full set of HRIRs for all elevations.

// Normalize the HRIR set and slightly attenuate the result.
static void NormalizeHrirs(const HrirDataT *hData)
{
    uint channels = (hData->mChannelType == CT_STEREO) ? 2 : 1;
    uint n = hData->mIrPoints;
    uint ti, fi, ei, ai, i;
    double maxLevel = 0.0;

    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                for(ti = 0;ti < channels;ti++)
                {
                    for(i = 0;i < n;i++)
                        maxLevel = fmax(fabs(azd->mIrs[ti][i]), maxLevel);
                }
            }
        }
    }
    maxLevel = 1.01 * maxLevel;
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                for(ti = 0;ti < channels;ti++)
                {
                    for(i = 0;i < n;i++)
                        azd->mIrs[ti][i] /= maxLevel;
                }
            }
        }
    }
}

// Calculate the left-ear time delay using a spherical head model.
static double CalcLTD(const double ev, const double az, const double rad, const double dist)
{
    double azp, dlp, l, al;

    azp = asin(cos(ev) * sin(az));
    dlp = sqrt((dist*dist) + (rad*rad) + (2.0*dist*rad*sin(azp)));
    l = sqrt((dist*dist) - (rad*rad));
    al = (0.5 * M_PI) + azp;
    if(dlp > l)
        dlp = l + (rad * (al - acos(rad / dist)));
    return dlp / 343.3;
}

// Calculate the effective head-related time delays for each minimum-phase
// HRIR.
static void CalculateHrtds(const HeadModelT model, const double radius, HrirDataT *hData)
{
    uint channels = (hData->mChannelType == CT_STEREO) ? 2 : 1;
    double minHrtd = INFINITY, maxHrtd = -INFINITY;
    uint ti, fi, ei, ai;
    double t;

    if(model == HM_DATASET)
    {
        for(fi = 0;fi < hData->mFdCount;fi++)
        {
            for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
            {
                for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
                {
                    HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                    for(ti = 0;ti < channels;ti++)
                    {
                        t = azd->mDelays[ti] * radius / hData->mRadius;
                        azd->mDelays[ti] = t;
                        maxHrtd = fmax(t, maxHrtd);
                        minHrtd = fmin(t, minHrtd);
                    }
                }
            }
        }
    }
    else
    {
        for(fi = 0;fi < hData->mFdCount;fi++)
        {
            for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
            {
                HrirEvT *evd = &hData->mFds[fi].mEvs[ei];

                for(ai = 0;ai < evd->mAzCount;ai++)
                {
                    HrirAzT *azd = &evd->mAzs[ai];

                    for(ti = 0;ti < channels;ti++)
                    {
                        t = CalcLTD(evd->mElevation, azd->mAzimuth, radius, hData->mFds[fi].mDistance);
                        azd->mDelays[ti] = t;
                        maxHrtd = fmax(t, maxHrtd);
                        minHrtd = fmin(t, minHrtd);
                    }
                }
            }
        }
    }
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ti = 0;ti < channels;ti++)
            {
                for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
                    hData->mFds[fi].mEvs[ei].mAzs[ai].mDelays[ti] -= minHrtd;
            }
        }
    }
}

// Clear the initial HRIR data state.
static void ResetHrirData(HrirDataT *hData)
{
    hData->mIrRate = 0;
    hData->mSampleType = ST_S24;
    hData->mChannelType = CT_NONE;
    hData->mIrPoints = 0;
    hData->mFftSize = 0;
    hData->mIrSize = 0;
    hData->mRadius = 0.0;
    hData->mIrCount = 0;
    hData->mFdCount = 0;
    hData->mFds = NULL;
}

// Allocate and configure dynamic HRIR structures.
static int PrepareHrirData(const uint fdCount, const double distances[MAX_FD_COUNT], const uint evCounts[MAX_FD_COUNT], const uint azCounts[MAX_FD_COUNT * MAX_EV_COUNT], HrirDataT *hData)
{
    uint evTotal = 0, azTotal = 0, fi, ei, ai;

    for(fi = 0;fi < fdCount;fi++)
    {
        evTotal += evCounts[fi];
        for(ei = 0;ei < evCounts[fi];ei++)
            azTotal += azCounts[(fi * MAX_EV_COUNT) + ei];
    }
    if(!fdCount || !evTotal || !azTotal)
        return 0;

    hData->mFds = calloc(fdCount, sizeof(*hData->mFds));
    if(hData->mFds == NULL)
        return 0;
    hData->mFds[0].mEvs = calloc(evTotal, sizeof(*hData->mFds[0].mEvs));
    if(hData->mFds[0].mEvs == NULL)
        return 0;
    hData->mFds[0].mEvs[0].mAzs = calloc(azTotal, sizeof(*hData->mFds[0].mEvs[0].mAzs));
    if(hData->mFds[0].mEvs[0].mAzs == NULL)
        return 0;
    hData->mIrCount = azTotal;
    hData->mFdCount = fdCount;
    evTotal = 0;
    azTotal = 0;
    for(fi = 0;fi < fdCount;fi++)
    {
        hData->mFds[fi].mDistance = distances[fi];
        hData->mFds[fi].mEvCount = evCounts[fi];
        hData->mFds[fi].mEvStart = 0;
        hData->mFds[fi].mEvs = &hData->mFds[0].mEvs[evTotal];
        evTotal += evCounts[fi];
        for(ei = 0;ei < evCounts[fi];ei++)
        {
            uint azCount = azCounts[(fi * MAX_EV_COUNT) + ei];

            hData->mFds[fi].mIrCount += azCount;
            hData->mFds[fi].mEvs[ei].mElevation = -M_PI / 2.0 + M_PI * ei / (evCounts[fi] - 1);
            hData->mFds[fi].mEvs[ei].mIrCount += azCount;
            hData->mFds[fi].mEvs[ei].mAzCount = azCount;
            hData->mFds[fi].mEvs[ei].mAzs = &hData->mFds[0].mEvs[0].mAzs[azTotal];
            for(ai = 0;ai < azCount;ai++)
            {
                hData->mFds[fi].mEvs[ei].mAzs[ai].mAzimuth = 2.0 * M_PI * ai / azCount;
                hData->mFds[fi].mEvs[ei].mAzs[ai].mIndex = azTotal + ai;
                hData->mFds[fi].mEvs[ei].mAzs[ai].mDelays[0] = 0.0;
                hData->mFds[fi].mEvs[ei].mAzs[ai].mDelays[1] = 0.0;
                hData->mFds[fi].mEvs[ei].mAzs[ai].mIrs[0] = NULL;
                hData->mFds[fi].mEvs[ei].mAzs[ai].mIrs[1] = NULL;
            }
            azTotal += azCount;
        }
    }
    return 1;
}

// Clean up HRIR data.
static void FreeHrirData(HrirDataT *hData)
{
    if(hData->mFds != NULL)
    {
        if(hData->mFds[0].mEvs != NULL)
        {
            if(hData->mFds[0].mEvs[0].mAzs)
            {
                free(hData->mFds[0].mEvs[0].mAzs[0].mIrs[0]);
                free(hData->mFds[0].mEvs[0].mAzs);
            }
            free(hData->mFds[0].mEvs);
        }
        free(hData->mFds);
        hData->mFds = NULL;
    }
}

// Match the channel type from a given identifier.
static ChannelTypeT MatchChannelType(const char *ident)
{
    if(strcasecmp(ident, "mono") == 0)
        return CT_MONO;
    if(strcasecmp(ident, "stereo") == 0)
        return CT_STEREO;
    return CT_NONE;
}

// Process the data set definition to read and validate the data set metrics.
static int ProcessMetrics(TokenReaderT *tr, const uint fftSize, const uint truncSize, HrirDataT *hData)
{
    int hasRate = 0, hasType = 0, hasPoints = 0, hasRadius = 0;
    int hasDistance = 0, hasAzimuths = 0;
    char ident[MAX_IDENT_LEN+1];
    uint line, col;
    double fpVal;
    uint points;
    int intVal;
    double distances[MAX_FD_COUNT];
    uint fdCount = 0;
    uint evCounts[MAX_FD_COUNT];
    uint *azCounts = calloc(MAX_FD_COUNT * MAX_EV_COUNT, sizeof(*azCounts));

    if(azCounts == NULL)
    {
        fprintf(stderr, "Error:  Out of memory.\n");
        exit(-1);
    }
    TrIndication(tr, &line, &col);
    while(TrIsIdent(tr))
    {
        TrIndication(tr, &line, &col);
        if(!TrReadIdent(tr, MAX_IDENT_LEN, ident))
            goto error;
        if(strcasecmp(ident, "rate") == 0)
        {
            if(hasRate)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'rate'.\n");
                goto error;
            }
            if(!TrReadOperator(tr, "="))
                goto error;
            if(!TrReadInt(tr, MIN_RATE, MAX_RATE, &intVal))
                goto error;
            hData->mIrRate = (uint)intVal;
            hasRate = 1;
        }
        else if(strcasecmp(ident, "type") == 0)
        {
            char type[MAX_IDENT_LEN+1];

            if(hasType)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'type'.\n");
                goto error;
            }
            if(!TrReadOperator(tr, "="))
                goto error;

            if(!TrReadIdent(tr, MAX_IDENT_LEN, type))
                goto error;
            hData->mChannelType = MatchChannelType(type);
            if(hData->mChannelType == CT_NONE)
            {
                TrErrorAt(tr, line, col, "Expected a channel type.\n");
                goto error;
            }
            hasType = 1;
        }
        else if(strcasecmp(ident, "points") == 0)
        {
            if(hasPoints)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'points'.\n");
                goto error;
            }
            if(!TrReadOperator(tr, "="))
                goto error;
            TrIndication(tr, &line, &col);
            if(!TrReadInt(tr, MIN_POINTS, MAX_POINTS, &intVal))
                goto error;
            points = (uint)intVal;
            if(fftSize > 0 && points > fftSize)
            {
                TrErrorAt(tr, line, col, "Value exceeds the overridden FFT size.\n");
                goto error;
            }
            if(points < truncSize)
            {
                TrErrorAt(tr, line, col, "Value is below the truncation size.\n");
                goto error;
            }
            hData->mIrPoints = points;
            if(fftSize <= 0)
            {
                hData->mFftSize = DEFAULT_FFTSIZE;
                hData->mIrSize = 1 + (DEFAULT_FFTSIZE / 2);
            }
            else
            {
                hData->mFftSize = fftSize;
                hData->mIrSize = 1 + (fftSize / 2);
                if(points > hData->mIrSize)
                    hData->mIrSize = points;
            }
            hasPoints = 1;
        }
        else if(strcasecmp(ident, "radius") == 0)
        {
            if(hasRadius)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'radius'.\n");
                goto error;
            }
            if(!TrReadOperator(tr, "="))
                goto error;
            if(!TrReadFloat(tr, MIN_RADIUS, MAX_RADIUS, &fpVal))
                goto error;
            hData->mRadius = fpVal;
            hasRadius = 1;
        }
        else if(strcasecmp(ident, "distance") == 0)
        {
            uint count = 0;

            if(hasDistance)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'distance'.\n");
                goto error;
            }
            if(!TrReadOperator(tr, "="))
                goto error;

            for(;;)
            {
                if(!TrReadFloat(tr, MIN_DISTANCE, MAX_DISTANCE, &fpVal))
                    goto error;
                if(count > 0 && fpVal <= distances[count - 1])
                {
                    TrError(tr, "Distances are not ascending.\n");
                    goto error;
                }
                distances[count++] = fpVal;
                if(!TrIsOperator(tr, ","))
                    break;
                if(count >= MAX_FD_COUNT)
                {
                    TrError(tr, "Exceeded the maximum of %d fields.\n", MAX_FD_COUNT);
                    goto error;
                }
                TrReadOperator(tr, ",");
            }
            if(fdCount != 0 && count != fdCount)
            {
                TrError(tr, "Did not match the specified number of %d fields.\n", fdCount);
                goto error;
            }
            fdCount = count;
            hasDistance = 1;
        }
        else if(strcasecmp(ident, "azimuths") == 0)
        {
            uint count = 0;

            if(hasAzimuths)
            {
                TrErrorAt(tr, line, col, "Redefinition of 'azimuths'.\n");
                goto error;
            }
            if(!TrReadOperator(tr, "="))
                goto error;

            evCounts[0] = 0;
            for(;;)
            {
                if(!TrReadInt(tr, MIN_AZ_COUNT, MAX_AZ_COUNT, &intVal))
                    goto error;
                azCounts[(count * MAX_EV_COUNT) + evCounts[count]++] = (uint)intVal;
                if(TrIsOperator(tr, ","))
                {
                    if(evCounts[count] >= MAX_EV_COUNT)
                    {
                        TrError(tr, "Exceeded the maximum of %d elevations.\n", MAX_EV_COUNT);
                        goto error;
                    }
                    TrReadOperator(tr, ",");
                }
                else
                {
                    if(evCounts[count] < MIN_EV_COUNT)
                    {
                        TrErrorAt(tr, line, col, "Did not reach the minimum of %d azimuth counts.\n", MIN_EV_COUNT);
                        goto error;
                    }
                    if(azCounts[count * MAX_EV_COUNT] != 1 || azCounts[(count * MAX_EV_COUNT) + evCounts[count] - 1] != 1)
                    {
                        TrError(tr, "Poles are not singular for field %d.\n", count - 1);
                        goto error;
                    }
                    count++;
                    if(TrIsOperator(tr, ";"))
                    {
                        if(count >= MAX_FD_COUNT)
                        {
                            TrError(tr, "Exceeded the maximum number of %d fields.\n", MAX_FD_COUNT);
                            goto error;
                        }
                        evCounts[count] = 0;
                        TrReadOperator(tr, ";");
                    }
                    else
                    {
                        break;
                    }
                }
            }
            if(fdCount != 0 && count != fdCount)
            {
                TrError(tr, "Did not match the specified number of %d fields.\n", fdCount);
                goto error;
            }
            fdCount = count;
            hasAzimuths = 1;
        }
        else
        {
            TrErrorAt(tr, line, col, "Expected a metric name.\n");
            goto error;
        }
        TrSkipWhitespace(tr);
    }
    if(!(hasRate && hasPoints && hasRadius && hasDistance && hasAzimuths))
    {
        TrErrorAt(tr, line, col, "Expected a metric name.\n");
        goto error;
    }
    if(distances[0] < hData->mRadius)
    {
        TrError(tr, "Distance cannot start below head radius.\n");
        goto error;
    }
    if(hData->mChannelType == CT_NONE)
        hData->mChannelType = CT_MONO;
    if(!PrepareHrirData(fdCount, distances, evCounts, azCounts, hData))
    {
        fprintf(stderr, "Error:  Out of memory.\n");
        exit(-1);
    }
    free(azCounts);
    return 1;

error:
    free(azCounts);
    return 0;
}

// Parse an index triplet from the data set definition.
static int ReadIndexTriplet(TokenReaderT *tr, const HrirDataT *hData, uint *fi, uint *ei, uint *ai)
{
    int intVal;

    if(hData->mFdCount > 1)
    {
        if(!TrReadInt(tr, 0, (int)hData->mFdCount - 1, &intVal))
            return 0;
        *fi = (uint)intVal;
        if(!TrReadOperator(tr, ","))
            return 0;
    }
    else
    {
        *fi = 0;
    }
    if(!TrReadInt(tr, 0, (int)hData->mFds[*fi].mEvCount - 1, &intVal))
        return 0;
    *ei = (uint)intVal;
    if(!TrReadOperator(tr, ","))
        return 0;
    if(!TrReadInt(tr, 0, (int)hData->mFds[*fi].mEvs[*ei].mAzCount - 1, &intVal))
        return 0;
    *ai = (uint)intVal;
    return 1;
}

// Match the source format from a given identifier.
static SourceFormatT MatchSourceFormat(const char *ident)
{
    if(strcasecmp(ident, "wave") == 0)
        return SF_WAVE;
    if(strcasecmp(ident, "bin_le") == 0)
        return SF_BIN_LE;
    if(strcasecmp(ident, "bin_be") == 0)
        return SF_BIN_BE;
    if(strcasecmp(ident, "ascii") == 0)
        return SF_ASCII;
    return SF_NONE;
}

// Match the source element type from a given identifier.
static ElementTypeT MatchElementType(const char *ident)
{
    if(strcasecmp(ident, "int") == 0)
        return ET_INT;
    if(strcasecmp(ident, "fp") == 0)
        return ET_FP;
    return ET_NONE;
}

// Parse and validate a source reference from the data set definition.
static int ReadSourceRef(TokenReaderT *tr, SourceRefT *src)
{
    char ident[MAX_IDENT_LEN+1];
    uint line, col;
    int intVal;

    TrIndication(tr, &line, &col);
    if(!TrReadIdent(tr, MAX_IDENT_LEN, ident))
        return 0;
    src->mFormat = MatchSourceFormat(ident);
    if(src->mFormat == SF_NONE)
    {
        TrErrorAt(tr, line, col, "Expected a source format.\n");
        return 0;
    }
    if(!TrReadOperator(tr, "("))
        return 0;
    if(src->mFormat == SF_WAVE)
    {
        if(!TrReadInt(tr, 0, MAX_WAVE_CHANNELS, &intVal))
            return 0;
        src->mType = ET_NONE;
        src->mSize = 0;
        src->mBits = 0;
        src->mChannel = (uint)intVal;
        src->mSkip = 0;
    }
    else
    {
        TrIndication(tr, &line, &col);
        if(!TrReadIdent(tr, MAX_IDENT_LEN, ident))
            return 0;
        src->mType = MatchElementType(ident);
        if(src->mType == ET_NONE)
        {
            TrErrorAt(tr, line, col, "Expected a source element type.\n");
            return 0;
        }
        if(src->mFormat == SF_BIN_LE || src->mFormat == SF_BIN_BE)
        {
            if(!TrReadOperator(tr, ","))
                return 0;
            if(src->mType == ET_INT)
            {
                if(!TrReadInt(tr, MIN_BIN_SIZE, MAX_BIN_SIZE, &intVal))
                    return 0;
                src->mSize = (uint)intVal;
                if(!TrIsOperator(tr, ","))
                    src->mBits = (int)(8*src->mSize);
                else
                {
                    TrReadOperator(tr, ",");
                    TrIndication(tr, &line, &col);
                    if(!TrReadInt(tr, -2147483647-1, 2147483647, &intVal))
                        return 0;
                    if(abs(intVal) < MIN_BIN_BITS || (uint)abs(intVal) > (8*src->mSize))
                    {
                        TrErrorAt(tr, line, col, "Expected a value of (+/-) %d to %d.\n", MIN_BIN_BITS, 8*src->mSize);
                        return 0;
                    }
                    src->mBits = intVal;
                }
            }
            else
            {
                TrIndication(tr, &line, &col);
                if(!TrReadInt(tr, -2147483647-1, 2147483647, &intVal))
                    return 0;
                if(intVal != 4 && intVal != 8)
                {
                    TrErrorAt(tr, line, col, "Expected a value of 4 or 8.\n");
                    return 0;
                }
                src->mSize = (uint)intVal;
                src->mBits = 0;
            }
        }
        else if(src->mFormat == SF_ASCII && src->mType == ET_INT)
        {
            if(!TrReadOperator(tr, ","))
                return 0;
            if(!TrReadInt(tr, MIN_ASCII_BITS, MAX_ASCII_BITS, &intVal))
                return 0;
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
                return 0;
            src->mSkip = (uint)intVal;
        }
    }
    if(!TrReadOperator(tr, ")"))
        return 0;
    if(TrIsOperator(tr, "@"))
    {
        TrReadOperator(tr, "@");
        if(!TrReadInt(tr, 0, 0x7FFFFFFF, &intVal))
            return 0;
        src->mOffset = (uint)intVal;
    }
    else
        src->mOffset = 0;
    if(!TrReadOperator(tr, ":"))
        return 0;
    if(!TrReadString(tr, MAX_PATH_LEN, src->mPath))
        return 0;
    return 1;
}

// Match the target ear (index) from a given identifier.
static int MatchTargetEar(const char *ident)
{
    if(strcasecmp(ident, "left") == 0)
        return 0;
    if(strcasecmp(ident, "right") == 0)
        return 1;
    return -1;
}

// Process the list of sources in the data set definition.
static int ProcessSources(const HeadModelT model, TokenReaderT *tr, HrirDataT *hData)
{
    uint channels = (hData->mChannelType == CT_STEREO) ? 2 : 1;
    double *hrirs = CreateDoubles(channels * hData->mIrCount * hData->mIrSize);
    double *hrir = CreateDoubles(hData->mIrPoints);
    uint line, col, fi, ei, ai, ti;
    int count;

    printf("Loading sources...");
    fflush(stdout);
    count = 0;
    while(TrIsOperator(tr, "["))
    {
        double factor[2] = { 1.0, 1.0 };

        TrIndication(tr, &line, &col);
        TrReadOperator(tr, "[");
        if(!ReadIndexTriplet(tr, hData, &fi, &ei, &ai))
            goto error;
        if(!TrReadOperator(tr, "]"))
            goto error;
        HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

        if(azd->mIrs[0] != NULL)
        {
            TrErrorAt(tr, line, col, "Redefinition of source.\n");
            goto error;
        }
        if(!TrReadOperator(tr, "="))
            goto error;

        for(;;)
        {
            SourceRefT src;
            uint ti = 0;

            if(!ReadSourceRef(tr, &src))
                goto error;

            // TODO: Would be nice to display 'x of y files', but that would
            // require preparing the source refs first to get a total count
            // before loading them.
            ++count;
            printf("\rLoading sources... %d file%s", count, (count==1)?"":"s");
            fflush(stdout);

            if(!LoadSource(&src, hData->mIrRate, hData->mIrPoints, hrir))
                goto error;

            if(hData->mChannelType == CT_STEREO)
            {
                char ident[MAX_IDENT_LEN+1];

                if(!TrReadIdent(tr, MAX_IDENT_LEN, ident))
                    goto error;
                ti = MatchTargetEar(ident);
                if((int)ti < 0)
                {
                    TrErrorAt(tr, line, col, "Expected a target ear.\n");
                    goto error;
                }
            }
            azd->mIrs[ti] = &hrirs[hData->mIrSize * (ti * hData->mIrCount + azd->mIndex)];
            if(model == HM_DATASET)
                azd->mDelays[ti] = AverageHrirOnset(hData->mIrRate, hData->mIrPoints, hrir, 1.0 / factor[ti], azd->mDelays[ti]);
            AverageHrirMagnitude(hData->mIrPoints, hData->mFftSize, hrir, 1.0 / factor[ti], azd->mIrs[ti]);
            factor[ti] += 1.0;
            if(!TrIsOperator(tr, "+"))
                break;
            TrReadOperator(tr, "+");
        }
        if(hData->mChannelType == CT_STEREO)
        {
            if(azd->mIrs[0] == NULL)
            {
                TrErrorAt(tr, line, col, "Missing left ear source reference(s).\n");
                goto error;
            }
            else if(azd->mIrs[1] == NULL)
            {
                TrErrorAt(tr, line, col, "Missing right ear source reference(s).\n");
                goto error;
            }
        }
    }
    printf("\n");
    for(fi = 0;fi < hData->mFdCount;fi++)
    {
        for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                if(azd->mIrs[0] != NULL)
                    break;
            }
            if(ai < hData->mFds[fi].mEvs[ei].mAzCount)
                break;
        }
        if(ei >= hData->mFds[fi].mEvCount)
        {
            TrError(tr, "Missing source references [ %d, *, * ].\n", fi);
            goto error;
        }
        hData->mFds[fi].mEvStart = ei;
        for(;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                if(azd->mIrs[0] == NULL)
                {
                    TrError(tr, "Missing source reference [ %d, %d, %d ].\n", fi, ei, ai);
                    goto error;
                }
            }
        }
    }
    for(ti = 0;ti < channels;ti++)
    {
        for(fi = 0;fi < hData->mFdCount;fi++)
        {
            for(ei = 0;ei < hData->mFds[fi].mEvCount;ei++)
            {
                for(ai = 0;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
                {
                    HrirAzT *azd = &hData->mFds[fi].mEvs[ei].mAzs[ai];

                    azd->mIrs[ti] = &hrirs[hData->mIrSize * (ti * hData->mIrCount + azd->mIndex)];
                }
            }
        }
    }
    if(!TrLoad(tr))
    {
        free(hrir);
        return 1;
    }
    TrError(tr, "Errant data at end of source list.\n");

error:
    free(hrir);
    return 0;
}

/* Parse the data set definition and process the source data, storing the
 * resulting data set as desired.  If the input name is NULL it will read
 * from standard input.
 */
static int ProcessDefinition(const char *inName, const uint outRate, const uint fftSize, const int equalize, const int surface, const double limit, const uint truncSize, const HeadModelT model, const double radius, const char *outName)
{
    char rateStr[8+1], expName[MAX_PATH_LEN];
    TokenReaderT tr;
    HrirDataT hData;
    FILE *fp;
    int ret;

    ResetHrirData(&hData);
    fprintf(stdout, "Reading HRIR definition from %s...\n", inName?inName:"stdin");
    if(inName != NULL)
    {
        fp = fopen(inName, "r");
        if(fp == NULL)
        {
            fprintf(stderr, "Error: Could not open definition file '%s'\n", inName);
            return 0;
        }
        TrSetup(fp, inName, &tr);
    }
    else
    {
        fp = stdin;
        TrSetup(fp, "<stdin>", &tr);
    }
    if(!ProcessMetrics(&tr, fftSize, truncSize, &hData))
    {
        if(inName != NULL)
            fclose(fp);
        return 0;
    }
    if(!ProcessSources(model, &tr, &hData))
    {
        FreeHrirData(&hData);
        if(inName != NULL)
            fclose(fp);
        return 0;
    }
    if(fp != stdin)
        fclose(fp);
    if(equalize)
    {
        uint c = (hData.mChannelType == CT_STEREO) ? 2 : 1;
        uint m = 1 + hData.mFftSize / 2;
        double *dfa = CreateDoubles(c * m);

        fprintf(stdout, "Calculating diffuse-field average...\n");
        CalculateDiffuseFieldAverage(&hData, c, m, surface, limit, dfa);
        fprintf(stdout, "Performing diffuse-field equalization...\n");
        DiffuseFieldEqualize(c, m, dfa, &hData);
        free(dfa);
    }
    fprintf(stdout, "Performing minimum phase reconstruction...\n");
    ReconstructHrirs(&hData);
    if(outRate != 0 && outRate != hData.mIrRate)
    {
        fprintf(stdout, "Resampling HRIRs...\n");
        ResampleHrirs(outRate, &hData);
    }
    fprintf(stdout, "Truncating minimum-phase HRIRs...\n");
    hData.mIrPoints = truncSize;
    fprintf(stdout, "Synthesizing missing elevations...\n");
    if(model == HM_DATASET)
        SynthesizeOnsets(&hData);
    SynthesizeHrirs(&hData);
    fprintf(stdout, "Normalizing final HRIRs...\n");
    NormalizeHrirs(&hData);
    fprintf(stdout, "Calculating impulse delays...\n");
    CalculateHrtds(model, (radius > DEFAULT_CUSTOM_RADIUS) ? radius : hData.mRadius, &hData);
    snprintf(rateStr, 8, "%u", hData.mIrRate);
    StrSubst(outName, "%r", rateStr, MAX_PATH_LEN, expName);
    fprintf(stdout, "Creating MHR data set %s...\n", expName);
    ret = StoreMhr(&hData, expName);

    FreeHrirData(&hData);
    return ret;
}

static void PrintHelp(const char *argv0, FILE *ofile)
{
    fprintf(ofile, "Usage:  %s [<option>...]\n\n", argv0);
    fprintf(ofile, "Options:\n");
    fprintf(ofile, " -m              Ignored for compatibility.\n");
    fprintf(ofile, " -r <rate>       Change the data set sample rate to the specified value and\n");
    fprintf(ofile, "                 resample the HRIRs accordingly.\n");
    fprintf(ofile, " -f <points>     Override the FFT window size (default: %u).\n", DEFAULT_FFTSIZE);
    fprintf(ofile, " -e {on|off}     Toggle diffuse-field equalization (default: %s).\n", (DEFAULT_EQUALIZE ? "on" : "off"));
    fprintf(ofile, " -s {on|off}     Toggle surface-weighted diffuse-field average (default: %s).\n", (DEFAULT_SURFACE ? "on" : "off"));
    fprintf(ofile, " -l {<dB>|none}  Specify a limit to the magnitude range of the diffuse-field\n");
    fprintf(ofile, "                 average (default: %.2f).\n", DEFAULT_LIMIT);
    fprintf(ofile, " -w <points>     Specify the size of the truncation window that's applied\n");
    fprintf(ofile, "                 after minimum-phase reconstruction (default: %u).\n", DEFAULT_TRUNCSIZE);
    fprintf(ofile, " -d {dataset|    Specify the model used for calculating the head-delay timing\n");
    fprintf(ofile, "     sphere}     values (default: %s).\n", ((DEFAULT_HEAD_MODEL == HM_DATASET) ? "dataset" : "sphere"));
    fprintf(ofile, " -c <size>       Use a customized head radius measured ear-to-ear in meters.\n");
    fprintf(ofile, " -i <filename>   Specify an HRIR definition file to use (defaults to stdin).\n");
    fprintf(ofile, " -o <filename>   Specify an output file. Use of '%%r' will be substituted with\n");
    fprintf(ofile, "                 the data set sample rate.\n");
}

// Standard command line dispatch.
int main(int argc, char *argv[])
{
    const char *inName = NULL, *outName = NULL;
    uint outRate, fftSize;
    int equalize, surface;
    char *end = NULL;
    HeadModelT model;
    uint truncSize;
    double radius;
    double limit;
    int opt;

    GET_UNICODE_ARGS(&argc, &argv);

    if(argc < 2)
    {
        fprintf(stdout, "HRTF Processing and Composition Utility\n\n");
        PrintHelp(argv[0], stdout);
        exit(EXIT_SUCCESS);
    }

    outName = "./oalsoft_hrtf_%r.mhr";
    outRate = 0;
    fftSize = 0;
    equalize = DEFAULT_EQUALIZE;
    surface = DEFAULT_SURFACE;
    limit = DEFAULT_LIMIT;
    truncSize = DEFAULT_TRUNCSIZE;
    model = DEFAULT_HEAD_MODEL;
    radius = DEFAULT_CUSTOM_RADIUS;

    while((opt=getopt(argc, argv, "mr:f:e:s:l:w:d:c:e:i:o:h")) != -1)
    {
        switch(opt)
        {
        case 'm':
            fprintf(stderr, "Ignoring unused command '-m'.\n");
            break;

        case 'r':
            outRate = strtoul(optarg, &end, 10);
            if(end[0] != '\0' || outRate < MIN_RATE || outRate > MAX_RATE)
            {
                fprintf(stderr, "Error: Got unexpected value \"%s\" for option -%c, expected between %u to %u.\n", optarg, opt, MIN_RATE, MAX_RATE);
                exit(EXIT_FAILURE);
            }
            break;

        case 'f':
            fftSize = strtoul(optarg, &end, 10);
            if(end[0] != '\0' || (fftSize&(fftSize-1)) || fftSize < MIN_FFTSIZE || fftSize > MAX_FFTSIZE)
            {
                fprintf(stderr, "Error: Got unexpected value \"%s\" for option -%c, expected a power-of-two between %u to %u.\n", optarg, opt, MIN_FFTSIZE, MAX_FFTSIZE);
                exit(EXIT_FAILURE);
            }
            break;

        case 'e':
            if(strcmp(optarg, "on") == 0)
                equalize = 1;
            else if(strcmp(optarg, "off") == 0)
                equalize = 0;
            else
            {
                fprintf(stderr, "Error: Got unexpected value \"%s\" for option -%c, expected on or off.\n", optarg, opt);
                exit(EXIT_FAILURE);
            }
            break;

        case 's':
            if(strcmp(optarg, "on") == 0)
                surface = 1;
            else if(strcmp(optarg, "off") == 0)
                surface = 0;
            else
            {
                fprintf(stderr, "Error: Got unexpected value \"%s\" for option -%c, expected on or off.\n", optarg, opt);
                exit(EXIT_FAILURE);
            }
            break;

        case 'l':
            if(strcmp(optarg, "none") == 0)
                limit = 0.0;
            else
            {
                limit = strtod(optarg, &end);
                if(end[0] != '\0' || limit < MIN_LIMIT || limit > MAX_LIMIT)
                {
                    fprintf(stderr, "Error: Got unexpected value \"%s\" for option -%c, expected between %.0f to %.0f.\n", optarg, opt, MIN_LIMIT, MAX_LIMIT);
                    exit(EXIT_FAILURE);
                }
            }
            break;

        case 'w':
            truncSize = strtoul(optarg, &end, 10);
            if(end[0] != '\0' || truncSize < MIN_TRUNCSIZE || truncSize > MAX_TRUNCSIZE || (truncSize%MOD_TRUNCSIZE))
            {
                fprintf(stderr, "Error: Got unexpected value \"%s\" for option -%c, expected multiple of %u between %u to %u.\n", optarg, opt, MOD_TRUNCSIZE, MIN_TRUNCSIZE, MAX_TRUNCSIZE);
                exit(EXIT_FAILURE);
            }
            break;

        case 'd':
            if(strcmp(optarg, "dataset") == 0)
                model = HM_DATASET;
            else if(strcmp(optarg, "sphere") == 0)
                model = HM_SPHERE;
            else
            {
                fprintf(stderr, "Error: Got unexpected value \"%s\" for option -%c, expected dataset or sphere.\n", optarg, opt);
                exit(EXIT_FAILURE);
            }
            break;

        case 'c':
            radius = strtod(optarg, &end);
            if(end[0] != '\0' || radius < MIN_CUSTOM_RADIUS || radius > MAX_CUSTOM_RADIUS)
            {
                fprintf(stderr, "Error: Got unexpected value \"%s\" for option -%c, expected between %.2f to %.2f.\n", optarg, opt, MIN_CUSTOM_RADIUS, MAX_CUSTOM_RADIUS);
                exit(EXIT_FAILURE);
            }
            break;

        case 'i':
            inName = optarg;
            break;

        case 'o':
            outName = optarg;
            break;

        case 'h':
            PrintHelp(argv[0], stdout);
            exit(EXIT_SUCCESS);

        default: /* '?' */
            PrintHelp(argv[0], stderr);
            exit(EXIT_FAILURE);
        }
    }

    if(!ProcessDefinition(inName, outRate, fftSize, equalize, surface, limit,
                          truncSize, model, radius, outName))
        return -1;
    fprintf(stdout, "Operation completed.\n");

    return EXIT_SUCCESS;
}
