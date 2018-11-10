/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by Chris Robinson
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <ctype.h>

#include <mutex>
#include <array>
#include <vector>
#include <memory>
#include <istream>
#include <algorithm>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alSource.h"
#include "alu.h"
#include "hrtf.h"
#include "alconfig.h"
#include "filters/splitter.h"

#include "compat.h"
#include "almalloc.h"


struct HrtfEntry {
    struct HrtfEntry *next;
    struct Hrtf *handle;
    char filename[];
};

namespace {

/* Current data set limits defined by the makehrtf utility. */
#define MIN_IR_SIZE                  (8)
#define MAX_IR_SIZE                  (512)
#define MOD_IR_SIZE                  (8)

#define MIN_FD_COUNT                 (1)
#define MAX_FD_COUNT                 (16)

#define MIN_FD_DISTANCE              (50)
#define MAX_FD_DISTANCE              (2500)

#define MIN_EV_COUNT                 (5)
#define MAX_EV_COUNT                 (128)

#define MIN_AZ_COUNT                 (1)
#define MAX_AZ_COUNT                 (128)

#define MAX_HRIR_DELAY               (HRTF_HISTORY_LENGTH-1)

constexpr ALchar magicMarker00[8]{'M','i','n','P','H','R','0','0'};
constexpr ALchar magicMarker01[8]{'M','i','n','P','H','R','0','1'};
constexpr ALchar magicMarker02[8]{'M','i','n','P','H','R','0','2'};

/* First value for pass-through coefficients (remaining are 0), used for omni-
 * directional sounds. */
constexpr ALfloat PassthruCoeff{0.707106781187f/*sqrt(0.5)*/};

std::mutex LoadedHrtfLock;
HrtfEntry *LoadedHrtfs{nullptr};


class databuf final : public std::streambuf {
    int_type underflow() override
    { return traits_type::eof(); }

    pos_type seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode) override
    {
        if((mode&std::ios_base::out) || !(mode&std::ios_base::in))
            return traits_type::eof();

        char_type *cur;
        switch(whence)
        {
            case std::ios_base::beg:
                if(offset < 0 || offset > egptr()-eback())
                    return traits_type::eof();
                cur = eback() + offset;
                break;

            case std::ios_base::cur:
                if((offset >= 0 && offset > egptr()-gptr()) ||
                   (offset < 0 && -offset > gptr()-eback()))
                    return traits_type::eof();
                cur = gptr() + offset;
                break;

            case std::ios_base::end:
                if(offset > 0 || -offset > egptr()-eback())
                    return traits_type::eof();
                cur = egptr() + offset;
                break;

            default:
                return traits_type::eof();
        }

        setg(eback(), cur, egptr());
        return cur - eback();
    }

    pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override
    {
        // Simplified version of seekoff
        if((mode&std::ios_base::out) || !(mode&std::ios_base::in))
            return traits_type::eof();

        if(pos < 0 || pos > egptr()-eback())
            return traits_type::eof();

        setg(eback(), eback() + pos, egptr());
        return pos;
    }

public:
    databuf(const char_type *start, const char_type *end) noexcept
    {
        setg(const_cast<char_type*>(start), const_cast<char_type*>(start),
             const_cast<char_type*>(end));
    }
};

class idstream final : public std::istream {
    databuf mStreamBuf;

public:
    idstream(const char *start, const char *end)
      : std::istream{nullptr}, mStreamBuf{start, end}
    { init(&mStreamBuf); }
};


/* Calculate the elevation index given the polar elevation in radians. This
 * will return an index between 0 and (evcount - 1).
 */
ALsizei CalcEvIndex(ALsizei evcount, ALfloat ev, ALfloat *mu)
{
    ev = (F_PI_2+ev) * (evcount-1) / F_PI;
    ALsizei idx{float2int(ev)};

    *mu = ev - idx;
    return mini(idx, evcount-1);
}

/* Calculate the azimuth index given the polar azimuth in radians. This will
 * return an index between 0 and (azcount - 1).
 */
ALsizei CalcAzIndex(ALsizei azcount, ALfloat az, ALfloat *mu)
{
    az = (F_TAU+az) * azcount / F_TAU;
    ALsizei idx{float2int(az)};

    *mu = az - idx;
    return idx % azcount;
}

} // namespace


/* Calculates static HRIR coefficients and delays for the given polar elevation
 * and azimuth in radians. The coefficients are normalized.
 */
void GetHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat spread,
                   ALfloat (*RESTRICT coeffs)[2], ALsizei *delays)
{
    ALfloat dirfact{1.0f - (spread / F_TAU)};

    /* Claculate the lower elevation index. */
    ALfloat emu;
    ALsizei evidx{CalcEvIndex(Hrtf->evCount, elevation, &emu)};
    ALsizei evoffset{Hrtf->evOffset[evidx]};

    /* Calculate lower azimuth index. */
    ALfloat amu[2];
    ALsizei azidx{CalcAzIndex(Hrtf->azCount[evidx], azimuth, &amu[0])};

    /* Calculate the lower HRIR indices. */
    ALsizei idx[4]{
        evoffset + azidx,
        evoffset + ((azidx+1) % Hrtf->azCount[evidx])
    };
    if(evidx < Hrtf->evCount-1)
    {
        /* Increment elevation to the next (upper) index. */
        evidx++;
        evoffset = Hrtf->evOffset[evidx];

        /* Calculate upper azimuth index. */
        azidx = CalcAzIndex(Hrtf->azCount[evidx], azimuth, &amu[1]);

        /* Calculate the upper HRIR indices. */
        idx[2] = evoffset + azidx;
        idx[3] = evoffset + ((azidx+1) % Hrtf->azCount[evidx]);
    }
    else
    {
        /* If the lower elevation is the top index, the upper elevation is the
         * same as the lower.
         */
        amu[1] = amu[0];
        idx[2] = idx[0];
        idx[3] = idx[1];
    }

    /* Calculate bilinear blending weights, attenuated according to the
     * directional panning factor.
     */
    ALfloat blend[4]{
        (1.0f-emu) * (1.0f-amu[0]) * dirfact,
        (1.0f-emu) * (     amu[0]) * dirfact,
        (     emu) * (1.0f-amu[1]) * dirfact,
        (     emu) * (     amu[1]) * dirfact
    };

    /* Calculate the blended HRIR delays. */
    delays[0] = fastf2i(
        Hrtf->delays[idx[0]][0]*blend[0] + Hrtf->delays[idx[1]][0]*blend[1] +
        Hrtf->delays[idx[2]][0]*blend[2] + Hrtf->delays[idx[3]][0]*blend[3]
    );
    delays[1] = fastf2i(
        Hrtf->delays[idx[0]][1]*blend[0] + Hrtf->delays[idx[1]][1]*blend[1] +
        Hrtf->delays[idx[2]][1]*blend[2] + Hrtf->delays[idx[3]][1]*blend[3]
    );

    /* Calculate the sample offsets for the HRIR indices. */
    idx[0] *= Hrtf->irSize;
    idx[1] *= Hrtf->irSize;
    idx[2] *= Hrtf->irSize;
    idx[3] *= Hrtf->irSize;

    ASSUME(Hrtf->irSize >= MIN_IR_SIZE && (Hrtf->irSize%MOD_IR_SIZE) == 0);

    /* Calculate the blended HRIR coefficients. */
    coeffs[0][0] = PassthruCoeff * (1.0f-dirfact);
    coeffs[0][1] = PassthruCoeff * (1.0f-dirfact);
    for(ALsizei i{1};i < Hrtf->irSize;i++)
    {
        coeffs[i][0] = 0.0f;
        coeffs[i][1] = 0.0f;
    }
    for(ALsizei c{0};c < 4;c++)
    {
        const ALfloat (*RESTRICT srccoeffs)[2] = Hrtf->coeffs + idx[c];
        for(ALsizei i{0};i < Hrtf->irSize;i++)
        {
            coeffs[i][0] += srccoeffs[i][0] * blend[c];
            coeffs[i][1] += srccoeffs[i][1] * blend[c];
        }
    }
}


void BuildBFormatHrtf(const struct Hrtf *Hrtf, DirectHrtfState *state, ALsizei NumChannels, const struct AngularPoint *AmbiPoints, const ALfloat (*RESTRICT AmbiMatrix)[MAX_AMBI_COEFFS], ALsizei AmbiCount, const ALfloat *RESTRICT AmbiOrderHFGain)
{
/* Set this to 2 for dual-band HRTF processing. May require a higher quality
 * band-splitter, or better calculation of the new IR length to deal with the
 * tail generated by the filter.
 */
#define NUM_BANDS 2
    ALsizei min_delay{HRTF_HISTORY_LENGTH};
    ALsizei max_delay{0};
    std::vector<ALsizei> idx(AmbiCount);
    for(ALsizei c{0};c < AmbiCount;c++)
    {
        ALuint evidx, azidx;
        ALuint evoffset;
        ALuint azcount;

        /* Calculate elevation index. */
        evidx = (ALsizei)((F_PI_2+AmbiPoints[c].Elev) * (Hrtf->evCount-1) / F_PI + 0.5f);
        evidx = clampi(evidx, 0, Hrtf->evCount-1);

        azcount = Hrtf->azCount[evidx];
        evoffset = Hrtf->evOffset[evidx];

        /* Calculate azimuth index for this elevation. */
        azidx = (ALsizei)((F_TAU+AmbiPoints[c].Azim) * azcount / F_TAU + 0.5f) % azcount;

        /* Calculate indices for left and right channels. */
        idx[c] = evoffset + azidx;

        min_delay = mini(min_delay, mini(Hrtf->delays[idx[c]][0], Hrtf->delays[idx[c]][1]));
        max_delay = maxi(max_delay, maxi(Hrtf->delays[idx[c]][0], Hrtf->delays[idx[c]][1]));
    }

    std::vector<std::array<std::array<ALdouble,2>,HRIR_LENGTH>> tmpres(NumChannels);
    ALfloat temps[3][HRIR_LENGTH]{};

    BandSplitter splitter;
    bandsplit_init(&splitter, 400.0f / (ALfloat)Hrtf->sampleRate);
    for(ALsizei c{0};c < AmbiCount;++c)
    {
        const ALfloat (*fir)[2] = &Hrtf->coeffs[idx[c] * Hrtf->irSize];
        ALsizei ldelay = Hrtf->delays[idx[c]][0] - min_delay;
        ALsizei rdelay = Hrtf->delays[idx[c]][1] - min_delay;

        if(NUM_BANDS == 1)
        {
            for(ALsizei i{0};i < NumChannels;++i)
            {
                ALdouble mult = (ALdouble)AmbiOrderHFGain[(ALsizei)sqrt(i)] * AmbiMatrix[c][i];
                ALsizei lidx = ldelay, ridx = rdelay;
                ALsizei j = 0;
                while(lidx < HRIR_LENGTH && ridx < HRIR_LENGTH && j < Hrtf->irSize)
                {
                    tmpres[i][lidx++][0] += fir[j][0] * mult;
                    tmpres[i][ridx++][1] += fir[j][1] * mult;
                    j++;
                }
            }
        }
        else
        {
            /* Band-split left HRIR into low and high frequency responses. */
            bandsplit_clear(&splitter);
            for(ALsizei i{0};i < Hrtf->irSize;++i)
                temps[2][i] = fir[i][0];
            bandsplit_process(&splitter, temps[0], temps[1], temps[2], HRIR_LENGTH);

            /* Apply left ear response with delay. */
            for(ALsizei i{0};i < NumChannels;++i)
            {
                ALdouble hfgain = AmbiOrderHFGain[(ALsizei)sqrt(i)];
                for(ALsizei b{0};b < NUM_BANDS;++b)
                {
                    ALdouble mult = AmbiMatrix[c][i] * ((b==0) ? hfgain : 1.0);
                    ALsizei lidx = ldelay;
                    ALsizei j = 0;
                    while(lidx < HRIR_LENGTH)
                        tmpres[i][lidx++][0] += temps[b][j++] * mult;
                }
            }

            /* Band-split right HRIR into low and high frequency responses. */
            bandsplit_clear(&splitter);
            for(ALsizei i{0};i < Hrtf->irSize;++i)
                temps[2][i] = fir[i][1];
            bandsplit_process(&splitter, temps[0], temps[1], temps[2], HRIR_LENGTH);

            /* Apply right ear response with delay. */
            for(ALsizei i{0};i < NumChannels;++i)
            {
                ALdouble hfgain = AmbiOrderHFGain[(ALsizei)sqrt(i)];
                for(ALsizei b{0};b < NUM_BANDS;++b)
                {
                    ALdouble mult = AmbiMatrix[c][i] * ((b==0) ? hfgain : 1.0);
                    ALsizei ridx = rdelay;
                    ALsizei j = 0;
                    while(ridx < HRIR_LENGTH)
                        tmpres[i][ridx++][1] += temps[b][j++] * mult;
                }
            }
        }
    }

    for(ALsizei i{0};i < NumChannels;++i)
    {
        for(ALsizei idx{0};idx < HRIR_LENGTH;idx++)
        {
            state->Chan[i].Coeffs[idx][0] = (ALfloat)tmpres[i][idx][0];
            state->Chan[i].Coeffs[idx][1] = (ALfloat)tmpres[i][idx][1];
        }
    }
    tmpres.clear();
    idx.clear();

    ALsizei max_length;
    if(NUM_BANDS == 1)
        max_length = mini(max_delay-min_delay + Hrtf->irSize, HRIR_LENGTH);
    else
    {
        /* Increase the IR size by 2/3rds to account for the tail generated by
         * the band-split filter.
         */
        const ALsizei irsize = mini(Hrtf->irSize*5/3, HRIR_LENGTH);
        max_length = mini(max_delay-min_delay + irsize, HRIR_LENGTH);
    }
    /* Round up to the next IR size multiple. */
    max_length += MOD_IR_SIZE-1;
    max_length -= max_length%MOD_IR_SIZE;

    TRACE("Skipped delay: %d, max delay: %d, new FIR length: %d\n",
          min_delay, max_delay-min_delay, max_length);
    state->IrSize = max_length;
#undef NUM_BANDS
}


