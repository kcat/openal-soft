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

#include "config.h"

#include "loadsofa.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "alnumeric.h"
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

/* Attempts to produce a compatible layout.  Most data sets tend to be
 * uniform and have the same major axis as used by OpenAL Soft's HRTF model.
 * This will remove outliers and produce a maximally dense layout when
 * possible.  Those sets that contain purely random measurements or use
 * different major axes will fail.
 */
auto PrepareLayout(const std::span<const float> xyzs, HrirDataT *hData) -> bool
{
    fmt::println("Detecting compatible layout...");

    auto fds = GetCompatibleLayout(xyzs);
    if(fds.size() > MAX_FD_COUNT)
    {
        fmt::println("Incompatible layout (inumerable radii).");
        return false;
    }

    std::array<double, MAX_FD_COUNT> distances{};
    std::array<unsigned, MAX_FD_COUNT> evCounts{};
    auto azCounts = std::vector<std::array<unsigned, MAX_EV_COUNT>>(MAX_FD_COUNT);
    for(auto &azs : azCounts) azs.fill(0u);

    auto fi = 0u;
    auto ir_total = 0u;
    for(const auto &field : fds)
    {
        distances[fi] = field.mDistance;
        evCounts[fi] = field.mEvCount;

        for(auto ei=0u;ei < field.mEvStart;++ei)
            azCounts[fi][ei] = field.mAzCounts[field.mEvCount-ei-1];
        for(auto ei=field.mEvStart;ei < field.mEvCount;++ei)
        {
            azCounts[fi][ei] = field.mAzCounts[ei];
            ir_total += field.mAzCounts[ei];
        }

        ++fi;
    }
    fmt::println("Using {} of {} IRs.", ir_total, xyzs.size()/3);
    const auto azs = std::span{azCounts}.first<MAX_FD_COUNT>();
    return PrepareHrirData(std::span{distances}.first(fi), evCounts, azs, hData);
}

auto GetSampleRate(MYSOFA_HRTF const *const sofaHrtf) -> f32
{
    auto *srate_dim = gsl::czstring{};
    auto *srate_units = gsl::czstring{};
    auto const &srate_array = sofaHrtf->DataSamplingRate;
    auto const *srate_attrs = srate_array.attributes;
    while(srate_attrs)
    {
        if("DIMENSION_LIST"sv == srate_attrs->name)
        {
            if(srate_dim)
            {
                fmt::println(std::cerr, "Duplicate SampleRate.DIMENSION_LIST");
                return 0.0f;
            }
            srate_dim = srate_attrs->value;
        }
        else if("Units"sv == srate_attrs->name)
        {
            if(srate_units)
            {
                fmt::println(std::cerr, "Duplicate SampleRate.Units");
                return 0.0f;
            }
            srate_units = srate_attrs->value;
        }
        else
            fmt::println(std::cerr, "Unexpected sample rate attribute: {} = {}", srate_attrs->name,
                srate_attrs->value ? srate_attrs->value : "<null>");
        srate_attrs = srate_attrs->next;
    }
    if(!srate_dim)
    {
        fmt::println(std::cerr, "Missing sample rate dimensions");
        return 0.0f;
    }
    if(srate_dim != "I"sv)
    {
        fmt::println(std::cerr, "Unsupported sample rate dimensions: {}", srate_dim);
        return 0.0f;
    }
    if(!srate_units)
    {
        fmt::println(std::cerr, "Missing sample rate unit type");
        return 0.0f;
    }
    if(srate_units != "hertz"sv)
    {
        fmt::println(std::cerr, "Unsupported sample rate unit type: {}", srate_units);
        return 0.0f;
    }
    /* I dimensions guarantees 1 element, so just extract it. */
    auto const values = std::span{srate_array.values, sofaHrtf->I};
    if(values[0] < float{MIN_RATE} || values[0] > float{MAX_RATE})
    {
        fmt::println(std::cerr, "Sample rate out of range: {:f} (expected {} to {})", values[0],
            MIN_RATE, MAX_RATE);
        return 0.0f;
    }
    return values[0];
}

