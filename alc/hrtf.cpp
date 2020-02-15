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

#include "hrtf.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <type_traits>
#include <utility>

#include "AL/al.h"

#include "alcmain.h"
#include "alconfig.h"
#include "alfstream.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "aloptional.h"
#include "alspan.h"
#include "filters/splitter.h"
#include "logging.h"
#include "math_defs.h"
#include "opthelpers.h"
#include "polyphase_resampler.h"


namespace {

using namespace std::placeholders;

struct HrtfEntry {
    std::string mDispName;
    std::string mFilename;
};

struct LoadedHrtf {
    std::string mFilename;
    std::unique_ptr<HrtfStore> mEntry;
};

/* Data set limits must be the same as or more flexible than those defined in
 * the makemhr utility.
 */
#define MIN_FD_COUNT                 (1)
#define MAX_FD_COUNT                 (16)

#define MIN_FD_DISTANCE              (50)
#define MAX_FD_DISTANCE              (2500)

#define MIN_EV_COUNT                 (5)
#define MAX_EV_COUNT                 (181)

#define MIN_AZ_COUNT                 (1)
#define MAX_AZ_COUNT                 (255)

#define MAX_HRIR_DELAY               (HRTF_HISTORY_LENGTH-1)

#define HRIR_DELAY_FRACBITS 2
#define HRIR_DELAY_FRACONE (1<<HRIR_DELAY_FRACBITS)
#define HRIR_DELAY_FRACHALF (HRIR_DELAY_FRACONE>>1)

static_assert(MAX_HRIR_DELAY*HRIR_DELAY_FRACONE < 256, "MAX_HRIR_DELAY or DELAY_FRAC too large");

constexpr ALchar magicMarker00[8]{'M','i','n','P','H','R','0','0'};
constexpr ALchar magicMarker01[8]{'M','i','n','P','H','R','0','1'};
constexpr ALchar magicMarker02[8]{'M','i','n','P','H','R','0','2'};
constexpr ALchar magicMarker03[8]{'M','i','n','P','H','R','0','3'};

/* First value for pass-through coefficients (remaining are 0), used for omni-
 * directional sounds. */
constexpr ALfloat PassthruCoeff{0.707106781187f/*sqrt(0.5)*/};

std::mutex LoadedHrtfLock;
al::vector<LoadedHrtf> LoadedHrtfs;

std::mutex EnumeratedHrtfLock;
al::vector<HrtfEntry> EnumeratedHrtfs;


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
    databuf(const char_type *start_, const char_type *end_) noexcept
    {
        setg(const_cast<char_type*>(start_), const_cast<char_type*>(start_),
             const_cast<char_type*>(end_));
    }
};

class idstream final : public std::istream {
    databuf mStreamBuf;

public:
    idstream(const char *start_, const char *end_)
      : std::istream{nullptr}, mStreamBuf{start_, end_}
    { init(&mStreamBuf); }
};


struct IdxBlend { ALuint idx; float blend; };
/* Calculate the elevation index given the polar elevation in radians. This
 * will return an index between 0 and (evcount - 1).
 */
IdxBlend CalcEvIndex(ALuint evcount, float ev)
{
    ev = (al::MathDefs<float>::Pi()*0.5f + ev) * static_cast<float>(evcount-1) /
        al::MathDefs<float>::Pi();
    ALuint idx{float2uint(ev)};

    return IdxBlend{minu(idx, evcount-1), ev-static_cast<float>(idx)};
}

/* Calculate the azimuth index given the polar azimuth in radians. This will
 * return an index between 0 and (azcount - 1).
 */
IdxBlend CalcAzIndex(ALuint azcount, float az)
{
    az = (al::MathDefs<float>::Tau()+az) * static_cast<float>(azcount) /
        al::MathDefs<float>::Tau();
    ALuint idx{float2uint(az)};

    return IdxBlend{idx%azcount, az-static_cast<float>(idx)};
}

} // namespace


/* Calculates static HRIR coefficients and delays for the given polar elevation
 * and azimuth in radians. The coefficients are normalized.
 */
void GetHrtfCoeffs(const HrtfStore *Hrtf, float elevation, float azimuth, float distance,
    float spread, HrirArray &coeffs, const al::span<ALuint,2> delays)
{
    const float dirfact{1.0f - (spread / al::MathDefs<float>::Tau())};

    const auto *field = Hrtf->field;
    const auto *field_end = field + Hrtf->fdCount-1;
    size_t ebase{0};
    while(distance < field->distance && field != field_end)
    {
        ebase += field->evCount;
        ++field;
    }

    /* Calculate the elevation indices. */
    const auto elev0 = CalcEvIndex(field->evCount, elevation);
    const size_t elev1_idx{minu(elev0.idx+1, field->evCount-1)};
    const size_t ir0offset{Hrtf->elev[ebase + elev0.idx].irOffset};
    const size_t ir1offset{Hrtf->elev[ebase + elev1_idx].irOffset};

    /* Calculate azimuth indices. */
    const auto az0 = CalcAzIndex(Hrtf->elev[ebase + elev0.idx].azCount, azimuth);
    const auto az1 = CalcAzIndex(Hrtf->elev[ebase + elev1_idx].azCount, azimuth);

    /* Calculate the HRIR indices to blend. */
    const size_t idx[4]{
        ir0offset + az0.idx,
        ir0offset + ((az0.idx+1) % Hrtf->elev[ebase + elev0.idx].azCount),
        ir1offset + az1.idx,
        ir1offset + ((az1.idx+1) % Hrtf->elev[ebase + elev1_idx].azCount)
    };

    /* Calculate bilinear blending weights, attenuated according to the
     * directional panning factor.
     */
    const float blend[4]{
        (1.0f-elev0.blend) * (1.0f-az0.blend) * dirfact,
        (1.0f-elev0.blend) * (     az0.blend) * dirfact,
        (     elev0.blend) * (1.0f-az1.blend) * dirfact,
        (     elev0.blend) * (     az1.blend) * dirfact
    };

    /* Calculate the blended HRIR delays. */
    float d{Hrtf->delays[idx[0]][0]*blend[0] + Hrtf->delays[idx[1]][0]*blend[1] +
        Hrtf->delays[idx[2]][0]*blend[2] + Hrtf->delays[idx[3]][0]*blend[3]};
    delays[0] = fastf2u(d * float{1.0f/HRIR_DELAY_FRACONE});
    d = Hrtf->delays[idx[0]][1]*blend[0] + Hrtf->delays[idx[1]][1]*blend[1] +
        Hrtf->delays[idx[2]][1]*blend[2] + Hrtf->delays[idx[3]][1]*blend[3];
    delays[1] = fastf2u(d * float{1.0f/HRIR_DELAY_FRACONE});

    /* Calculate the blended HRIR coefficients. */
    float *coeffout{al::assume_aligned<16>(&coeffs[0][0])};
    coeffout[0] = PassthruCoeff * (1.0f-dirfact);
    coeffout[1] = PassthruCoeff * (1.0f-dirfact);
    std::fill_n(coeffout+2, size_t{HRIR_LENGTH-1}*2, 0.0f);
    for(size_t c{0};c < 4;c++)
    {
        const float *srccoeffs{al::assume_aligned<16>(Hrtf->coeffs[idx[c]][0].data())};
        const float mult{blend[c]};
        auto blend_coeffs = [mult](const float src, const float coeff) noexcept -> float
        { return src*mult + coeff; };
        std::transform(srccoeffs, srccoeffs + HRIR_LENGTH*2, coeffout, coeffout, blend_coeffs);
    }
}


