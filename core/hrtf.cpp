
#include "config.h"

#include "hrtf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <istream>
#include <iterator>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "alnumeric.h"
#include "alstring.h"
#include "ambidefs.h"
#include "filters/splitter.h"
#include "gsl/gsl"
#include "helpers.h"
#include "hrtf_loader.hpp"
#include "hrtf_resource.hpp"
#include "mixer/hrtfdefs.h"
#include "polyphase_resampler.h"
#include "zudl.hpp"

#if HAVE_CXXMODULES
import filesystem;
import format;
import logging;
import spanstream;
import types;
#else
#include "alformat.hpp"
#include "alformattypes.hpp"
#include "filesystem.h"
#include "logging.h"
#include "spanstream.hpp"
#endif


namespace {

using namespace std::string_view_literals;

struct HrtfEntry {
    std::string mDispName;
    std::string mFilename;

    template<typename T, typename U>
    HrtfEntry(T&& dispname, U&& fname)
        : mDispName{std::forward<T>(dispname)}, mFilename{std::forward<U>(fname)}
    { }
    /* GCC warns when it tries to inline this. */
    NOINLINE ~HrtfEntry() = default;
};


/* First value for pass-through coefficients (remaining are 0), used for omni-
 * directional sounds. */
constexpr auto PassthruCoeff = gsl::narrow_cast<float>(1.0/std::numbers::sqrt2);

auto LoadedHrtfLock = std::mutex{};
auto LoadedHrtfs = std::vector<std::unique_ptr<HrtfStore>>{};

auto EnumeratedHrtfLock = std::mutex{};
auto EnumeratedHrtfs = std::vector<HrtfEntry>{};


struct IdxBlend { unsigned idx; float blend; };
/* Calculate the elevation index given the polar elevation in radians. This
 * will return an index between 0 and (evcount - 1).
 */
auto CalcEvIndex(unsigned evcount, float ev) -> IdxBlend
{
    ev = (std::numbers::inv_pi_v<float>*ev + 0.5f) * gsl::narrow_cast<float>(evcount-1);

    const auto idx = float2uint(ev);
    return IdxBlend{std::min(idx, evcount-1u), ev-gsl::narrow_cast<float>(idx)};
}

/* Calculate the azimuth index given the polar azimuth in radians. This will
 * return an index between 0 and (azcount - 1).
 */
auto CalcAzIndex(unsigned azcount, float az) -> IdxBlend
{
    az = (std::numbers::inv_pi_v<float>*0.5f*az + 1.0f) * gsl::narrow_cast<float>(azcount);

    const auto idx = float2uint(az);
    return IdxBlend{idx%azcount, az-gsl::narrow_cast<float>(idx)};
}

} // namespace


/* Calculates static HRIR coefficients and delays for the given polar elevation
 * and azimuth in radians. The coefficients are normalized.
 */
