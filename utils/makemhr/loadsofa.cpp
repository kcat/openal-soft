/*
 * HRTF utility for producing and demonstrating the process of creating an
 * OpenAL Soft compatible HRIR data set.
 *
 * Copyright (C) 2018-2019  Christopher Fitzgerald
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

#include "loadsofa.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "makemhr.h"

#include "mysofa.h"


using namespace std::placeholders;

using double3 = std::array<double,3>;

static const char *SofaErrorStr(int err)
{
    switch(err)
    {
    case MYSOFA_OK: return "OK";
    case MYSOFA_INVALID_FORMAT: return "Invalid format";
    case MYSOFA_UNSUPPORTED_FORMAT: return "Unsupported format";
    case MYSOFA_INTERNAL_ERROR: return "Internal error";
    case MYSOFA_NO_MEMORY: return "Out of memory";
    case MYSOFA_READ_ERROR: return "Read error";
    }
    return "Unknown";
}


/* Produces a sorted array of unique elements from a particular axis of the
 * triplets array.  The filters are used to focus on particular coordinates
 * of other axes as necessary.  The epsilons are used to constrain the
 * equality of unique elements.
 */
static std::vector<double> GetUniquelySortedElems(const std::vector<double3> &aers,
    const uint axis, const double *const (&filters)[3], const double (&epsilons)[3])
{
    std::vector<double> elems;
    for(const double3 &aer : aers)
    {
        const double elem{aer[axis]};

        uint j;
        for(j = 0;j < 3;j++)
        {
            if(filters[j] && std::abs(aer[j] - *filters[j]) > epsilons[j])
                break;
        }
        if(j < 3)
            continue;

        auto iter = elems.begin();
        for(;iter != elems.end();++iter)
        {
            const double delta{elem - *iter};
            if(delta > epsilons[axis]) continue;
            if(delta >= -epsilons[axis]) break;

            iter = elems.emplace(iter, elem);
            break;
        }
        if(iter == elems.end())
            elems.emplace_back(elem);
    }
    return elems;
}

/* Given a list of azimuths, this will produce the smallest step size that can
 * uniformly cover the list. Ideally this will be over half, but in degenerate
 * cases this can fall to a minimum of 5 (the lower limit).
 */
static double GetUniformAzimStep(const double epsilon, const std::vector<double> &elems)
{
    if(elems.size() < 5) return 0.0;

    /* Get the maximum count possible, given the first two elements. It would
     * be impossible to have more than this since the first element must be
     * included.
     */
    uint count{static_cast<uint>(std::ceil(360.0 / (elems[1]-elems[0])))};
    count = std::min(count, uint{MAX_AZ_COUNT});

    for(;count >= 5;--count)
    {
        /* Given the stepping value for this number of elements, check each
         * multiple to ensure there's a matching element.
         */
        const double step{360.0 / count};
        bool good{true};
        size_t idx{1u};
        for(uint mult{1u};mult < count && good;++mult)
        {
            const double target{step*mult + elems[0]};
            while(idx < elems.size() && target-elems[idx] > epsilon)
                ++idx;
            good &= (idx < elems.size()) && !(std::abs(target-elems[idx++]) > epsilon);
        }
        if(good)
            return step;
    }
    return 0.0;
}

/* Given a list of elevations, this will produce the smallest step size that
 * can uniformly cover the list. Ideally this will be over half, but in
 * degenerate cases this can fall to a minimum of 5 (the lower limit).
 */
static double GetUniformElevStep(const double epsilon, const std::vector<double> &elems)
{
    if(elems.size() < 5) return 0.0;

    uint count{static_cast<uint>(std::ceil(180.0 / (elems[1]-elems[0])))};
    count = std::min(count, uint{MAX_EV_COUNT}-1u);

    for(;count >= 5;--count)
    {
        const double step{180.0 / count};
        bool good{true};
        size_t idx{1u};
        /* Elevations don't need to match all multiples if there's not enough
         * elements to check. Missing elevations can be synthesized.
         */
        for(uint mult{1u};mult <= count && idx < elems.size() && good;++mult)
        {
            const double target{step*mult + elems[0]};
            while(idx < elems.size() && target-elems[idx] > epsilon)
                ++idx;
            good &= !(idx < elems.size()) || !(std::abs(target-elems[idx++]) > epsilon);
        }
        if(good)
            return step;
    }
    return 0.0;
}

