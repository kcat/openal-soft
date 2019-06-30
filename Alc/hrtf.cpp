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
#include <functional>

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
#include "alspan.h"


struct HrtfHandle {
    std::unique_ptr<HrtfEntry> entry;
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
    void *ptr{al_calloc(alignof(HrtfHandle), HrtfHandle::Sizeof(fname_len))};
    return std::unique_ptr<HrtfHandle>{new (ptr) HrtfHandle{fname_len}};
}

namespace {

using namespace std::placeholders;

using HrtfHandlePtr = std::unique_ptr<HrtfHandle>;

/* Data set limits must be the same as or more flexible than those defined in
 * the makemhr utility.
 */
#define MIN_IR_SIZE                  (8)
#define MAX_IR_SIZE                  (512)
#define MOD_IR_SIZE                  (2)

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
    ALfloat spread, HrirArray<ALfloat> &coeffs, ALsizei (&delays)[2])
{
    const ALfloat dirfact{1.0f - (spread / al::MathDefs<float>::Tau())};

    const auto *field = Hrtf->field;
    const auto *field_end = field + Hrtf->fdCount-1;
    ALsizei ebase{0};
    while(distance < field->distance && field != field_end)
    {
        ebase += field->evCount;
        ++field;
    }

    /* Claculate the elevation indinces. */
    const auto elev0 = CalcEvIndex(field->evCount, elevation);
    const ALsizei elev1_idx{mini(elev0.idx+1, field->evCount-1)};
    const ALsizei ir0offset{Hrtf->elev[ebase + elev0.idx].irOffset};
    const ALsizei ir1offset{Hrtf->elev[ebase + elev1_idx].irOffset};

    /* Calculate azimuth indices. */
    const auto az0 = CalcAzIndex(Hrtf->elev[ebase + elev0.idx].azCount, azimuth);
    const auto az1 = CalcAzIndex(Hrtf->elev[ebase + elev1_idx].azCount, azimuth);

    /* Calculate the HRIR indices to blend. */
    ALsizei idx[4]{
        ir0offset + az0.idx,
        ir0offset + ((az0.idx+1) % Hrtf->elev[ebase + elev0.idx].azCount),
        ir1offset + az1.idx,
        ir1offset + ((az1.idx+1) % Hrtf->elev[ebase + elev1_idx].azCount)
    };

    /* Calculate bilinear blending weights, attenuated according to the
     * directional panning factor.
     */
    const ALfloat blend[4]{
        (1.0f-elev0.blend) * (1.0f-az0.blend) * dirfact,
        (1.0f-elev0.blend) * (     az0.blend) * dirfact,
        (     elev0.blend) * (1.0f-az1.blend) * dirfact,
        (     elev0.blend) * (     az1.blend) * dirfact
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
    ALfloat *coeffout{al::assume_aligned<16>(&coeffs[0][0])};
    coeffout[0] = PassthruCoeff * (1.0f-dirfact);
    coeffout[1] = PassthruCoeff * (1.0f-dirfact);
    std::fill(coeffout+2, coeffout + irSize*2, 0.0f);
    for(ALsizei c{0};c < 4;c++)
    {
        const ALfloat *srccoeffs{al::assume_aligned<16>(Hrtf->coeffs[idx[c]])};
        const ALfloat mult{blend[c]};
        auto blend_coeffs = [mult](const ALfloat src, const ALfloat coeff) noexcept -> ALfloat
        { return src*mult + coeff; };
        std::transform(srccoeffs, srccoeffs + irSize*2, coeffout, coeffout, blend_coeffs);
    }
}


std::unique_ptr<DirectHrtfState> DirectHrtfState::Create(size_t num_chans)
{
    void *ptr{al_calloc(16, DirectHrtfState::Sizeof(num_chans))};
    return std::unique_ptr<DirectHrtfState>{new (ptr) DirectHrtfState{num_chans}};
}

void BuildBFormatHrtf(const HrtfEntry *Hrtf, DirectHrtfState *state, const ALuint NumChannels,
    const AngularPoint *AmbiPoints, const ALfloat (*RESTRICT AmbiMatrix)[MAX_AMBI_CHANNELS],
    const size_t AmbiCount, const ALfloat *RESTRICT AmbiOrderHFGain)
{
    static constexpr int OrderFromChan[MAX_AMBI_CHANNELS]{
        0, 1,1,1, 2,2,2,2,2, 3,3,3,3,3,3,3,
    };
    /* Set this to true for dual-band HRTF processing. May require better
     * calculation of the new IR length to deal with the head and tail
     * generated by the HF scaling.
     */
    static constexpr bool DualBand{true};

    ASSUME(NumChannels > 0);
    ASSUME(AmbiCount > 0);

    auto &field = Hrtf->field[0];
    ALsizei min_delay{HRTF_HISTORY_LENGTH};
    ALsizei max_delay{0};
    auto idx = al::vector<ALsizei>(AmbiCount);
    auto calc_idxs = [Hrtf,&field,&max_delay,&min_delay](const AngularPoint &pt) noexcept -> ALsizei
    {
        /* Calculate elevation index. */
        const auto evidx = clampi(
            static_cast<ALsizei>((90.0f+pt.Elev)*(field.evCount-1)/180.0f + 0.5f),
            0, field.evCount-1);

        const ALsizei azcount{Hrtf->elev[evidx].azCount};
        const ALsizei iroffset{Hrtf->elev[evidx].irOffset};

        /* Calculate azimuth index for this elevation. */
        const auto azidx = static_cast<ALsizei>((360.0f+pt.Azim)*azcount/360.0f + 0.5f) % azcount;

        /* Calculate the index for the impulse response. */
        ALsizei idx{iroffset + azidx};

        min_delay = mini(min_delay, mini(Hrtf->delays[idx][0], Hrtf->delays[idx][1]));
        max_delay = maxi(max_delay, maxi(Hrtf->delays[idx][0], Hrtf->delays[idx][1]));

        return idx;
    };
    std::transform(AmbiPoints, AmbiPoints+AmbiCount, idx.begin(), calc_idxs);

    /* For dual-band processing, add a 16-sample delay to compensate for the HF
     * scale on the minimum-phase response.
     */
    static constexpr ALsizei base_delay{DualBand ? 16 : 0};
    const ALdouble xover_norm{400.0 / Hrtf->sampleRate};
    BandSplitterR<double> splitter{xover_norm};

    auto tmpres = al::vector<HrirArray<ALdouble>>(NumChannels);
    auto tmpfilt = al::vector<std::array<ALdouble,HRIR_LENGTH*4>>(3);
    for(size_t c{0u};c < AmbiCount;++c)
    {
        const ALfloat (*fir)[2]{&Hrtf->coeffs[idx[c] * Hrtf->irSize]};
        const ALsizei ldelay{Hrtf->delays[idx[c]][0] - min_delay + base_delay};
        const ALsizei rdelay{Hrtf->delays[idx[c]][1] - min_delay + base_delay};

        if(!DualBand)
        {
            /* For single-band decoding, apply the HF scale to the response. */
            for(ALuint i{0u};i < NumChannels;++i)
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

        /* For dual-band processing, the HRIR needs to be split into low and
         * high frequency responses. The band-splitter alone creates frequency-
         * dependent phase-shifts, which is not ideal. To counteract it,
         * combine it with a backwards phase-shift.
         */

        /* Load the (left) HRIR backwards, into a temp buffer with padding. */
        std::fill(tmpfilt[2].begin(), tmpfilt[2].end(), 0.0);
        std::transform(fir, fir+Hrtf->irSize, tmpfilt[2].rbegin() + HRIR_LENGTH*3,
            [](const ALfloat (&ir)[2]) noexcept -> ALdouble { return ir[0]; });

        /* Apply the all-pass on the reversed signal and reverse the resulting
         * sample array. This produces the forward response with a backwards
         * phase-shift (+n degrees becomes -n degrees).
         */
        splitter.applyAllpass(tmpfilt[2].data(), static_cast<int>(tmpfilt[2].size()));
        std::reverse(tmpfilt[2].begin(), tmpfilt[2].end());

        /* Now apply the band-splitter. This applies the normal phase-shift,
         * which cancels out with the backwards phase-shift to get the original
         * phase on the split signal.
         */
        splitter.clear();
        splitter.process(tmpfilt[0].data(), tmpfilt[1].data(), tmpfilt[2].data(),
            static_cast<int>(tmpfilt[2].size()));

        /* Apply left ear response with delay and HF scale. */
        for(ALuint i{0u};i < NumChannels;++i)
        {
            const ALdouble mult{AmbiMatrix[c][i]};
            const ALdouble hfgain{AmbiOrderHFGain[OrderFromChan[i]]};
            ALsizei j{HRIR_LENGTH*3 - ldelay};
            for(ALsizei lidx{0};lidx < HRIR_LENGTH;++lidx,++j)
                tmpres[i][lidx][0] += (tmpfilt[0][j]*hfgain + tmpfilt[1][j]) * mult;
        }

        /* Now run the same process on the right HRIR. */
        std::fill(tmpfilt[2].begin(), tmpfilt[2].end(), 0.0);
        std::transform(fir, fir+Hrtf->irSize, tmpfilt[2].rbegin() + HRIR_LENGTH*3,
            [](const ALfloat (&ir)[2]) noexcept -> ALdouble { return ir[1]; });

        splitter.applyAllpass(tmpfilt[2].data(), static_cast<int>(tmpfilt[2].size()));
        std::reverse(tmpfilt[2].begin(), tmpfilt[2].end());

        splitter.clear();
        splitter.process(tmpfilt[0].data(), tmpfilt[1].data(), tmpfilt[2].data(),
            static_cast<int>(tmpfilt[2].size()));

        for(ALuint i{0u};i < NumChannels;++i)
        {
            const ALdouble mult{AmbiMatrix[c][i]};
            const ALdouble hfgain{AmbiOrderHFGain[OrderFromChan[i]]};
            ALsizei j{HRIR_LENGTH*3 - rdelay};
            for(ALsizei ridx{0};ridx < HRIR_LENGTH;++ridx,++j)
                tmpres[i][ridx][1] += (tmpfilt[0][j]*hfgain + tmpfilt[1][j]) * mult;
        }
    }
    tmpfilt.clear();
    idx.clear();

    for(ALuint i{0u};i < NumChannels;++i)
    {
        auto copy_arr = [](const std::array<double,2> &in) noexcept -> std::array<float,2>
        { return std::array<float,2>{{static_cast<float>(in[0]), static_cast<float>(in[1])}}; };
        std::transform(tmpres[i].begin(), tmpres[i].end(), state->Chan[i].Coeffs.begin(),
            copy_arr);
    }
    tmpres.clear();

    ALsizei max_length{HRIR_LENGTH};
    /* Increase the IR size by double the base delay with dual-band processing
     * to account for the head and tail from the HF response scale.
     */
    const ALsizei irsize{mini(Hrtf->irSize + base_delay*2, max_length)};
    max_length = mini(max_delay-min_delay + irsize, max_length);

    /* Round up to the next IR size multiple. */
    max_length += MOD_IR_SIZE-1;
    max_length -= max_length%MOD_IR_SIZE;

    TRACE("Skipped delay: %d, max delay: %d, new FIR length: %d\n",
          min_delay, max_delay-min_delay, max_length);
    state->IrSize = max_length;
}


namespace {

std::unique_ptr<HrtfEntry> CreateHrtfStore(ALuint rate, ALsizei irSize, const ALsizei fdCount,
    const ALubyte *evCount, const ALfloat *distance, const ALushort *azCount,
    const ALushort *irOffset, ALsizei irCount, const ALfloat (*coeffs)[2],
    const ALubyte (*delays)[2], const char *filename)
{
    std::unique_ptr<HrtfEntry> Hrtf;

    ALsizei evTotal{std::accumulate(evCount, evCount+fdCount, 0)};
    size_t total{sizeof(HrtfEntry)};
    total  = RoundUp(total, alignof(HrtfEntry::Field)); /* Align for field infos */
    total += sizeof(HrtfEntry::Field)*fdCount;
    total  = RoundUp(total, alignof(HrtfEntry::Elevation)); /* Align for elevation infos */
    total += sizeof(Hrtf->elev[0])*evTotal;
    total  = RoundUp(total, 16); /* Align for coefficients using SIMD */
    total += sizeof(Hrtf->coeffs[0])*irSize*irCount;
    total += sizeof(Hrtf->delays[0])*irCount;

    Hrtf.reset(new (al_calloc(16, total)) HrtfEntry{});
    if(!Hrtf)
        ERR("Out of memory allocating storage for %s.\n", filename);
    else
    {
        InitRef(&Hrtf->ref, 1u);
        Hrtf->sampleRate = rate;
        Hrtf->irSize = irSize;
        Hrtf->fdCount = fdCount;

        /* Set up pointers to storage following the main HRTF struct. */
        char *base = reinterpret_cast<char*>(Hrtf.get());
        uintptr_t offset = sizeof(HrtfEntry);

        offset = RoundUp(offset, alignof(HrtfEntry::Field)); /* Align for field infos */
        auto field_ = reinterpret_cast<HrtfEntry::Field*>(base + offset);
        offset += sizeof(field_[0])*fdCount;

        offset = RoundUp(offset, alignof(HrtfEntry::Elevation)); /* Align for elevation infos */
        auto elev_ = reinterpret_cast<HrtfEntry::Elevation*>(base + offset);
        offset += sizeof(elev_[0])*evTotal;

        offset = RoundUp(offset, 16); /* Align for coefficients using SIMD */
        auto coeffs_ = reinterpret_cast<ALfloat(*)[2]>(base + offset);
        offset += sizeof(coeffs_[0])*irSize*irCount;

        auto delays_ = reinterpret_cast<ALubyte(*)[2]>(base + offset);
        offset += sizeof(delays_[0])*irCount;

        assert(offset == total);

        /* Copy input data to storage. */
        for(ALsizei i{0};i < fdCount;i++)
        {
            field_[i].distance = distance[i];
            field_[i].evCount = evCount[i];
        }
        for(ALsizei i{0};i < evTotal;i++)
        {
            elev_[i].azCount = azCount[i];
            elev_[i].irOffset = irOffset[i];
        }
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
        Hrtf->elev = elev_;
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

std::unique_ptr<HrtfEntry> LoadHrtf00(std::istream &data, const char *filename)
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
        ERR("Invalid evOffset: evOffset[%zu]=%d (irCount=%d)\n",
            evOffset.size()-1, evOffset.back(), irCount);
        failed = AL_TRUE;
    }
    if(failed)
        return nullptr;

    al::vector<ALushort> azCount(evCount);
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
        ERR("Unsupported azimuth count: azCount[%zu]=%d (%d to %d)\n",
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
        const ALushort evoffset{evOffset[i]};
        const ALushort azcount{azCount[i]};
        for(ALsizei j{0};j < azcount;j++)
        {
            const ALsizei lidx{evoffset + j};
            const ALsizei ridx{evoffset + ((azcount-j) % azcount)};

            for(ALsizei k{0};k < irSize;k++)
                coeffs[ridx*irSize + k][1] = coeffs[lidx*irSize + k][0];
            delays[ridx][1] = delays[lidx][0];
        }
    }

    static constexpr ALfloat distance{0.0f};
    return CreateHrtfStore(rate, irSize, 1, &evCount, &distance, azCount.data(), evOffset.data(),
        irCount, &reinterpret_cast<ALfloat(&)[2]>(coeffs[0]),
        &reinterpret_cast<ALubyte(&)[2]>(delays[0]), filename);
}

std::unique_ptr<HrtfEntry> LoadHrtf01(std::istream &data, const char *filename)
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

    al::vector<ALushort> azCount(evCount);
    std::generate(azCount.begin(), azCount.end(), std::bind(GetLE_ALubyte, std::ref(data)));
    if(!data || data.eof())
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
        const ALushort evoffset{evOffset[i]};
        const ALushort azcount{azCount[i]};
        for(ALsizei j{0};j < azcount;j++)
        {
            const ALsizei lidx{evoffset + j};
            const ALsizei ridx{evoffset + ((azcount-j) % azcount)};

            for(ALsizei k{0};k < irSize;k++)
                coeffs[ridx*irSize + k][1] = coeffs[lidx*irSize + k][0];
            delays[ridx][1] = delays[lidx][0];
        }
    }

    static constexpr ALfloat distance{0.0f};
    return CreateHrtfStore(rate, irSize, 1, &evCount, &distance, azCount.data(), evOffset.data(),
        irCount, &reinterpret_cast<ALfloat(&)[2]>(coeffs[0]),
        &reinterpret_cast<ALubyte(&)[2]>(delays[0]), filename);
}

#define SAMPLETYPE_S16 0
#define SAMPLETYPE_S24 1

#define CHANTYPE_LEFTONLY  0
#define CHANTYPE_LEFTRIGHT 1

std::unique_ptr<HrtfEntry> LoadHrtf02(std::istream &data, const char *filename)
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
    al::vector<ALushort> azCount;
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
        std::generate(azCount.begin()+ebase, azCount.end(),
            std::bind(GetLE_ALubyte, std::ref(data)));
        if(!data || data.eof())
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

    al::vector<ALushort> evOffset(azCount.size());
    evOffset[0] = 0;
    std::partial_sum(azCount.cbegin(), azCount.cend()-1, evOffset.begin()+1);
    const ALsizei irTotal{evOffset.back() + azCount.back()};

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
                const ALushort evoffset{evOffset[ebase+e]};
                const ALushort azcount{azCount[ebase+e]};
                for(ALsizei a{0};a < azcount;a++)
                {
                    const ALsizei lidx{evoffset + a};
                    const ALsizei ridx{evoffset + ((azcount-a) % azcount)};

                    for(ALsizei k{0};k < irSize;k++)
                        coeffs[ridx*irSize + k][1] = coeffs[lidx*irSize + k][0];
                    delays[ridx][1] = delays[lidx][0];
                }
            }
            ebase += evCount[f];
        }
    }

    if(fdCount > 1)
    {
        auto distance_ = al::vector<ALfloat>(distance.size());
        auto evCount_ = al::vector<ALubyte>(evCount.size());
        auto azCount_ = al::vector<ALushort>(azCount.size());
        auto evOffset_ = al::vector<ALushort>(evOffset.size());
        auto coeffs_ = al::vector<float2>(coeffs.size());
        auto delays_ = al::vector<std::array<ALubyte,2>>(delays.size());

        /* Simple reverse for the per-field elements. */
        std::reverse_copy(distance.cbegin(), distance.cend(), distance_.begin());
        std::reverse_copy(evCount.cbegin(), evCount.cend(), evCount_.begin());

        /* Each field has a group of elevations, which each have an azimuth
         * count. Reverse the order of the groups, keeping the relative order
         * of per-group azimuth counts.
         */
        auto azcnt_end = azCount_.end();
        auto copy_azs = [&azCount,&azcnt_end](const size_t ebase, const ALubyte num_evs) -> size_t
        {
            auto azcnt_src = azCount.begin()+ebase;
            azcnt_end = std::copy_backward(azcnt_src, azcnt_src+num_evs, azcnt_end);
            return ebase + num_evs;
        };
        std::accumulate(evCount.cbegin(), evCount.cend(), size_t{0u}, copy_azs);
        assert(azCount_.begin() == azcnt_end);

        /* Reestablish the IR offset for each elevation index, given the new
         * ordering of elevations.
         */
        evOffset_[0] = 0;
        std::partial_sum(azCount_.cbegin(), azCount_.cend()-1, evOffset_.begin()+1);

        /* Reverse the order of each field's group of IRs. */
        auto coeffs_end = coeffs_.end();
        auto delays_end = delays_.end();
        auto copy_irs = [irSize,&azCount,&coeffs,&delays,&coeffs_end,&delays_end](const size_t ebase, const ALubyte num_evs) -> size_t
        {
            const ALsizei abase{std::accumulate(azCount.cbegin(), azCount.cbegin()+ebase, 0)};
            const ALsizei num_azs{std::accumulate(azCount.cbegin()+ebase,
                azCount.cbegin() + (ebase+num_evs), 0)};

            coeffs_end = std::copy_backward(coeffs.cbegin() + abase*irSize,
                coeffs.cbegin() + (abase+num_azs)*irSize, coeffs_end);
            delays_end = std::copy_backward(delays.cbegin() + abase,
                delays.cbegin() + (abase+num_azs), delays_end);

            return ebase + num_evs;
        };
        std::accumulate(evCount.cbegin(), evCount.cend(), size_t{0u}, copy_irs);
        assert(coeffs_.begin() == coeffs_end);
        assert(delays_.begin() == delays_end);

        distance = std::move(distance_);
        evCount = std::move(evCount_);
        azCount = std::move(azCount_);
        evOffset = std::move(evOffset_);
        coeffs = std::move(coeffs_);
        delays = std::move(delays_);
    }

    return CreateHrtfStore(rate, irSize, fdCount, evCount.data(), distance.data(), azCount.data(),
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
        std::copy(filename.begin(), filename.end(), (*loaded_entry)->filename.begin());
        (*loaded_entry)->filename.back() = '\0';
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

using ResData = al::span<const char>;
#ifndef ALSOFT_EMBED_HRTF_DATA

ResData GetResource(int UNUSED(name))
{ return ResData{}; }

#else

#include "default-44100.mhr.h"
#include "default-48000.mhr.h"

ResData GetResource(int name)
{
    if(name == IDR_DEFAULT_44100_MHR)
        return {reinterpret_cast<const char*>(hrtf_default_44100), sizeof(hrtf_default_44100)};
    if(name == IDR_DEFAULT_48000_MHR)
        return {reinterpret_cast<const char*>(hrtf_default_48000), sizeof(hrtf_default_48000)};
    return ResData{};
}
#endif

} // namespace


