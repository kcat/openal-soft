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
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "makemhr.h"
#include "polyphase_resampler.h"
#include "sofa-support.h"

#include "mysofa.h"


using uint = unsigned int;

/* Attempts to produce a compatible layout.  Most data sets tend to be
 * uniform and have the same major axis as used by OpenAL Soft's HRTF model.
 * This will remove outliers and produce a maximally dense layout when
 * possible.  Those sets that contain purely random measurements or use
 * different major axes will fail.
 */
static bool PrepareLayout(const uint m, const float *xyzs, HrirDataT *hData)
{
    fprintf(stdout, "Detecting compatible layout...\n");

    auto fds = GetCompatibleLayout(m, xyzs);
    if(fds.size() > MAX_FD_COUNT)
    {
        fprintf(stdout, "Incompatible layout (inumerable radii).\n");
        return false;
    }

    double distances[MAX_FD_COUNT]{};
    uint evCounts[MAX_FD_COUNT]{};
    auto azCounts = std::vector<uint>(MAX_FD_COUNT*MAX_EV_COUNT, 0u);

    uint fi{0u}, ir_total{0u};
    for(const auto &field : fds)
    {
        distances[fi] = field.mDistance;
        evCounts[fi] = field.mEvCount;

        for(uint ei{0u};ei < field.mEvStart;ei++)
            azCounts[fi*MAX_EV_COUNT + ei] = field.mAzCounts[field.mEvCount-ei-1];
        for(uint ei{field.mEvStart};ei < field.mEvCount;ei++)
        {
            azCounts[fi*MAX_EV_COUNT + ei] = field.mAzCounts[ei];
            ir_total += field.mAzCounts[ei];
        }

        ++fi;
    }
    fprintf(stdout, "Using %u of %u IRs.\n", ir_total, m);
    return PrepareHrirData(fi, distances, evCounts, azCounts.data(), hData) != 0;
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
    std::atomic<uint> loaded_count{0u};

    auto load_proc = [sofaHrtf,hData,&loaded_count]() -> bool
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

        for(uint si{0u};si < sofaHrtf->M;++si)
        {
            loaded_count.fetch_add(1u);

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
                fprintf(stderr, "\nMultiple measurements near [ a=%f, e=%f, r=%f ].\n",
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

            /* TODO: Since some SOFA files contain minimum phase HRIRs,
             * it would be beneficial to check for per-measurement delays
             * (when available) to reconstruct the HRTDs.
             */
        }
        return true;
    };

    std::future_status load_status{};
    auto load_future = std::async(std::launch::async, load_proc);
    do {
        load_status = load_future.wait_for(std::chrono::milliseconds{50});
        printf("\rLoading HRIRs... %u of %u", loaded_count.load(), sofaHrtf->M);
        fflush(stdout);
    } while(load_status != std::future_status::ready);
    fputc('\n', stdout);
    return load_future.get();
}


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