/* Attempts to produce a compatible layout.  Most data sets tend to be
 * uniform and have the same major axis as used by OpenAL Soft's HRTF model.
 * This will remove outliers and produce a maximally dense layout when
 * possible.  Those sets that contain purely random measurements or use
 * different major axes will fail.
 */
static bool PrepareLayout(const uint m, const float *xyzs, HrirDataT *hData)
{
    auto aers = std::vector<double3>(m, double3{});
    for(uint i{0u};i < m;++i)
    {
        float aer[3]{xyzs[i*3], xyzs[i*3 + 1], xyzs[i*3 + 2]};
        mysofa_c2s(&aer[0]);
        aers[i][0] = aer[0];
        aers[i][1] = aer[1];
        aers[i][2] = aer[2];
    }

    auto radii = GetUniquelySortedElems(aers, 2, {}, {0.1, 0.1, 0.001});
    if(radii.size() > MAX_FD_COUNT)
    {
        fprintf(stdout, "Incompatible layout (inumerable radii).\n");
        return false;
    }

    double distances[MAX_FD_COUNT]{};
    uint evCounts[MAX_FD_COUNT]{};
    auto azCounts = std::vector<uint>(MAX_FD_COUNT*MAX_EV_COUNT, 0u);

    auto dist_end = std::copy_if(radii.cbegin(), radii.cend(), std::begin(distances),
        std::bind(std::greater_equal<double>{}, _1, hData->mRadius));
    auto fdCount = static_cast<uint>(std::distance(std::begin(distances), dist_end));

    for(uint fi{0u};fi < fdCount;)
    {
        const double dist{distances[fi]};
        auto elevs = GetUniquelySortedElems(aers, 1, {nullptr, nullptr, &dist},
            {0.1, 0.1, 0.001});

        /* Remove elevations that don't have a valid set of azimuths. */
        auto invalid_elev = [&dist,&aers](const double ev) -> bool
        {
            auto azims = GetUniquelySortedElems(aers, 0, {nullptr, &ev, &dist}, {0.1, 0.1, 0.001});

            if(std::abs(90.0 - std::abs(ev)) < 0.1)
                return azims.size() != 1;
            if(azims.empty() || !(std::abs(azims[0]) < 0.1))
                return true;
            return GetUniformAzimStep(0.1, azims) <= 0.0;
        };
        elevs.erase(std::remove_if(elevs.begin(), elevs.end(), invalid_elev), elevs.end());

        /* Reverse the elevations so it increments starting with -90 (flipped
         * from +90). This makes it easier to work out a proper stepping value.
         */
        std::reverse(elevs.begin(), elevs.end());
        for(auto &ev : elevs) ev *= -1.0;

        double step{GetUniformElevStep(0.1, elevs)};
        if(step <= 0.0)
        {
            fprintf(stdout, "Non-uniform elevations on field distance %f.\n", dist);
            std::copy(&distances[fi+1], &distances[fdCount], &distances[fi]);
            --fdCount;
            continue;
        }

        /* Re-reverse the elevations to restore the correct order. */
        for(auto &ev : elevs) ev *= -1.0;
        std::reverse(elevs.begin(), elevs.end());

        uint evStart{0u};
        for(uint ei{0u};ei < elevs.size();ei++)
        {
            if(!(elevs[ei] < 0.0))
            {
                fprintf(stdout, "Too many missing elevations on field distance %f.\n", dist);
                return false;
            }

            double eif{(90.0+elevs[ei]) / step};
            const double ev_start{std::round(eif)};

            if(std::abs(eif - ev_start) < (0.1/step))
            {
                evStart = static_cast<uint>(ev_start);
                break;
            }
        }

        const auto evCount = static_cast<uint>(std::round(180.0 / step)) + 1;
        if(evCount < 5)
        {
            fprintf(stdout, "Too few uniform elevations on field distance %f.\n", dist);
            std::copy(&distances[fi+1], &distances[fdCount], &distances[fi]);
            --fdCount;
            continue;
        }
        evCounts[fi] = evCount;

        for(uint ei{evStart};ei < evCount;ei++)
        {
            const double ev{-90.0 + ei*180.0/(evCount - 1)};
            auto azims = GetUniquelySortedElems(aers, 0, {nullptr, &ev, &dist}, {0.1, 0.1, 0.001});

            if(ei == 0 || ei == (evCount-1))
            {
                if(azims.size() != 1)
                {
                    fprintf(stdout, "Non-singular poles on field distance %f.\n", dist);
                    return false;
                }
                azCounts[fi*MAX_EV_COUNT + ei] = 1u;
            }
            else
            {
                step = GetUniformAzimStep(0.1, azims);
                if(step <= 0.0)
                {
                    fprintf(stdout, "Non-uniform azimuths on elevation %f, field distance %f.\n",
                        ev, dist);
                    return false;
                }
                azCounts[fi*MAX_EV_COUNT + ei] = static_cast<uint>(std::round(360.0 / step));
            }
        }

        for(uint ei{0u};ei < evStart;ei++)
            azCounts[fi*MAX_EV_COUNT + ei] = azCounts[fi*MAX_EV_COUNT + evCount - ei - 1];
        ++fi;
    }
    return PrepareHrirData(fdCount, distances, evCounts, azCounts.data(), hData) != 0;
}