al::vector<EnumeratedHrtf> EnumerateHrtf(const char *devname)
{
    al::vector<EnumeratedHrtf> list;

    bool usedefaults{true};
    if(auto pathopt = ConfigValueStr(devname, nullptr, "hrtf-paths"))
    {
        const char *pathlist{pathopt->c_str()};
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

        if(!GetResource(IDR_DEFAULT_44100_MHR).empty())
            AddBuiltInEntry(list, "Built-In 44100hz", IDR_DEFAULT_44100_MHR);

        if(!GetResource(IDR_DEFAULT_48000_MHR).empty())
            AddBuiltInEntry(list, "Built-In 48000hz", IDR_DEFAULT_48000_MHR);
    }

    if(!list.empty())
    {
        if(auto defhrtfopt = ConfigValueStr(devname, nullptr, "default-hrtf"))
        {
            auto iter = std::find_if(list.begin(), list.end(),
                [&defhrtfopt](const EnumeratedHrtf &entry) -> bool
                { return entry.name == *defhrtfopt; }
            );
            if(iter == list.end())
                WARN("Failed to find default HRTF \"%s\"\n", defhrtfopt->c_str());
            else if(iter != list.begin())
            {
                EnumeratedHrtf entry{std::move(*iter)};
                list.erase(iter);
                list.insert(list.begin(), std::move(entry));
            }
        }
    }

    return list;
}