enum class DelayType : u8::value_t {
    None,
    I_R, /* [1][Channels] */
    M_R, /* [HRIRs][Channels] */
};
auto PrepareDelay(MYSOFA_HRTF const *const sofaHrtf) -> std::optional<DelayType>
{
    auto *delay_dim = gsl::czstring{};
    auto const *delay_attrs = sofaHrtf->DataDelay.attributes;
    while(delay_attrs)
    {
        if("DIMENSION_LIST"sv == delay_attrs->name)
        {
            if(delay_dim)
            {
                fmt::println(std::cerr, "Duplicate Delay.DIMENSION_LIST");
                return std::nullopt;
            }
            delay_dim = delay_attrs->value;
        }
        else
            fmt::println(std::cerr, "Unexpected delay attribute: {} = {}", delay_attrs->name,
                delay_attrs->value ? delay_attrs->value : "<null>");
        delay_attrs = delay_attrs->next;
    }
    if(!delay_dim)
    {
        fmt::println(std::cerr, "Missing delay dimensions");
        return DelayType::None;
    }
    if(delay_dim == "I,R"sv)
        return DelayType::I_R;
    if(delay_dim == "M,R"sv)
        return DelayType::M_R;

    fmt::println(std::cerr, "Unsupported delay dimensions: {}", delay_dim);
    return std::nullopt;
}

auto CheckIrData(MYSOFA_HRTF const *const sofaHrtf) -> bool
{
    auto *ir_dim = gsl::czstring{};
    auto const *ir_attrs = sofaHrtf->DataIR.attributes;
    while(ir_attrs)
    {
        if("DIMENSION_LIST"sv == ir_attrs->name)
        {
            if(ir_dim)
            {
                fmt::println(std::cerr, "Duplicate IR.DIMENSION_LIST");
                return false;
            }
            ir_dim = ir_attrs->value;
        }
        else
            fmt::println(std::cerr, "Unexpected IR attribute: {} = {}", ir_attrs->name,
                ir_attrs->value ? ir_attrs->value : "<null>");
        ir_attrs = ir_attrs->next;
    }
    if(!ir_dim)
    {
        fmt::println(std::cerr, "Missing IR dimensions");
        return false;
    }
    if(ir_dim != "M,R,N"sv)
    {
        fmt::println(std::cerr, "Unsupported IR dimensions: {}", ir_dim);
        return false;
    }
    return true;
}


/* Calculate the onset time of a HRIR. */
constexpr auto OnsetRateMultiple = 10;
auto CalcHrirOnset(PPhaseResampler &rs, unsigned const rate, std::span<double> upsampled,
    const std::span<const double> hrir) -> double
{
    rs.process(hrir, upsampled);

    static constexpr auto make_abs = [](const double value) { return std::abs(value); };
    const auto iter = std::ranges::max_element(upsampled, std::less{}, make_abs);
    return static_cast<double>(std::distance(upsampled.begin(), iter)) /
        (double{OnsetRateMultiple}*rate);
}

/* Calculate the magnitude response of a HRIR. */
void CalcHrirMagnitude(unsigned const points, std::span<complex_d> h, const std::span<double> hrir)
{
    auto iter = std::copy_n(hrir.begin(), points, h.begin());
    std::fill(iter, h.end(), complex_d{0.0, 0.0});

    forward_fft(h);
    MagnitudeResponse(h, hrir.first((h.size()/2) + 1));
}

