
#include "config.h"

#include "hrtf.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "almalloc.h"
#include "alnumeric.h"
#include "alstring.h"
#include "ambidefs.h"
#include "filters/splitter.h"
#include "fmt/core.h"
#include "gsl/gsl"
#include "helpers.h"
#include "hrtf_loader.hpp"
#include "hrtf_resource.hpp"
#include "logging.h"
#include "mixer/hrtfdefs.h"
#include "polyphase_resampler.h"


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

struct LoadedHrtf {
    std::string mFilename;
    uint mSampleRate{};
    std::unique_ptr<HrtfStore> mEntry;

    template<typename T, typename U>
    LoadedHrtf(T&& name, uint srate, U&& entry)
        : mFilename{std::forward<T>(name)}, mSampleRate{srate}, mEntry{std::forward<U>(entry)}
    { }
    LoadedHrtf(LoadedHrtf&&) = default;
    /* GCC warns when it tries to inline this. */
    NOINLINE ~LoadedHrtf() = default;

    LoadedHrtf& operator=(LoadedHrtf&&) = default;
};


/* First value for pass-through coefficients (remaining are 0), used for omni-
 * directional sounds. */
constexpr auto PassthruCoeff = gsl::narrow_cast<float>(1.0/std::numbers::sqrt2);

auto LoadedHrtfLock = std::mutex{};
auto LoadedHrtfs = std::vector<LoadedHrtf>{};

auto EnumeratedHrtfLock = std::mutex{};
auto EnumeratedHrtfs = std::vector<HrtfEntry>{};


/* NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
 * To access a memory buffer through the std::istream interface, a custom
 * std::streambuf implementation is needed that has to do pointer manipulation
 * for seeking. With C++23, we may be able to use std::spanstream instead.
 */
class databuf final : public std::streambuf {
    auto underflow() -> int_type final { return traits_type::eof(); }

    auto seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode)
        -> pos_type final
    {
        if((mode&std::ios_base::out) || !(mode&std::ios_base::in))
            return traits_type::eof();

        switch(whence)
        {
        case std::ios_base::beg:
            if(offset < 0 || offset > egptr()-eback())
                return traits_type::eof();
            setg(eback(), eback()+offset, egptr());
            break;

        case std::ios_base::cur:
            if((offset >= 0 && offset > egptr()-gptr()) ||
                (offset < 0 && -offset > gptr()-eback()))
                return traits_type::eof();
            setg(eback(), gptr()+offset, egptr());
            break;

        case std::ios_base::end:
            if(offset > 0 || -offset > egptr()-eback())
                return traits_type::eof();
            setg(eback(), egptr()+offset, egptr());
            break;

        default:
            return traits_type::eof();
        }

        return gptr() - eback();
    }

    auto seekpos(pos_type pos, std::ios_base::openmode mode) -> pos_type final
    {
        // Simplified version of seekoff
        if((mode&std::ios_base::out) || !(mode&std::ios_base::in))
            return traits_type::eof();

        if(pos < 0 || pos > egptr()-eback())
            return traits_type::eof();

        setg(eback(), eback()+gsl::narrow_cast<size_t>(pos), egptr());
        return pos;
    }

public:
    explicit databuf(const std::span<char_type> data) noexcept
    {
        setg(data.data(), data.data(), std::to_address(data.end()));
    }
};
/* NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic) */

class idstream final : public std::istream {
    databuf mStreamBuf;

public:
    explicit idstream(const std::span<char_type> data) : std::istream{nullptr}, mStreamBuf{data}
    { init(&mStreamBuf); }
};


struct IdxBlend { uint idx; float blend; };
/* Calculate the elevation index given the polar elevation in radians. This
 * will return an index between 0 and (evcount - 1).
 */
auto CalcEvIndex(uint evcount, float ev) -> IdxBlend
{
    ev = (std::numbers::inv_pi_v<float>*ev + 0.5f) * gsl::narrow_cast<float>(evcount-1);

    const auto idx = float2uint(ev);
    return IdxBlend{std::min(idx, evcount-1u), ev-gsl::narrow_cast<float>(idx)};
}

/* Calculate the azimuth index given the polar azimuth in radians. This will
 * return an index between 0 and (azcount - 1).
 */