bool PrepareSampleRate(MYSOFA_HRTF *sofaHrtf, HrirDataT *hData)
{
    const char *srate_dim{nullptr};
    const char *srate_units{nullptr};
    MYSOFA_ARRAY *srate_array{&sofaHrtf->DataSamplingRate};
    MYSOFA_ATTRIBUTE *srate_attrs{srate_array->attributes};
    while(srate_attrs)
    {
        if(std::string{"DIMENSION_LIST"} == srate_attrs->name)
        {
            if(srate_dim)
            {
                fprintf(stderr, "Duplicate SampleRate.DIMENSION_LIST\n");
                return false;
            }
            srate_dim = srate_attrs->value;
        }
        else if(std::string{"Units"} == srate_attrs->name)
        {
            if(srate_units)
            {
                fprintf(stderr, "Duplicate SampleRate.Units\n");
                return false;
            }
            srate_units = srate_attrs->value;
        }
        else
            fprintf(stderr, "Unexpected sample rate attribute: %s = %s\n", srate_attrs->name,
                srate_attrs->value);
        srate_attrs = srate_attrs->next;
    }
    if(!srate_dim)
    {
        fprintf(stderr, "Missing sample rate dimensions\n");
        return false;
    }
    if(srate_dim != std::string{"I"})
    {
        fprintf(stderr, "Unsupported sample rate dimensions: %s\n", srate_dim);
        return false;
    }
    if(!srate_units)
    {
        fprintf(stderr, "Missing sample rate unit type\n");
        return false;
    }
    if(srate_units != std::string{"hertz"})
    {
        fprintf(stderr, "Unsupported sample rate unit type: %s\n", srate_units);
        return false;
    }
    /* I dimensions guarantees 1 element, so just extract it. */
    hData->mIrRate = static_cast<uint>(srate_array->values[0] + 0.5f);
    if(hData->mIrRate < MIN_RATE || hData->mIrRate > MAX_RATE)
    {
        fprintf(stderr, "Sample rate out of range: %u (expected %u to %u)", hData->mIrRate,
            MIN_RATE, MAX_RATE);
        return false;
    }
    return true;
}