auto LoadResponses(MYSOFA_HRTF *sofaHrtf, HrirDataT *hData, DelayType const delayType,
    unsigned const outRate) -> bool
{
    auto loaded_count = std::atomic{0u};

    auto load_proc = [sofaHrtf,hData,delayType,outRate,&loaded_count]() -> bool
    {
        auto const channels = (hData->mChannelType == CT_STEREO) ? 2u : 1u;
        hData->mHrirsBase.resize(channels * size_t{hData->mIrCount} * hData->mIrSize, 0.0);
        const auto hrirs = std::span{hData->mHrirsBase};

        std::vector<double> restmp;
        std::optional<PPhaseResampler> resampler;
        if(outRate && outRate != hData->mIrRate)
        {
            resampler.emplace().init(hData->mIrRate, outRate);
            restmp.resize(sofaHrtf->N);
        }

        const auto srcPosValues = std::span{sofaHrtf->SourcePosition.values, sofaHrtf->M*3_uz};
        const auto irValues = std::span{sofaHrtf->DataIR.values,
            size_t{sofaHrtf->M}*sofaHrtf->R*sofaHrtf->N};
        for(auto si=0u;si < sofaHrtf->M;++si)
        {
            loaded_count.fetch_add(1u);

            std::array aer{srcPosValues[3_uz*si], srcPosValues[3_uz*si + 1],
                srcPosValues[3_uz*si + 2]};
            mysofa_c2s(aer.data());

            if(std::abs(aer[1]) >= 89.999f)
                aer[0] = 0.0f;
            else
                aer[0] = std::fmod(360.0f - aer[0], 360.0f);

            auto field = std::ranges::find_if(hData->mFds, [&aer](const HrirFdT &fld) -> bool
            { return (std::abs(aer[2] - fld.mDistance) < 0.001); });
            if(field == hData->mFds.end())
                continue;

            auto const evscale = 180.0 / static_cast<double>(field->mEvs.size()-1);
            auto ef = (90.0 + aer[1]) / evscale;
            auto const ei = static_cast<unsigned>(std::round(ef));
            ef = (ef - ei) * evscale;
            if(std::abs(ef) >= 0.1) continue;

            auto const azscale = 360.0 / static_cast<double>(field->mEvs[ei].mAzs.size());
            auto af = aer[0] / azscale;
            auto ai = static_cast<unsigned>(std::round(af));
            af = (af-ai) * azscale;
            ai %= static_cast<unsigned>(field->mEvs[ei].mAzs.size());
            if(std::abs(af) >= 0.1) continue;

            HrirAzT &azd = field->mEvs[ei].mAzs[ai];
            if(!azd.mIrs[0].empty())
            {
                fmt::println(std::cerr, "\nMultiple measurements near [ a={:f}, e={:f}, r={:f} ].",
                    aer[0], aer[1], aer[2]);
                return false;
            }

            for(auto ti=0u;ti < channels;++ti)
            {
                azd.mIrs[ti] = hrirs.subspan(
                    (size_t{hData->mIrCount}*ti + azd.mIndex) * hData->mIrSize, hData->mIrSize);
                const auto ir = irValues.subspan((size_t{si}*sofaHrtf->R + ti)*sofaHrtf->N,
                    sofaHrtf->N);
                if(!resampler)
                    std::copy_n(ir.begin(), ir.size(), azd.mIrs[ti].begin());
                else
                {
                    std::copy_n(ir.begin(), ir.size(), restmp.begin());
                    resampler->process(restmp, azd.mIrs[ti]);
                }
            }

            /* Include any per-channel or per-HRIR delays. */
            if(delayType == DelayType::I_R)
            {
                const auto delayValues = std::span{sofaHrtf->DataDelay.values,
                    size_t{sofaHrtf->I}*sofaHrtf->R};
                for(auto ti=0u;ti < channels;++ti)
                    azd.mDelays[ti] = delayValues[ti] / static_cast<float>(hData->mIrRate);
            }
            else if(delayType == DelayType::M_R)
            {
                const auto delayValues = std::span{sofaHrtf->DataDelay.values,
                    size_t{sofaHrtf->M}*sofaHrtf->R};
                for(auto ti=0u;ti < channels;++ti)
                    azd.mDelays[ti] = delayValues[si*sofaHrtf->R + ti] /
                        static_cast<float>(hData->mIrRate);
            }
        }

        if(outRate && outRate != hData->mIrRate)
        {
            auto const scale = static_cast<double>(outRate) / hData->mIrRate;
            hData->mIrRate = outRate;
            hData->mIrPoints = std::min(static_cast<unsigned>(std::ceil(hData->mIrPoints*scale)),
                hData->mIrSize);
        }
        return true;
    };

    std::future_status load_status{};
    auto load_future = std::async(std::launch::async, load_proc);
    do {
        load_status = load_future.wait_for(std::chrono::milliseconds{50});
        fmt::print("\rLoading HRIRs... {} of {}", loaded_count.load(), sofaHrtf->M);
        std::cout.flush();
    } while(load_status != std::future_status::ready);
    fmt::println("");
    return load_future.get();
}


/* Calculates the frequency magnitudes of the HRIR set. Work is delegated to
 * this struct, which runs asynchronously on one or more threads (sharing the
 * same calculator object).
 */