namespace {

struct Hrtf *CreateHrtfStore(ALuint rate, ALsizei irSize, ALfloat distance, ALsizei evCount,
  ALsizei irCount, const ALubyte *azCount, const ALushort *evOffset, const ALfloat (*coeffs)[2],
  const ALubyte (*delays)[2], const char *filename)
{
    struct Hrtf *Hrtf;
    size_t total;

    total  = sizeof(struct Hrtf);
    total += sizeof(Hrtf->azCount[0])*evCount;
    total  = RoundUp(total, sizeof(ALushort)); /* Align for ushort fields */
    total += sizeof(Hrtf->evOffset[0])*evCount;
    total  = RoundUp(total, 16); /* Align for coefficients using SIMD */
    total += sizeof(Hrtf->coeffs[0])*irSize*irCount;
    total += sizeof(Hrtf->delays[0])*irCount;

    Hrtf = static_cast<struct Hrtf*>(al_calloc(16, total));
    if(Hrtf == nullptr)
        ERR("Out of memory allocating storage for %s.\n", filename);
    else
    {
        uintptr_t offset = sizeof(struct Hrtf);
        char *base = (char*)Hrtf;
        ALushort *_evOffset;
        ALubyte *_azCount;
        ALubyte (*_delays)[2];
        ALfloat (*_coeffs)[2];
        ALsizei i;

        InitRef(&Hrtf->ref, 0);
        Hrtf->sampleRate = rate;
        Hrtf->irSize = irSize;
        Hrtf->distance = distance;
        Hrtf->evCount = evCount;

        /* Set up pointers to storage following the main HRTF struct. */
        _azCount = reinterpret_cast<ALubyte*>(base + offset);
        offset += sizeof(_azCount[0])*evCount;

        offset = RoundUp(offset, sizeof(ALushort)); /* Align for ushort fields */
        _evOffset = reinterpret_cast<ALushort*>(base + offset);
        offset += sizeof(_evOffset[0])*evCount;

        offset = RoundUp(offset, 16); /* Align for coefficients using SIMD */
        _coeffs = reinterpret_cast<ALfloat(*)[2]>(base + offset);
        offset += sizeof(_coeffs[0])*irSize*irCount;

        _delays = reinterpret_cast<ALubyte(*)[2]>(base + offset);
        offset += sizeof(_delays[0])*irCount;

        assert(offset == total);

        /* Copy input data to storage. */
        for(i = 0;i < evCount;i++) _azCount[i] = azCount[i];
        for(i = 0;i < evCount;i++) _evOffset[i] = evOffset[i];
        for(i = 0;i < irSize*irCount;i++)
        {
            _coeffs[i][0] = coeffs[i][0];
            _coeffs[i][1] = coeffs[i][1];
        }
        for(i = 0;i < irCount;i++)
        {
            _delays[i][0] = delays[i][0];
            _delays[i][1] = delays[i][1];
        }

        /* Finally, assign the storage pointers. */
        Hrtf->azCount = _azCount;
        Hrtf->evOffset = _evOffset;
        Hrtf->coeffs = _coeffs;
        Hrtf->delays = _delays;
    }

    return Hrtf;
}

ALubyte GetLE_ALubyte(std::istream &data)
{
    return static_cast<ALubyte>(data.get());
}

ALshort GetLE_ALshort(std::istream &data)
{
    int ret = data.get();
    ret |= data.get() << 8;
    return static_cast<ALshort>((ret^32768) - 32768);
}

ALushort GetLE_ALushort(std::istream &data)
{
    int ret = data.get();
    ret |= data.get() << 8;
    return static_cast<ALushort>(ret);
}

ALint GetLE_ALint24(std::istream &data)
{
    int ret = data.get();
    ret |= data.get() << 8;
    ret |= data.get() << 16;
    return (ret^8388608) - 8388608;
}

ALuint GetLE_ALuint(std::istream &data)
{
    int ret = data.get();
    ret |= data.get() << 8;
    ret |= data.get() << 16;
    ret |= data.get() << 24;
    return ret;
}

struct Hrtf *LoadHrtf00(std::istream &data, const char *filename)
{
    ALuint rate{GetLE_ALuint(data)};
    ALushort irCount{GetLE_ALushort(data)};
    ALushort irSize{GetLE_ALushort(data)};
    ALubyte evCount{GetLE_ALubyte(data)};
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }

    ALboolean failed{AL_FALSE};
    if(irSize < MIN_IR_SIZE || irSize > MAX_IR_SIZE || (irSize%MOD_IR_SIZE))
    {
        ERR("Unsupported HRIR size: irSize=%d (%d to %d by %d)\n",
            irSize, MIN_IR_SIZE, MAX_IR_SIZE, MOD_IR_SIZE);
        failed = AL_TRUE;
    }
    if(evCount < MIN_EV_COUNT || evCount > MAX_EV_COUNT)
    {
        ERR("Unsupported elevation count: evCount=%d (%d to %d)\n",
            evCount, MIN_EV_COUNT, MAX_EV_COUNT);
        failed = AL_TRUE;
    }
    if(failed)
        return nullptr;

    std::vector<ALushort> evOffset(evCount);
    for(auto &val : evOffset)
        val = GetLE_ALushort(data);
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }
    for(ALsizei i{1};i < evCount;i++)
    {
        if(evOffset[i] <= evOffset[i-1])
        {
            ERR("Invalid evOffset: evOffset[%d]=%d (last=%d)\n",
                i, evOffset[i], evOffset[i-1]);
            failed = AL_TRUE;
        }
    }
    if(irCount <= evOffset.back())
    {
        ERR("Invalid evOffset: evOffset[" SZFMT "]=%d (irCount=%d)\n",
            evOffset.size()-1, evOffset.back(), irCount);
        failed = AL_TRUE;
    }
    if(failed)
        return nullptr;

    std::vector<ALubyte> azCount(evCount);
    for(ALsizei i{1};i < evCount;i++)
    {
        azCount[i-1] = evOffset[i] - evOffset[i-1];
        if(azCount[i-1] < MIN_AZ_COUNT || azCount[i-1] > MAX_AZ_COUNT)
        {
            ERR("Unsupported azimuth count: azCount[%d]=%d (%d to %d)\n",
                i-1, azCount[i-1], MIN_AZ_COUNT, MAX_AZ_COUNT);
            failed = AL_TRUE;
        }
    }
    azCount.back() = irCount - evOffset.back();
    if(azCount.back() < MIN_AZ_COUNT || azCount.back() > MAX_AZ_COUNT)
    {
        ERR("Unsupported azimuth count: azCount[" SZFMT "]=%d (%d to %d)\n",
            azCount.size()-1, azCount.back(), MIN_AZ_COUNT, MAX_AZ_COUNT);
        failed = AL_TRUE;
    }
    if(failed)
        return nullptr;

    std::vector<std::array<ALfloat,2>> coeffs(irSize*irCount);
    std::vector<std::array<ALubyte,2>> delays(irCount);
    for(auto &val : coeffs)
        val[0] = GetLE_ALshort(data) / 32768.0f;
    for(auto &val : delays)
        val[0] = GetLE_ALubyte(data);
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }
    for(ALsizei i{0};i < irCount;i++)
    {
        if(delays[i][0] > MAX_HRIR_DELAY)
        {
            ERR("Invalid delays[%d]: %d (%d)\n", i, delays[i][0], MAX_HRIR_DELAY);
            failed = AL_TRUE;
        }
    }
    if(failed)
        return nullptr;

    /* Mirror the left ear responses to the right ear. */
    for(ALsizei i{0};i < evCount;i++)
    {
        ALushort evoffset = evOffset[i];
        ALubyte azcount = azCount[i];
        for(ALsizei j{0};j < azcount;j++)
        {
            ALsizei lidx = evoffset + j;
            ALsizei ridx = evoffset + ((azcount-j) % azcount);

            for(ALsizei k{0};k < irSize;k++)
                coeffs[ridx*irSize + k][1] = coeffs[lidx*irSize + k][0];
            delays[ridx][1] = delays[lidx][0];
        }
    }

    return CreateHrtfStore(rate, irSize, 0.0f, evCount, irCount, azCount.data(),
                           evOffset.data(), &reinterpret_cast<ALfloat(&)[2]>(coeffs[0]),
                           &reinterpret_cast<ALubyte(&)[2]>(delays[0]), filename);
}