bool PrepareDelay(MYSOFA_HRTF *sofaHrtf, HrirDataT *hData)
{
    const char *delay_dim{nullptr};
    MYSOFA_ARRAY *delay_array{&sofaHrtf->DataDelay};
    MYSOFA_ATTRIBUTE *delay_attrs{delay_array->attributes};
    while(delay_attrs)
    {
        if(std::string{"DIMENSION_LIST"} == delay_attrs->name)
        {
            if(delay_dim)
            {
                fprintf(stderr, "Duplicate Delay.DIMENSION_LIST\n");
                return false;
            }
            delay_dim = delay_attrs->value;
        }
        else
            fprintf(stderr, "Unexpected delay attribute: %s = %s\n", delay_attrs->name,
                delay_attrs->value);
        delay_attrs = delay_attrs->next;
    }
    if(!delay_dim)
    {
        fprintf(stderr, "Missing delay dimensions\n");
        /*return false;*/
    }
    else if(delay_dim != std::string{"I,R"})
    {
        fprintf(stderr, "Unsupported delay dimensions: %s\n", delay_dim);
        return false;
    }
    else if(hData->mChannelType == CT_STEREO)
    {
        /* I,R is 1xChannelCount. Makemhr currently removes any delay constant,
         * so we can ignore this as long as it's equal.
         */
        if(delay_array->values[0] != delay_array->values[1])
        {
            fprintf(stderr, "Mismatched delays not supported: %f, %f\n", delay_array->values[0],
                delay_array->values[1]);
            return false;
        }
    }
    return true;
}

bool CheckIrData(MYSOFA_HRTF *sofaHrtf)
{
    const char *ir_dim{nullptr};
    MYSOFA_ARRAY *ir_array{&sofaHrtf->DataIR};
    MYSOFA_ATTRIBUTE *ir_attrs{ir_array->attributes};
    while(ir_attrs)
    {
        if(std::string{"DIMENSION_LIST"} == ir_attrs->name)
        {
            if(ir_dim)
            {
                fprintf(stderr, "Duplicate IR.DIMENSION_LIST\n");
                return false;
            }
            ir_dim = ir_attrs->value;
        }
        else
            fprintf(stderr, "Unexpected IR attribute: %s = %s\n", ir_attrs->name,
                ir_attrs->value);
        ir_attrs = ir_attrs->next;
    }
    if(!ir_dim)
    {
        fprintf(stderr, "Missing IR dimensions\n");
        return false;
    }
    if(ir_dim != std::string{"M,R,N"})
    {
        fprintf(stderr, "Unsupported IR dimensions: %s\n", ir_dim);
        return false;
    }
    return true;
}


/* Calculate the onset time of a HRIR. */
static double CalcHrirOnset(const uint rate, const uint n, std::vector<double> &upsampled,
    const double *hrir)
{
    {
        PPhaseResampler rs;
        rs.init(rate, 10 * rate);
        rs.process(n, hrir, 10 * n, upsampled.data());
    }

    double mag{std::accumulate(upsampled.cbegin(), upsampled.cend(), double{0.0},
        [](const double magnitude, const double sample) -> double
        { return std::max(magnitude, std::abs(sample)); })};

    mag *= 0.15;
    auto iter = std::find_if(upsampled.cbegin(), upsampled.cend(),
        [mag](const double sample) -> bool { return (std::abs(sample) >= mag); });
    return static_cast<double>(std::distance(upsampled.cbegin(), iter)) / (10.0*rate);
}

/* Calculate the magnitude response of a HRIR. */
static void CalcHrirMagnitude(const uint points, const uint n, std::vector<complex_d> &h,
    const double *hrir, double *mag)
{
    auto iter = std::copy_n(hrir, points, h.begin());
    std::fill(iter, h.end(), complex_d{0.0, 0.0});

    FftForward(n, h.data());
    MagnitudeResponse(n, h.data(), mag);
}