struct MagCalculator {
    unsigned const mFftSize{};
    unsigned const mIrPoints{};
    std::vector<std::span<double>> mIrs;
    std::atomic<size_t> mCurrent;
    std::atomic<size_t> mDone;

    MagCalculator(unsigned const fftsize, unsigned const irpoints)
        : mFftSize{fftsize}, mIrPoints{irpoints}
    { }

    void Worker()
    {
        auto htemp = std::vector<complex_d>(mFftSize);

        while(true)
        {
            /* Load the current index to process. */
            auto idx = mCurrent.load();
            do {
                /* If the index is at the end, we're done. */
                if(idx >= mIrs.size())
                    return;
                /* Otherwise, increment the current index atomically so other
                 * threads know to go to the next one. If this call fails, the
                 * current index was just changed by another thread and the new
                 * value is loaded into idx, which we'll recheck.
                 */
            } while(!mCurrent.compare_exchange_weak(idx, idx+1, std::memory_order_relaxed));

            CalcHrirMagnitude(mIrPoints, htemp, mIrs[idx]);

            /* Increment the number of IRs done. */
            mDone.fetch_add(1);
        }
    }
};

} // namespace

bool LoadSofaFile(std::string_view const filename, unsigned const numThreads,
    unsigned const fftSize, unsigned const truncSize, unsigned const outRate,
    ChannelModeT const chanMode, HrirDataT *hData)
{
    auto err = int{};
    auto sofaHrtf = MySofaHrtfPtr{mysofa_load(std::string{filename}.c_str(), &err)};
    if(!sofaHrtf)
    {
        fmt::println("Error: Could not load {}: {} ({})", filename, SofaErrorStr(err), err);
        return false;
    }

    /* NOTE: Some valid SOFA files are failing this check. */
    err = mysofa_check(sofaHrtf.get());
    if(err != MYSOFA_OK)
        fmt::println(std::cerr, "Warning: Supposedly malformed source file '{}': {} ({})",
            filename, SofaErrorStr(err), err);

    mysofa_tocartesian(sofaHrtf.get());

    /* Make sure emitter and receiver counts are sane. */
    if(sofaHrtf->E != 1)
    {
        fmt::println(std::cerr, "{} emitters not supported", sofaHrtf->E);
        return false;
    }
    if(sofaHrtf->R > 2 || sofaHrtf->R < 1)
    {
        fmt::println(std::cerr, "{} receivers not supported", sofaHrtf->R);
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
        fmt::println(std::cerr, "Sample points exceeds the FFT size ({} > {}).", sofaHrtf->N,
            fftSize);
        return false;
    }
    if(sofaHrtf->N < truncSize)
    {
        fmt::println(std::cerr, "Sample points is below the truncation size ({} < {}).",
            sofaHrtf->N, truncSize);
        return false;
    }
    hData->mIrPoints = sofaHrtf->N;
    hData->mFftSize = fftSize;
    hData->mIrSize = std::max(1u + (fftSize/2u), sofaHrtf->N);

    /* Assume a default head radius of 9cm. */
    hData->mRadius = 0.09;

    hData->mIrRate = static_cast<unsigned>(std::lround(GetSampleRate(sofaHrtf.get())));
    if(!hData->mIrRate)
        return false;

    const auto delayType = PrepareDelay(sofaHrtf.get());
    if(!delayType)
        return false;

    if(!CheckIrData(sofaHrtf.get()))
        return false;
    if(!PrepareLayout(std::span{sofaHrtf->SourcePosition.values, sofaHrtf->M*3_uz}, hData))
        return false;
    if(!LoadResponses(sofaHrtf.get(), hData, *delayType, outRate))
        return false;
    sofaHrtf = nullptr;

    for(auto fi=0u;fi < hData->mFds.size();fi++)
    {
        auto ei = 0u;
        for(;ei < hData->mFds[fi].mEvs.size();++ei)
        {
            auto ai = 0u;
            for(;ai < hData->mFds[fi].mEvs[ei].mAzs.size();++ai)
            {
                auto &azd = hData->mFds[fi].mEvs[ei].mAzs[ai];
                if(!azd.mIrs[0].empty()) break;
            }
            if(ai < hData->mFds[fi].mEvs[ei].mAzs.size())
                break;
        }
        if(ei >= hData->mFds[fi].mEvs.size())
        {
            fmt::println(std::cerr, "Missing source references [{}, *, *].", fi);
            return false;
        }
        hData->mFds[fi].mEvStart = ei;
        for(;ei < hData->mFds[fi].mEvs.size();ei++)
        {
            for(auto ai=0u;ai < hData->mFds[fi].mEvs[ei].mAzs.size();++ai)
            {
                auto &azd = hData->mFds[fi].mEvs[ei].mAzs[ai];
                if(azd.mIrs[0].empty())
                {
                    fmt::println(std::cerr, "Missing source reference [{}, {}, {}].", fi, ei, ai);
                    return false;
                }
            }
        }
    }


    size_t hrir_total{0};
    auto const channels = (hData->mChannelType == CT_STEREO) ? 2u : 1u;
    auto const hrirs = std::span{hData->mHrirsBase};
    for(auto fi=0u;fi < hData->mFds.size();++fi)
    {
        for(auto ei=0u;ei < hData->mFds[fi].mEvStart;++ei)
        {
            for(auto ai=0u;ai < hData->mFds[fi].mEvs[ei].mAzs.size();++ai)
            {
                auto &azd = hData->mFds[fi].mEvs[ei].mAzs[ai];
                for(size_t ti{0u};ti < channels;++ti)
                    azd.mIrs[ti] = hrirs.subspan((hData->mIrCount*ti + azd.mIndex)*hData->mIrSize,
                        hData->mIrSize);
            }
        }

        for(auto ei=hData->mFds[fi].mEvStart;ei < hData->mFds[fi].mEvs.size();++ei)
            hrir_total += hData->mFds[fi].mEvs[ei].mAzs.size() * channels;
    }

    std::atomic<size_t> hrir_done{0};
    auto onset_proc = [hData,channels,&hrir_done]() -> bool
    {
        /* Temporary buffer used to calculate the IR's onset. */
        auto upsampled = std::vector<double>(size_t{OnsetRateMultiple} * hData->mIrPoints);
        /* This resampler is used to help detect the response onset. */
        PPhaseResampler rs;
        rs.init(hData->mIrRate, OnsetRateMultiple*hData->mIrRate);

        for(auto &field : hData->mFds)
        {
            for(auto &elev : field.mEvs.subspan(field.mEvStart))
            {
                for(auto &azd : elev.mAzs)
                {
                    for(auto ti=0u;ti < channels;++ti)
                    {
                        hrir_done.fetch_add(1u, std::memory_order_acq_rel);
                        azd.mDelays[ti] += CalcHrirOnset(rs, hData->mIrRate, upsampled,
                            azd.mIrs[ti].first(hData->mIrPoints));
                    }
                }
            }
        }
        return true;
    };

    std::future_status load_status{};
    auto load_future = std::async(std::launch::async, onset_proc);
    do {
        load_status = load_future.wait_for(std::chrono::milliseconds{50});
        fmt::print("\rCalculating HRIR onsets... {} of {}", hrir_done.load(), hrir_total);
        std::cout.flush();
    } while(load_status != std::future_status::ready);
    fmt::println("");
    if(!load_future.get())
        return false;

    MagCalculator calculator{hData->mFftSize, hData->mIrPoints};
    for(auto &field : hData->mFds)
    {
        for(auto &elev : field.mEvs.subspan(field.mEvStart))
        {
            for(auto &azd : elev.mAzs)
            {
                for(auto ti=0u;ti < channels;++ti)
                    calculator.mIrs.push_back(azd.mIrs[ti]);
            }
        }
    }

    std::vector<std::thread> thrds;
    thrds.reserve(numThreads);
    for(size_t i{0};i < numThreads;++i)
        thrds.emplace_back(&MagCalculator::Worker, &calculator);
    size_t count;
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        count = calculator.mDone.load();

        fmt::print("\rCalculating HRIR magnitudes... {} of {}", count, calculator.mIrs.size());
        std::cout.flush();
    } while(count != calculator.mIrs.size());
    fmt::println("");

    for(auto &thrd : thrds)
    {
        if(thrd.joinable())
            thrd.join();
    }
    return true;
}