static struct Hrtf *LoadHrtf01(std::istream &data, const char *filename)
{
    ALuint rate{GetLE_ALuint(data)};
    ALushort irSize{GetLE_ALubyte(data)};
    ALubyte evCount{GetLE_ALubyte(data)};
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }

    ALboolean failed{AL_FALSE};
    if(irSize < MIN_IR_SIZE || irSize > MAX_IR_SIZE || (irSize%MOD_IR_SIZE))
    {
        ERR("Unsupported HRIR size: irSize=%d (%d to %d by %d)\n",
            irSize, MIN_IR_SIZE, MAX_IR_SIZE, MOD_IR_SIZE);
        failed = AL_TRUE;
    }
    if(evCount < MIN_EV_COUNT || evCount > MAX_EV_COUNT)
    {
        ERR("Unsupported elevation count: evCount=%d (%d to %d)\n",
            evCount, MIN_EV_COUNT, MAX_EV_COUNT);
        failed = AL_TRUE;
    }
    if(failed)
        return nullptr;

    std::vector<ALubyte> azCount(evCount);
    data.read(reinterpret_cast<char*>(azCount.data()), evCount);
    if(!data || data.eof() || data.gcount() < evCount)
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }
    for(ALsizei i{0};i < evCount;++i)
    {
        if(azCount[i] < MIN_AZ_COUNT || azCount[i] > MAX_AZ_COUNT)
        {
            ERR("Unsupported azimuth count: azCount[%d]=%d (%d to %d)\n",
                i, azCount[i], MIN_AZ_COUNT, MAX_AZ_COUNT);
            failed = AL_TRUE;
        }
    }
    if(failed)
        return nullptr;

    std::vector<ALushort> evOffset(evCount);
    evOffset[0] = 0;
    ALushort irCount{azCount[0]};
    for(ALsizei i{1};i < evCount;i++)
    {
        evOffset[i] = evOffset[i-1] + azCount[i-1];
        irCount += azCount[i];
    }

    std::vector<std::array<ALfloat,2>> coeffs(irSize*irCount);
    std::vector<std::array<ALubyte,2>> delays(irCount);
    for(auto &val : coeffs)
        val[0] = GetLE_ALshort(data) / 32768.0f;
    for(auto &val : delays)
        val[0] = GetLE_ALubyte(data);
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }
    for(ALsizei i{0};i < irCount;i++)
    {
        if(delays[i][0] > MAX_HRIR_DELAY)
        {
            ERR("Invalid delays[%d]: %d (%d)\n", i, delays[i][0], MAX_HRIR_DELAY);
            failed = AL_TRUE;
        }
    }
    if(failed)
        return nullptr;

    /* Mirror the left ear responses to the right ear. */
    for(ALsizei i{0};i < evCount;i++)
    {
        ALushort evoffset = evOffset[i];
        ALubyte azcount = azCount[i];
        for(ALsizei j{0};j < azcount;j++)
        {
            ALsizei lidx = evoffset + j;
            ALsizei ridx = evoffset + ((azcount-j) % azcount);

            for(ALsizei k{0};k < irSize;k++)
                coeffs[ridx*irSize + k][1] = coeffs[lidx*irSize + k][0];
            delays[ridx][1] = delays[lidx][0];
        }
    }

    return CreateHrtfStore(rate, irSize, 0.0f, evCount, irCount, azCount.data(),
                           evOffset.data(), &reinterpret_cast<ALfloat(&)[2]>(coeffs[0]),
                           &reinterpret_cast<ALubyte(&)[2]>(delays[0]), filename);
}

#define SAMPLETYPE_S16 0
#define SAMPLETYPE_S24 1

#define CHANTYPE_LEFTONLY  0
#define CHANTYPE_LEFTRIGHT 1

struct Hrtf *LoadHrtf02(std::istream &data, const char *filename)
{
    ALuint rate{GetLE_ALuint(data)};
    ALubyte sampleType{GetLE_ALubyte(data)};
    ALubyte channelType{GetLE_ALubyte(data)};
    ALushort irSize{GetLE_ALubyte(data)};
    ALubyte fdCount{GetLE_ALubyte(data)};
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }

    ALboolean failed{AL_FALSE};
    if(sampleType > SAMPLETYPE_S24)
    {
        ERR("Unsupported sample type: %d\n", sampleType);
        failed = AL_TRUE;
    }
    if(channelType > CHANTYPE_LEFTRIGHT)
    {
        ERR("Unsupported channel type: %d\n", channelType);
        failed = AL_TRUE;
    }

    if(irSize < MIN_IR_SIZE || irSize > MAX_IR_SIZE || (irSize%MOD_IR_SIZE))
    {
        ERR("Unsupported HRIR size: irSize=%d (%d to %d by %d)\n",
            irSize, MIN_IR_SIZE, MAX_IR_SIZE, MOD_IR_SIZE);
        failed = AL_TRUE;
    }
    if(fdCount != 1)
    {
        ERR("Multiple field-depths not supported: fdCount=%d (%d to %d)\n",
            fdCount, MIN_FD_COUNT, MAX_FD_COUNT);
        failed = AL_TRUE;
    }
    if(failed)
        return nullptr;

    ALushort distance{};
    ALubyte evCount{};
    std::vector<ALubyte> azCount;
    for(ALsizei i{0};i < fdCount;i++)
    {
        distance = GetLE_ALushort(data);
        evCount = GetLE_ALubyte(data);
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }

        if(distance < MIN_FD_DISTANCE || distance > MAX_FD_DISTANCE)
        {
            ERR("Unsupported field distance: distance=%d (%dmm to %dmm)\n",
                distance, MIN_FD_DISTANCE, MAX_FD_DISTANCE);
            failed = AL_TRUE;
        }
        if(evCount < MIN_EV_COUNT || evCount > MAX_EV_COUNT)
        {
            ERR("Unsupported elevation count: evCount=%d (%d to %d)\n",
                evCount, MIN_EV_COUNT, MAX_EV_COUNT);
            failed = AL_TRUE;
        }
        if(failed)
            return nullptr;

        azCount.resize(evCount);
        data.read(reinterpret_cast<char*>(azCount.data()), evCount);
        if(!data || data.eof() || data.gcount() < evCount)
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }

        for(ALsizei j{0};j < evCount;j++)
        {
            if(azCount[j] < MIN_AZ_COUNT || azCount[j] > MAX_AZ_COUNT)
            {
                ERR("Unsupported azimuth count: azCount[%d]=%d (%d to %d)\n",
                    j, azCount[j], MIN_AZ_COUNT, MAX_AZ_COUNT);
                failed = AL_TRUE;
            }
        }
        if(failed)
            return nullptr;
    }

    std::vector<ALushort> evOffset(evCount);
    evOffset[0] = 0;
    ALushort irCount{azCount[0]};
    for(ALsizei i{1};i < evCount;++i)
    {
        evOffset[i] = evOffset[i-1] + azCount[i-1];
        irCount += azCount[i];
    }

    std::vector<std::array<ALfloat,2>> coeffs(irSize*irCount);
    std::vector<std::array<ALubyte,2>> delays(irCount);
    if(channelType == CHANTYPE_LEFTONLY)
    {
        if(sampleType == SAMPLETYPE_S16)
        {
            for(auto &val : coeffs)
                val[0] = GetLE_ALshort(data) / 32768.0f;
        }
        else if(sampleType == SAMPLETYPE_S24)
        {
            for(auto &val : coeffs)
                val[0] = GetLE_ALint24(data) / 8388608.0f;
        }
        for(auto &val : delays)
            val[0] = GetLE_ALubyte(data);
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }
        for(ALsizei i{0};i < irCount;++i)
        {
            if(delays[i][0] > MAX_HRIR_DELAY)
            {
                ERR("Invalid delays[%d][0]: %d (%d)\n", i, delays[i][0], MAX_HRIR_DELAY);
                failed = AL_TRUE;
            }
        }
    }
    else if(channelType == CHANTYPE_LEFTRIGHT)
    {
        if(sampleType == SAMPLETYPE_S16)
        {
            for(auto &val : coeffs)
            {
                val[0] = GetLE_ALshort(data) / 32768.0f;
                val[1] = GetLE_ALshort(data) / 32768.0f;
            }
        }
        else if(sampleType == SAMPLETYPE_S24)
        {
            for(auto &val : coeffs)
            {
                val[0] = GetLE_ALint24(data) / 8388608.0f;
                val[1] = GetLE_ALint24(data) / 8388608.0f;
            }
        }
        for(auto &val : delays)
        {
            val[0] = GetLE_ALubyte(data);
            val[1] = GetLE_ALubyte(data);
        }
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }

        for(ALsizei i{0};i < irCount;++i)
        {
            if(delays[i][0] > MAX_HRIR_DELAY)
            {
                ERR("Invalid delays[%d][0]: %d (%d)\n", i, delays[i][0], MAX_HRIR_DELAY);
                failed = AL_TRUE;
            }
            if(delays[i][1] > MAX_HRIR_DELAY)
            {
                ERR("Invalid delays[%d][1]: %d (%d)\n", i, delays[i][1], MAX_HRIR_DELAY);
                failed = AL_TRUE;
            }
        }
    }
    if(failed)
        return nullptr;

    if(channelType == CHANTYPE_LEFTONLY)
    {
        /* Mirror the left ear responses to the right ear. */
        for(ALsizei i{0};i < evCount;i++)
        {
            ALushort evoffset = evOffset[i];
            ALubyte azcount = azCount[i];
            for(ALsizei j{0};j < azcount;j++)
            {
                ALsizei lidx = evoffset + j;
                ALsizei ridx = evoffset + ((azcount-j) % azcount);

                for(ALsizei k{0};k < irSize;k++)
                    coeffs[ridx*irSize + k][1] = coeffs[lidx*irSize + k][0];
                delays[ridx][1] = delays[lidx][0];
            }
        }
    }

    return CreateHrtfStore(rate, irSize,
        (ALfloat)distance / 1000.0f, evCount, irCount, azCount.data(), evOffset.data(),
        &reinterpret_cast<ALfloat(&)[2]>(coeffs[0]),
        &reinterpret_cast<ALubyte(&)[2]>(delays[0]), filename
    );
}