void HrtfStore::getCoeffs(float const elevation, float const azimuth, float const distance,
    float const spread, HrirSpan const coeffs, std::span<unsigned, 2> const delays) const noexcept
    NONBLOCKING
{
    auto const dirfact = 1.0f - (std::numbers::inv_pi_v<float>/2.0f * spread);

    auto ebase = 0_uz;
    auto const field = std::ranges::find_if(mFields.begin(), mFields.end()-1,
        [&ebase,distance](Field const &fd) noexcept -> bool
    {
        if(distance >= fd.distance)
            return true;
        ebase += fd.evCount;
        return false;
    });

    /* Calculate the elevation indices. */
    auto const elev0 = CalcEvIndex(field->evCount, elevation);
    auto const elev1_idx = std::size_t{std::min(elev0.idx+1u, field->evCount-1u)};
    auto const ir0offset = std::size_t{mElev[ebase + elev0.idx].irOffset};
    auto const ir1offset = std::size_t{mElev[ebase + elev1_idx].irOffset};

    /* Calculate azimuth indices. */
    auto const az0 = CalcAzIndex(mElev[ebase + elev0.idx].azCount, azimuth);
    auto const az1 = CalcAzIndex(mElev[ebase + elev1_idx].azCount, azimuth);

    /* Calculate the HRIR indices to blend. */
    auto const idx = std::array{
        ir0offset + az0.idx,
        ir0offset + ((az0.idx+1) % mElev[ebase + elev0.idx].azCount),
        ir1offset + az1.idx,
        ir1offset + ((az1.idx+1) % mElev[ebase + elev1_idx].azCount)};

    /* Calculate bilinear blending weights, attenuated according to the
     * directional panning factor.
     */
    auto const blend = std::array{
        (1.0f-elev0.blend) * (1.0f-az0.blend) * dirfact,
        (1.0f-elev0.blend) * (     az0.blend) * dirfact,
        (     elev0.blend) * (1.0f-az1.blend) * dirfact,
        (     elev0.blend) * (     az1.blend) * dirfact};

    /* Calculate the blended HRIR delays. */
    auto d = mDelays[idx[0]][0]*blend[0]
        + mDelays[idx[1]][0]*blend[1]
        + mDelays[idx[2]][0]*blend[2]
        + mDelays[idx[3]][0]*blend[3];
    delays[0] = fastf2u(d * float{1.0f/HrirDelayFracOne});

    d = mDelays[idx[0]][1]*blend[0]
        + mDelays[idx[1]][1]*blend[1]
        + mDelays[idx[2]][1]*blend[2]
        + mDelays[idx[3]][1]*blend[3];
    delays[1] = fastf2u(d * float{1.0f/HrirDelayFracOne});

    /* Calculate the blended HRIR coefficients. */
    coeffs[0][0] = PassthruCoeff * (1.0f-dirfact);
    coeffs[0][1] = PassthruCoeff * (1.0f-dirfact);
    std::ranges::fill(coeffs | std::views::drop(1) | std::views::join, 0.0f);
    for(auto c = 0_uz;c < 4;c++)
    {
        const auto joined_coeffs = coeffs | std::views::join;
        std::ranges::transform(mCoeffs[idx[c]] | std::views::join, joined_coeffs,
            joined_coeffs.begin(), [mult = blend[c]](float const src, float const coeff) -> float
        {
            return src*mult + coeff;
        });
    }
}


auto DirectHrtfState::Create(std::size_t const num_chans) -> std::unique_ptr<DirectHrtfState>
{ return std::unique_ptr<DirectHrtfState>{new(FamCount{num_chans}) DirectHrtfState{num_chans}}; }

