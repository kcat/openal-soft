
#include "config.h"

#include <algorithm>
#include <iterator>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>

#include "hrtf_loader.hpp"

#include "alnumeric.h"
#include "fmt/core.h"
#include "fmt/ranges.h"
#include "gsl/gsl"
#include "hrtf.h"
#include "logging.h"


namespace {

using namespace std::string_view_literals;

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

static_assert(MaxHrirDelay*HrirDelayFracOne < 256, "MaxHrirDelay or HrirDelayFracOne too large");

constexpr auto HeaderMarkerSize = 8_uz;
[[nodiscard]] constexpr auto GetMarker00Name() noexcept { return "MinPHR00"sv; }
[[nodiscard]] constexpr auto GetMarker01Name() noexcept { return "MinPHR01"sv; }
[[nodiscard]] constexpr auto GetMarker02Name() noexcept { return "MinPHR02"sv; }
[[nodiscard]] constexpr auto GetMarker03Name() noexcept { return "MinPHR03"sv; }

static_assert(GetMarker00Name().size() == HeaderMarkerSize);
static_assert(GetMarker01Name().size() == HeaderMarkerSize);
static_assert(GetMarker02Name().size() == HeaderMarkerSize);
static_assert(GetMarker03Name().size() == HeaderMarkerSize);


auto CreateHrtfStore(uint rate, uint8_t irSize, const std::span<const HrtfStore::Field> fields,
    const std::span<const HrtfStore::Elevation> elevs, const std::span<const HrirArray> coeffs,
    const std::span<const ubyte2> delays) -> std::unique_ptr<HrtfStore>
{
    static_assert(16 <= alignof(HrtfStore));
    static_assert(alignof(HrtfStore::Field) <= alignof(HrtfStore));
    static_assert(alignof(HrtfStore::Elevation) <= alignof(HrtfStore::Field));

    if(rate > MaxHrtfSampleRate)
        throw std::runtime_error{fmt::format("Sample rate is too large (max: {}hz)",
            MaxHrtfSampleRate)};

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
    const auto storage = std::span{reinterpret_cast<char*>(std::to_address(Hrtf)), total};
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
        const auto evoffset = size_t{elev.irOffset};
        const auto azcount = size_t{elev.azCount};
        for(const auto j : std::views::iota(0_uz, azcount))
        {
            const auto lidx = evoffset + j;
            const auto ridx = evoffset + ((azcount-j) % azcount);

            std::ranges::copy(coeffs[lidx] | std::views::elements<0>,
                (coeffs[ridx] | std::views::elements<1>).begin());
            delays[ridx][1] = delays[lidx][0];
        }
    }
}


template<size_t num_bits, typename T>
constexpr auto fixsign(T value) noexcept -> T
{
    if constexpr(std::is_signed<T>::value && num_bits < sizeof(T)*8)
    {
        constexpr auto signbit = gsl::narrow_cast<T>(1u << (num_bits-1));
        return gsl::narrow_cast<T>((value^signbit) - signbit);
    }
    else
        return value;
}

template<typename T, size_t num_bits=sizeof(T)*8>
auto readle(std::istream &data) -> T
{
    static_assert((num_bits&7) == 0, "num_bits must be a multiple of 8");
    static_assert(num_bits <= sizeof(T)*8, "num_bits is too large for the type");

    alignas(T) auto ret = std::array<char,sizeof(T)>{};
    if(!data.read(ret.data(), num_bits/8))
        return gsl::narrow_cast<T>(EOF);
    if constexpr(std::endian::native == std::endian::big)
        std::reverse(ret.begin(), ret.end());

    return fixsign<num_bits>(std::bit_cast<T>(ret));
}

