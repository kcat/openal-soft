#ifndef AL_BUFFER_H
#define AL_BUFFER_H

#include "config.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>

#include "AL/al.h"

#include "alc/inprogext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "core/buffer_storage.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"
#include "vector.h"

#if ALSOFT_EAX
enum class EaxStorage : u8::value_t {
    Automatic,
    Accessible,
    Hardware
};
#endif // ALSOFT_EAX

namespace al {

struct Context;

struct Buffer : BufferStorage {
    ALbitfieldSOFT mAccess{0u};

    std::variant<al::vector<u8, 16>,
        al::vector<i16, 16>,
        al::vector<i32, 16>,
        al::vector<f32, 16>,
        al::vector<f64, 16>,
        al::vector<MulawSample, 16>,
        al::vector<AlawSample, 16>,
        al::vector<IMA4Data, 16>,
        al::vector<MSADPCMData, 16>> mDataStorage;

    u32 mOriginalSize{0_u32};

    u32 mUnpackAlign{0_u32};
    u32 mPackAlign{0_u32};
    u32 mUnpackAmbiOrder{1_u32};

    u32 mMappedAccess{0_u32};
    i32 mMappedOffset{0_i32};
    i32 mMappedSize{0_i32};

    u32 mLoopStart{0_u32};
    u32 mLoopEnd{0_u32};

    std::atomic<u32> mRef{0_u32};

    /* Self ID */
    u32 mId{0_u32};

    auto inc_ref() noexcept { return mRef.fetch_add(1, std::memory_order_acq_rel)+1; }
    auto dec_ref() noexcept { return mRef.fetch_sub(1, std::memory_order_acq_rel)-1; }
    auto newReference() noexcept
    {
        mRef.fetch_add(1, std::memory_order_acq_rel);
        return al::intrusive_ptr{this};
    }

    static void SetName(gsl::not_null<al::Context*> context, u32 id, std::string_view name);

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
