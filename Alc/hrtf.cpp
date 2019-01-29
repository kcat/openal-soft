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
#include <numeric>
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


struct HrtfHandle {
    HrtfEntry *entry{nullptr};
    al::FlexArray<char> filename;

    HrtfHandle(size_t fname_len) : filename{fname_len} { }
    HrtfHandle(const HrtfHandle&) = delete;
    HrtfHandle& operator=(const HrtfHandle&) = delete;

    static std::unique_ptr<HrtfHandle> Create(size_t fname_len);
    static constexpr size_t Sizeof(size_t length) noexcept
    {
        return maxz(sizeof(HrtfHandle),
            al::FlexArray<char>::Sizeof(length, offsetof(HrtfHandle, filename)));
    }

    DEF_PLACE_NEWDEL()
};

std::unique_ptr<HrtfHandle> HrtfHandle::Create(size_t fname_len)
{
    void *ptr{al_calloc(DEF_ALIGN, HrtfHandle::Sizeof(fname_len))};
    return std::unique_ptr<HrtfHandle>{new (ptr) HrtfHandle{fname_len}};
}

namespace {

using HrtfHandlePtr = std::unique_ptr<HrtfHandle>;

/* Current data set limits defined by the makehrtf utility. */
#define MIN_IR_SIZE                  (8)
#define MAX_IR_SIZE                  (512)
#define MOD_IR_SIZE                  (8)

#define MIN_FD_COUNT                 (1)
#define MAX_FD_COUNT                 (16)

#define MIN_FD_DISTANCE              (0.05f)
#define MAX_FD_DISTANCE              (2.5f)

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
al::vector<HrtfHandlePtr> LoadedHrtfs;


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

        setg(eback(), eback() + static_cast<size_t>(pos), egptr());
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


struct IdxBlend { ALsizei idx; ALfloat blend; };
/* Calculate the elevation index given the polar elevation in radians. This
 * will return an index between 0 and (evcount - 1).
 */
IdxBlend CalcEvIndex(ALsizei evcount, ALfloat ev)
{
    ev = (al::MathDefs<float>::Pi()*0.5f + ev) * (evcount-1) / al::MathDefs<float>::Pi();
    ALsizei idx{float2int(ev)};

    return IdxBlend{mini(idx, evcount-1), ev-idx};
}

/* Calculate the azimuth index given the polar azimuth in radians. This will
 * return an index between 0 and (azcount - 1).
 */
IdxBlend CalcAzIndex(ALsizei azcount, ALfloat az)
{
    az = (al::MathDefs<float>::Tau()+az) * azcount / al::MathDefs<float>::Tau();
    ALsizei idx{float2int(az)};

    return IdxBlend{idx%azcount, az-idx};
}

} // namespace


/* Calculates static HRIR coefficients and delays for the given polar elevation
 * and azimuth in radians. The coefficients are normalized.
 */
