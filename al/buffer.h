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

namespace al {
struct Context;
} // namespace al

#if ALSOFT_EAX
enum class EaxStorage : u8 {
    Automatic,
    Accessible,
    Hardware
};
#endif // ALSOFT_EAX


struct ALbuffer : public BufferStorage {
    ALbitfieldSOFT Access{0u};

    std::variant<al::vector<u8, 16>,
        al::vector<i16, 16>,
        al::vector<i32, 16>,
        al::vector<f32, 16>,
        al::vector<f64, 16>,
        al::vector<MulawSample, 16>,
        al::vector<AlawSample, 16>,
        al::vector<IMA4Data, 16>,
        al::vector<MSADPCMData, 16>> mDataStorage;

    u32 OriginalSize{0_u32};

    u32 UnpackAlign{0_u32};
    u32 PackAlign{0_u32};
    u32 UnpackAmbiOrder{1_u32};

    u32 MappedAccess{0_u32};
    i32 MappedOffset{0_i32};
    i32 MappedSize{0_i32};

    u32 mLoopStart{0_u32};
    u32 mLoopEnd{0_u32};

    /* Number of times buffer was attached to a source (deletion can only occur when 0) */
    std::atomic<u32> mRef{0_u32};

    /* Self ID */
    u32 id{0_u32};

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
    EaxStorage eax_x_ram_mode{EaxStorage::Automatic};
    bool eax_x_ram_is_hardware{};
#endif // ALSOFT_EAX
};

struct BufferSubList {
    u64 FreeMask{~0_u64};
    gsl::owner<std::array<ALbuffer,64>*> Buffers{nullptr};

    BufferSubList() noexcept = default;
    BufferSubList(const BufferSubList&) = delete;
    BufferSubList(BufferSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Buffers{rhs.Buffers}
    { rhs.FreeMask = ~0_u64; rhs.Buffers = nullptr; }
    ~BufferSubList();

    BufferSubList& operator=(const BufferSubList&) = delete;
    BufferSubList& operator=(BufferSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Buffers, rhs.Buffers); return *this; }
};

#endif