void AddFileEntry(vector_EnumeratedHrtf *list, const_al_string filename)
{
    /* Check if this file has already been loaded globally. */
    HrtfEntry *loaded_entry{LoadedHrtfs};
    while(loaded_entry)
    {
        if(alstr_cmp_cstr(filename, loaded_entry->filename) == 0)
        {
            const EnumeratedHrtf *iter;
            /* Check if this entry has already been added to the list. */
#define MATCH_ENTRY(i) (loaded_entry == (i)->hrtf)
            VECTOR_FIND_IF(iter, const EnumeratedHrtf, *list, MATCH_ENTRY);
#undef MATCH_ENTRY
            if(iter != VECTOR_END(*list))
            {
                TRACE("Skipping duplicate file entry %s\n", alstr_get_cstr(filename));
                return;
            }

            break;
        }
        loaded_entry = loaded_entry->next;
    }

    if(!loaded_entry)
    {
        TRACE("Got new file \"%s\"\n", alstr_get_cstr(filename));

        loaded_entry = static_cast<HrtfEntry*>(al_calloc(DEF_ALIGN,
            FAM_SIZE(struct HrtfEntry, filename, alstr_length(filename)+1)
        ));
        loaded_entry->next = LoadedHrtfs;
        loaded_entry->handle = nullptr;
        strcpy(loaded_entry->filename, alstr_get_cstr(filename));
        LoadedHrtfs = loaded_entry;
    }

    /* TODO: Get a human-readable name from the HRTF data (possibly coming in a
     * format update). */
    const char *name{strrchr(alstr_get_cstr(filename), '/')};
    if(!name) name = strrchr(alstr_get_cstr(filename), '\\');
    if(!name) name = alstr_get_cstr(filename);
    else ++name;

    const char *ext{strrchr(name, '.')};

    EnumeratedHrtf entry = { AL_STRING_INIT_STATIC(), nullptr };
    const EnumeratedHrtf *iter{};
    int i{0};
    do {
        if(!ext)
            alstr_copy_cstr(&entry.name, name);
        else
            alstr_copy_range(&entry.name, name, ext);
        if(i != 0)
        {
            char str[64];
            snprintf(str, sizeof(str), " #%d", i+1);
            alstr_append_cstr(&entry.name, str);
        }
        ++i;

#define MATCH_NAME(i)  (alstr_cmp(entry.name, (i)->name) == 0)
        VECTOR_FIND_IF(iter, const EnumeratedHrtf, *list, MATCH_NAME);
#undef MATCH_NAME
    } while(iter != VECTOR_END(*list));
    entry.hrtf = loaded_entry;

    TRACE("Adding file entry \"%s\"\n", alstr_get_cstr(entry.name));
    VECTOR_PUSH_BACK(*list, entry);
}