void GetHrtfCoeffs(const HrtfEntry *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat distance,
                   ALfloat spread, ALfloat (*RESTRICT coeffs)[2], ALsizei *delays)
{
    const ALfloat dirfact{1.0f - (spread / al::MathDefs<float>::Tau())};

    const auto *field = Hrtf->field;
    const auto *field_end = field + Hrtf->fdCount-1;
    ALsizei fdoffset{0};
    for(;field != field_end && (field+1)->distance <= distance;++field)
        fdoffset += field->evCount;

    /* Claculate the lower elevation index. */
    const auto elev = CalcEvIndex(field->evCount, elevation);
    ALsizei ev0offset{Hrtf->evOffset[fdoffset + elev.idx]};
    ALsizei ev1offset{ev0offset};

    /* Calculate lower azimuth index. */
    const auto az0 = CalcAzIndex(Hrtf->azCount[fdoffset + elev.idx], azimuth);
    auto az1 = az0;

    if(LIKELY(elev.idx < field->evCount-1))
    {
        /* Increment elevation to the next (upper) index. */
        ALsizei evidx{elev.idx+1};
        ev1offset = Hrtf->evOffset[fdoffset + evidx];

        /* Calculate upper azimuth index. */
        az1 = CalcAzIndex(Hrtf->azCount[fdoffset + evidx], azimuth);
    }

    /* Calculate the HRIR indices to blend. */
    ALsizei idx[4]{
        ev0offset + az0.idx,
        ev0offset + ((az0.idx+1) % Hrtf->azCount[fdoffset + elev.idx]),
        ev1offset + az1.idx,
        ev1offset + ((az1.idx+1) % Hrtf->azCount[fdoffset + elev.idx])
    };

    /* Calculate bilinear blending weights, attenuated according to the
     * directional panning factor.
     */
    const ALfloat blend[4]{
        (1.0f-elev.blend) * (1.0f-az0.blend) * dirfact,
        (1.0f-elev.blend) * (     az0.blend) * dirfact,
        (     elev.blend) * (1.0f-az1.blend) * dirfact,
        (     elev.blend) * (     az1.blend) * dirfact
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

    const ALsizei irSize{Hrtf->irSize};
    ASSUME(irSize >= MIN_IR_SIZE);

    /* Calculate the sample offsets for the HRIR indices. */
    idx[0] *= irSize;
    idx[1] *= irSize;
    idx[2] *= irSize;
    idx[3] *= irSize;

    /* Calculate the blended HRIR coefficients. */
    ALfloat *coeffout{al::assume_aligned<16>(coeffs[0])};
    coeffout[0] = PassthruCoeff * (1.0f-dirfact);
    coeffout[1] = PassthruCoeff * (1.0f-dirfact);
    std::fill(coeffout+2, coeffout + irSize*2, 0.0f);
    for(ALsizei c{0};c < 4;c++)
    {
        const ALfloat *srccoeffs{al::assume_aligned<16>(Hrtf->coeffs[idx[c]])};
        const ALfloat mult{blend[c]};
        auto blend_coeffs = [mult](const ALfloat src, const ALfloat coeff) noexcept -> ALfloat
        { return src*mult + coeff; };
        std::transform<const ALfloat*RESTRICT>(srccoeffs, srccoeffs + irSize*2, coeffout,
            coeffout, blend_coeffs);
    }
}


std::unique_ptr<DirectHrtfState> DirectHrtfState::Create(size_t num_chans)
{
    void *ptr{al_calloc(16, DirectHrtfState::Sizeof(num_chans))};
    return std::unique_ptr<DirectHrtfState>{new (ptr) DirectHrtfState{num_chans}};
}

void BuildBFormatHrtf(const HrtfEntry *Hrtf, DirectHrtfState *state, const ALsizei NumChannels, const AngularPoint *AmbiPoints, const ALfloat (*RESTRICT AmbiMatrix)[MAX_AMBI_COEFFS], const ALsizei AmbiCount, const ALfloat *RESTRICT AmbiOrderHFGain)
{
    static constexpr int OrderFromChan[MAX_AMBI_COEFFS]{
        0, 1,1,1, 2,2,2,2,2, 3,3,3,3,3,3,3,
    };
    /* Set this to true for dual-band HRTF processing. May require a higher
     * quality filter, or better calculation of the new IR length to deal with
     * the tail generated by the filter.
     */
    static constexpr bool DualBand{true};

    ASSUME(NumChannels > 0);
    ASSUME(AmbiCount > 0);

    const auto &field = Hrtf->field[Hrtf->fdCount-1];
    ALsizei min_delay{HRTF_HISTORY_LENGTH};
    ALsizei max_delay{0};
    al::vector<ALsizei> idx(AmbiCount);
    auto calc_idxs = [Hrtf,&field,&max_delay,&min_delay](const AngularPoint &pt) noexcept -> ALsizei
    {
        /* Calculate elevation index. */
        const auto evidx = clampi(
            static_cast<ALsizei>((90.0f+pt.Elev)*(field.evCount-1)/180.0f + 0.5f),
            0, field.evCount-1);

        const ALsizei azcount{Hrtf->azCount[evidx]};
        const ALsizei evoffset{Hrtf->evOffset[evidx]};

        /* Calculate azimuth index for this elevation. */
        const auto azidx = static_cast<ALsizei>((360.0f+pt.Azim)*azcount/360.0f + 0.5f) % azcount;

        /* Calculate the index for the impulse response. */
        ALsizei idx{evoffset + azidx};

        min_delay = mini(min_delay, mini(Hrtf->delays[idx][0], Hrtf->delays[idx][1]));
        max_delay = maxi(max_delay, maxi(Hrtf->delays[idx][0], Hrtf->delays[idx][1]));

        return idx;
    };
    std::transform(AmbiPoints, AmbiPoints+AmbiCount, idx.begin(), calc_idxs);

    const ALdouble xover_norm{400.0 / Hrtf->sampleRate};
    BandSplitterR<double> splitter;
    splitter.init(xover_norm);

    al::vector<std::array<std::array<ALdouble,2>,HRIR_LENGTH>> tmpres(NumChannels);
    al::vector<std::array<ALdouble,HRIR_LENGTH>> tmpfilt(3);
    for(ALsizei c{0};c < AmbiCount;++c)
    {
        const ALfloat (*fir)[2]{&Hrtf->coeffs[idx[c] * Hrtf->irSize]};
        const ALsizei ldelay{Hrtf->delays[idx[c]][0] - min_delay};
        const ALsizei rdelay{Hrtf->delays[idx[c]][1] - min_delay};

        if(!DualBand)
        {
            /* For single-band decoding, apply the HF scale to the response. */
            for(ALsizei i{0};i < NumChannels;++i)
            {
                const ALdouble mult{ALdouble{AmbiOrderHFGain[OrderFromChan[i]]} *
                    AmbiMatrix[c][i]};
                const ALsizei numirs{mini(Hrtf->irSize, HRIR_LENGTH-maxi(ldelay, rdelay))};
                ALsizei lidx{ldelay}, ridx{rdelay};
                for(ALsizei j{0};j < numirs;++j)
                {
                    tmpres[i][lidx++][0] += fir[j][0] * mult;
                    tmpres[i][ridx++][1] += fir[j][1] * mult;
                }
            }
            continue;
        }

        /* Split the left HRIR into low and high frequency bands. */
        auto tmpfilt_iter = std::transform(fir, fir+Hrtf->irSize, tmpfilt[2].begin(),
            [](const ALfloat (&ir)[2]) noexcept { return ir[0]; });
        std::fill(tmpfilt_iter, tmpfilt[2].end(), 0.0);
        splitter.clear();
        splitter.process(tmpfilt[0].data(), tmpfilt[1].data(), tmpfilt[2].data(), HRIR_LENGTH);

        /* Apply left ear response with delay and HF scale. */
        for(ALsizei i{0};i < NumChannels;++i)
        {
            const ALdouble mult{AmbiMatrix[c][i]};
            const ALdouble hfgain{AmbiOrderHFGain[OrderFromChan[i]]};
            for(ALsizei lidx{ldelay},j{0};lidx < HRIR_LENGTH;++lidx,++j)
                tmpres[i][lidx][0] += (tmpfilt[0][j]*hfgain + tmpfilt[1][j]) * mult;
        }

        /* Split the right HRIR into low and high frequency bands. */
        tmpfilt_iter = std::transform(fir, fir+Hrtf->irSize, tmpfilt[2].begin(),
            [](const ALfloat (&ir)[2]) noexcept { return ir[1]; });
        std::fill(tmpfilt_iter, tmpfilt[2].end(), 0.0);
        splitter.clear();
        splitter.process(tmpfilt[0].data(), tmpfilt[1].data(), tmpfilt[2].data(), HRIR_LENGTH);

        /* Apply right ear response with delay and HF scale. */
        for(ALsizei i{0};i < NumChannels;++i)
        {
            const ALdouble mult{AmbiMatrix[c][i]};
            const ALdouble hfgain{AmbiOrderHFGain[OrderFromChan[i]]};
            for(ALsizei ridx{rdelay},j{0};ridx < HRIR_LENGTH;++ridx,++j)
                tmpres[i][ridx][1] += (tmpfilt[0][j]*hfgain + tmpfilt[1][j]) * mult;
        }
    }
    tmpfilt.clear();

    for(ALsizei i{0};i < NumChannels;++i)
    {
        for(ALsizei idx{0};idx < HRIR_LENGTH;idx++)
        {
            state->Chan[i].Coeffs[idx][0] = static_cast<ALfloat>(tmpres[i][idx][0]);
            state->Chan[i].Coeffs[idx][1] = static_cast<ALfloat>(tmpres[i][idx][1]);
        }
    }
    tmpres.clear();
    idx.clear();

    ALsizei max_length;
    if(!DualBand)
        max_length = mini(max_delay-min_delay + Hrtf->irSize, HRIR_LENGTH);
    else
    {
        /* Increase the IR size by 2/3rds to account for the tail generated by
         * the filter.
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
}


namespace {

HrtfEntry *CreateHrtfStore(ALuint rate, ALsizei irSize, const ALsizei fdCount,
    const ALfloat *distance, const ALubyte *evCount, const ALubyte *azCount,
    const ALushort *evOffset, ALsizei irCount, const ALfloat (*coeffs)[2],
    const ALubyte (*delays)[2], const char *filename)
{
    HrtfEntry *Hrtf;

    ALsizei evTotal{std::accumulate(evCount, evCount+fdCount, 0)};
    size_t total{sizeof(HrtfEntry)};
    total  = RoundUp(total, alignof(HrtfEntry::Field)); /* Align for field infos */
    total += sizeof(HrtfEntry::Field)*fdCount;
    total += sizeof(Hrtf->azCount[0])*evTotal;
    total  = RoundUp(total, sizeof(ALushort)); /* Align for ushort fields */
    total += sizeof(Hrtf->evOffset[0])*evTotal;
    total  = RoundUp(total, 16); /* Align for coefficients using SIMD */
    total += sizeof(Hrtf->coeffs[0])*irSize*irCount;
    total += sizeof(Hrtf->delays[0])*irCount;

    Hrtf = new (al_calloc(16, total)) HrtfEntry{};
    if(Hrtf == nullptr)
        ERR("Out of memory allocating storage for %s.\n", filename);
    else
    {
        uintptr_t offset = sizeof(HrtfEntry);
        char *base = reinterpret_cast<char*>(Hrtf);
        HrtfEntry::Field *field_;
        ALushort *evOffset_;
        ALubyte *azCount_;
        ALubyte (*delays_)[2];
        ALfloat (*coeffs_)[2];

        InitRef(&Hrtf->ref, 0);
        Hrtf->sampleRate = rate;
        Hrtf->irSize = irSize;
        Hrtf->fdCount = fdCount;

        /* Set up pointers to storage following the main HRTF struct. */
        offset  = RoundUp(offset, alignof(HrtfEntry::Field)); /* Align for field infos */
        field_ = reinterpret_cast<HrtfEntry::Field*>(base + offset);
        offset += sizeof(field_[0])*fdCount;

        azCount_ = reinterpret_cast<ALubyte*>(base + offset);
        offset += sizeof(azCount_[0])*evTotal;

        offset = RoundUp(offset, sizeof(ALushort)); /* Align for ushort fields */
        evOffset_ = reinterpret_cast<ALushort*>(base + offset);
        offset += sizeof(evOffset_[0])*evTotal;

        offset = RoundUp(offset, 16); /* Align for coefficients using SIMD */
        coeffs_ = reinterpret_cast<ALfloat(*)[2]>(base + offset);
        offset += sizeof(coeffs_[0])*irSize*irCount;

        delays_ = reinterpret_cast<ALubyte(*)[2]>(base + offset);
        offset += sizeof(delays_[0])*irCount;

        assert(offset == total);

        /* Copy input data to storage. */
        for(ALsizei i{0};i < fdCount;i++)
        {
            field_[i].distance = distance[i];
            field_[i].evCount = evCount[i];
        }
        for(ALsizei i{0};i < evTotal;i++) azCount_[i] = azCount[i];
        for(ALsizei i{0};i < evTotal;i++) evOffset_[i] = evOffset[i];
        for(ALsizei i{0};i < irSize*irCount;i++)
        {
            coeffs_[i][0] = coeffs[i][0];
            coeffs_[i][1] = coeffs[i][1];
        }
        for(ALsizei i{0};i < irCount;i++)
        {
            delays_[i][0] = delays[i][0];
            delays_[i][1] = delays[i][1];
        }

        /* Finally, assign the storage pointers. */
        Hrtf->field = field_;
        Hrtf->azCount = azCount_;
        Hrtf->evOffset = evOffset_;
        Hrtf->coeffs = coeffs_;
        Hrtf->delays = delays_;
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

HrtfEntry *LoadHrtf00(std::istream &data, const char *filename)
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

    al::vector<ALushort> evOffset(evCount);
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

    al::vector<ALubyte> azCount(evCount);
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

    al::vector<std::array<ALfloat,2>> coeffs(irSize*irCount);
    al::vector<std::array<ALubyte,2>> delays(irCount);
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

    static constexpr ALfloat distance{0.0f};
    return CreateHrtfStore(rate, irSize, 1, &distance, &evCount, azCount.data(), evOffset.data(),
        irCount, &reinterpret_cast<ALfloat(&)[2]>(coeffs[0]),
        &reinterpret_cast<ALubyte(&)[2]>(delays[0]), filename);
}

HrtfEntry *LoadHrtf01(std::istream &data, const char *filename)
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

    al::vector<ALubyte> azCount(evCount);
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

    al::vector<ALushort> evOffset(evCount);
    evOffset[0] = 0;
    ALushort irCount{azCount[0]};
    for(ALsizei i{1};i < evCount;i++)
    {
        evOffset[i] = evOffset[i-1] + azCount[i-1];
        irCount += azCount[i];
    }

    al::vector<std::array<ALfloat,2>> coeffs(irSize*irCount);
    al::vector<std::array<ALubyte,2>> delays(irCount);
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

    static constexpr ALfloat distance{0.0f};
    return CreateHrtfStore(rate, irSize, 1, &distance, &evCount, azCount.data(), evOffset.data(),
        irCount, &reinterpret_cast<ALfloat(&)[2]>(coeffs[0]),
        &reinterpret_cast<ALubyte(&)[2]>(delays[0]), filename);
}

#define SAMPLETYPE_S16 0
#define SAMPLETYPE_S24 1

#define CHANTYPE_LEFTONLY  0
#define CHANTYPE_LEFTRIGHT 1

HrtfEntry *LoadHrtf02(std::istream &data, const char *filename)
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
    if(fdCount < 1 || fdCount > MAX_FD_COUNT)
    {
        ERR("Multiple field-depths not supported: fdCount=%d (%d to %d)\n",
            fdCount, MIN_FD_COUNT, MAX_FD_COUNT);
        failed = AL_TRUE;
    }
    if(failed)
        return nullptr;

    al::vector<ALfloat> distance(fdCount);
    al::vector<ALubyte> evCount(fdCount);
    al::vector<ALubyte> azCount;
    for(ALsizei f{0};f < fdCount;f++)
    {
        distance[f] = GetLE_ALushort(data) / 1000.0f;
        evCount[f] = GetLE_ALubyte(data);
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }

        if(distance[f] < MIN_FD_DISTANCE || distance[f] > MAX_FD_DISTANCE)
        {
            ERR("Unsupported field distance[%d]=%f (%f to %f meters)\n", f,
                distance[f], MIN_FD_DISTANCE, MAX_FD_DISTANCE);
            failed = AL_TRUE;
        }
        if(f > 0 && distance[f] <= distance[f-1])
        {
            ERR("Field distance[%d] is not after previous (%f > %f)\n", f, distance[f],
                distance[f-1]);
            failed = AL_TRUE;
        }
        if(evCount[f] < MIN_EV_COUNT || evCount[f] > MAX_EV_COUNT)
        {
            ERR("Unsupported elevation count: evCount[%d]=%d (%d to %d)\n", f,
                evCount[f], MIN_EV_COUNT, MAX_EV_COUNT);
            failed = AL_TRUE;
        }
        if(failed)
            return nullptr;

        size_t ebase{azCount.size()};
        azCount.resize(ebase + evCount[f]);
        data.read(reinterpret_cast<char*>(azCount.data()+ebase), evCount[f]);
        if(!data || data.eof() || data.gcount() < evCount[f])
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }

        for(ALsizei e{0};e < evCount[f];e++)
        {
            if(azCount[ebase+e] < MIN_AZ_COUNT || azCount[ebase+e] > MAX_AZ_COUNT)
            {
                ERR("Unsupported azimuth count: azCount[%d][%d]=%d (%d to %d)\n", f, e,
                    azCount[ebase+e], MIN_AZ_COUNT, MAX_AZ_COUNT);
                failed = AL_TRUE;
            }
        }
        if(failed)
            return nullptr;
    }

    al::vector<ALushort> evOffset;
    evOffset.resize(evCount[0]);

    evOffset[0] = 0;
    ALushort irCount{azCount[0]};
    for(ALsizei e{1};e < evCount[0];++e)
    {
        evOffset[e] = evOffset[e-1] + azCount[e-1];
        irCount += azCount[e];
    }

    ALsizei irTotal{irCount};
    for(ALsizei f{1};f < fdCount;f++)
    {
        const ALsizei ebase{std::accumulate(evCount.begin(), evCount.begin()+f, 0)};
        evOffset.resize(ebase + evCount[f]);

        evOffset[ebase] = irTotal;
        irTotal += azCount[ebase];
        for(ALsizei e{1};e < evCount[f];++e)
        {
            evOffset[ebase+e] = evOffset[ebase+e-1] + azCount[ebase+e-1];
            irTotal += azCount[ebase+e];
        }
    }
    al::vector<std::array<ALfloat,2>> coeffs(irSize*irTotal);
    al::vector<std::array<ALubyte,2>> delays(irTotal);
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
        for(ALsizei i{0};i < irTotal;++i)
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

        for(ALsizei i{0};i < irTotal;++i)
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
        ALsizei ebase{0};
        for(ALsizei f{0};f < fdCount;f++)
        {
            for(ALsizei e{0};e < evCount[f];e++)
            {
                ALushort evoffset = evOffset[ebase+e];
                ALubyte azcount = azCount[ebase+e];
                for(ALsizei a{0};a < azcount;a++)
                {
                    ALsizei lidx = evoffset + a;
                    ALsizei ridx = evoffset + ((azcount-a) % azcount);

                    for(ALsizei k{0};k < irSize;k++)
                        coeffs[ridx*irSize + k][1] = coeffs[lidx*irSize + k][0];
                    delays[ridx][1] = delays[lidx][0];
                }
            }
            ebase += evCount[f];
        }
    }

    return CreateHrtfStore(rate, irSize, fdCount, distance.data(), evCount.data(), azCount.data(),
        evOffset.data(), irTotal, &reinterpret_cast<ALfloat(&)[2]>(coeffs[0]),
        &reinterpret_cast<ALubyte(&)[2]>(delays[0]), filename);
}