template<>
auto readle<uint8_t,8>(std::istream &data) -> uint8_t
{ return gsl::narrow_cast<uint8_t>(data.get()); }


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
        throw std::runtime_error{fmt::format("Unsupported HRIR size, irSize={} ({} to {})", irSize,
            MinIrLength, HrirLength)};
    }
    if(evCount < MinEvCount || evCount > MaxEvCount)
    {
        throw std::runtime_error{fmt::format("Unsupported elevation count: evCount={} ({} to {})",
            evCount, MinEvCount, MaxEvCount)};
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
            throw std::runtime_error{fmt::format("Invalid evOffset: evOffset[{}]={} (last={})", i,
                elevs[i].irOffset, elevs[i-1].irOffset)};
        }
    }
    if(irCount <= elevs.back().irOffset)
    {
        throw std::runtime_error{fmt::format("Invalid evOffset: evOffset[{}]={} (irCount={})",
            elevs.size()-1, elevs.back().irOffset, irCount)};
    }

    for(size_t i{1};i < evCount;i++)
    {
        elevs[i-1].azCount = gsl::narrow_cast<ushort>(elevs[i].irOffset - elevs[i-1].irOffset);
        if(elevs[i-1].azCount < MinAzCount || elevs[i-1].azCount > MaxAzCount)
        {
            throw std::runtime_error{fmt::format(
                "Unsupported azimuth count: azCount[{}]={} ({} to {})", i-1, elevs[i-1].azCount,
                MinAzCount, MaxAzCount)};
        }
    }
    elevs.back().azCount = gsl::narrow_cast<ushort>(irCount - elevs.back().irOffset);
    if(elevs.back().azCount < MinAzCount || elevs.back().azCount > MaxAzCount)
    {
        throw std::runtime_error{fmt::format(
            "Unsupported azimuth count: azCount[{}]={} ({} to {})", elevs.size()-1,
            elevs.back().azCount, MinAzCount, MaxAzCount)};
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
            throw std::runtime_error{fmt::format("Invalid delays[{}]: {} ({})", i, delays[i][0],
                MaxHrirDelay)};
        }
        delays[i][0] <<= HrirDelayFracBits;
    }

    /* Mirror the left ear responses to the right ear. */
    MirrorLeftHrirs(elevs, coeffs, delays);

    const auto field = std::array{HrtfStore::Field{0.0f, evCount}};
    return CreateHrtfStore(rate, gsl::narrow_cast<uint8_t>(irSize), field, elevs, coeffs, delays);
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
        throw std::runtime_error{fmt::format("Unsupported HRIR size, irSize={} ({} to {})", irSize,
            MinIrLength, HrirLength)};
    }
    if(evCount < MinEvCount || evCount > MaxEvCount)
    {
        throw std::runtime_error{fmt::format("Unsupported elevation count: evCount={} ({} to {})",
            evCount, MinEvCount, MaxEvCount)};
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
            throw std::runtime_error{fmt::format(
                "Unsupported azimuth count: azCount[{}]={} ({} to {})", i, elevs[i].azCount,
                MinAzCount, MaxAzCount)};
        }
    }

    elevs[0].irOffset = 0;
    for(size_t i{1};i < evCount;i++)
        elevs[i].irOffset = gsl::narrow_cast<ushort>(elevs[i-1].irOffset + elevs[i-1].azCount);
    const auto irCount = gsl::narrow_cast<ushort>(elevs.back().irOffset + elevs.back().azCount);

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
            throw std::runtime_error{fmt::format("Invalid delays[{}]: {} ({})", i, delays[i][0],
                MaxHrirDelay)};
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
        throw std::runtime_error{fmt::format("Unsupported sample type: {}", sampleType)};
    if(channelType > ChanType_LeftRight)
        throw std::runtime_error{fmt::format("Unsupported channel type: {}", channelType)};

    if(irSize < MinIrLength || irSize > HrirLength)
    {
        throw std::runtime_error{fmt::format("Unsupported HRIR size, irSize={} ({} to {})", irSize,
            MinIrLength, HrirLength)};
    }
    if(fdCount < 1 || fdCount > MaxFdCount)
    {
        throw std::runtime_error{fmt::format(
            "Unsupported number of field-depths: fdCount={} ({} to {})", fdCount, MinFdCount,
            MaxFdCount)};
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
            throw std::runtime_error{fmt::format(
                "Unsupported field distance[{}]={} ({} to {} millimeters)", f, distance,
                MinFdDistance, MaxFdDistance)};
        }
        if(evCount < MinEvCount || evCount > MaxEvCount)
        {
            throw std::runtime_error{fmt::format(
                "Unsupported elevation count: evCount[{}]={} ({} to {})", f, evCount, MinEvCount,
                MaxEvCount)};
        }

        fields[f].distance = gsl::narrow_cast<float>(distance) / 1000.0f;
        fields[f].evCount = evCount;
        if(f > 0 && !(fields[f].distance > fields[f-1].distance))
        {
            throw std::runtime_error{fmt::format(
                "Field distance[{}] is not after previous ({} > {})", f, fields[f].distance,
                fields[f-1].distance)};
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
            throw std::runtime_error{fmt::format(
                "Unsupported azimuth count: azCount[{}][{}]={} ({} to {})", f, idx, *invazi,
                MinAzCount, MaxAzCount)};
        }
    }

    elevs[0].irOffset = 0;
    std::partial_sum(elevs.cbegin(), elevs.cend(), elevs.begin(),
        [](const HrtfStore::Elevation &last, const HrtfStore::Elevation &cur)->HrtfStore::Elevation
    {
        return HrtfStore::Elevation{cur.azCount,
            gsl::narrow_cast<ushort>(last.azCount + last.irOffset)};
    });
    const auto irTotal = gsl::narrow_cast<ushort>(elevs.back().azCount + elevs.back().irOffset);

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
            throw std::runtime_error{fmt::format("Invalid delays[{}][0]: {} > {}", idx, *invdelay,
                MaxHrirDelay)};
        }

        std::ranges::transform(ldelays, ldelays.begin(), [](const ubyte delay) -> ubyte
        { return gsl::narrow_cast<ubyte>(delay << HrirDelayFracBits); });

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
            throw std::runtime_error{fmt::format("Invalid delays[{}][{}]: {} > {}", idx>>1, idx&1,
                *invdelay, MaxHrirDelay)};
        }

        std::ranges::transform(joined_delays, joined_delays.begin(), [](const ubyte delay) -> ubyte
        { return gsl::narrow_cast<ubyte>(delay << HrirDelayFracBits); });
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
                gsl::narrow_cast<ushort>(last.azCount + last.irOffset)};
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
        throw std::runtime_error{fmt::format("Unsupported channel type: {}", channelType)};

    if(irSize < MinIrLength || irSize > HrirLength)
    {
        throw std::runtime_error{fmt::format("Unsupported HRIR size, irSize={} ({} to {})", irSize,
            MinIrLength, HrirLength)};
    }
    if(fdCount < 1 || fdCount > MaxFdCount)
    {
        throw std::runtime_error{fmt::format(
            "Unsupported number of field-depths: fdCount={} ({} to {})", fdCount, MinFdCount,
            MaxFdCount)};
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
            throw std::runtime_error{fmt::format(
                "Unsupported field distance[{}]={} ({} to {} millimeters)", f, distance,
                MinFdDistance, MaxFdDistance)};
        }
        if(evCount < MinEvCount || evCount > MaxEvCount)
        {
            throw std::runtime_error{fmt::format(
                "Unsupported elevation count: evCount[{}]={} ({} to {})", f, evCount, MinEvCount,
                MaxEvCount)};
        }

        fields[f].distance = gsl::narrow_cast<float>(distance) / 1000.0f;
        fields[f].evCount = evCount;
        if(f > 0 && !(fields[f].distance < fields[f-1].distance))
        {
            throw std::runtime_error{fmt::format(
                "Field distance[{}] is not before previous ({} < {})", f, fields[f].distance,
                fields[f-1].distance)};
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
            throw std::runtime_error{fmt::format(
                "Unsupported azimuth count: azCount[{}][{}]={} ({} to {})", f, idx, *invazi,
                MinAzCount, MaxAzCount)};
        }
    }

    elevs[0].irOffset = 0;
    std::partial_sum(elevs.cbegin(), elevs.cend(), elevs.begin(),
        [](const HrtfStore::Elevation &last, const HrtfStore::Elevation &cur)->HrtfStore::Elevation
    {
        return HrtfStore::Elevation{cur.azCount,
            gsl::narrow_cast<ushort>(last.azCount + last.irOffset)};
    });
    const auto irTotal = gsl::narrow_cast<ushort>(elevs.back().azCount + elevs.back().irOffset);

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
            throw std::runtime_error{fmt::format("Invalid delays[{}][0]: {:f} > {}", idx,
                gsl::narrow_cast<float>(*invdelay)/float{HrirDelayFracOne}, MaxHrirDelay)};
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
            throw std::runtime_error{fmt::format("Invalid delays[{}][{}]: {:f} ({})", idx>>1,
                idx&1, gsl::narrow_cast<float>(*invdelay)/float{HrirDelayFracOne}, MaxHrirDelay)};
        }
    }

    return CreateHrtfStore(rate, irSize, fields, elevs, coeffs, delays);
}


} // namespace

auto LoadHrtf(std::istream &stream) -> std::unique_ptr<HrtfStore>
{
    auto magic = std::array<char,HeaderMarkerSize>{};
    stream.read(magic.data(), magic.size());
    if(stream.gcount() < std::streamsize{magic.size()})
        throw std::runtime_error{fmt::format("Data is too short ({} bytes)", stream.gcount())};
    if(std::ranges::equal(GetMarker03Name(), magic))
    {
        TRACE("Detected data set format v3");
        return LoadHrtf03(stream);
    }
    if(std::ranges::equal(GetMarker02Name(), magic))
    {
        TRACE("Detected data set format v2");
        return LoadHrtf02(stream);
    }
    if(std::ranges::equal(GetMarker01Name(), magic))
    {
        TRACE("Detected data set format v1");
        return LoadHrtf01(stream);
    }
    if(std::ranges::equal(GetMarker00Name(), magic))
    {
        TRACE("Detected data set format v0");
        return LoadHrtf00(stream);
    }
    throw std::runtime_error{fmt::format("Invalid header: {::#04X}",
        std::as_bytes(std::span{magic}))};
}