auto CalcAzIndex(uint azcount, float az) -> IdxBlend
{
    az = (std::numbers::inv_pi_v<float>*0.5f*az + 1.0f) * gsl::narrow_cast<float>(azcount);

    const auto idx = float2uint(az);
    return IdxBlend{idx%azcount, az-gsl::narrow_cast<float>(idx)};
}

} // namespace


/* Calculates static HRIR coefficients and delays for the given polar elevation
 * and azimuth in radians. The coefficients are normalized.
 */
void HrtfStore::getCoeffs(float elevation, float azimuth, float distance, float spread,
    const HrirSpan coeffs, const std::span<uint,2> delays) const
{
    const auto dirfact = 1.0f - (std::numbers::inv_pi_v<float>/2.0f * spread);

    auto ebase = 0_uz;
    const auto field = std::ranges::find_if(mFields | std::views::take(mFields.size()-1),
        [&ebase,distance](const Field &fd) noexcept -> bool
    {
        if(distance >= fd.distance)
            return true;
        ebase += fd.evCount;
        return false;
    });

    /* Calculate the elevation indices. */
    const auto elev0 = CalcEvIndex(field->evCount, elevation);
    const auto elev1_idx = size_t{std::min(elev0.idx+1u, field->evCount-1u)};
    const auto ir0offset = size_t{mElev[ebase + elev0.idx].irOffset};
    const auto ir1offset = size_t{mElev[ebase + elev1_idx].irOffset};

    /* Calculate azimuth indices. */
    const auto az0 = CalcAzIndex(mElev[ebase + elev0.idx].azCount, azimuth);
    const auto az1 = CalcAzIndex(mElev[ebase + elev1_idx].azCount, azimuth);

    /* Calculate the HRIR indices to blend. */
    const auto idx = std::array{
        ir0offset + az0.idx,
        ir0offset + ((az0.idx+1) % mElev[ebase + elev0.idx].azCount),
        ir1offset + az1.idx,
        ir1offset + ((az1.idx+1) % mElev[ebase + elev1_idx].azCount)};

    /* Calculate bilinear blending weights, attenuated according to the
     * directional panning factor.
     */
    const auto blend = std::array{
        (1.0f-elev0.blend) * (1.0f-az0.blend) * dirfact,
        (1.0f-elev0.blend) * (     az0.blend) * dirfact,
        (     elev0.blend) * (1.0f-az1.blend) * dirfact,
        (     elev0.blend) * (     az1.blend) * dirfact};

    /* Calculate the blended HRIR delays. */
    auto d = gsl::narrow_cast<float>(mDelays[idx[0]][0])*blend[0]
        + gsl::narrow_cast<float>(mDelays[idx[1]][0])*blend[1]
        + gsl::narrow_cast<float>(mDelays[idx[2]][0])*blend[2]
        + gsl::narrow_cast<float>(mDelays[idx[3]][0])*blend[3];
    delays[0] = fastf2u(d * float{1.0f/HrirDelayFracOne});

    d = gsl::narrow_cast<float>(mDelays[idx[0]][1])*blend[0]
        + gsl::narrow_cast<float>(mDelays[idx[1]][1])*blend[1]
        + gsl::narrow_cast<float>(mDelays[idx[2]][1])*blend[2]
        + gsl::narrow_cast<float>(mDelays[idx[3]][1])*blend[3];
    delays[1] = fastf2u(d * float{1.0f/HrirDelayFracOne});

    /* Calculate the blended HRIR coefficients. */
    coeffs[0][0] = PassthruCoeff * (1.0f-dirfact);
    coeffs[0][1] = PassthruCoeff * (1.0f-dirfact);
    std::ranges::fill(coeffs | std::views::drop(1) | std::views::join, 0.0f);
    for(auto c = 0_uz;c < 4;c++)
    {
        const auto joined_coeffs = coeffs | std::views::join;
        std::ranges::transform(mCoeffs[idx[c]] | std::views::join, joined_coeffs,
            joined_coeffs.begin(), [mult = blend[c]](const float src, const float coeff) -> float
        {
            return src*mult + coeff;
        });
    }
}


auto DirectHrtfState::Create(size_t num_chans) -> std::unique_ptr<DirectHrtfState>
{ return std::unique_ptr<DirectHrtfState>{new(FamCount{num_chans}) DirectHrtfState{num_chans}}; }