std::unique_ptr<DirectHrtfState> DirectHrtfState::Create(size_t num_chans)
{
    return std::unique_ptr<DirectHrtfState>{new (FamCount{num_chans}) DirectHrtfState{num_chans}};
}

void BuildBFormatHrtf(const HrtfStore *Hrtf, DirectHrtfState *state,
    const al::span<const AngularPoint> AmbiPoints, const float (*AmbiMatrix)[MAX_AMBI_CHANNELS],
    const al::span<const float,MAX_AMBI_ORDER+1> AmbiOrderHFGain)
{
    using double2 = std::array<double,2>;
    struct ImpulseResponse {
        const HrirArray &hrir;
        ALuint ldelay, rdelay;
    };

    /* Set this to true for dual-band HRTF processing. May require better
     * calculation of the new IR length to deal with the head and tail
     * generated by the HF scaling.
     */
    static constexpr bool DualBand{true};

    ALuint min_delay{HRTF_HISTORY_LENGTH*HRIR_DELAY_FRACONE};
    ALuint max_delay{0};
    al::vector<ImpulseResponse> impres; impres.reserve(AmbiPoints.size());
    auto calc_res = [Hrtf,&max_delay,&min_delay](const AngularPoint &pt) -> ImpulseResponse
    {
        auto &field = Hrtf->field[0];
        const auto elev0 = CalcEvIndex(field.evCount, pt.Elev.value);
        const size_t elev1_idx{minu(elev0.idx+1, field.evCount-1)};
        const size_t ir0offset{Hrtf->elev[elev0.idx].irOffset};
        const size_t ir1offset{Hrtf->elev[elev1_idx].irOffset};

        const auto az0 = CalcAzIndex(Hrtf->elev[elev0.idx].azCount, pt.Azim.value);
        const auto az1 = CalcAzIndex(Hrtf->elev[elev1_idx].azCount, pt.Azim.value);

        const size_t idx[4]{
            ir0offset + az0.idx,
            ir0offset + ((az0.idx+1) % Hrtf->elev[elev0.idx].azCount),
            ir1offset + az1.idx,
            ir1offset + ((az1.idx+1) % Hrtf->elev[elev1_idx].azCount)
        };

        const std::array<double,4> blend{{
            (1.0-elev0.blend) * (1.0-az0.blend),
            (1.0-elev0.blend) * (    az0.blend),
            (    elev0.blend) * (1.0-az1.blend),
            (    elev0.blend) * (    az1.blend)
        }};

        /* The largest blend factor serves as the closest HRIR. */
        const size_t irOffset{idx[std::max_element(blend.begin(), blend.end()) - blend.begin()]};
        ImpulseResponse res{Hrtf->coeffs[irOffset],
            Hrtf->delays[irOffset][0], Hrtf->delays[irOffset][1]};

        min_delay = minu(min_delay, minu(res.ldelay, res.rdelay));
        max_delay = maxu(max_delay, maxu(res.ldelay, res.rdelay));

        return res;
    };
    std::transform(AmbiPoints.begin(), AmbiPoints.end(), std::back_inserter(impres), calc_res);
    auto hrir_delay_round = [](const ALuint d) noexcept -> ALuint
    { return (d+HRIR_DELAY_FRACHALF) >> HRIR_DELAY_FRACBITS; };

    /* For dual-band processing, add a 16-sample delay to compensate for the HF
     * scale on the minimum-phase response.
     */
    static constexpr ALuint base_delay{DualBand ? 16 : 0};
    const double xover_norm{400.0 / Hrtf->sampleRate};
    BandSplitterR<double> splitter{xover_norm};

    auto tmpres = al::vector<std::array<double2,HRIR_LENGTH>>(state->Coeffs.size());
    auto tmpflt = al::vector<std::array<double,HRIR_LENGTH*4>>(3);
    const al::span<double,HRIR_LENGTH*4> tempir{tmpflt[2].data(), tmpflt[2].size()};
    for(size_t c{0u};c < AmbiPoints.size();++c)
    {
        const HrirArray &hrir{impres[c].hrir};
        const ALuint ldelay{hrir_delay_round(impres[c].ldelay-min_delay) + base_delay};
        const ALuint rdelay{hrir_delay_round(impres[c].rdelay-min_delay) + base_delay};

        if /*constexpr*/(!DualBand)
        {
            /* For single-band decoding, apply the HF scale to the response. */
            for(size_t i{0u};i < state->Coeffs.size();++i)
            {
                const size_t order{AmbiIndex::OrderFromChannel[i]};
                const double mult{double{AmbiOrderHFGain[order]} * AmbiMatrix[c][i]};
                const ALuint numirs{HRIR_LENGTH - maxu(ldelay, rdelay)};
                ALuint lidx{ldelay}, ridx{rdelay};
                for(ALuint j{0};j < numirs;++j)
                {
                    tmpres[i][lidx++][0] += hrir[j][0] * mult;
                    tmpres[i][ridx++][1] += hrir[j][1] * mult;
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
        std::fill(tempir.begin(), tempir.end(), 0.0);
        std::transform(hrir.crbegin(), hrir.crend(), tempir.begin(),
            [](const float2 &ir) noexcept -> double { return ir[0]; });

        /* Apply the all-pass on the reversed signal and reverse the resulting
         * sample array. This produces the forward response with a backwards
         * phase-shift (+n degrees becomes -n degrees).
         */
        splitter.applyAllpass({tempir.data(), tempir.size()});
        std::reverse(tempir.begin(), tempir.end());

        /* Now apply the band-splitter. This applies the normal phase-shift,
         * which cancels out with the backwards phase-shift to get the original
         * phase on the split signal.
         */
        splitter.clear();
        splitter.process(tempir, tmpflt[0].data(), tmpflt[1].data());

        /* Apply left ear response with delay and HF scale. */
        for(size_t i{0u};i < state->Coeffs.size();++i)
        {
            const double mult{AmbiMatrix[c][i]};
            const double hfgain{AmbiOrderHFGain[AmbiIndex::OrderFromChannel[i]]};
            size_t j{tmpflt[0].size()-HRIR_LENGTH - ldelay};
            for(size_t lidx{0};lidx < HRIR_LENGTH;++lidx,++j)
                tmpres[i][lidx][0] += (tmpflt[0][j]*hfgain + tmpflt[1][j]) * mult;
        }

        /* Now run the same process on the right HRIR. */
        std::fill(tempir.begin(), tempir.end(), 0.0);
        std::transform(hrir.crbegin(), hrir.crend(), tempir.begin(),
            [](const float2 &ir) noexcept -> double { return ir[1]; });

        splitter.applyAllpass({tempir.data(), tempir.size()});
        std::reverse(tempir.begin(), tempir.end());

        splitter.clear();
        splitter.process(tempir, tmpflt[0].data(), tmpflt[1].data());

        for(size_t i{0u};i < state->Coeffs.size();++i)
        {
            const double mult{AmbiMatrix[c][i]};
            const double hfgain{AmbiOrderHFGain[AmbiIndex::OrderFromChannel[i]]};
            size_t j{tmpflt[0].size()-HRIR_LENGTH - rdelay};
            for(size_t ridx{0};ridx < HRIR_LENGTH;++ridx,++j)
                tmpres[i][ridx][1] += (tmpflt[0][j]*hfgain + tmpflt[1][j]) * mult;
        }
    }
    tmpflt.clear();
    impres.clear();

    for(size_t i{0u};i < state->Coeffs.size();++i)
    {
        auto copy_arr = [](const double2 &in) noexcept -> float2
        { return float2{{static_cast<float>(in[0]), static_cast<float>(in[1])}}; };
        std::transform(tmpres[i].cbegin(), tmpres[i].cend(), state->Coeffs[i].begin(),
            copy_arr);
    }
    tmpres.clear();

    max_delay -= min_delay;
    /* Increase the IR size by double the base delay with dual-band processing
     * to account for the head and tail from the HF response scale.
     */
    const ALuint irsize{minu(Hrtf->irSize + base_delay*2, HRIR_LENGTH)};
    const ALuint max_length{minu(hrir_delay_round(max_delay) + irsize, HRIR_LENGTH)};

    TRACE("Skipped delay: %.2f, max delay: %.2f, new FIR length: %u\n",
        min_delay/double{HRIR_DELAY_FRACONE}, max_delay/double{HRIR_DELAY_FRACONE},
        max_length);
    state->IrSize = max_length;
}


namespace {

std::unique_ptr<HrtfStore> CreateHrtfStore(ALuint rate, ALushort irSize,
    const al::span<const HrtfStore::Field> fields,
    const al::span<const HrtfStore::Elevation> elevs, const HrirArray *coeffs,
    const ubyte2 *delays, const char *filename)
{
    std::unique_ptr<HrtfStore> Hrtf;

    const size_t irCount{size_t{elevs.back().azCount} + elevs.back().irOffset};
    size_t total{sizeof(HrtfStore)};
    total  = RoundUp(total, alignof(HrtfStore::Field)); /* Align for field infos */
    total += sizeof(HrtfStore::Field)*fields.size();
    total  = RoundUp(total, alignof(HrtfStore::Elevation)); /* Align for elevation infos */
    total += sizeof(Hrtf->elev[0])*elevs.size();
    total  = RoundUp(total, 16); /* Align for coefficients using SIMD */
    total += sizeof(Hrtf->coeffs[0])*irCount;
    total += sizeof(Hrtf->delays[0])*irCount;

    Hrtf.reset(new (al_calloc(16, total)) HrtfStore{});
    if(!Hrtf)
        ERR("Out of memory allocating storage for %s.\n", filename);
    else
    {
        InitRef(Hrtf->mRef, 1u);
        Hrtf->sampleRate = rate;
        Hrtf->irSize = irSize;
        Hrtf->fdCount = static_cast<ALuint>(fields.size());

        /* Set up pointers to storage following the main HRTF struct. */
        char *base = reinterpret_cast<char*>(Hrtf.get());
        uintptr_t offset = sizeof(HrtfStore);

        offset = RoundUp(offset, alignof(HrtfStore::Field)); /* Align for field infos */
        auto field_ = reinterpret_cast<HrtfStore::Field*>(base + offset);
        offset += sizeof(field_[0])*fields.size();

        offset = RoundUp(offset, alignof(HrtfStore::Elevation)); /* Align for elevation infos */
        auto elev_ = reinterpret_cast<HrtfStore::Elevation*>(base + offset);
        offset += sizeof(elev_[0])*elevs.size();

        offset = RoundUp(offset, 16); /* Align for coefficients using SIMD */
        auto coeffs_ = reinterpret_cast<HrirArray*>(base + offset);
        offset += sizeof(coeffs_[0])*irCount;

        auto delays_ = reinterpret_cast<ubyte2*>(base + offset);
        offset += sizeof(delays_[0])*irCount;

        assert(offset == total);

        /* Copy input data to storage. */
        std::copy(fields.cbegin(), fields.cend(), field_);
        std::copy(elevs.cbegin(), elevs.cend(), elev_);
        std::copy_n(coeffs, irCount, coeffs_);
        std::copy_n(delays, irCount, delays_);

        /* Finally, assign the storage pointers. */
        Hrtf->field = field_;
        Hrtf->elev = elev_;
        Hrtf->coeffs = coeffs_;
        Hrtf->delays = delays_;
    }

    return Hrtf;
}

void MirrorLeftHrirs(const al::span<const HrtfStore::Elevation> elevs, HrirArray *coeffs,
    ubyte2 *delays)
{
    for(const auto &elev : elevs)
    {
        const ALushort evoffset{elev.irOffset};
        const ALushort azcount{elev.azCount};
        for(size_t j{0};j < azcount;j++)
        {
            const size_t lidx{evoffset + j};
            const size_t ridx{evoffset + ((azcount-j) % azcount)};

            const size_t irSize{coeffs[ridx].size()};
            for(size_t k{0};k < irSize;k++)
                coeffs[ridx][k][1] = coeffs[lidx][k][0];
            delays[ridx][1] = delays[lidx][0];
        }
    }
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
    return static_cast<ALuint>(ret);
}

std::unique_ptr<HrtfStore> LoadHrtf00(std::istream &data, const char *filename)
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

    if(irSize < MIN_IR_LENGTH || irSize > HRIR_LENGTH)
    {
        ERR("Unsupported HRIR size, irSize=%d (%d to %d)\n", irSize, MIN_IR_LENGTH, HRIR_LENGTH);
        return nullptr;
    }
    if(evCount < MIN_EV_COUNT || evCount > MAX_EV_COUNT)
    {
        ERR("Unsupported elevation count: evCount=%d (%d to %d)\n",
            evCount, MIN_EV_COUNT, MAX_EV_COUNT);
        return nullptr;
    }

    auto elevs = al::vector<HrtfStore::Elevation>(evCount);
    for(auto &elev : elevs)
        elev.irOffset = GetLE_ALushort(data);
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }
    for(size_t i{1};i < evCount;i++)
    {
        if(elevs[i].irOffset <= elevs[i-1].irOffset)
        {
            ERR("Invalid evOffset: evOffset[%zu]=%d (last=%d)\n", i, elevs[i].irOffset,
                elevs[i-1].irOffset);
            return nullptr;
        }
    }
    if(irCount <= elevs.back().irOffset)
    {
        ERR("Invalid evOffset: evOffset[%zu]=%d (irCount=%d)\n",
            elevs.size()-1, elevs.back().irOffset, irCount);
        return nullptr;
    }

    for(size_t i{1};i < evCount;i++)
    {
        elevs[i-1].azCount = static_cast<ALushort>(elevs[i].irOffset - elevs[i-1].irOffset);
        if(elevs[i-1].azCount < MIN_AZ_COUNT || elevs[i-1].azCount > MAX_AZ_COUNT)
        {
            ERR("Unsupported azimuth count: azCount[%zd]=%d (%d to %d)\n",
                i-1, elevs[i-1].azCount, MIN_AZ_COUNT, MAX_AZ_COUNT);
            return nullptr;
        }
    }
    elevs.back().azCount = static_cast<ALushort>(irCount - elevs.back().irOffset);
    if(elevs.back().azCount < MIN_AZ_COUNT || elevs.back().azCount > MAX_AZ_COUNT)
    {
        ERR("Unsupported azimuth count: azCount[%zu]=%d (%d to %d)\n",
            elevs.size()-1, elevs.back().azCount, MIN_AZ_COUNT, MAX_AZ_COUNT);
        return nullptr;
    }

    auto coeffs = al::vector<HrirArray>(irCount, HrirArray{});
    auto delays = al::vector<ubyte2>(irCount);
    for(auto &hrir : coeffs)
    {
        for(auto &val : al::span<float2>{hrir.data(), irSize})
            val[0] = GetLE_ALshort(data) / 32768.0f;
    }
    for(auto &val : delays)
        val[0] = GetLE_ALubyte(data);
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }
    for(size_t i{0};i < irCount;i++)
    {
        if(delays[i][0] > MAX_HRIR_DELAY)
        {
            ERR("Invalid delays[%zd]: %d (%d)\n", i, delays[i][0], MAX_HRIR_DELAY);
            return nullptr;
        }
        delays[i][0] <<= HRIR_DELAY_FRACBITS;
    }

    /* Mirror the left ear responses to the right ear. */
    MirrorLeftHrirs({elevs.data(), elevs.size()}, coeffs.data(), delays.data());

    const HrtfStore::Field field[1]{{0.0f, evCount}};
    return CreateHrtfStore(rate, irSize, field, {elevs.data(), elevs.size()}, coeffs.data(),
        delays.data(), filename);
}

std::unique_ptr<HrtfStore> LoadHrtf01(std::istream &data, const char *filename)
{
    ALuint rate{GetLE_ALuint(data)};
    ALushort irSize{GetLE_ALubyte(data)};
    ALubyte evCount{GetLE_ALubyte(data)};
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }

    if(irSize < MIN_IR_LENGTH || irSize > HRIR_LENGTH)
    {
        ERR("Unsupported HRIR size, irSize=%d (%d to %d)\n", irSize, MIN_IR_LENGTH, HRIR_LENGTH);
        return nullptr;
    }
    if(evCount < MIN_EV_COUNT || evCount > MAX_EV_COUNT)
    {
        ERR("Unsupported elevation count: evCount=%d (%d to %d)\n",
            evCount, MIN_EV_COUNT, MAX_EV_COUNT);
        return nullptr;
    }

    auto elevs = al::vector<HrtfStore::Elevation>(evCount);
    for (auto &elev : elevs) elev.azCount = GetLE_ALubyte(data);
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }
    for(size_t i{0};i < evCount;++i)
    {
        if(elevs[i].azCount < MIN_AZ_COUNT || elevs[i].azCount > MAX_AZ_COUNT)
        {
            ERR("Unsupported azimuth count: azCount[%zd]=%d (%d to %d)\n", i, elevs[i].azCount,
                MIN_AZ_COUNT, MAX_AZ_COUNT);
            return nullptr;
        }
    }

    elevs[0].irOffset = 0;
    for(size_t i{1};i < evCount;i++)
        elevs[i].irOffset = static_cast<ALushort>(elevs[i-1].irOffset + elevs[i-1].azCount);
    const ALushort irCount{static_cast<ALushort>(elevs.back().irOffset + elevs.back().azCount)};

    auto coeffs = al::vector<HrirArray>(irCount, HrirArray{});
    auto delays = al::vector<ubyte2>(irCount);
    for(auto &hrir : coeffs)
    {
        for(auto &val : al::span<float2>{hrir.data(), irSize})
            val[0] = GetLE_ALshort(data) / 32768.0f;
    }
    for(auto &val : delays)
        val[0] = GetLE_ALubyte(data);
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }
    for(size_t i{0};i < irCount;i++)
    {
        if(delays[i][0] > MAX_HRIR_DELAY)
        {
            ERR("Invalid delays[%zd]: %d (%d)\n", i, delays[i][0], MAX_HRIR_DELAY);
            return nullptr;
        }
        delays[i][0] <<= HRIR_DELAY_FRACBITS;
    }

    /* Mirror the left ear responses to the right ear. */
    MirrorLeftHrirs({elevs.data(), elevs.size()}, coeffs.data(), delays.data());

    const HrtfStore::Field field[1]{{0.0f, evCount}};
    return CreateHrtfStore(rate, irSize, field, {elevs.data(), elevs.size()}, coeffs.data(),
        delays.data(), filename);
}