bool checkName(al::vector<EnumeratedHrtf> &list, const std::string &name)
{
    return std::find_if(list.cbegin(), list.cend(),
        [&name](const EnumeratedHrtf &entry)
        { return name == entry.name; }
    ) != list.cend();
}

void AddFileEntry(al::vector<EnumeratedHrtf> &list, const std::string &filename)
{
    /* Check if this file has already been loaded globally. */
    auto loaded_entry = LoadedHrtfs.begin();
    for(;loaded_entry != LoadedHrtfs.end();++loaded_entry)
    {
        if(filename != (*loaded_entry)->filename.data())
            continue;

        /* Check if this entry has already been added to the list. */
        auto iter = std::find_if(list.cbegin(), list.cend(),
            [loaded_entry](const EnumeratedHrtf &entry) -> bool
            { return loaded_entry->get() == entry.hrtf; }
        );
        if(iter != list.cend())
        {
            TRACE("Skipping duplicate file entry %s\n", filename.c_str());
            return;
        }

        break;
    }

    if(loaded_entry == LoadedHrtfs.end())
    {
        TRACE("Got new file \"%s\"\n", filename.c_str());

        LoadedHrtfs.emplace_back(HrtfHandle::Create(filename.length()+1));
        loaded_entry = LoadedHrtfs.end()-1;
        strcpy((*loaded_entry)->filename.data(), filename.c_str());
    }

    /* TODO: Get a human-readable name from the HRTF data (possibly coming in a
     * format update). */
    size_t namepos = filename.find_last_of('/')+1;
    if(!namepos) namepos = filename.find_last_of('\\')+1;

    size_t extpos{filename.find_last_of('.')};
    if(extpos <= namepos) extpos = std::string::npos;

    const std::string basename{(extpos == std::string::npos) ?
        filename.substr(namepos) : filename.substr(namepos, extpos-namepos)};
    std::string newname{basename};
    int count{1};
    while(checkName(list, newname))
    {
        newname = basename;
        newname += " #";
        newname += std::to_string(++count);
    }
    list.emplace_back(EnumeratedHrtf{newname, loaded_entry->get()});
    const EnumeratedHrtf &entry = list.back();

    TRACE("Adding file entry \"%s\"\n", entry.name.c_str());
}