void DirectHrtfState::build(HrtfStore const *const Hrtf, unsigned const irSize,
    bool const perHrirMin, std::span<AngularPoint const> const AmbiPoints,
    std::span<std::array<float, MaxAmbiChannels> const> const AmbiMatrix,
    float const XOverFreq, std::span<float const, MaxAmbiOrder+1> const AmbiOrderHFGain)
{
    using f64x2 = std::array<double, 2>;
    struct ImpulseResponse {
        ConstHrirSpan hrir;
        unsigned ldelay, rdelay;
    };

    auto const xover_norm = double{XOverFreq} / Hrtf->mSampleRate;
    mChannels[0].mSplitter.init(gsl::narrow_cast<float>(xover_norm));
    std::ranges::fill(mChannels | std::views::transform(&HrtfChannelState::mSplitter)
        | std::views::drop(1), mChannels[0].mSplitter);

    std::ranges::transform(std::views::iota(0_uz, mChannels.size()), (mChannels
        | std::views::transform(&HrtfChannelState::mHfScale)).begin(),
        [AmbiOrderHFGain](std::size_t const idx)
    { return AmbiOrderHFGain[AmbiIndex::OrderFromChannel[idx]]; });

    auto min_delay = unsigned{HrtfHistoryLength * HrirDelayFracOne};
    auto max_delay = 0u;
    auto impulses = std::vector<ImpulseResponse>{};
    impulses.reserve(AmbiPoints.size());
    std::ranges::transform(AmbiPoints, std::back_inserter(impulses),
        [Hrtf,&max_delay,&min_delay](AngularPoint const &pt) -> ImpulseResponse
    {
        auto const &field = Hrtf->mFields[0];
        auto const elev0 = CalcEvIndex(field.evCount, pt.Elev.value);
        auto const elev1_idx = std::min(elev0.idx+1_uz, field.evCount-1_uz);
        auto const ir0offset = Hrtf->mElev[elev0.idx].irOffset;
        auto const ir1offset = Hrtf->mElev[elev1_idx].irOffset;

        auto const az0 = CalcAzIndex(Hrtf->mElev[elev0.idx].azCount, pt.Azim.value);
        auto const az1 = CalcAzIndex(Hrtf->mElev[elev1_idx].azCount, pt.Azim.value);

        auto const idx = std::array{
            ir0offset + az0.idx,
            ir0offset + ((az0.idx+1) % Hrtf->mElev[elev0.idx].azCount),
            ir1offset + az1.idx,
            ir1offset + ((az1.idx+1) % Hrtf->mElev[elev1_idx].azCount)};

        /* The largest blend factor serves as the closest HRIR. */
        const auto irOffset = idx[(elev0.blend >= 0.5f)*2_uz + (az1.blend >= 0.5f)];
        const auto res = ImpulseResponse{.hrir=Hrtf->mCoeffs[irOffset],
            .ldelay=Hrtf->mDelays[irOffset][0], .rdelay=Hrtf->mDelays[irOffset][1]};

        min_delay = std::min(min_delay, std::min(res.ldelay, res.rdelay));
        max_delay = std::max(max_delay, std::max(res.ldelay, res.rdelay));

        return res;
    });

    TRACE("Min delay: {:.2f}, max delay: {:.2f}, FIR length: {}",
        min_delay/double{HrirDelayFracOne}, max_delay/double{HrirDelayFracOne}, irSize);

    static constexpr auto hrir_delay_round = [](unsigned const d) noexcept -> unsigned
    { return (d+HrirDelayFracHalf) >> HrirDelayFracBits; };

    auto tmpres = std::vector<std::array<f64x2, HrirLength>>(mChannels.size());
    max_delay = 0;
    std::ignore = std::ranges::mismatch(impulses, AmbiMatrix,
        [perHrirMin,min_delay,&max_delay,&tmpres](ImpulseResponse const &impulse,
            std::span<float const, MaxAmbiChannels> const matrixline)
    {
        auto const hrir = impulse.hrir;
        auto const base_delay = perHrirMin ? std::min(impulse.ldelay, impulse.rdelay) : min_delay;
        auto const ldelay = hrir_delay_round(impulse.ldelay - base_delay);
        auto const rdelay = hrir_delay_round(impulse.rdelay - base_delay);
        max_delay = std::max(max_delay, std::max(impulse.ldelay, impulse.rdelay) - base_delay);

        std::ignore = std::ranges::mismatch(tmpres, matrixline,
            [hrir,ldelay,rdelay](std::array<f64x2, HrirLength> &result, double const mult)
        {
            auto const lresult = result | std::views::drop(ldelay) | std::views::elements<0>;
            std::ranges::transform(hrir | std::views::elements<0>, lresult, lresult.begin(),
                std::plus{}, [mult](double const coeff) noexcept { return coeff*mult; });

            auto const rresult = result | std::views::drop(rdelay) | std::views::elements<1>;
            std::ranges::transform(hrir | std::views::elements<1>, rresult, rresult.begin(),
                std::plus{}, [mult](double const coeff) noexcept { return coeff*mult; });
            return true;
        });
        return true;
    });
    impulses.clear();

    constexpr auto join_join = std::views::join | std::views::join;
    std::ignore = std::ranges::transform(tmpres | join_join,
        (mChannels | std::views::transform(&HrtfChannelState::mCoeffs) | join_join).begin(),
        [](double const in) noexcept { return gsl::narrow_cast<float>(in); });
    tmpres.clear();

    const auto max_length = std::min(hrir_delay_round(max_delay) + irSize, HrirLength);
    TRACE("New max delay: {:.2f}, FIR length: {}", max_delay/double{HrirDelayFracOne}, max_length);
    mIrSize = max_length;
}