std::unique_ptr<HrtfStore> LoadHrtf02(std::istream &data, const char *filename)
{
    constexpr ALubyte SampleType_S16{0};
    constexpr ALubyte SampleType_S24{1};
    constexpr ALubyte ChanType_LeftOnly{0};
    constexpr ALubyte ChanType_LeftRight{1};

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

    if(sampleType > SampleType_S24)
    {
        ERR("Unsupported sample type: %d\n", sampleType);
        return nullptr;
    }
    if(channelType > ChanType_LeftRight)
    {
        ERR("Unsupported channel type: %d\n", channelType);
        return nullptr;
    }

    if(irSize < MIN_IR_LENGTH || irSize > HRIR_LENGTH)
    {
        ERR("Unsupported HRIR size, irSize=%d (%d to %d)\n", irSize, MIN_IR_LENGTH, HRIR_LENGTH);
        return nullptr;
    }
    if(fdCount < 1 || fdCount > MAX_FD_COUNT)
    {
        ERR("Unsupported number of field-depths: fdCount=%d (%d to %d)\n", fdCount, MIN_FD_COUNT,
            MAX_FD_COUNT);
        return nullptr;
    }

    auto fields = al::vector<HrtfStore::Field>(fdCount);
    auto elevs = al::vector<HrtfStore::Elevation>{};
    for(size_t f{0};f < fdCount;f++)
    {
        const ALushort distance{GetLE_ALushort(data)};
        const ALubyte evCount{GetLE_ALubyte(data)};
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }

        if(distance < MIN_FD_DISTANCE || distance > MAX_FD_DISTANCE)
        {
            ERR("Unsupported field distance[%zu]=%d (%d to %d millimeters)\n", f, distance,
                MIN_FD_DISTANCE, MAX_FD_DISTANCE);
            return nullptr;
        }
        if(evCount < MIN_EV_COUNT || evCount > MAX_EV_COUNT)
        {
            ERR("Unsupported elevation count: evCount[%zu]=%d (%d to %d)\n", f, evCount,
                MIN_EV_COUNT, MAX_EV_COUNT);
            return nullptr;
        }

        fields[f].distance = distance / 1000.0f;
        fields[f].evCount = evCount;
        if(f > 0 && fields[f].distance <= fields[f-1].distance)
        {
            ERR("Field distance[%zu] is not after previous (%f > %f)\n", f, fields[f].distance,
                fields[f-1].distance);
            return nullptr;
        }

        const size_t ebase{elevs.size()};
        elevs.resize(ebase + evCount);
        for(auto &elev : al::span<HrtfStore::Elevation>(elevs.data()+ebase, evCount))
            elev.azCount = GetLE_ALubyte(data);
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }

        for(size_t e{0};e < evCount;e++)
        {
            if(elevs[ebase+e].azCount < MIN_AZ_COUNT || elevs[ebase+e].azCount > MAX_AZ_COUNT)
            {
                ERR("Unsupported azimuth count: azCount[%zu][%zu]=%d (%d to %d)\n", f, e,
                    elevs[ebase+e].azCount, MIN_AZ_COUNT, MAX_AZ_COUNT);
                return nullptr;
            }
        }
    }

    elevs[0].irOffset = 0;
    std::partial_sum(elevs.cbegin(), elevs.cend(), elevs.begin(),
        [](const HrtfStore::Elevation &last, const HrtfStore::Elevation &cur)
            -> HrtfStore::Elevation
        {
            return HrtfStore::Elevation{cur.azCount,
                static_cast<ALushort>(last.azCount + last.irOffset)};
        });
    const auto irTotal = static_cast<ALushort>(elevs.back().azCount + elevs.back().irOffset);

    auto coeffs = al::vector<HrirArray>(irTotal, HrirArray{});
    auto delays = al::vector<ubyte2>(irTotal);
    if(channelType == ChanType_LeftOnly)
    {
        if(sampleType == SampleType_S16)
        {
            for(auto &hrir : coeffs)
            {
                for(auto &val : al::span<float2>{hrir.data(), irSize})
                    val[0] = GetLE_ALshort(data) / 32768.0f;
            }
        }
        else if(sampleType == SampleType_S24)
        {
            for(auto &hrir : coeffs)
            {
                for(auto &val : al::span<float2>{hrir.data(), irSize})
                    val[0] = static_cast<float>(GetLE_ALint24(data)) / 8388608.0f;
            }
        }
        for(auto &val : delays)
            val[0] = GetLE_ALubyte(data);
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }
        for(size_t i{0};i < irTotal;++i)
        {
            if(delays[i][0] > MAX_HRIR_DELAY)
            {
                ERR("Invalid delays[%zu][0]: %d (%d)\n", i, delays[i][0], MAX_HRIR_DELAY);
                return nullptr;
            }
            delays[i][0] <<= HRIR_DELAY_FRACBITS;
        }

        /* Mirror the left ear responses to the right ear. */
        MirrorLeftHrirs({elevs.data(), elevs.size()}, coeffs.data(), delays.data());
    }
    else if(channelType == ChanType_LeftRight)
    {
        if(sampleType == SampleType_S16)
        {
            for(auto &hrir : coeffs)
            {
                for(auto &val : al::span<float2>{hrir.data(), irSize})
                {
                    val[0] = GetLE_ALshort(data) / 32768.0f;
                    val[1] = GetLE_ALshort(data) / 32768.0f;
                }
            }
        }
        else if(sampleType == SampleType_S24)
        {
            for(auto &hrir : coeffs)
            {
                for(auto &val : al::span<float2>{hrir.data(), irSize})
                {
                    val[0] = static_cast<float>(GetLE_ALint24(data)) / 8388608.0f;
                    val[1] = static_cast<float>(GetLE_ALint24(data)) / 8388608.0f;
                }
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

        for(size_t i{0};i < irTotal;++i)
        {
            if(delays[i][0] > MAX_HRIR_DELAY)
            {
                ERR("Invalid delays[%zu][0]: %d (%d)\n", i, delays[i][0], MAX_HRIR_DELAY);
                return nullptr;
            }
            if(delays[i][1] > MAX_HRIR_DELAY)
            {
                ERR("Invalid delays[%zu][1]: %d (%d)\n", i, delays[i][1], MAX_HRIR_DELAY);
                return nullptr;
            }
            delays[i][0] <<= HRIR_DELAY_FRACBITS;
            delays[i][1] <<= HRIR_DELAY_FRACBITS;
        }
    }

    if(fdCount > 1)
    {
        auto fields_ = al::vector<HrtfStore::Field>(fields.size());
        auto elevs_ = al::vector<HrtfStore::Elevation>(elevs.size());
        auto coeffs_ = al::vector<HrirArray>(coeffs.size());
        auto delays_ = al::vector<ubyte2>(delays.size());

        /* Simple reverse for the per-field elements. */
        std::reverse_copy(fields.cbegin(), fields.cend(), fields_.begin());

        /* Each field has a group of elevations, which each have an azimuth
         * count. Reverse the order of the groups, keeping the relative order
         * of per-group azimuth counts.
         */
        auto elevs__end = elevs_.end();
        auto copy_azs = [&elevs,&elevs__end](const ptrdiff_t ebase, const HrtfStore::Field &field)
            -> ptrdiff_t
        {
            auto elevs_src = elevs.begin()+ebase;
            elevs__end = std::copy_backward(elevs_src, elevs_src+field.evCount, elevs__end);
            return ebase + field.evCount;
        };
        std::accumulate(fields.cbegin(), fields.cend(), ptrdiff_t{0}, copy_azs);
        assert(elevs_.begin() == elevs__end);

        /* Reestablish the IR offset for each elevation index, given the new
         * ordering of elevations.
         */
        elevs_[0].irOffset = 0;
        std::partial_sum(elevs_.cbegin(), elevs_.cend(), elevs_.begin(),
            [](const HrtfStore::Elevation &last, const HrtfStore::Elevation &cur)
                -> HrtfStore::Elevation
            {
                return HrtfStore::Elevation{cur.azCount,
                    static_cast<ALushort>(last.azCount + last.irOffset)};
            });

        /* Reverse the order of each field's group of IRs. */
        auto coeffs_end = coeffs_.end();
        auto delays_end = delays_.end();
        auto copy_irs = [&elevs,&coeffs,&delays,&coeffs_end,&delays_end](
            const ptrdiff_t ebase, const HrtfStore::Field &field) -> ptrdiff_t
        {
            auto accum_az = [](ALsizei count, const HrtfStore::Elevation &elev) noexcept -> ALsizei
            { return count + elev.azCount; };
            const auto elevs_mid = elevs.cbegin() + ebase;
            const auto elevs_end = elevs_mid + field.evCount;
            const ALsizei abase{std::accumulate(elevs.cbegin(), elevs_mid, 0, accum_az)};
            const ALsizei num_azs{std::accumulate(elevs_mid, elevs_end, 0, accum_az)};

            coeffs_end = std::copy_backward(coeffs.cbegin() + abase,
                coeffs.cbegin() + (abase+num_azs), coeffs_end);
            delays_end = std::copy_backward(delays.cbegin() + abase,
                delays.cbegin() + (abase+num_azs), delays_end);

            return ebase + field.evCount;
        };
        std::accumulate(fields.cbegin(), fields.cend(), ptrdiff_t{0}, copy_irs);
        assert(coeffs_.begin() == coeffs_end);
        assert(delays_.begin() == delays_end);

        fields = std::move(fields_);
        elevs = std::move(elevs_);
        coeffs = std::move(coeffs_);
        delays = std::move(delays_);
    }

    return CreateHrtfStore(rate, irSize, {fields.data(), fields.size()},
        {elevs.data(), elevs.size()}, coeffs.data(), delays.data(), filename);
}

std::unique_ptr<HrtfStore> LoadHrtf03(std::istream &data, const char *filename)
{
    constexpr ALubyte ChanType_LeftOnly{0};
    constexpr ALubyte ChanType_LeftRight{1};

    ALuint rate{GetLE_ALuint(data)};
    ALubyte channelType{GetLE_ALubyte(data)};
    ALushort irSize{GetLE_ALubyte(data)};
    ALubyte fdCount{GetLE_ALubyte(data)};
    if(!data || data.eof())
    {
        ERR("Failed reading %s\n", filename);
        return nullptr;
    }

    if(channelType > ChanType_LeftRight)
    {
        ERR("Unsupported channel type: %d\n", channelType);
        return nullptr;
    }

    if(irSize < MIN_IR_LENGTH || irSize > HRIR_LENGTH)
    {
        ERR("Unsupported HRIR size, irSize=%d (%d to %d)\n", irSize, MIN_IR_LENGTH, HRIR_LENGTH);
        return nullptr;
    }
    if(fdCount < 1 || fdCount > MAX_FD_COUNT)
    {
        ERR("Unsupported number of field-depths: fdCount=%d (%d to %d)\n", fdCount, MIN_FD_COUNT,
            MAX_FD_COUNT);
        return nullptr;
    }

    auto fields = al::vector<HrtfStore::Field>(fdCount);
    auto elevs = al::vector<HrtfStore::Elevation>{};
    for(size_t f{0};f < fdCount;f++)
    {
        const ALushort distance{GetLE_ALushort(data)};
        const ALubyte evCount{GetLE_ALubyte(data)};
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }

        if(distance < MIN_FD_DISTANCE || distance > MAX_FD_DISTANCE)
        {
            ERR("Unsupported field distance[%zu]=%d (%d to %d millimeters)\n", f, distance,
                MIN_FD_DISTANCE, MAX_FD_DISTANCE);
            return nullptr;
        }
        if(evCount < MIN_EV_COUNT || evCount > MAX_EV_COUNT)
        {
            ERR("Unsupported elevation count: evCount[%zu]=%d (%d to %d)\n", f, evCount,
                MIN_EV_COUNT, MAX_EV_COUNT);
            return nullptr;
        }

        fields[f].distance = distance / 1000.0f;
        fields[f].evCount = evCount;
        if(f > 0 && fields[f].distance > fields[f-1].distance)
        {
            ERR("Field distance[%zu] is not before previous (%f <= %f)\n", f, fields[f].distance,
                fields[f-1].distance);
            return nullptr;
        }

        const size_t ebase{elevs.size()};
        elevs.resize(ebase + evCount);
        for(auto &elev : al::span<HrtfStore::Elevation>(elevs.data()+ebase, evCount))
            elev.azCount = GetLE_ALubyte(data);
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }

        for(size_t e{0};e < evCount;e++)
        {
            if(elevs[ebase+e].azCount < MIN_AZ_COUNT || elevs[ebase+e].azCount > MAX_AZ_COUNT)
            {
                ERR("Unsupported azimuth count: azCount[%zu][%zu]=%d (%d to %d)\n", f, e,
                    elevs[ebase+e].azCount, MIN_AZ_COUNT, MAX_AZ_COUNT);
                return nullptr;
            }
        }
    }

    elevs[0].irOffset = 0;
    std::partial_sum(elevs.cbegin(), elevs.cend(), elevs.begin(),
        [](const HrtfStore::Elevation &last, const HrtfStore::Elevation &cur)
            -> HrtfStore::Elevation
        {
            return HrtfStore::Elevation{cur.azCount,
                static_cast<ALushort>(last.azCount + last.irOffset)};
        });
    const auto irTotal = static_cast<ALushort>(elevs.back().azCount + elevs.back().irOffset);

    auto coeffs = al::vector<HrirArray>(irTotal, HrirArray{});
    auto delays = al::vector<ubyte2>(irTotal);
    if(channelType == ChanType_LeftOnly)
    {
        for(auto &hrir : coeffs)
        {
            for(auto &val : al::span<float2>{hrir.data(), irSize})
                val[0] = static_cast<float>(GetLE_ALint24(data)) / 8388608.0f;
        }
        for(auto &val : delays)
            val[0] = GetLE_ALubyte(data);
        if(!data || data.eof())
        {
            ERR("Failed reading %s\n", filename);
            return nullptr;
        }
        for(size_t i{0};i < irTotal;++i)
        {
            if(delays[i][0] > MAX_HRIR_DELAY<<HRIR_DELAY_FRACBITS)
            {
                ERR("Invalid delays[%zu][0]: %f (%d)\n", i,
                    delays[i][0] / float{HRIR_DELAY_FRACONE}, MAX_HRIR_DELAY);
                return nullptr;
            }
        }

        /* Mirror the left ear responses to the right ear. */
        MirrorLeftHrirs({elevs.data(), elevs.size()}, coeffs.data(), delays.data());
    }
    else if(channelType == ChanType_LeftRight)
    {
        for(auto &hrir : coeffs)
        {
            for(auto &val : al::span<float2>{hrir.data(), irSize})
            {
                val[0] = static_cast<float>(GetLE_ALint24(data)) / 8388608.0f;
                val[1] = static_cast<float>(GetLE_ALint24(data)) / 8388608.0f;
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

        for(size_t i{0};i < irTotal;++i)
        {
            if(delays[i][0] > MAX_HRIR_DELAY<<HRIR_DELAY_FRACBITS)
            {
                ERR("Invalid delays[%zu][0]: %f (%d)\n", i,
                    delays[i][0] / float{HRIR_DELAY_FRACONE}, MAX_HRIR_DELAY);
                return nullptr;
            }
            if(delays[i][1] > MAX_HRIR_DELAY<<HRIR_DELAY_FRACBITS)
            {
                ERR("Invalid delays[%zu][1]: %f (%d)\n", i,
                    delays[i][1] / float{HRIR_DELAY_FRACONE}, MAX_HRIR_DELAY);
                return nullptr;
            }
        }
    }

    return CreateHrtfStore(rate, irSize, {fields.data(), fields.size()},
        {elevs.data(), elevs.size()}, coeffs.data(), delays.data(), filename);
}


bool checkName(const std::string &name)
{
    auto match_name = [&name](const HrtfEntry &entry) -> bool { return name == entry.mDispName; };
    auto &enum_names = EnumeratedHrtfs;
    return std::find_if(enum_names.cbegin(), enum_names.cend(), match_name) != enum_names.cend();
}

void AddFileEntry(const std::string &filename)
{
    /* Check if this file has already been enumerated. */
    auto enum_iter = std::find_if(EnumeratedHrtfs.cbegin(), EnumeratedHrtfs.cend(),
        [&filename](const HrtfEntry &entry) -> bool
        { return entry.mFilename == filename; });
    if(enum_iter != EnumeratedHrtfs.cend())
    {
        TRACE("Skipping duplicate file entry %s\n", filename.c_str());
        return;
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
    while(checkName(newname))
    {
        newname = basename;
        newname += " #";
        newname += std::to_string(++count);
    }
    EnumeratedHrtfs.emplace_back(HrtfEntry{newname, filename});
    const HrtfEntry &entry = EnumeratedHrtfs.back();

    TRACE("Adding file entry \"%s\"\n", entry.mFilename.c_str());
}

/* Unfortunate that we have to duplicate AddFileEntry to take a memory buffer
 * for input instead of opening the given filename.
 */
void AddBuiltInEntry(const std::string &dispname, ALuint residx)
{
    const std::string filename{'!'+std::to_string(residx)+'_'+dispname};

    auto enum_iter = std::find_if(EnumeratedHrtfs.cbegin(), EnumeratedHrtfs.cend(),
        [&filename](const HrtfEntry &entry) -> bool
        { return entry.mFilename == filename; });
    if(enum_iter != EnumeratedHrtfs.cend())
    {
        TRACE("Skipping duplicate file entry %s\n", filename.c_str());
        return;
    }

    /* TODO: Get a human-readable name from the HRTF data (possibly coming in a
     * format update). */

    std::string newname{dispname};
    int count{1};
    while(checkName(newname))
    {
        newname = dispname;
        newname += " #";
        newname += std::to_string(++count);
    }
    EnumeratedHrtfs.emplace_back(HrtfEntry{newname, filename});
    const HrtfEntry &entry = EnumeratedHrtfs.back();

    TRACE("Adding built-in entry \"%s\"\n", entry.mFilename.c_str());
}


#define IDR_DEFAULT_HRTF_MHR 1

#ifndef ALSOFT_EMBED_HRTF_DATA

al::span<const char> GetResource(int /*name*/)
{ return {}; }

#else

#include "hrtf_default.h"

al::span<const char> GetResource(int name)
{
    if(name == IDR_DEFAULT_HRTF_MHR)
        return {reinterpret_cast<const char*>(hrtf_default), sizeof(hrtf_default)};
    return {};
}
#endif

} // namespace


al::vector<std::string> EnumerateHrtf(const char *devname)
{
    std::lock_guard<std::mutex> _{EnumeratedHrtfLock};
    EnumeratedHrtfs.clear();

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
                    AddFileEntry(fname);
            }

            pathlist = next;
        }
    }

    if(usedefaults)
    {
        for(const auto &fname : SearchDataFiles(".mhr", "openal/hrtf"))
            AddFileEntry(fname);

        if(!GetResource(IDR_DEFAULT_HRTF_MHR).empty())
            AddBuiltInEntry("Built-In HRTF", IDR_DEFAULT_HRTF_MHR);
    }

    al::vector<std::string> list;
    list.reserve(EnumeratedHrtfs.size());
    for(auto &entry : EnumeratedHrtfs)
        list.emplace_back(entry.mDispName);

    if(auto defhrtfopt = ConfigValueStr(devname, nullptr, "default-hrtf"))
    {
        auto iter = std::find(list.begin(), list.end(), *defhrtfopt);
        if(iter == list.end())
            WARN("Failed to find default HRTF \"%s\"\n", defhrtfopt->c_str());
        else if(iter != list.begin())
            std::rotate(list.begin(), iter, iter+1);
    }

    return list;
}