/* Unfortunate that we have to duplicate AddFileEntry to take a memory buffer
 * for input instead of opening the given filename.
 */
void AddBuiltInEntry(vector_EnumeratedHrtf *list, const_al_string filename, ALuint residx)
{
    HrtfEntry *loaded_entry{LoadedHrtfs};
    while(loaded_entry)
    {
        if(alstr_cmp_cstr(filename, loaded_entry->filename) == 0)
        {
            const EnumeratedHrtf *iter{};
#define MATCH_ENTRY(i) (loaded_entry == (i)->hrtf)
            VECTOR_FIND_IF(iter, const EnumeratedHrtf, *list, MATCH_ENTRY);
#undef MATCH_ENTRY
            if(iter != VECTOR_END(*list))
            {
                TRACE("Skipping duplicate file entry %s\n", alstr_get_cstr(filename));
                return;
            }

            break;
        }
        loaded_entry = loaded_entry->next;
    }

    if(!loaded_entry)
    {
        size_t namelen = alstr_length(filename)+32;

        TRACE("Got new file \"%s\"\n", alstr_get_cstr(filename));

        loaded_entry = static_cast<HrtfEntry*>(al_calloc(DEF_ALIGN,
            FAM_SIZE(struct HrtfEntry, filename, namelen)
        ));
        loaded_entry->next = LoadedHrtfs;
        loaded_entry->handle = nullptr;
        snprintf(loaded_entry->filename, namelen,  "!%u_%s",
                 residx, alstr_get_cstr(filename));
        LoadedHrtfs = loaded_entry;
    }

    /* TODO: Get a human-readable name from the HRTF data (possibly coming in a
     * format update). */
    const char *name{strrchr(alstr_get_cstr(filename), '/')};
    if(!name) name = strrchr(alstr_get_cstr(filename), '\\');
    if(!name) name = alstr_get_cstr(filename);
    else ++name;

    const char *ext{strrchr(name, '.')};

    EnumeratedHrtf entry{AL_STRING_INIT_STATIC(), nullptr};
    const EnumeratedHrtf *iter{};
    int i{0};
    do {
        if(!ext)
            alstr_copy_cstr(&entry.name, name);
        else
            alstr_copy_range(&entry.name, name, ext);
        if(i != 0)
        {
            char str[64];
            snprintf(str, sizeof(str), " #%d", i+1);
            alstr_append_cstr(&entry.name, str);
        }
        ++i;

#define MATCH_NAME(i)  (alstr_cmp(entry.name, (i)->name) == 0)
        VECTOR_FIND_IF(iter, const EnumeratedHrtf, *list, MATCH_NAME);
#undef MATCH_NAME
    } while(iter != VECTOR_END(*list));
    entry.hrtf = loaded_entry;

    TRACE("Adding built-in entry \"%s\"\n", alstr_get_cstr(entry.name));
    VECTOR_PUSH_BACK(*list, entry);
}


#define IDR_DEFAULT_44100_MHR 1
#define IDR_DEFAULT_48000_MHR 2

struct ResData { const char *data; size_t size; };
#ifndef ALSOFT_EMBED_HRTF_DATA

ResData GetResource(int UNUSED(name))
{ return {nullptr, 0u}; }

#else

#include "default-44100.mhr.h"
#include "default-48000.mhr.h"

ResData GetResource(int name)
{
    if(name == IDR_DEFAULT_44100_MHR)
        return {reinterpret_cast<const char*>(hrtf_default_44100), sizeof(hrtf_default_44100)};
    if(name == IDR_DEFAULT_48000_MHR)
        return {reinterpret_cast<const char*>(hrtf_default_48000), sizeof(hrtf_default_48000)};
    return {nullptr, 0u};
}
#endif

} // namespace


vector_EnumeratedHrtf EnumerateHrtf(const_al_string devname)
{
    vector_EnumeratedHrtf list{VECTOR_INIT_STATIC()};
    bool usedefaults{true};
    const char *pathlist{""};
    if(ConfigValueStr(alstr_get_cstr(devname), nullptr, "hrtf-paths", &pathlist))
    {
        al_string pname = AL_STRING_INIT_STATIC();
        while(pathlist && *pathlist)
        {
            const char *next, *end;

            while(isspace(*pathlist) || *pathlist == ',')
                pathlist++;
            if(*pathlist == '\0')
                continue;

            next = strchr(pathlist, ',');
            if(next)
                end = next++;
            else
            {
                end = pathlist + strlen(pathlist);
                usedefaults = false;
            }

            while(end != pathlist && isspace(*(end-1)))
                --end;
            if(end != pathlist)
            {
                alstr_copy_range(&pname, pathlist, end);

                vector_al_string flist{SearchDataFiles(".mhr", alstr_get_cstr(pname))};
                for(size_t i{0};i < VECTOR_SIZE(flist);i++)
                    AddFileEntry(&list, VECTOR_ELEM(flist, i));
                VECTOR_FOR_EACH(al_string, flist, alstr_reset);
                VECTOR_DEINIT(flist);
            }

            pathlist = next;
        }

        alstr_reset(&pname);
    }
    else if(ConfigValueExists(alstr_get_cstr(devname), nullptr, "hrtf_tables"))
        ERR("The hrtf_tables option is deprecated, please use hrtf-paths instead.\n");

    if(usedefaults)
    {
        vector_al_string flist{SearchDataFiles(".mhr", "openal/hrtf")};
        for(size_t i{0};i < VECTOR_SIZE(flist);i++)
            AddFileEntry(&list, VECTOR_ELEM(flist, i));
        VECTOR_FOR_EACH(al_string, flist, alstr_reset);
        VECTOR_DEINIT(flist);

        al_string ename = AL_STRING_INIT_STATIC();
        ResData res{GetResource(IDR_DEFAULT_44100_MHR)};
        if(res.data != nullptr && res.size > 0)
        {
            alstr_copy_cstr(&ename, "Built-In 44100hz");
            AddBuiltInEntry(&list, ename, IDR_DEFAULT_44100_MHR);
        }

        res = GetResource(IDR_DEFAULT_48000_MHR);
        if(res.data != nullptr && res.size > 0)
        {
            alstr_copy_cstr(&ename, "Built-In 48000hz");
            AddBuiltInEntry(&list, ename, IDR_DEFAULT_48000_MHR);
        }
        alstr_reset(&ename);
    }

    const char *defaulthrtf{""};
    if(VECTOR_SIZE(list) > 1 && ConfigValueStr(alstr_get_cstr(devname), nullptr, "default-hrtf", &defaulthrtf))
    {
        const EnumeratedHrtf *iter{};
        /* Find the preferred HRTF and move it to the front of the list. */
#define FIND_ENTRY(i)  (alstr_cmp_cstr((i)->name, defaulthrtf) == 0)
        VECTOR_FIND_IF(iter, const EnumeratedHrtf, list, FIND_ENTRY);
#undef FIND_ENTRY
        if(iter == VECTOR_END(list))
            WARN("Failed to find default HRTF \"%s\"\n", defaulthrtf);
        else if(iter != VECTOR_BEGIN(list))
        {
            EnumeratedHrtf entry{*iter};
            memmove(&VECTOR_ELEM(list,1), &VECTOR_ELEM(list,0),
                    (iter-VECTOR_BEGIN(list))*sizeof(EnumeratedHrtf));
            VECTOR_ELEM(list,0) = entry;
        }
    }

    return list;
}