/* Unfortunate that we have to duplicate AddFileEntry to take a memory buffer
 * for input instead of opening the given filename.
 */
void AddBuiltInEntry(al::vector<EnumeratedHrtf> &list, const std::string &filename, ALuint residx)
{
    auto loaded_entry = LoadedHrtfs.begin();
    for(;loaded_entry != LoadedHrtfs.end();++loaded_entry)
    {
        if(filename != (*loaded_entry)->filename.data())
            continue;

        /* Check if this entry has already been added to the list. */
        auto iter = std::find_if(list.cbegin(), list.cend(),
            [loaded_entry](const EnumeratedHrtf &entry) -> bool
            { return loaded_entry->get() == entry.hrtf; }
        );
        if(iter != list.cend())
        {
            TRACE("Skipping duplicate file entry %s\n", filename.c_str());
            return;
        }

        break;
    }

    if(loaded_entry == LoadedHrtfs.end())
    {
        TRACE("Got new file \"%s\"\n", filename.c_str());

        LoadedHrtfs.emplace_back(HrtfHandle::Create(filename.length()+32));
        loaded_entry = LoadedHrtfs.end()-1;
        snprintf((*loaded_entry)->filename.data(), (*loaded_entry)->filename.size(), "!%u_%s",
            residx, filename.c_str());
    }

    /* TODO: Get a human-readable name from the HRTF data (possibly coming in a
     * format update). */

    std::string newname{filename};
    int count{1};
    while(checkName(list, newname))
    {
        newname = filename;
        newname += " #";
        newname += std::to_string(++count);
    }
    list.emplace_back(EnumeratedHrtf{newname, loaded_entry->get()});
    const EnumeratedHrtf &entry = list.back();

    TRACE("Adding built-in entry \"%s\"\n", entry.name.c_str());
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


al::vector<EnumeratedHrtf> EnumerateHrtf(const char *devname)
{
    al::vector<EnumeratedHrtf> list;

    bool usedefaults{true};
    const char *pathlist{""};
    if(ConfigValueStr(devname, nullptr, "hrtf-paths", &pathlist))
    {
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
                const std::string pname{pathlist, end};
                for(const auto &fname : SearchDataFiles(".mhr", pname.c_str()))
                    AddFileEntry(list, fname);
            }

            pathlist = next;
        }
    }
    else if(ConfigValueExists(devname, nullptr, "hrtf_tables"))
        ERR("The hrtf_tables option is deprecated, please use hrtf-paths instead.\n");

    if(usedefaults)
    {
        for(const auto &fname : SearchDataFiles(".mhr", "openal/hrtf"))
            AddFileEntry(list, fname);

        ResData res{GetResource(IDR_DEFAULT_44100_MHR)};
        if(res.data != nullptr && res.size > 0)
            AddBuiltInEntry(list, "Built-In 44100hz", IDR_DEFAULT_44100_MHR);

        res = GetResource(IDR_DEFAULT_48000_MHR);
        if(res.data != nullptr && res.size > 0)
            AddBuiltInEntry(list, "Built-In 48000hz", IDR_DEFAULT_48000_MHR);
    }

    const char *defaulthrtf{""};
    if(!list.empty() && ConfigValueStr(devname, nullptr, "default-hrtf", &defaulthrtf))
    {
        auto iter = std::find_if(list.begin(), list.end(),
            [defaulthrtf](const EnumeratedHrtf &entry) -> bool
            { return entry.name == defaulthrtf; }
        );
        if(iter == list.end())
            WARN("Failed to find default HRTF \"%s\"\n", defaulthrtf);
        else if(iter != list.begin())
        {
            EnumeratedHrtf entry{*iter};
            list.erase(iter);
            list.insert(list.begin(), entry);
        }
    }

    return list;
}