static bool LoadResponses(MYSOFA_HRTF *sofaHrtf, HrirDataT *hData)
{
    const uint channels{(hData->mChannelType == CT_STEREO) ? 2u : 1u};
    hData->mHrirsBase.resize(channels * hData->mIrCount * hData->mIrSize);
    double *hrirs = hData->mHrirsBase.data();

    /* Temporary buffers used to calculate the IR's onset and frequency
     * magnitudes.
     */
    auto upsampled = std::vector<double>(10 * hData->mIrPoints);
    auto htemp = std::vector<complex_d>(hData->mFftSize);
    auto hrir = std::vector<double>(hData->mFftSize);

    for(uint si{0u};si < sofaHrtf->M;si++)
    {
        printf("\rLoading HRIRs... %d of %d", si+1, sofaHrtf->M);
        fflush(stdout);

        float aer[3]{
            sofaHrtf->SourcePosition.values[3*si],
            sofaHrtf->SourcePosition.values[3*si + 1],
            sofaHrtf->SourcePosition.values[3*si + 2]
        };
        mysofa_c2s(aer);

        if(std::abs(aer[1]) >= 89.999f)
            aer[0] = 0.0f;
        else
            aer[0] = std::fmod(360.0f - aer[0], 360.0f);

        auto field = std::find_if(hData->mFds.cbegin(), hData->mFds.cend(),
            [&aer](const HrirFdT &fld) -> bool
            {
                double delta = aer[2] - fld.mDistance;
                return (std::abs(delta) < 0.001);
            });
        if(field == hData->mFds.cend())
            continue;

        double ef{(90.0+aer[1]) / 180.0 * (field->mEvCount-1)};
        auto ei = static_cast<int>(std::round(ef));
        ef = (ef-ei) * 180.0 / (field->mEvCount-1);
        if(std::abs(ef) >= 0.1) continue;

        double af{aer[0] / 360.0 * field->mEvs[ei].mAzCount};
        auto ai = static_cast<int>(std::round(af));
        af = (af-ai) * 360.0 / field->mEvs[ei].mAzCount;
        ai %= field->mEvs[ei].mAzCount;
        if(std::abs(af) >= 0.1) continue;

        HrirAzT *azd = &field->mEvs[ei].mAzs[ai];
        if(azd->mIrs[0] != nullptr)
        {
            fprintf(stderr, "Multiple measurements near [ a=%f, e=%f, r=%f ].\n",
                aer[0], aer[1], aer[2]);
            return false;
        }

        for(uint ti{0u};ti < channels;++ti)
        {
            std::copy_n(&sofaHrtf->DataIR.values[(si*sofaHrtf->R + ti)*sofaHrtf->N],
                hData->mIrPoints, hrir.begin());
            azd->mIrs[ti] = &hrirs[hData->mIrSize * (hData->mIrCount*ti + azd->mIndex)];
            azd->mDelays[ti] = CalcHrirOnset(hData->mIrRate, hData->mIrPoints, upsampled,
                hrir.data());
            CalcHrirMagnitude(hData->mIrPoints, hData->mFftSize, htemp, hrir.data(),
                azd->mIrs[ti]);
        }

        // TODO: Since some SOFA files contain minimum phase HRIRs,
        // it would be beneficial to check for per-measurement delays
        // (when available) to reconstruct the HRTDs.
    }
    printf("\n");
    return true;
}

struct MySofaHrtfDeleter {
    void operator()(MYSOFA_HRTF *ptr) { mysofa_free(ptr); }
};
using MySofaHrtfPtr = std::unique_ptr<MYSOFA_HRTF,MySofaHrtfDeleter>;