void FreeHrtfList(vector_EnumeratedHrtf *list)
{
#define CLEAR_ENTRY(i) alstr_reset(&(i)->name)
    VECTOR_FOR_EACH(EnumeratedHrtf, *list, CLEAR_ENTRY);
    VECTOR_DEINIT(*list);
#undef CLEAR_ENTRY
}

struct Hrtf *GetLoadedHrtf(struct HrtfEntry *entry)
{
    std::lock_guard<std::mutex> _{LoadedHrtfLock};

    if(entry->handle)
    {
        Hrtf *hrtf{entry->handle};
        Hrtf_IncRef(hrtf);
        return hrtf;
    }

    std::unique_ptr<std::istream> stream;
    const char *name{""};
    ALuint residx{};
    char ch{};
    if(sscanf(entry->filename, "!%u%c", &residx, &ch) == 2 && ch == '_')
    {
        name = strchr(entry->filename, ch)+1;

        TRACE("Loading %s...\n", name);
        ResData res{GetResource(residx)};
        if(!res.data || res.size == 0)
        {
            ERR("Could not get resource %u, %s\n", residx, name);
            return nullptr;
        }
        stream.reset(new idstream{res.data, res.data+res.size});
    }
    else
    {
        name = entry->filename;

        TRACE("Loading %s...\n", entry->filename);
        std::unique_ptr<al::ifstream> fstr{new al::ifstream{entry->filename, std::ios::binary}};
        if(!fstr->is_open())
        {
            ERR("Could not open %s\n", entry->filename);
            return nullptr;
        }
        stream = std::move(fstr);
    }

    Hrtf *hrtf{};
    char magic[sizeof(magicMarker02)];
    stream->read(magic, sizeof(magic));
    if(stream->gcount() < static_cast<std::streamsize>(sizeof(magicMarker02)))
        ERR("%s data is too short (" SZFMT " bytes)\n", name, stream->gcount());
    else if(memcmp(magic, magicMarker02, sizeof(magicMarker02)) == 0)
    {
        TRACE("Detected data set format v2\n");
        hrtf = LoadHrtf02(*stream, name);
    }
    else if(memcmp(magic, magicMarker01, sizeof(magicMarker01)) == 0)
    {
        TRACE("Detected data set format v1\n");
        hrtf = LoadHrtf01(*stream, name);
    }
    else if(memcmp(magic, magicMarker00, sizeof(magicMarker00)) == 0)
    {
        TRACE("Detected data set format v0\n");
        hrtf = LoadHrtf00(*stream, name);
    }
    else
        ERR("Invalid header in %s: \"%.8s\"\n", name, magic);
    stream.reset();

    if(!hrtf)
        ERR("Failed to load %s\n", name);
    else
    {
        entry->handle = hrtf;
        Hrtf_IncRef(hrtf);
        TRACE("Loaded HRTF support for format: %s %uhz\n",
              DevFmtChannelsString(DevFmtStereo), hrtf->sampleRate);
    }

    return hrtf;
}


void Hrtf_IncRef(struct Hrtf *hrtf)
{
    uint ref = IncrementRef(&hrtf->ref);
    TRACEREF("%p increasing refcount to %u\n", hrtf, ref);
}

void Hrtf_DecRef(struct Hrtf *hrtf)
{
    uint ref = DecrementRef(&hrtf->ref);
    TRACEREF("%p decreasing refcount to %u\n", hrtf, ref);
    if(ref == 0)
    {
        std::lock_guard<std::mutex> _{LoadedHrtfLock};

        struct HrtfEntry *Hrtf{LoadedHrtfs};
        while(Hrtf != nullptr)
        {
            /* Need to double-check that it's still unused, as another device
             * could've reacquired this HRTF after its reference went to 0 and
             * before the lock was taken.
             */
            if(hrtf == Hrtf->handle && ReadRef(&hrtf->ref) == 0)
            {
                al_free(Hrtf->handle);
                Hrtf->handle = nullptr;
                TRACE("Unloaded unused HRTF %s\n", Hrtf->filename);
            }
            Hrtf = Hrtf->next;
        }
    }
}


void FreeHrtfs(void)
{
    struct HrtfEntry *Hrtf{LoadedHrtfs};
    LoadedHrtfs = nullptr;

    while(Hrtf != nullptr)
    {
        struct HrtfEntry *next{Hrtf->next};
        al_free(Hrtf->handle);
        al_free(Hrtf);
        Hrtf = next;
    }
}