HrtfEntry *GetLoadedHrtf(HrtfHandle *handle)
{
    std::lock_guard<std::mutex> _{LoadedHrtfLock};

    if(handle->entry)
    {
        HrtfEntry *hrtf{handle->entry};
        hrtf->IncRef();
        return hrtf;
    }

    std::unique_ptr<std::istream> stream;
    const char *name{""};
    ALuint residx{};
    char ch{};
    if(sscanf(handle->filename.data(), "!%u%c", &residx, &ch) == 2 && ch == '_')
    {
        name = strchr(handle->filename.data(), ch)+1;

        TRACE("Loading %s...\n", name);
        ResData res{GetResource(residx)};
        if(!res.data || res.size == 0)
        {
            ERR("Could not get resource %u, %s\n", residx, name);
            return nullptr;
        }
        stream = al::make_unique<idstream>(res.data, res.data+res.size);
    }
    else
    {
        name = handle->filename.data();

        TRACE("Loading %s...\n", handle->filename.data());
        auto fstr = al::make_unique<al::ifstream>(handle->filename.data(), std::ios::binary);
        if(!fstr->is_open())
        {
            ERR("Could not open %s\n", handle->filename.data());
            return nullptr;
        }
        stream = std::move(fstr);
    }

    HrtfEntry *hrtf{nullptr};
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
        handle->entry = hrtf;
        hrtf->IncRef();
        TRACE("Loaded HRTF support for format: %s %uhz\n",
              DevFmtChannelsString(DevFmtStereo), hrtf->sampleRate);
    }

    return hrtf;
}


void HrtfEntry::IncRef()
{
    auto ref = IncrementRef(&this->ref);
    TRACEREF("%p increasing refcount to %u\n", this, ref);
}

void HrtfEntry::DecRef()
{
    auto ref = DecrementRef(&this->ref);
    TRACEREF("%p decreasing refcount to %u\n", this, ref);
    if(ref == 0)
    {
        std::lock_guard<std::mutex> _{LoadedHrtfLock};

        /* Need to double-check that it's still unused, as another device
         * could've reacquired this HRTF after its reference went to 0 and
         * before the lock was taken.
         */
        auto iter = std::find_if(LoadedHrtfs.begin(), LoadedHrtfs.end(),
            [this](const HrtfHandlePtr &entry) noexcept -> bool
            { return this == entry->entry; }
        );
        if(iter != LoadedHrtfs.end() && ReadRef(&this->ref) == 0)
        {
            delete (*iter)->entry;
            (*iter)->entry = nullptr;
            TRACE("Unloaded unused HRTF %s\n", (*iter)->filename.data());
        }
    }
}