bool LoadSofaFile(const char *filename, const uint fftSize, const uint truncSize,
    const ChannelModeT chanMode, HrirDataT *hData)
{
    int err;
    MySofaHrtfPtr sofaHrtf{mysofa_load(filename, &err)};
    if(!sofaHrtf)
    {
        fprintf(stdout, "Error: Could not load %s: %s\n", filename, SofaErrorStr(err));
        return false;
    }

    /* NOTE: Some valid SOFA files are failing this check. */
    err = mysofa_check(sofaHrtf.get());
    if(err != MYSOFA_OK)
        fprintf(stderr, "Warning: Supposedly malformed source file '%s' (%s).\n", filename,
            SofaErrorStr(err));

    mysofa_tocartesian(sofaHrtf.get());

    /* Make sure emitter and receiver counts are sane. */
    if(sofaHrtf->E != 1)
    {
        fprintf(stderr, "%u emitters not supported\n", sofaHrtf->E);
        return false;
    }
    if(sofaHrtf->R > 2 || sofaHrtf->R < 1)
    {
        fprintf(stderr, "%u receivers not supported\n", sofaHrtf->R);
        return false;
    }
    /* Assume R=2 is a stereo measurement, and R=1 is mono left-ear-only. */
    if(sofaHrtf->R == 2 && chanMode == CM_AllowStereo)
        hData->mChannelType = CT_STEREO;
    else
        hData->mChannelType = CT_MONO;

    /* Check and set the FFT and IR size. */
    if(sofaHrtf->N > fftSize)
    {
        fprintf(stderr, "Sample points exceeds the FFT size.\n");
        return false;
    }
    if(sofaHrtf->N < truncSize)
    {
        fprintf(stderr, "Sample points is below the truncation size.\n");
        return false;
    }
    hData->mIrPoints = sofaHrtf->N;
    hData->mFftSize = fftSize;
    hData->mIrSize = std::max(1u + (fftSize/2u), sofaHrtf->N);

    /* Assume a default head radius of 9cm. */
    hData->mRadius = 0.09;

    if(!PrepareSampleRate(sofaHrtf.get(), hData) || !PrepareDelay(sofaHrtf.get(), hData)
        || !CheckIrData(sofaHrtf.get()))
        return false;
    if(!PrepareLayout(sofaHrtf->M, sofaHrtf->SourcePosition.values, hData))
        return false;

    if(!LoadResponses(sofaHrtf.get(), hData))
        return false;
    sofaHrtf = nullptr;

    for(uint fi{0u};fi < hData->mFdCount;fi++)
    {
        uint ei{0u};
        for(;ei < hData->mFds[fi].mEvCount;ei++)
        {
            uint ai{0u};
            for(;ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT &azd = hData->mFds[fi].mEvs[ei].mAzs[ai];
                if(azd.mIrs[0] != nullptr) break;
            }
            if(ai < hData->mFds[fi].mEvs[ei].mAzCount)
                break;
        }
        if(ei >= hData->mFds[fi].mEvCount)
        {
            fprintf(stderr, "Missing source references [ %d, *, * ].\n", fi);
            return false;
        }
        hData->mFds[fi].mEvStart = ei;
        for(;ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(uint ai{0u};ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT &azd = hData->mFds[fi].mEvs[ei].mAzs[ai];
                if(azd.mIrs[0] == nullptr)
                {
                    fprintf(stderr, "Missing source reference [ %d, %d, %d ].\n", fi, ei, ai);
                    return false;
                }
            }
        }
    }

    const uint channels{(hData->mChannelType == CT_STEREO) ? 2u : 1u};
    double *hrirs = hData->mHrirsBase.data();
    for(uint fi{0u};fi < hData->mFdCount;fi++)
    {
        for(uint ei{0u};ei < hData->mFds[fi].mEvCount;ei++)
        {
            for(uint ai{0u};ai < hData->mFds[fi].mEvs[ei].mAzCount;ai++)
            {
                HrirAzT &azd = hData->mFds[fi].mEvs[ei].mAzs[ai];
                for(uint ti{0u};ti < channels;ti++)
                    azd.mIrs[ti] = &hrirs[hData->mIrSize * (hData->mIrCount*ti + azd.mIndex)];
            }
        }
    }

    return true;
}