namespace {

auto checkName(std::string_view const name) -> bool
{
    using std::ranges::find;
    return find(EnumeratedHrtfs, name, &HrtfEntry::mDispName) != EnumeratedHrtfs.end();
}

void AddFileEntry(std::string_view const filename)
{
    /* Check if this file has already been enumerated. */
    if(std::ranges::find(EnumeratedHrtfs, filename,&HrtfEntry::mFilename) != EnumeratedHrtfs.end())
    {
        TRACE("Skipping duplicate file entry {}", filename);
        return;
    }

    /* TODO: Get a human-readable name from the HRTF data (possibly coming in a
     * format update).
     */
    auto const namepos = std::max(filename.rfind('/')+1, filename.rfind('\\')+1);
    auto const extpos = filename.substr(namepos).rfind('.');

    auto const basename = (extpos == std::string_view::npos) ?
        filename.substr(namepos) : filename.substr(namepos, extpos);

    auto count = 1;
    auto newname = std::string{basename};
    while(checkName(newname))
        newname = al::format("{} #{}", basename, ++count);

    auto const &entry = EnumeratedHrtfs.emplace_back(newname, filename);
    TRACE("Adding file entry \"{}\"", entry.mFilename);
}

/* Unfortunate that we have to duplicate AddFileEntry to take a memory buffer
 * for input instead of opening the given filename.
 */
void AddBuiltInEntry(std::string_view const dispname, unsigned const residx)
{
    auto filename = al::format("!{}_{}", residx, dispname);

    if(std::ranges::find(EnumeratedHrtfs, filename,&HrtfEntry::mFilename) != EnumeratedHrtfs.end())
    {
        TRACE("Skipping duplicate file entry {}", filename);
        return;
    }

    /* TODO: Get a human-readable name from the HRTF data (possibly coming in a
     * format update). */

    auto count = 1;
    auto newname = std::string{dispname};
    while(checkName(newname))
        newname = al::format("{} #{}", dispname, ++count);

    auto const &entry = EnumeratedHrtfs.emplace_back(std::move(newname), std::move(filename));
    TRACE("Adding built-in entry \"{}\"", entry.mFilename);
}

} // namespace


auto EnumerateHrtf(std::optional<std::string> const &pathopt) -> std::vector<std::string>
{
    auto enumlock = std::lock_guard{EnumeratedHrtfLock};
    EnumeratedHrtfs.clear();

    std::ranges::for_each(SearchDataFiles(".mhr"sv), AddFileEntry);

    auto usedefaults = true;
    if(pathopt)
    {
        std::ranges::for_each(*pathopt | std::views::split(','), [&usedefaults](auto&& range)
        {
            auto entry = std::string_view{range.begin(), range.end()};
            constexpr auto wspace_chars = " \t\n\f\r\v"sv;
            entry.remove_prefix(std::min(entry.find_first_not_of(wspace_chars), entry.size()));
            entry.remove_suffix(entry.size() - (entry.find_last_not_of(wspace_chars)+1));
            if(entry.empty())
            {
                usedefaults = true;
                return;
            }
            usedefaults = false;
            std::ranges::for_each(SearchDataFiles(".mhr"sv, entry), AddFileEntry);
        });
    }

    if(usedefaults)
    {
        std::ranges::for_each(SearchDataFiles(".mhr"sv, "openal/hrtf"sv), AddFileEntry);
        if(!GetHrtfResource(DefaultHrtfResourceID).empty())
            AddBuiltInEntry("Built-In HRTF", DefaultHrtfResourceID);
    }

    auto list = std::vector<std::string>(EnumeratedHrtfs.size());
    std::ranges::transform(EnumeratedHrtfs, list.begin(), &HrtfEntry::mDispName);

    return list;
}