HrtfEntry *GetLoadedHrtf(HrtfHandle *handle)
{
    std::lock_guard<std::mutex> _{LoadedHrtfLock};

    if(handle->entry)
    {
        HrtfEntry *hrtf{handle->entry.get()};
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
        if(res.empty())
        {
            ERR("Could not get resource %u, %s\n", residx, name);
            return nullptr;
        }
        stream = al::make_unique<idstream>(res.begin(), res.end());
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

    std::unique_ptr<HrtfEntry> hrtf;
    char magic[sizeof(magicMarker02)];
    stream->read(magic, sizeof(magic));
    if(stream->gcount() < static_cast<std::streamsize>(sizeof(magicMarker02)))
        ERR("%s data is too short (%zu bytes)\n", name, stream->gcount());
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
    {
        ERR("Failed to load %s\n", name);
        return nullptr;
    }

    TRACE("Loaded HRTF support for format: %s %uhz\n",
        DevFmtChannelsString(DevFmtStereo), hrtf->sampleRate);
    handle->entry = std::move(hrtf);

    return handle->entry.get();
}


void HrtfEntry::IncRef()
{
    auto ref = IncrementRef(&this->ref);
    TRACEREF("HrtfEntry %p increasing refcount to %u\n", this, ref);
}

void HrtfEntry::DecRef()
{
    auto ref = DecrementRef(&this->ref);
    TRACEREF("HrtfEntry %p decreasing refcount to %u\n", this, ref);
    if(ref == 0)
    {
        std::lock_guard<std::mutex> _{LoadedHrtfLock};

        /* Go through and clear all unused HRTFs. */
        auto delete_unused = [](HrtfHandlePtr &handle) -> void
        {
            HrtfEntry *entry{handle->entry.get()};
            if(entry && ReadRef(&entry->ref) == 0)
            {
                TRACE("Unloading unused HRTF %s\n", handle->filename.data());
                handle->entry = nullptr;
            }
        };
        std::for_each(LoadedHrtfs.begin(), LoadedHrtfs.end(), delete_unused);
    }
}