HrtfStore *GetLoadedHrtf(const std::string &name, const char *devname, const ALuint devrate)
{
    std::lock_guard<std::mutex> _{EnumeratedHrtfLock};
    auto entry_iter = std::find_if(EnumeratedHrtfs.cbegin(), EnumeratedHrtfs.cend(),
        [&name](const HrtfEntry &entry) -> bool { return entry.mDispName == name; }
    );
    if(entry_iter == EnumeratedHrtfs.cend())
        return nullptr;
    const std::string &fname = entry_iter->mFilename;

    std::lock_guard<std::mutex> __{LoadedHrtfLock};
    auto hrtf_lt_fname = [](LoadedHrtf &hrtf, const std::string &filename) -> bool
    { return hrtf.mFilename < filename; };
    auto handle = std::lower_bound(LoadedHrtfs.begin(), LoadedHrtfs.end(), fname, hrtf_lt_fname);
    while(handle != LoadedHrtfs.end() && handle->mFilename == fname)
    {
        HrtfStore *hrtf{handle->mEntry.get()};
        if(hrtf && hrtf->sampleRate == devrate)
        {
            hrtf->IncRef();
            return hrtf;
        }
        ++handle;
    }

    std::unique_ptr<std::istream> stream;
    ALint residx{};
    char ch{};
    if(sscanf(fname.c_str(), "!%d%c", &residx, &ch) == 2 && ch == '_')
    {
        TRACE("Loading %s...\n", fname.c_str());
        al::span<const char> res{GetResource(residx)};
        if(res.empty())
        {
            ERR("Could not get resource %u, %s\n", residx, name.c_str());
            return nullptr;
        }
        stream = al::make_unique<idstream>(res.begin(), res.end());
    }
    else
    {
        TRACE("Loading %s...\n", fname.c_str());
        auto fstr = al::make_unique<al::ifstream>(fname.c_str(), std::ios::binary);
        if(!fstr->is_open())
        {
            ERR("Could not open %s\n", fname.c_str());
            return nullptr;
        }
        stream = std::move(fstr);
    }

    std::unique_ptr<HrtfStore> hrtf;
    char magic[sizeof(magicMarker03)];
    stream->read(magic, sizeof(magic));
    if(stream->gcount() < static_cast<std::streamsize>(sizeof(magicMarker03)))
        ERR("%s data is too short (%zu bytes)\n", name.c_str(), stream->gcount());
    else if(memcmp(magic, magicMarker03, sizeof(magicMarker03)) == 0)
    {
        TRACE("Detected data set format v3\n");
        hrtf = LoadHrtf03(*stream, name.c_str());
    }
    else if(memcmp(magic, magicMarker02, sizeof(magicMarker02)) == 0)
    {
        TRACE("Detected data set format v2\n");
        hrtf = LoadHrtf02(*stream, name.c_str());
    }
    else if(memcmp(magic, magicMarker01, sizeof(magicMarker01)) == 0)
    {
        TRACE("Detected data set format v1\n");
        hrtf = LoadHrtf01(*stream, name.c_str());
    }
    else if(memcmp(magic, magicMarker00, sizeof(magicMarker00)) == 0)
    {
        TRACE("Detected data set format v0\n");
        hrtf = LoadHrtf00(*stream, name.c_str());
    }
    else
        ERR("Invalid header in %s: \"%.8s\"\n", name.c_str(), magic);
    stream.reset();

    if(!hrtf)
    {
        ERR("Failed to load %s\n", name.c_str());
        return nullptr;
    }

    if(hrtf->sampleRate != devrate)
    {
        /* Calculate the last elevation's index and get the total IR count. */
        const size_t lastEv{std::accumulate(hrtf->field, hrtf->field+hrtf->fdCount, size_t{0},
            [](const size_t curval, const HrtfStore::Field &field) noexcept -> size_t
            { return curval + field.evCount; }
        ) - 1};
        const size_t irCount{size_t{hrtf->elev[lastEv].irOffset} + hrtf->elev[lastEv].azCount};

        /* Resample all the IRs. */
        std::array<std::array<double,HRIR_LENGTH>,2> inout;
        PPhaseResampler rs;
        rs.init(hrtf->sampleRate, devrate);
        for(size_t i{0};i < irCount;++i)
        {
            HrirArray &coeffs = const_cast<HrirArray&>(hrtf->coeffs[i]);
            for(size_t j{0};j < 2;++j)
            {
                std::transform(coeffs.cbegin(), coeffs.cend(), inout[0].begin(),
                    [j](const float2 &in) noexcept -> double { return in[j]; });
                rs.process(HRIR_LENGTH, inout[0].data(), HRIR_LENGTH, inout[1].data());
                for(size_t k{0};k < HRIR_LENGTH;++k)
                    coeffs[k][j] = static_cast<float>(inout[1][k]);
            }
        }
        rs = {};

        /* Scale the delays for the new sample rate. */
        float max_delay{0.0f};
        auto new_delays = al::vector<float2>(irCount);
        const float rate_scale{static_cast<float>(devrate)/static_cast<float>(hrtf->sampleRate)};
        for(size_t i{0};i < irCount;++i)
        {
            for(size_t j{0};j < 2;++j)
            {
                const float new_delay{std::round(hrtf->delays[i][j] * rate_scale) /
                    float{HRIR_DELAY_FRACONE}};
                max_delay = maxf(max_delay, new_delay);
                new_delays[i][j] = new_delay;
            }
        }

        /* If the new delays exceed the max, scale it down to fit (essentially
         * shrinking the head radius; not ideal but better than a per-delay
         * clamp).
         */
        float delay_scale{HRIR_DELAY_FRACONE};
        if(max_delay > MAX_HRIR_DELAY)
        {
            WARN("Resampled delay exceeds max (%.2f > %d)\n", max_delay, MAX_HRIR_DELAY);
            delay_scale *= float{MAX_HRIR_DELAY} / max_delay;
        }

        for(size_t i{0};i < irCount;++i)
        {
            ubyte2 &delays = const_cast<ubyte2&>(hrtf->delays[i]);
            for(size_t j{0};j < 2;++j)
                delays[j] = static_cast<ALubyte>(float2int(new_delays[i][j] * delay_scale));
        }

        /* Scale the IR size for the new sample rate and update the stored
         * sample rate.
         */
        const float newIrSize{std::round(static_cast<float>(hrtf->irSize) * rate_scale)};
        hrtf->irSize = static_cast<ALuint>(minf(HRIR_LENGTH, newIrSize));
        hrtf->sampleRate = devrate;
    }

    if(auto hrtfsizeopt = ConfigValueUInt(devname, nullptr, "hrtf-size"))
    {
        if(*hrtfsizeopt > 0 && *hrtfsizeopt < hrtf->irSize)
            hrtf->irSize = maxu(*hrtfsizeopt, MIN_IR_LENGTH);
    }

    TRACE("Loaded HRTF %s for sample rate %uhz, %u-sample filter\n", name.c_str(),
        hrtf->sampleRate, hrtf->irSize);
    handle = LoadedHrtfs.emplace(handle, LoadedHrtf{fname, std::move(hrtf)});

    return handle->mEntry.get();
}


void HrtfStore::IncRef()
{
    auto ref = IncrementRef(mRef);
    TRACE("HrtfEntry %p increasing refcount to %u\n", decltype(std::declval<void*>()){this}, ref);
}

void HrtfStore::DecRef()
{
    auto ref = DecrementRef(mRef);
    TRACE("HrtfEntry %p decreasing refcount to %u\n", decltype(std::declval<void*>()){this}, ref);
    if(ref == 0)
    {
        std::lock_guard<std::mutex> _{LoadedHrtfLock};

        /* Go through and remove all unused HRTFs. */
        auto remove_unused = [](LoadedHrtf &hrtf) -> bool
        {
            HrtfStore *entry{hrtf.mEntry.get()};
            if(entry && ReadRef(entry->mRef) == 0)
            {
                TRACE("Unloading unused HRTF %s\n", hrtf.mFilename.data());
                hrtf.mEntry = nullptr;
                return true;
            }
            return false;
        };
        auto iter = std::remove_if(LoadedHrtfs.begin(), LoadedHrtfs.end(), remove_unused);
        LoadedHrtfs.erase(iter, LoadedHrtfs.end());
    }
}
