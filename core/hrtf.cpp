
#include "config.h"

#include "hrtf.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "almalloc.h"
#include "alnumeric.h"
#include "alstring.h"
#include "ambidefs.h"
#include "filesystem.h"
#include "filters/splitter.h"
#include "fmt/core.h"
#include "fmt/ranges.h"
#include "gsl/gsl"
#include "helpers.h"
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
    ~HrtfEntry();
};
HrtfEntry::~HrtfEntry() = default;

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
    ~LoadedHrtf();

    LoadedHrtf& operator=(LoadedHrtf&&) = default;
};
LoadedHrtf::~LoadedHrtf() = default;


/* Data set limits must be the same as or more flexible than those defined in
 * the makemhr utility.
 */
constexpr auto MinFdCount = 1u;
constexpr auto MaxFdCount = 16u;

constexpr auto MinFdDistance = 50u;
constexpr auto MaxFdDistance = 2500u;

constexpr auto MinEvCount = 5u;
constexpr auto MaxEvCount = 181u;

constexpr auto MinAzCount = 1u;
constexpr auto MaxAzCount = 255u;

constexpr auto MaxHrirDelay = uint{HrtfHistoryLength} - 1u;

constexpr auto HrirDelayFracBits = 2u;
constexpr auto HrirDelayFracOne = 1u << HrirDelayFracBits;
constexpr auto HrirDelayFracHalf = HrirDelayFracOne >> 1u;

/* The sample rate is stored as a 24-bit integer, so 16MHz is the largest
 * supported.
 */
constexpr auto MaxSampleRate = 0xff'ff'ffu;

static_assert(MaxHrirDelay*HrirDelayFracOne < 256, "MAX_HRIR_DELAY or DELAY_FRAC too large");


constexpr auto HeaderMarkerSize = 8_uz;
[[nodiscard]] constexpr auto GetMarker00Name() noexcept { return "MinPHR00"sv; }
[[nodiscard]] constexpr auto GetMarker01Name() noexcept { return "MinPHR01"sv; }
[[nodiscard]] constexpr auto GetMarker02Name() noexcept { return "MinPHR02"sv; }
[[nodiscard]] constexpr auto GetMarker03Name() noexcept { return "MinPHR03"sv; }

static_assert(GetMarker00Name().size() == HeaderMarkerSize);
static_assert(GetMarker01Name().size() == HeaderMarkerSize);
static_assert(GetMarker02Name().size() == HeaderMarkerSize);
static_assert(GetMarker03Name().size() == HeaderMarkerSize);

/* First value for pass-through coefficients (remaining are 0), used for omni-
 * directional sounds. */
constexpr auto PassthruCoeff = static_cast<float>(1.0/std::numbers::sqrt2);

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

        setg(eback(), eback()+static_cast<size_t>(pos), egptr());
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
    ev = (std::numbers::inv_pi_v<float>*ev + 0.5f) * static_cast<float>(evcount-1);

    const auto idx = float2uint(ev);
    return IdxBlend{std::min(idx, evcount-1u), ev-static_cast<float>(idx)};
}

/* Calculate the azimuth index given the polar azimuth in radians. This will
 * return an index between 0 and (azcount - 1).
 */
auto CalcAzIndex(uint azcount, float az) -> IdxBlend
{
    az = (std::numbers::inv_pi_v<float>*0.5f*az + 1.0f) * static_cast<float>(azcount);

    const auto idx = float2uint(az);
    return IdxBlend{idx%azcount, az-static_cast<float>(idx)};
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
    mChannels[0].mSplitter.init(static_cast<float>(xover_norm));
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
        [](const double in) noexcept { return static_cast<float>(in); });
    tmpres.clear();

    const auto max_length = std::min(hrir_delay_round(max_delay) + irSize, HrirLength);
    TRACE("New max delay: {:.2f}, FIR length: {}", max_delay/double{HrirDelayFracOne}, max_length);
    mIrSize = max_length;
}


