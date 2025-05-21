#ifndef AL_BUFFER_H
#define AL_BUFFER_H

#include "config.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>

#include "AL/al.h"
#include "AL/alc.h"

#include "alc/inprogext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "core/buffer_storage.h"
#include "intrusive_ptr.h"
#include "vector.h"

#if ALSOFT_EAX
enum class EaxStorage : uint8_t {
    Automatic,
    Accessible,
    Hardware
};
#endif // ALSOFT_EAX


struct ALbuffer : public BufferStorage {
    ALbitfieldSOFT Access{0u};

    std::variant<al::vector<uint8_t,16>,
        al::vector<int16_t,16>,
        al::vector<int32_t,16>,
        al::vector<float,16>,
        al::vector<double,16>,
        al::vector<MulawSample,16>,
        al::vector<AlawSample,16>,
        al::vector<IMA4Data,16>,
        al::vector<MSADPCMData,16>> mDataStorage;

    ALuint OriginalSize{0};

    ALuint UnpackAlign{0};
    ALuint PackAlign{0};
    ALuint UnpackAmbiOrder{1};

    ALbitfieldSOFT MappedAccess{0u};
    ALsizei MappedOffset{0};
    ALsizei MappedSize{0};

    ALuint mLoopStart{0u};
    ALuint mLoopEnd{0u};

    /* Number of times buffer was attached to a source (deletion can only occur when 0) */
    std::atomic<ALuint> mRef{0u};

    /* Self ID */
    ALuint id{0};

    auto inc_ref() noexcept { return mRef.fetch_add(1, std::memory_order_acq_rel)+1; }
    auto dec_ref() noexcept { return mRef.fetch_sub(1, std::memory_order_acq_rel)-1; }
    auto newReference() noexcept
    {
        mRef.fetch_add(1, std::memory_order_acq_rel);
        return al::intrusive_ptr{this};
    }

    static void SetName(ALCcontext *context, ALuint id, std::string_view name);

    DISABLE_ALLOC

#if ALSOFT_EAX
    EaxStorage eax_x_ram_mode{EaxStorage::Automatic};
    bool eax_x_ram_is_hardware{};
#endif // ALSOFT_EAX
};

struct BufferSubList {
    uint64_t FreeMask{~0_u64};
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
