#ifndef AL_BUFFER_H
#define AL_BUFFER_H

#include "config.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>
#include <variant>

#include "AL/al.h"

#include "alc/inprogext.h"
#include "altypes.hpp"
#include "core/buffer_storage.h"
#include "disable_alloc.hpp"
#include "gsl/gsl"
#include "intrusive_ptr.h"
#include "vector.h"

#if ALSOFT_EAX
enum class EaxStorage : std::uint8_t {
    Automatic,
    Accessible,
    Hardware
};
#endif // ALSOFT_EAX

namespace al {

struct Context;

struct Buffer : BufferStorage {
    ALbitfieldSOFT mAccess{0u};

    std::variant<al::vector<std::uint8_t, 16>,
        al::vector<std::int16_t, 16>,
        al::vector<std::int32_t, 16>,
        al::vector<float, 16>,
        al::vector<double, 16>,
        al::vector<MulawSample, 16>,
        al::vector<AlawSample, 16>,
        al::vector<IMA4Data, 16>,
        al::vector<MSADPCMData, 16>> mDataStorage;

    ALuint mOriginalSize{0u};

    ALuint mUnpackAlign{0u};
    ALuint mPackAlign{0u};
    ALuint mUnpackAmbiOrder{1u};

    ALuint mMappedAccess{0u};
    ALint mMappedOffset{0};
    ALint mMappedSize{0};

    ALuint mLoopStart{0u};
    ALuint mLoopEnd{0u};

    std::atomic<ALuint> mRef{0u};

    /* Self ID */
    ALuint mId{0u};

    auto inc_ref() noexcept { return mRef.fetch_add(1, std::memory_order_acq_rel)+1; }
    auto dec_ref() noexcept { return mRef.fetch_sub(1, std::memory_order_acq_rel)-1; }
    auto newReference() noexcept
    {
        mRef.fetch_add(1, std::memory_order_acq_rel);
        return al::intrusive_ptr{this};
    }

    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    ~Buffer() = default;
    auto operator=(const Buffer&) -> Buffer& = delete;
    auto operator=(Buffer&&) -> Buffer& = delete;

    static void SetName(al::Context& context, ALuint id, std::string_view name);

    DISABLE_ALLOC

#if ALSOFT_EAX
    EaxStorage mEaxXRamMode{EaxStorage::Automatic};
    bool mEaxXRamIsHardware{};
#endif // ALSOFT_EAX
};

} /* namespace al */

struct BufferSubList {
    u64 mFreeMask{~0_u64};
    gsl::owner<std::array<al::Buffer,64>*> mBuffers{nullptr};

    BufferSubList() noexcept = default;
    BufferSubList(const BufferSubList&) = delete;
    BufferSubList(BufferSubList&& rhs) noexcept : mFreeMask{rhs.mFreeMask}, mBuffers{rhs.mBuffers}
    { rhs.mFreeMask = ~0_u64; rhs.mBuffers = nullptr; }
    ~BufferSubList();

    BufferSubList& operator=(const BufferSubList&) = delete;
    BufferSubList& operator=(BufferSubList&& rhs) noexcept
    { std::swap(mFreeMask, rhs.mFreeMask); std::swap(mBuffers, rhs.mBuffers); return *this; }
};

#endif