void DirectHrtfState::build(const HrtfStore *Hrtf, const uint irSize, const bool perHrirMin,
    const std::span<const AngularPoint> AmbiPoints,
    const std::span<const std::array<float,MaxAmbiChannels>> AmbiMatrix,
    const float XOverFreq, const std::span<const float,MaxAmbiOrder+1> AmbiOrderHFGain)
{
    using double2 = std::array<double,2>;
    struct ImpulseResponse {
        ConstHrirSpan hrir;
        uint ldelay, rdelay;
    };

    const auto xover_norm = double{XOverFreq} / Hrtf->mSampleRate;
    mChannels[0].mSplitter.init(gsl::narrow_cast<float>(xover_norm));
    std::ranges::fill(mChannels | std::views::transform(&HrtfChannelState::mSplitter)
        | std::views::drop(1), mChannels[0].mSplitter);

    std::ranges::transform(std::views::iota(0_uz, mChannels.size()), (mChannels
        | std::views::transform(&HrtfChannelState::mHfScale)).begin(),
        [AmbiOrderHFGain](const size_t idx)
    {
        const auto order = AmbiIndex::OrderFromChannel[idx];
        return AmbiOrderHFGain[order];
    });

    auto min_delay = uint{HrtfHistoryLength*HrirDelayFracOne};
    auto max_delay = 0u;
    auto impulses = std::vector<ImpulseResponse>{};
    impulses.reserve(AmbiPoints.size());
    std::ranges::transform(AmbiPoints, std::back_inserter(impulses),
        [Hrtf,&max_delay,&min_delay](const AngularPoint &pt) -> ImpulseResponse
    {
        auto &field = Hrtf->mFields[0];
        const auto elev0 = CalcEvIndex(field.evCount, pt.Elev.value);
        const auto elev1_idx = std::min(elev0.idx+1_uz, field.evCount-1_uz);
        const auto ir0offset = Hrtf->mElev[elev0.idx].irOffset;
        const auto ir1offset = Hrtf->mElev[elev1_idx].irOffset;

        const auto az0 = CalcAzIndex(Hrtf->mElev[elev0.idx].azCount, pt.Azim.value);
        const auto az1 = CalcAzIndex(Hrtf->mElev[elev1_idx].azCount, pt.Azim.value);

        const auto idx = std::array{
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

    static constexpr auto hrir_delay_round = [](const uint d) noexcept -> uint
    { return (d+HrirDelayFracHalf) >> HrirDelayFracBits; };

    auto tmpres = std::vector<std::array<double2,HrirLength>>(mChannels.size());
    max_delay = 0;
    std::ignore = std::ranges::mismatch(impulses, AmbiMatrix,
        [perHrirMin,min_delay,&max_delay,&tmpres](const ImpulseResponse &impulse,
            const std::span<const float,MaxAmbiChannels> matrixline)
    {
        const auto hrir = impulse.hrir;
        const auto base_delay = perHrirMin ? std::min(impulse.ldelay, impulse.rdelay) : min_delay;
        const auto ldelay = hrir_delay_round(impulse.ldelay - base_delay);
        const auto rdelay = hrir_delay_round(impulse.rdelay - base_delay);
        max_delay = std::max(max_delay, std::max(impulse.ldelay, impulse.rdelay) - base_delay);

        std::ignore = std::ranges::mismatch(tmpres, matrixline,
            [hrir,ldelay,rdelay](std::array<double2,HrirLength> &result, const double mult)
        {
            const auto lresult = result | std::views::drop(ldelay) | std::views::elements<0>;
            std::ranges::transform(hrir | std::views::elements<0>, lresult, lresult.begin(),
                std::plus{}, [mult](const double coeff) noexcept { return coeff*mult; });

            const auto rresult = result | std::views::drop(rdelay) | std::views::elements<1>;
            std::ranges::transform(hrir | std::views::elements<1>, rresult, rresult.begin(),
                std::plus{}, [mult](const double coeff) noexcept { return coeff*mult; });
            return true;
        });
        return true;
    });
    impulses.clear();

    const auto join_join = std::views::join | std::views::join;
    std::ignore = std::ranges::transform(tmpres | join_join,
        (mChannels | std::views::transform(&HrtfChannelState::mCoeffs) | join_join).begin(),
        [](const double in) noexcept { return gsl::narrow_cast<float>(in); });
    tmpres.clear();

    const auto max_length = std::min(hrir_delay_round(max_delay) + irSize, HrirLength);
    TRACE("New max delay: {:.2f}, FIR length: {}", max_delay/double{HrirDelayFracOne}, max_length);
    mIrSize = max_length;
}


namespace {

auto checkName(const std::string_view name) -> bool
{
    using std::ranges::find;
    return find(EnumeratedHrtfs, name, &HrtfEntry::mDispName) != EnumeratedHrtfs.end();
}

void AddFileEntry(const std::string_view filename)
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
    const auto namepos = std::max(filename.rfind('/')+1, filename.rfind('\\')+1);
    const auto extpos = filename.substr(namepos).rfind('.');

    const auto basename = (extpos == std::string_view::npos) ?
        filename.substr(namepos) : filename.substr(namepos, extpos);

    auto count = 1;
    auto newname = std::string{basename};
    while(checkName(newname))
        newname = fmt::format("{} #{}", basename, ++count);

    const auto &entry = EnumeratedHrtfs.emplace_back(newname, filename);
    TRACE("Adding file entry \"{}\"", entry.mFilename);
}

/* Unfortunate that we have to duplicate AddFileEntry to take a memory buffer
 * for input instead of opening the given filename.
 */
void AddBuiltInEntry(const std::string_view dispname, uint residx)
{
    auto filename = fmt::format("!{}_{}", residx, dispname);

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
        newname = fmt::format("{} #{}", dispname, ++count);

    const auto &entry = EnumeratedHrtfs.emplace_back(std::move(newname), std::move(filename));
    TRACE("Adding built-in entry \"{}\"", entry.mFilename);
}

} // namespace


auto EnumerateHrtf(std::optional<std::string> pathopt) -> std::vector<std::string>
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

auto GetLoadedHrtf(const std::string_view name, const uint devrate) -> HrtfStorePtr
try {
    if(devrate > MaxHrtfSampleRate)
    {
        WARN("Device sample rate too large for HRTF ({}hz > {}hz)", devrate, MaxHrtfSampleRate);
        return nullptr;
    }
    const auto fname = std::invoke([name]() -> std::string
    {
        auto enumlock = std::lock_guard{EnumeratedHrtfLock};
        auto entry_iter = std::ranges::find(EnumeratedHrtfs, name, &HrtfEntry::mDispName);
        if(entry_iter == EnumeratedHrtfs.cend())
            return std::string{};
        return entry_iter->mFilename;
    });
    if(fname.empty())
        return nullptr;

    auto loadlock = std::lock_guard{LoadedHrtfLock};
    auto handle = std::lower_bound(LoadedHrtfs.begin(), LoadedHrtfs.end(), fname,
        [devrate](LoadedHrtf &hrtf, const std::string &filename) -> bool
    {
        return hrtf.mSampleRate < devrate
            || (hrtf.mSampleRate == devrate && hrtf.mFilename < filename);
    });
    if(handle != LoadedHrtfs.end() && handle->mSampleRate == devrate && handle->mFilename == fname)
    {
        if(auto *hrtf = handle->mEntry.get())
        {
            Expects(hrtf->mSampleRate == devrate);
            hrtf->inc_ref();
            return HrtfStorePtr{hrtf};
        }
    }

    auto stream = std::unique_ptr<std::istream>{};
    auto residx = int{};
    auto ch = char{};
    /* NOLINTNEXTLINE(cert-err34-c,cppcoreguidelines-pro-type-vararg) */
    if(sscanf(fname.c_str(), "!%d%c", &residx, &ch) == 2 && ch == '_')
    {
        TRACE("Loading built-in HRTF {}...", residx);
        auto res = GetHrtfResource(residx);
        if(res.empty())
        {
            ERR("Could not get resource {}, {}", residx, name);
            return nullptr;
        }
        /* NOLINTNEXTLINE(*-const-cast) */
        stream = std::make_unique<idstream>(std::span{const_cast<char*>(res.data()), res.size()});
    }
    else
    {
        TRACE("Loading {}...", fname);
        auto fstr = std::make_unique<std::ifstream>(std::filesystem::path(al::char_as_u8(fname)),
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
        TRACE("Resampling HRTF {} ({}hz -> {}hz)", name, uint{hrtf->mSampleRate}, devrate);

        /* Resample all the IRs. */
        auto inout = std::array<std::array<double,HrirLength>,2>{};
        auto rs = PPhaseResampler{};
        rs.init(hrtf->mSampleRate, devrate);
        std::ranges::for_each(hrtf->mCoeffs, [&inout,&rs](const HrirArray &const_coeffs)
        {
            /* NOLINTNEXTLINE(*-const-cast) */
            auto coeffs = std::span{const_cast<HrirArray&>(const_coeffs)};

            auto coeffs0 = coeffs | std::views::elements<0>;
            std::ranges::copy(coeffs0, inout[0].begin());
            rs.process(inout[0], inout[1]);
            std::ranges::transform(inout[1], coeffs0.begin(),
                [](const double d) noexcept { return gsl::narrow_cast<float>(d); });

            auto coeffs1 = coeffs | std::views::elements<1>;
            std::ranges::copy(coeffs1, inout[0].begin());
            rs.process(inout[0], inout[1]);
            std::ranges::transform(inout[1], coeffs1.begin(),
                [](const double d) noexcept { return gsl::narrow_cast<float>(d); });
        });
        rs = {};

        /* Scale the delays for the new sample rate. */
        auto max_delay = 0.0f;
        auto new_delays = std::vector<float>(hrtf->mDelays.size()*2_uz);
        const auto rate_scale = gsl::narrow_cast<float>(devrate)
            / gsl::narrow_cast<float>(uint{hrtf->mSampleRate});
        std::ranges::transform(hrtf->mDelays | std::views::join, new_delays.begin(),
            [&max_delay,rate_scale](const ubyte oldval)
        {
            const auto ret = std::round(gsl::narrow_cast<float>(oldval) * rate_scale)
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
        auto delays = std::span{const_cast<ubyte2*>(hrtf->mDelays.data()), hrtf->mDelays.size()};
        std::ranges::transform(new_delays, (delays | std::views::join).begin(),
            [delay_scale](const float fdelay) -> ubyte
        {
            return gsl::narrow_cast<ubyte>(float2int(fdelay*delay_scale + 0.5f));
        });

        /* Scale the IR size for the new sample rate and update the stored
         * sample rate.
         */
        const auto newIrSize = std::round(gsl::narrow_cast<float>(uint{hrtf->mIrSize})*rate_scale);
        hrtf->mIrSize = gsl::narrow_cast<uint8_t>(std::min(float{HrirLength}, newIrSize));
        hrtf->mSampleRate = devrate & 0xff'ff'ff;
    }

    handle = LoadedHrtfs.emplace(handle, fname, devrate, std::move(hrtf));
    TRACE("Loaded HRTF {} for sample rate {}hz, {}-sample filter", name,
        uint{handle->mEntry->mSampleRate}, uint{handle->mEntry->mIrSize});

    return HrtfStorePtr{handle->mEntry.get()};
}
catch(std::exception& e) {
    ERR("Failed to load {}: {}", name, e.what());
    return nullptr;
}


void HrtfStore::inc_ref() noexcept
{
    const auto ref = mRef.fetch_add(1, std::memory_order_acq_rel)+1;
    TRACE("HrtfStore {} increasing refcount to {}", decltype(std::declval<void*>()){this}, ref);
}

void HrtfStore::dec_ref() noexcept
{
    const auto ref = mRef.fetch_sub(1, std::memory_order_acq_rel)-1;
    TRACE("HrtfStore {} decreasing refcount to {}", decltype(std::declval<void*>()){this}, ref);
    if(ref == 0)
    {
        const auto loadlock = std::lock_guard{LoadedHrtfLock};

        /* Go through and remove all unused HRTFs. */
        auto iter = std::ranges::remove_if(LoadedHrtfs, [](LoadedHrtf &hrtf) -> bool
        {
            if(auto *entry = hrtf.mEntry.get(); entry && entry->mRef.load() == 0)
            {
                TRACE("Unloading unused HRTF {}", hrtf.mFilename);
                hrtf.mEntry = nullptr;
                return true;
            }
            return false;
        });
        LoadedHrtfs.erase(iter.begin(), iter.end());
    }
}