auto GetLoadedHrtf(std::string_view const name, unsigned const devrate) -> HrtfStorePtr
try {
    if(devrate > MaxHrtfSampleRate)
    {
        WARN("Device sample rate too large for HRTF ({}hz > {}hz)", devrate, MaxHrtfSampleRate);
        return nullptr;
    }
    auto const fname = std::invoke([name]() -> std::string
    {
        auto const enumlock = std::lock_guard{EnumeratedHrtfLock};
        auto const entry_iter = std::ranges::find(EnumeratedHrtfs, name, &HrtfEntry::mDispName);
        if(entry_iter == EnumeratedHrtfs.cend())
            return std::string{};
        return entry_iter->mFilename;
    });
    if(fname.empty())
        return nullptr;

    auto const loadlock = std::lock_guard{LoadedHrtfLock};
    auto handle = std::lower_bound(LoadedHrtfs.begin(), LoadedHrtfs.end(), fname,
        [devrate](std::unique_ptr<HrtfStore> &hrtf, std::string_view const filename) -> bool
    {
        return hrtf->mSampleRate < devrate
            || (hrtf->mSampleRate == devrate && hrtf->mFilename < filename);
    });
    if(handle != LoadedHrtfs.end() && (*handle)->mSampleRate == devrate
        && (*handle)->mFilename == fname)
    {
        (*handle)->inc_ref();
        return HrtfStorePtr{std::to_address(*handle)};
    }

    auto builtin_name = [](al::ispanstream stream) -> std::optional<int>
    {
        if(stream.get() != '!')
            return std::nullopt;
        auto residx = int{};
        if((stream >> residx).get() != '_')
            return std::nullopt;
        return residx;
    };
    auto stream = std::unique_ptr<std::istream>{};
    if(auto residx = builtin_name(al::ispanstream{fname}); residx.has_value())
    {
        TRACE("Loading built-in HRTF {}...", *residx);
        auto const res = GetHrtfResource(*residx);
        if(res.empty())
        {
            ERR("Could not get resource {}, {}", *residx, name);
            return nullptr;
        }
        auto spstream = std::make_unique<al::ispanstream>(std::span<char>{}, std::ios::binary);
        spstream->span(res);
        stream = std::move(spstream);
    }
    else
    {
        TRACE("Loading {}...", fname);
        auto fstr = std::make_unique<fs::ifstream>(fs::path(al::char_as_u8(fname)),
            std::ios::binary);
        if(!fstr->is_open())
        {
            ERR("Could not open {}", fname);
            return nullptr;
        }
        stream = std::move(fstr);
    }

    auto hrtf = LoadHrtf(*stream);
    stream.reset();
    Expects(hrtf != nullptr);

    if(hrtf->mSampleRate != devrate)
    {
        TRACE("Resampling HRTF {} ({}hz -> {}hz)", name, unsigned{hrtf->mSampleRate}, devrate);

        /* Resample all the IRs. */
        auto inout = std::array<std::array<double, HrirLength>, 2>{};
        auto rs = PPhaseResampler{};
        rs.init(hrtf->mSampleRate, devrate);
        std::ranges::for_each(hrtf->mCoeffs, [&inout,&rs](HrirArray const &const_coeffs)
        {
            /* NOLINTNEXTLINE(*-const-cast) */
            auto coeffs = std::span{const_cast<HrirArray&>(const_coeffs)};

            auto coeffs0 = coeffs | std::views::elements<0>;
            std::ranges::copy(coeffs0, inout[0].begin());
            rs.process(inout[0], inout[1]);
            std::ranges::transform(inout[1], coeffs0.begin(),
                [](double const d) noexcept { return gsl::narrow_cast<float>(d); });

            auto coeffs1 = coeffs | std::views::elements<1>;
            std::ranges::copy(coeffs1, inout[0].begin());
            rs.process(inout[0], inout[1]);
            std::ranges::transform(inout[1], coeffs1.begin(),
                [](double const d) noexcept { return gsl::narrow_cast<float>(d); });
        });
        rs = {};

        /* Scale the delays for the new sample rate. */
        auto max_delay = 0.0f;
        auto new_delays = std::vector<float>(hrtf->mDelays.size()*2);
        auto const rate_scale = gsl::narrow_cast<float>(devrate)
            / gsl::narrow_cast<float>(unsigned{hrtf->mSampleRate});
        std::ranges::transform(hrtf->mDelays | std::views::join, new_delays.begin(),
            [&max_delay,rate_scale](u8 const oldval)
        {
            auto const ret = std::round(gsl::narrow_cast<float>(oldval.c_val) * rate_scale)
                / float{HrirDelayFracOne};
            max_delay = std::max(max_delay, ret);
            return ret;
        });

        /* If the new delays exceed the max, scale it down to fit (essentially
         * shrinking the head radius; not ideal but better than a per-delay
         * clamp).
         */
        auto delay_scale = float{HrirDelayFracOne};
        if(max_delay > MaxHrirDelay)
        {
            WARN("Resampled delay exceeds max ({:.2f} > {})", max_delay, MaxHrirDelay);
            delay_scale *= float{MaxHrirDelay} / max_delay;
        }

        /* NOLINTNEXTLINE(*-const-cast) */
        auto const delays = std::span{const_cast<u8x2*>(hrtf->mDelays.data()),
            hrtf->mDelays.size()};
        std::ranges::transform(new_delays, (delays | std::views::join).begin(),
            [delay_scale](float const fdelay) -> std::uint8_t
        {
            return al::saturate_cast<std::uint8_t>(float2int(fdelay*delay_scale + 0.5f));
        });

        /* Scale the IR size for the new sample rate and update the stored
         * sample rate.
         */
        auto const newIrSize = std::round(gsl::narrow_cast<float>(unsigned{hrtf->mIrSize})
            * rate_scale);
        hrtf->mIrSize = gsl::narrow_cast<u8::value_t>(std::min(float{HrirLength}, newIrSize));
        hrtf->mSampleRate = devrate & 0xff'ff'ff;
    }

    handle = LoadedHrtfs.emplace(handle, std::move(hrtf));
    TRACE("Loaded HRTF {} for sample rate {}hz, {}-sample filter", name,
        unsigned{(*handle)->mSampleRate}, unsigned{(*handle)->mIrSize});

    return HrtfStorePtr{std::to_address(*handle)};
}
catch(std::exception& e) {
    ERR("Failed to load {}: {}", name, e.what());
    return nullptr;
}


void HrtfStore::inc_ref() noexcept
{
    auto const ref = mRef.fetch_add(1, std::memory_order_acq_rel)+1;
    TRACE("HrtfStore {} increasing refcount to {}", decltype(std::declval<void*>()){this}, ref);
}

void HrtfStore::dec_ref() noexcept
{
    auto const ref = mRef.fetch_sub(1, std::memory_order_acq_rel)-1;
    TRACE("HrtfStore {} decreasing refcount to {}", decltype(std::declval<void*>()){this}, ref);
    if(ref == 0)
    {
        auto const loadlock = std::lock_guard{LoadedHrtfLock};

        /* Go through and remove all unused HRTFs. */
        auto unused = std::ranges::remove_if(LoadedHrtfs, [](HrtfStore &hrtf) -> bool
        {
            if(hrtf.mRef.load() == 0)
            {
                TRACE("Unloading unused HRTF {}", hrtf.mFilename);
                return true;
            }
            return false;
        }, al::dereference{});
        LoadedHrtfs.erase(unused.begin(), unused.end());
    }
}