namespace {

auto CreateHrtfStore(uint rate, uint8_t irSize, const std::span<const HrtfStore::Field> fields,
    const std::span<const HrtfStore::Elevation> elevs, const std::span<const HrirArray> coeffs,
    const std::span<const ubyte2> delays) -> std::unique_ptr<HrtfStore>
{
    static_assert(16 <= alignof(HrtfStore));
    static_assert(alignof(HrtfStore::Field) <= alignof(HrtfStore));
    static_assert(alignof(HrtfStore::Elevation) <= alignof(HrtfStore::Field));

    if(rate > MaxSampleRate)
        throw std::runtime_error{fmt::format("Sample rate is too large (max: {}hz)",
            MaxSampleRate)};

    const auto irCount = size_t{elevs.back().azCount} + elevs.back().irOffset;
    auto total = sizeof(HrtfStore);
    total  = RoundUp(total, alignof(HrtfStore::Field)); /* Align for field infos */
    total += fields.size_bytes();
    total  = RoundUp(total, alignof(HrtfStore::Elevation)); /* Align for elevation infos */
    total += elevs.size_bytes();
    total  = RoundUp(total, 16); /* Align for coefficients using SIMD */
    total += coeffs.size_bytes();
    total += delays.size_bytes();

    static constexpr auto AlignVal = std::align_val_t{alignof(HrtfStore)};
    auto Hrtf = std::unique_ptr<HrtfStore>{::new(::operator new[](total, AlignVal)) HrtfStore{}};
    Hrtf->mRef.store(1u, std::memory_order_relaxed);
    Hrtf->mSampleRate = rate & 0xff'ff'ff;
    Hrtf->mIrSize = irSize;

    /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
     * Set up pointers to storage following the main HRTF struct.
     */
    const auto storage = std::span{reinterpret_cast<char*>(Hrtf.get()), total};
    const auto base = storage.begin();
    auto storeiter = base;
    std::advance(storeiter, sizeof(HrtfStore));

    auto field_ = std::span{reinterpret_cast<HrtfStore::Field*>(std::to_address(storeiter)),
        fields.size()};
    std::advance(storeiter, fields.size_bytes());

    static_assert(alignof(HrtfStore::Field) >= alignof(HrtfStore::Elevation));
    auto elev_ = std::span{reinterpret_cast<HrtfStore::Elevation*>(std::to_address(storeiter)),
        elevs.size()};
    std::advance(storeiter, elevs.size_bytes());

    storeiter = RoundUp(storeiter-base, 16)+base; /* Align for coefficients using SIMD */
    auto coeffs_ = std::span{reinterpret_cast<HrirArray*>(std::to_address(storeiter)), irCount};
    std::advance(storeiter, coeffs.size_bytes());

    auto delays_ = std::span{reinterpret_cast<ubyte2*>(std::to_address(storeiter)), irCount};
    std::advance(storeiter, delays.size_bytes());
    /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

    if(gsl::narrow_cast<size_t>(std::distance(base, storeiter)) != total)
        throw std::runtime_error{"HrtfStore allocation size mismatch"};

    /* Copy input data to storage. */
    std::ranges::uninitialized_copy(fields, field_);
    std::ranges::uninitialized_copy(elevs, elev_);
    std::ranges::uninitialized_copy(coeffs, coeffs_);
    std::ranges::uninitialized_copy(delays, delays_);

    /* Finally, assign the storage pointers. */
    Hrtf->mFields = field_;
    Hrtf->mElev = elev_;
    Hrtf->mCoeffs = coeffs_;
    Hrtf->mDelays = delays_;

    return Hrtf;
}

void MirrorLeftHrirs(const std::span<const HrtfStore::Elevation> elevs,
    std::span<HrirArray> coeffs, std::span<ubyte2> delays)
{
    for(const auto &elev : elevs)
    {
        const ushort evoffset{elev.irOffset};
        const ushort azcount{elev.azCount};
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


template<size_t num_bits, typename T>
constexpr auto fixsign(T value) noexcept -> T
{
    if constexpr(std::is_signed<T>::value && num_bits < sizeof(T)*8)
    {
        constexpr auto signbit = static_cast<T>(1u << (num_bits-1));
        return static_cast<T>((value^signbit) - signbit);
    }
    else
        return value;
}

template<typename T, size_t num_bits=sizeof(T)*8>
inline auto readle(std::istream &data) -> T
{
    static_assert((num_bits&7) == 0, "num_bits must be a multiple of 8");
    static_assert(num_bits <= sizeof(T)*8, "num_bits is too large for the type");

    alignas(T) std::array<char,sizeof(T)> ret{};
    if(!data.read(ret.data(), num_bits/8))
        return static_cast<T>(EOF);
    if constexpr(std::endian::native == std::endian::big)
        std::reverse(ret.begin(), ret.end());

    return fixsign<num_bits>(std::bit_cast<T>(ret));
}

template<>
inline auto readle<uint8_t,8>(std::istream &data) -> uint8_t
{ return static_cast<uint8_t>(data.get()); }


auto LoadHrtf00(std::istream &data) -> std::unique_ptr<HrtfStore>
{
    const auto rate = readle<uint32_t>(data);
    const auto irCount = readle<uint16_t>(data);
    const auto irSize = readle<uint16_t>(data);
    const auto evCount = readle<uint8_t>(data);
    if(!data || data.eof())
        throw std::runtime_error{"Premature end of file"};

    if(irSize < MinIrLength || irSize > HrirLength)
    {
        ERR("Unsupported HRIR size, irSize={} ({} to {})", irSize, MinIrLength, HrirLength);
        return nullptr;
    }
    if(evCount < MinEvCount || evCount > MaxEvCount)
    {
        ERR("Unsupported elevation count: evCount={} ({} to {})", evCount, MinEvCount,
            MaxEvCount);
        return nullptr;
    }

    auto elevs = std::vector<HrtfStore::Elevation>(evCount);
    std::ranges::generate(elevs | std::views::transform(&HrtfStore::Elevation::irOffset),
        [&data] { return readle<uint16_t>(data); });
    if(!data || data.eof())
        throw std::runtime_error{"Premature end of file"};

    for(size_t i{1};i < evCount;i++)
    {
        if(elevs[i].irOffset <= elevs[i-1].irOffset)
        {
            ERR("Invalid evOffset: evOffset[{}]={} (last={})", i, elevs[i].irOffset,
                elevs[i-1].irOffset);
            return nullptr;
        }
    }
    if(irCount <= elevs.back().irOffset)
    {
        ERR("Invalid evOffset: evOffset[{}]={} (irCount={})", elevs.size()-1,
            elevs.back().irOffset, irCount);
        return nullptr;
    }

    for(size_t i{1};i < evCount;i++)
    {
        elevs[i-1].azCount = static_cast<ushort>(elevs[i].irOffset - elevs[i-1].irOffset);
        if(elevs[i-1].azCount < MinAzCount || elevs[i-1].azCount > MaxAzCount)
        {
            ERR("Unsupported azimuth count: azCount[{}]={} ({} to {})", i-1, elevs[i-1].azCount,
                MinAzCount, MaxAzCount);
            return nullptr;
        }
    }
    elevs.back().azCount = static_cast<ushort>(irCount - elevs.back().irOffset);
    if(elevs.back().azCount < MinAzCount || elevs.back().azCount > MaxAzCount)
    {
        ERR("Unsupported azimuth count: azCount[{}]={} ({} to {})", elevs.size()-1,
            elevs.back().azCount, MinAzCount, MaxAzCount);
        return nullptr;
    }

    auto coeffs = std::vector<HrirArray>(irCount, HrirArray{});
    auto delays = std::vector<ubyte2>(irCount);
    std::ranges::for_each(coeffs, [&data,irSize](HrirSpan hrir)
    {
        std::ranges::generate(hrir | std::views::take(irSize) | std::views::elements<0>,
            [&data]{ return gsl::narrow_cast<float>(readle<int16_t>(data)) / 32768.0f; });
    });
    std::ranges::generate(delays|std::views::elements<0>, [&data]{return readle<uint8_t>(data);});
    if(!data || data.eof())
        throw std::runtime_error{"Premature end of file"};

    for(size_t i{0};i < irCount;i++)
    {
        if(delays[i][0] > MaxHrirDelay)
        {
            ERR("Invalid delays[{}]: {} ({})", i, delays[i][0], MaxHrirDelay);
            return nullptr;
        }
        delays[i][0] <<= HrirDelayFracBits;
    }

    /* Mirror the left ear responses to the right ear. */
    MirrorLeftHrirs(elevs, coeffs, delays);

    const auto field = std::array{HrtfStore::Field{0.0f, evCount}};
    return CreateHrtfStore(rate, static_cast<uint8_t>(irSize), field, elevs, coeffs, delays);
}

auto LoadHrtf01(std::istream &data) -> std::unique_ptr<HrtfStore>
{
    const auto rate = readle<uint32_t>(data);
    const auto irSize = readle<uint8_t>(data);
    const auto evCount = readle<uint8_t>(data);
    if(!data || data.eof())
        throw std::runtime_error{"Premature end of file"};

    if(irSize < MinIrLength || irSize > HrirLength)
    {
        ERR("Unsupported HRIR size, irSize={} ({} to {})", irSize, MinIrLength, HrirLength);
        return nullptr;
    }
    if(evCount < MinEvCount || evCount > MaxEvCount)
    {
        ERR("Unsupported elevation count: evCount={} ({} to {})", evCount, MinEvCount,
            MaxEvCount);
        return nullptr;
    }

    auto elevs = std::vector<HrtfStore::Elevation>(evCount);
    std::ranges::generate(elevs | std::views::transform(&HrtfStore::Elevation::azCount),
        [&data] { return readle<uint8_t>(data); });
    if(!data || data.eof())
        throw std::runtime_error{"Premature end of file"};

    for(size_t i{0};i < evCount;++i)
    {
        if(elevs[i].azCount < MinAzCount || elevs[i].azCount > MaxAzCount)
        {
            ERR("Unsupported azimuth count: azCount[{}]={} ({} to {})", i, elevs[i].azCount,
                MinAzCount, MaxAzCount);
            return nullptr;
        }
    }

    elevs[0].irOffset = 0;
    for(size_t i{1};i < evCount;i++)
        elevs[i].irOffset = static_cast<ushort>(elevs[i-1].irOffset + elevs[i-1].azCount);
    const auto irCount = static_cast<ushort>(elevs.back().irOffset + elevs.back().azCount);

    auto coeffs = std::vector<HrirArray>(irCount, HrirArray{});
    auto delays = std::vector<ubyte2>(irCount);
    std::ranges::for_each(coeffs, [&data,irSize](HrirSpan hrir)
    {
        std::ranges::generate(hrir | std::views::take(irSize) | std::views::elements<0>,
            [&data]{ return gsl::narrow_cast<float>(readle<int16_t>(data)) / 32768.0f; });
    });
    std::ranges::generate(delays|std::views::elements<0>, [&data]{return readle<uint8_t>(data);});
    if(!data || data.eof())
        throw std::runtime_error{"Premature end of file"};

    for(size_t i{0};i < irCount;i++)
    {
        if(delays[i][0] > MaxHrirDelay)
        {
            ERR("Invalid delays[{}]: {} ({})", i, delays[i][0], MaxHrirDelay);
            return nullptr;
        }
        delays[i][0] <<= HrirDelayFracBits;
    }

    /* Mirror the left ear responses to the right ear. */
    MirrorLeftHrirs(elevs, coeffs, delays);

    const auto field = std::array{HrtfStore::Field{0.0f, evCount}};
    return CreateHrtfStore(rate, irSize, field, elevs, coeffs, delays);
}

auto LoadHrtf02(std::istream &data) -> std::unique_ptr<HrtfStore>
{
    static constexpr auto SampleType_S16 = ubyte{0};
    static constexpr auto SampleType_S24 = ubyte{1};
    static constexpr auto ChanType_LeftOnly = ubyte{0};
    static constexpr auto ChanType_LeftRight = ubyte{1};

    const auto rate = readle<uint32_t>(data);
    const auto sampleType = readle<uint8_t>(data);
    const auto channelType = readle<uint8_t>(data);
    const auto irSize = readle<uint8_t>(data);
    const auto fdCount = readle<uint8_t>(data);
    if(!data || data.eof())
        throw std::runtime_error{"Premature end of file"};

    if(sampleType > SampleType_S24)
    {
        ERR("Unsupported sample type: {}", sampleType);
        return nullptr;
    }
    if(channelType > ChanType_LeftRight)
    {
        ERR("Unsupported channel type: {}", channelType);
        return nullptr;
    }

    if(irSize < MinIrLength || irSize > HrirLength)
    {
        ERR("Unsupported HRIR size, irSize={} ({} to {})", irSize, MinIrLength, HrirLength);
        return nullptr;
    }
    if(fdCount < 1 || fdCount > MaxFdCount)
    {
        ERR("Unsupported number of field-depths: fdCount={} ({} to {})", fdCount, MinFdCount,
            MaxFdCount);
        return nullptr;
    }

    auto fields = std::vector<HrtfStore::Field>(fdCount);
    auto elevs = std::vector<HrtfStore::Elevation>{};
    for(size_t f{0};f < fdCount;f++)
    {
        const auto distance = readle<uint16_t>(data);
        const auto evCount = readle<uint8_t>(data);
        if(!data || data.eof())
            throw std::runtime_error{"Premature end of file"};

        if(distance < MinFdDistance || distance > MaxFdDistance)
        {
            ERR("Unsupported field distance[{}]={} ({} to {} millimeters)", f, distance,
                MinFdDistance, MaxFdDistance);
            return nullptr;
        }
        if(evCount < MinEvCount || evCount > MaxEvCount)
        {
            ERR("Unsupported elevation count: evCount[{}]={} ({} to {})", f, evCount,
                MinEvCount, MaxEvCount);
            return nullptr;
        }

        fields[f].distance = gsl::narrow_cast<float>(distance) / 1000.0f;
        fields[f].evCount = evCount;
        if(f > 0 && !(fields[f].distance > fields[f-1].distance))
        {
            ERR("Field distance[{}] is not after previous ({} > {})", f, fields[f].distance,
                fields[f-1].distance);
            return nullptr;
        }

        const auto ebase = elevs.size();
        elevs.resize(ebase + evCount);

        const auto new_azs = elevs | std::views::transform(&HrtfStore::Elevation::azCount)
            | std::views::drop(ebase);
        std::ranges::generate(new_azs, [&data] { return readle<uint8_t>(data); });
        if(!data || data.eof())
            throw std::runtime_error{"Premature end of file"};

        const auto invazi = std::ranges::find_if_not(new_azs, [](const auto &azi) noexcept
        { return azi >= MinAzCount && azi <= MaxAzCount; });
        if(invazi != new_azs.end())
        {
            const auto idx = std::distance(new_azs.begin(), invazi);
            ERR("Unsupported azimuth count: azCount[{}][{}]={} ({} to {})", f, idx, *invazi,
                MinAzCount, MaxAzCount);
            return nullptr;
        }
    }

    elevs[0].irOffset = 0;
    std::partial_sum(elevs.cbegin(), elevs.cend(), elevs.begin(),
        [](const HrtfStore::Elevation &last, const HrtfStore::Elevation &cur)
            -> HrtfStore::Elevation
    {
        return HrtfStore::Elevation{cur.azCount,
            static_cast<ushort>(last.azCount + last.irOffset)};
    });
    const auto irTotal = static_cast<ushort>(elevs.back().azCount + elevs.back().irOffset);

    auto coeffs = std::vector<HrirArray>(irTotal, HrirArray{});
    auto delays = std::vector<ubyte2>(irTotal);
    if(channelType == ChanType_LeftOnly)
    {
        if(sampleType == SampleType_S16)
        {
            std::ranges::for_each(coeffs, [&data,irSize](HrirSpan hrir)
            {
                std::ranges::generate(hrir | std::views::take(irSize) | std::views::elements<0>,
                    [&data]{ return gsl::narrow_cast<float>(readle<int16_t>(data)) / 32768.0f; });
            });
        }
        else if(sampleType == SampleType_S24)
        {
            std::ranges::for_each(coeffs, [&data,irSize](HrirSpan hrir)
            {
                std::ranges::generate(hrir | std::views::take(irSize) | std::views::elements<0>,
                    [&data]{ return gsl::narrow_cast<float>(readle<int,24>(data)) / 8388608.0f; });
            });
        }

        const auto ldelays = delays | std::views::elements<0>;
        std::ranges::generate(ldelays, [&data]{ return readle<uint8_t>(data); });
        if(!data || data.eof())
            throw std::runtime_error{"Premature end of file"};

        auto invdelay = std::ranges::find_if(ldelays, [](const ubyte delay) noexcept
        { return delay > MaxHrirDelay; });
        if(invdelay != ldelays.end())
        {
            const auto idx = std::distance(ldelays.begin(), invdelay);
            ERR("Invalid delays[{}][0]: {} > {}", idx, *invdelay, MaxHrirDelay);
            return nullptr;
        }

        std::ranges::transform(ldelays, ldelays.begin(), [](const ubyte delay) -> ubyte
        { return static_cast<ubyte>(delay << HrirDelayFracBits); });

        /* Mirror the left ear responses to the right ear. */
        MirrorLeftHrirs(elevs, coeffs, delays);
    }
    else if(channelType == ChanType_LeftRight)
    {
        if(sampleType == SampleType_S16)
        {
            std::ranges::for_each(coeffs, [&data,irSize](HrirSpan hrir)
            {
                std::ranges::generate(hrir | std::views::take(irSize) | std::views::join,
                    [&data]{ return gsl::narrow_cast<float>(readle<int16_t>(data)) / 32768.0f; });
            });
        }
        else if(sampleType == SampleType_S24)
        {
            std::ranges::for_each(coeffs, [&data,irSize](HrirSpan hrir)
            {
                std::ranges::generate(hrir | std::views::take(irSize) | std::views::join,
                    [&data]{ return gsl::narrow_cast<float>(readle<int,24>(data)) / 8388608.0f; });
            });
        }

        const auto joined_delays = delays | std::views::join;
        std::ranges::generate(joined_delays, [&data]{ return readle<uint8_t>(data); });
        if(!data || data.eof())
            throw std::runtime_error{"Premature end of file"};

        auto invdelay = std::ranges::find_if(joined_delays, [](const ubyte delay) noexcept
        { return delay > MaxHrirDelay; });
        if(invdelay != joined_delays.end())
        {
            const auto idx = std::distance(joined_delays.begin(), invdelay);
            ERR("Invalid delays[{}][{}]: {} > {}", idx>>1, idx&1, *invdelay, MaxHrirDelay);
            return nullptr;
        }

        std::ranges::transform(joined_delays, joined_delays.begin(), [](const ubyte delay) -> ubyte
        { return static_cast<ubyte>(delay << HrirDelayFracBits); });
    }

    if(fdCount > 1)
    {
        auto fields_ = std::vector<HrtfStore::Field>(fields.size());
        auto elevs_ = std::vector<HrtfStore::Elevation>(elevs.size());
        auto coeffs_ = std::vector<HrirArray>(coeffs.size());
        auto delays_ = std::vector<ubyte2>(delays.size());

        /* Simple reverse for the per-field elements. */
        std::ranges::reverse_copy(fields, fields_.begin());

        /* Each field has a group of elevations, which each have an azimuth
         * count. Reverse the order of the groups, keeping the relative order
         * of per-group azimuth counts.
         */
        auto elevs_end = elevs_.end();
        std::ignore = std::accumulate(fields.cbegin(), fields.cend(), ptrdiff_t{0},
            [&elevs,&elevs_end](const ptrdiff_t ebase, const HrtfStore::Field &field) -> ptrdiff_t
        {
            elevs_end = std::ranges::copy_backward(elevs | std::views::drop(ebase)
                | std::views::take(field.evCount), elevs_end).out;
            return ebase + field.evCount;
        });
        Ensures(elevs_.begin() == elevs_end);

        /* Reestablish the IR offset for each elevation index, given the new
         * ordering of elevations.
         */
        elevs_[0].irOffset = 0;
        std::partial_sum(elevs_.cbegin(), elevs_.cend(), elevs_.begin(),
            [](const HrtfStore::Elevation &last, const HrtfStore::Elevation &cur)
                -> HrtfStore::Elevation
        {
            return HrtfStore::Elevation{cur.azCount,
                static_cast<ushort>(last.azCount + last.irOffset)};
        });

        /* Reverse the order of each field's group of IRs. */
        auto coeffs_end = coeffs_.end();
        auto delays_end = delays_.end();
        std::ignore = std::accumulate(fields.cbegin(), fields.cend(), ptrdiff_t{0},
            [&elevs,&coeffs,&delays,&coeffs_end,&delays_end](const ptrdiff_t ebase,
                const HrtfStore::Field &field) -> ptrdiff_t
        {
            auto accum_az = [](const ptrdiff_t count, const HrtfStore::Elevation &elev) noexcept
                -> ptrdiff_t { return count + elev.azCount; };
            const auto elev_mid = elevs.cbegin() + ebase;
            const auto abase = std::accumulate(elevs.cbegin(), elev_mid, ptrdiff_t{0}, accum_az);
            const auto num_azs = std::accumulate(elev_mid, elev_mid + field.evCount, ptrdiff_t{0},
                accum_az);

            coeffs_end = std::ranges::copy_backward(coeffs | std::views::drop(abase)
                | std::views::take(num_azs), coeffs_end).out;
            delays_end = std::ranges::copy_backward(delays | std::views::drop(abase)
                | std::views::take(num_azs), delays_end).out;

            return ebase + field.evCount;
        });
        Ensures(coeffs_.begin() == coeffs_end);
        Ensures(delays_.begin() == delays_end);

        fields = std::move(fields_);
        elevs = std::move(elevs_);
        coeffs = std::move(coeffs_);
        delays = std::move(delays_);
    }

    return CreateHrtfStore(rate, irSize, fields, elevs, coeffs, delays);
}

auto LoadHrtf03(std::istream &data) -> std::unique_ptr<HrtfStore>
{
    static constexpr auto ChanType_LeftOnly = ubyte{0};
    static constexpr auto ChanType_LeftRight = ubyte{1};

    const auto rate = readle<uint32_t>(data);
    const auto channelType = readle<uint8_t>(data);
    const auto irSize = readle<uint8_t>(data);
    const auto fdCount = readle<uint8_t>(data);
    if(!data || data.eof())
        throw std::runtime_error{"Premature end of file"};

    if(channelType > ChanType_LeftRight)
    {
        ERR("Unsupported channel type: {}", channelType);
        return nullptr;
    }

    if(irSize < MinIrLength || irSize > HrirLength)
    {
        ERR("Unsupported HRIR size, irSize={} ({} to {})", irSize, MinIrLength, HrirLength);
        return nullptr;
    }
    if(fdCount < 1 || fdCount > MaxFdCount)
    {
        ERR("Unsupported number of field-depths: fdCount={} ({} to {})", fdCount, MinFdCount,
            MaxFdCount);
        return nullptr;
    }

    auto fields = std::vector<HrtfStore::Field>(fdCount);
    auto elevs = std::vector<HrtfStore::Elevation>{};
    for(size_t f{0};f < fdCount;f++)
    {
        const auto distance = readle<uint16_t>(data);
        const auto evCount = readle<uint8_t>(data);
        if(!data || data.eof())
            throw std::runtime_error{"Premature end of file"};

        if(distance < MinFdDistance || distance > MaxFdDistance)
        {
            ERR("Unsupported field distance[{}]={} ({} to {} millimeters)", f, distance,
                MinFdDistance, MaxFdDistance);
            return nullptr;
        }
        if(evCount < MinEvCount || evCount > MaxEvCount)
        {
            ERR("Unsupported elevation count: evCount[{}]={} ({} to {})", f, evCount,
                MinEvCount, MaxEvCount);
            return nullptr;
        }

        fields[f].distance = gsl::narrow_cast<float>(distance) / 1000.0f;
        fields[f].evCount = evCount;
        if(f > 0 && !(fields[f].distance < fields[f-1].distance))
        {
            ERR("Field distance[{}] is not before previous ({} < {})", f, fields[f].distance,
                fields[f-1].distance);
            return nullptr;
        }

        const auto ebase = elevs.size();
        elevs.resize(ebase + evCount);

        const auto new_azs = elevs | std::views::transform(&HrtfStore::Elevation::azCount)
            | std::views::drop(ebase);
        std::ranges::generate(new_azs, [&data] { return readle<uint8_t>(data); });
        if(!data || data.eof())
            throw std::runtime_error{"Premature end of file"};

        const auto invazi = std::ranges::find_if_not(new_azs, [](const auto &azi) noexcept
        { return azi >= MinAzCount && azi <= MaxAzCount; });
        if(invazi != new_azs.end())
        {
            const auto idx = std::distance(new_azs.begin(), invazi);
            ERR("Unsupported azimuth count: azCount[{}][{}]={} ({} to {})", f, idx, *invazi,
                MinAzCount, MaxAzCount);
            return nullptr;
        }
    }

    elevs[0].irOffset = 0;
    std::partial_sum(elevs.cbegin(), elevs.cend(), elevs.begin(),
        [](const HrtfStore::Elevation &last, const HrtfStore::Elevation &cur)
            -> HrtfStore::Elevation
    {
        return HrtfStore::Elevation{cur.azCount,
            static_cast<ushort>(last.azCount + last.irOffset)};
    });
    const auto irTotal = static_cast<ushort>(elevs.back().azCount + elevs.back().irOffset);

    auto coeffs = std::vector<HrirArray>(irTotal, HrirArray{});
    auto delays = std::vector<ubyte2>(irTotal);
    if(channelType == ChanType_LeftOnly)
    {
        std::ranges::for_each(coeffs, [&data,irSize](HrirSpan hrir)
        {
            std::ranges::generate(hrir | std::views::take(irSize) | std::views::elements<0>,
                [&data]{ return gsl::narrow_cast<float>(readle<int,24>(data)) / 8388608.0f; });
        });

        const auto ldelays = delays | std::views::elements<0>;
        std::ranges::generate(ldelays, [&data]{ return readle<uint8_t>(data); });
        if(!data || data.eof())
            throw std::runtime_error{"Premature end of file"};

        auto invdelay = std::ranges::find_if(ldelays, [](const ubyte delay) noexcept
        { return delay > MaxHrirDelay<<HrirDelayFracBits; });
        if(invdelay != ldelays.end())
        {
            const auto idx = std::distance(ldelays.begin(), invdelay);
            ERR("Invalid delays[{}][0]: {:f} > {}", idx, *invdelay/float{HrirDelayFracOne},
                MaxHrirDelay);
            return nullptr;
        }

        /* Mirror the left ear responses to the right ear. */
        MirrorLeftHrirs(elevs, coeffs, delays);
    }
    else if(channelType == ChanType_LeftRight)
    {
        std::ranges::for_each(coeffs, [&data,irSize](HrirSpan hrir)
        {
            std::ranges::generate(hrir | std::views::take(irSize) | std::views::join,
                [&data]{ return gsl::narrow_cast<float>(readle<int,24>(data)) / 8388608.0f; });
        });

        const auto joined_delays = delays | std::views::join;
        std::ranges::generate(joined_delays, [&data]{ return readle<uint8_t>(data); });
        if(!data || data.eof())
            throw std::runtime_error{"Premature end of file"};

        auto invdelay = std::ranges::find_if(joined_delays, [](const ubyte delay) noexcept
        { return delay > MaxHrirDelay<<HrirDelayFracBits; });
        if(invdelay != joined_delays.end())
        {
            const auto idx = std::distance(joined_delays.begin(), invdelay);
            ERR("Invalid delays[{}][{}]: {:f} ({})", idx>>1, idx&1,
                *invdelay/float{HrirDelayFracOne}, MaxHrirDelay);
            return nullptr;
        }
    }

    return CreateHrtfStore(rate, irSize, fields, elevs, coeffs, delays);
}


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
    if(devrate > MaxSampleRate)
    {
        WARN("Device sample rate too large for HRTF ({}hz > {}hz)", devrate, MaxSampleRate);
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
        auto fstr = std::make_unique<fs::ifstream>(fs::path(al::char_as_u8(fname)),
            std::ios::binary);
        if(!fstr->is_open())
        {
            ERR("Could not open {}", fname);
            return nullptr;
        }
        stream = std::move(fstr);
    }

    auto hrtf = std::unique_ptr<HrtfStore>{};
    auto magic = std::array<char,HeaderMarkerSize>{};
    stream->read(magic.data(), magic.size());
    if(stream->gcount() < std::streamsize{magic.size()})
        ERR("{} data is too short ({} bytes)", name, stream->gcount());
    else if(std::ranges::equal(GetMarker03Name(), magic))
    {
        TRACE("Detected data set format v3");
        hrtf = LoadHrtf03(*stream);
    }
    else if(std::ranges::equal(GetMarker02Name(), magic))
    {
        TRACE("Detected data set format v2");
        hrtf = LoadHrtf02(*stream);
    }
    else if(std::ranges::equal(GetMarker01Name(), magic))
    {
        TRACE("Detected data set format v1");
        hrtf = LoadHrtf01(*stream);
    }
    else if(std::ranges::equal(GetMarker00Name(), magic))
    {
        TRACE("Detected data set format v0");
        hrtf = LoadHrtf00(*stream);
    }
    else
        ERR("Invalid header in {}: {::#04X}", name, std::as_bytes(std::span{magic}));
    stream.reset();

    if(!hrtf)
        return nullptr;

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
        const auto rate_scale = static_cast<float>(devrate)/static_cast<float>(hrtf->mSampleRate);
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
            return static_cast<ubyte>(float2int(fdelay*delay_scale + 0.5f));
        });

        /* Scale the IR size for the new sample rate and update the stored
         * sample rate.
         */
        const auto newIrSize = std::round(static_cast<float>(hrtf->mIrSize) * rate_scale);
        hrtf->mIrSize = static_cast<uint8_t>(std::min(float{HrirLength}, newIrSize));
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
