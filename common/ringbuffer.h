#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>

#include "almalloc.h"
#include "alnumeric.h"
#include "flexarray.h"
#include "opthelpers.h"


/* NOTE: This lockless ringbuffer implementation is copied from JACK, extended
 * to include an element size. Consequently, parameters and return values for a
 * size or count are in 'elements', not bytes. Additionally, it only supports
 * single-consumer/single-provider operation.
 */

struct RingBuffer {
private:
#if defined(__cpp_lib_hardware_interference_size) && !defined(_LIBCPP_VERSION)
    static constexpr std::size_t sCacheAlignment{std::hardware_destructive_interference_size};
#else
    /* Assume a 64-byte cache line, the most common/likely value. */
    static constexpr std::size_t sCacheAlignment{64};
#endif
    alignas(sCacheAlignment) std::atomic<std::size_t> mWriteCount{0u};
    alignas(sCacheAlignment) std::atomic<std::size_t> mReadCount{0u};

    alignas(sCacheAlignment) const std::size_t mWriteSize;
    const std::size_t mSizeMask;
    const std::size_t mElemSize;

    al::FlexArray<std::byte, 16> mBuffer;

public:
    struct Data {
        std::byte *buf;
        std::size_t len;
    };
    using DataPair = std::array<Data,2>;

    RingBuffer(const std::size_t writesize, const std::size_t mask, const std::size_t elemsize,
        const std::size_t numbytes)
        : mWriteSize{writesize}, mSizeMask{mask}, mElemSize{elemsize}, mBuffer{numbytes}
    { }

    /** Reset the read and write pointers to zero. This is not thread safe. */
    auto reset() noexcept -> void;

    /**
     * Return the number of elements available for reading. This is the number
     * of elements in front of the read pointer and behind the write pointer.
     */
    [[nodiscard]] auto readSpace() const noexcept -> std::size_t
    {
        const std::size_t w{mWriteCount.load(std::memory_order_acquire)};
        const std::size_t r{mReadCount.load(std::memory_order_acquire)};
        /* mWriteCount is never more than mWriteSize greater than mReadCount. */
        return w - r;
    }

    /**
     * The copying data reader. Copy at most `count' elements into `dest'.
     * Returns the actual number of elements copied.
     */
    [[nodiscard]] auto read(void *dest, std::size_t count) noexcept -> std::size_t;
    /**
     * The copying data reader w/o read pointer advance. Copy at most `count'
     * elements into `dest'. Returns the actual number of elements copied.
     */
    [[nodiscard]] auto peek(void *dest, std::size_t count) const noexcept -> std::size_t;

    /**
     * The non-copying data reader. Returns two ringbuffer data pointers that
     * hold the current readable data. If the readable data is in one segment
     * the second segment has zero length.
     */
    [[nodiscard]] auto getReadVector() noexcept -> DataPair;
    /** Advance the read pointer `count' places. */
    auto readAdvance(std::size_t count) noexcept -> void
    {
        const std::size_t w{mWriteCount.load(std::memory_order_acquire)};
        const std::size_t r{mReadCount.load(std::memory_order_relaxed)};
        [[maybe_unused]] const std::size_t readable{w - r};
        assert(readable >= count);
        mReadCount.store(r+count, std::memory_order_release);
    }


    /**
     * Return the number of elements available for writing. This is the total
     * number of writable elements excluding what's readable (already written).
     */
    [[nodiscard]] auto writeSpace() const noexcept -> std::size_t
    { return mWriteSize - readSpace(); }

    /**
     * The copying data writer. Copy at most `count' elements from `src'. Returns
     * the actual number of elements copied.
     */
    [[nodiscard]] auto write(const void *src, std::size_t count) noexcept -> std::size_t;

    /**
     * The non-copying data writer. Returns two ringbuffer data pointers that
     * hold the current writeable data. If the writeable data is in one segment
     * the second segment has zero length.
     */
    [[nodiscard]] auto getWriteVector() noexcept -> DataPair;
    /** Advance the write pointer `count' places. */
    auto writeAdvance(std::size_t count) noexcept -> void
    {
        const std::size_t w{mWriteCount.load(std::memory_order_relaxed)};
        const std::size_t r{mReadCount.load(std::memory_order_acquire)};
        [[maybe_unused]] const std::size_t writable{mWriteSize - (w - r)};
        assert(writable >= count);
        mWriteCount.store(w+count, std::memory_order_release);
    }

    [[nodiscard]] auto getElemSize() const noexcept -> std::size_t { return mElemSize; }

    /**
     * Create a new ringbuffer to hold at least `sz' elements of `elem_sz'
     * bytes. The number of elements is rounded up to a power of two. If
     * `limit_writes' is true, the writable space will be limited to `sz'
     * elements regardless of the rounded size.
     */
    [[nodiscard]] static
    auto Create(std::size_t sz, std::size_t elem_sz, bool limit_writes) -> std::unique_ptr<RingBuffer>;

    DEF_FAM_NEWDEL(RingBuffer, mBuffer)
};
using RingBufferPtr = std::unique_ptr<RingBuffer>;


/* A ring buffer like the above, except that the read/write vectors return
 * spans, sized according to the number of readable/writable values instead of
 * number of elements. The storage type is also templated rather than always
 * std::byte, but must be trivially copyable.
 */
template<typename T>
class RingBuffer2 {
    static_assert(std::is_trivially_copyable_v<T>);

#if defined(__cpp_lib_hardware_interference_size) && !defined(_LIBCPP_VERSION)
    static constexpr std::size_t sCacheAlignment{std::hardware_destructive_interference_size};
#else
    /* Assume a 64-byte cache line, the most common/likely value. */
    static constexpr std::size_t sCacheAlignment{64};
#endif
    alignas(sCacheAlignment) std::atomic<std::size_t> mWriteCount{0_uz};
    alignas(sCacheAlignment) std::atomic<std::size_t> mReadCount{0_uz};

    alignas(sCacheAlignment) const std::size_t mWriteSize;
    const std::size_t mSizeMask;
    const std::size_t mElemSize;

    al::FlexArray<T, 16> mBuffer;

public:
    using DataPair = std::array<std::span<T>,2>;

    RingBuffer2(const std::size_t writesize, const std::size_t mask, const std::size_t elemsize,
        const std::size_t numvals)
        : mWriteSize{writesize}, mSizeMask{mask}, mElemSize{elemsize}, mBuffer{numvals}
    { }

    /** Reset the read and write pointers to zero. This is not thread safe. */
    auto reset() noexcept -> void
    {
        mWriteCount.store(0_uz, std::memory_order_relaxed);
        mReadCount.store(0_uz, std::memory_order_relaxed);
        std::ranges::fill(mBuffer, T{});
    }

    /**
     * Return the number of elements available for reading. This is the number
     * of elements in front of the read pointer and behind the write pointer.
     */
    [[nodiscard]] auto readSpace() const noexcept -> std::size_t
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_acquire);
        /* mWriteCount is never more than mWriteSize greater than mReadCount. */
        return w - r;
    }

    /**
     * The copying data reader. Copy as many complete elements into `dest' as
     * possible. Returns the actual number of elements (not values!) copied.
     */
    [[nodiscard]] NOINLINE auto read(const std::span<T> dest) noexcept -> std::size_t
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_relaxed);
        const auto readable = w - r;
        if(readable == 0) return 0;

        const auto to_read = std::min(dest.size()/mElemSize, readable);
        const auto read_idx = r & mSizeMask;

        const auto rdend = read_idx + to_read;
        const auto [n1, n2] = (rdend <= mSizeMask+1) ? std::array{to_read, 0_uz}
            : std::array{mSizeMask+1 - read_idx, rdend&mSizeMask};

        auto outiter = std::ranges::copy(mBuffer | std::views::drop(read_idx*mElemSize)
            | std::views::take(n1*mElemSize), dest.begin()).out;
        std::ranges::copy(mBuffer | std::views::take(n2*mElemSize), outiter);
        mReadCount.store(r+n1+n2, std::memory_order_release);
        return to_read;
    }

    /**
     * The copying data reader w/o read pointer advance. Copy as many complete
     * elements into `dest' as possible. Returns the actual number of elements
     * (not values!) copied.
     */
    [[nodiscard]] NOINLINE auto peek(const std::span<T> dest) noexcept -> std::size_t
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_relaxed);
        const auto readable = w - r;
        if(readable == 0) return 0;

        const auto to_read = std::min(dest.size()/mElemSize, readable);
        const auto read_idx = r & mSizeMask;

        const auto rdend = read_idx + to_read;
        const auto [n1, n2] = (rdend <= mSizeMask+1) ? std::array{to_read, 0_uz}
            : std::array{mSizeMask+1 - read_idx, rdend&mSizeMask};

        auto outiter = std::ranges::copy(mBuffer | std::views::drop(read_idx*mElemSize)
            | std::views::take(n1*mElemSize), dest.begin()).out;
        std::ranges::copy(mBuffer | std::views::take(n2*mElemSize), outiter);

        return to_read;
    }

    /**
     * The non-copying data reader. Returns two spans that hold the current
     * readable data. If the readable data is fully in one segment, the second
     * segment has zero length.
     */
    [[nodiscard]] NOINLINE auto getReadVector() noexcept -> DataPair
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_relaxed);
        const auto readable = w - r;
        const auto read_idx = r & mSizeMask;

        const auto rdend = read_idx + readable;
        if(rdend > mSizeMask+1)
        {
            /* Two part vector: the rest of the buffer after the current read
             * ptr, plus some from the start of the buffer.
             */
            return DataPair{std::span{mBuffer}.subspan(read_idx*mElemSize),
                std::span{mBuffer}.first(rdend&mSizeMask)};
        }
        return DataPair{std::span{mBuffer}.subspan(read_idx*mElemSize, readable), {}};
    }

    /** Advance the read pointer `count' places. */
    auto readAdvance(std::size_t count) noexcept -> void
    {
        const std::size_t w = mWriteCount.load(std::memory_order_acquire);
        const std::size_t r = mReadCount.load(std::memory_order_relaxed);
        const auto readable [[maybe_unused]] = w - r;
        assert(readable >= count);
        mReadCount.store(r+count, std::memory_order_release);
    }


    /**
     * Return the number of elements available for writing. This is the total
     * number of writable elements excluding what's readable (already written).
     */
    [[nodiscard]] auto writeSpace() const noexcept -> std::size_t
    { return mWriteSize - readSpace(); }

    /**
     * The copying data writer. Copy as many elements from `src' as possible.
     * Returns the actual number of elements (not values!) copied.
     */
    [[nodiscard]] NOINLINE auto write(const std::span<const T> src) noexcept -> std::size_t
    {
        const auto w = mWriteCount.load(std::memory_order_relaxed);
        const auto r = mReadCount.load(std::memory_order_acquire);
        const auto writable = mWriteSize - (w - r);
        if(writable == 0) return 0;

        const auto to_write = std::min(src.size()/mElemSize, writable);
        const auto write_idx = w & mSizeMask;

        const auto wrend = write_idx + to_write;
        const auto [n1, n2] = (wrend <= mSizeMask+1) ? std::array{to_write, 0_uz}
            : std::array{mSizeMask+1 - write_idx, wrend&mSizeMask};

        std::ranges::copy(src.first(n1*mElemSize),
            (mBuffer | std::views::drop(write_idx*mElemSize)).begin());
        std::ranges::copy(src.subspan(n1*mElemSize, n2*mElemSize), mBuffer.begin());
        mWriteCount.store(w+n1+n2, std::memory_order_release);
        return to_write;
    }

    /**
     * The non-copying data writer. Returns two ringbuffer data pointers that
     * hold the current writeable data. If the writeable data is in one segment
     * the second segment has zero length.
     */
    [[nodiscard]] NOINLINE auto getWriteVector() noexcept -> DataPair
    {
        const auto w = mWriteCount.load(std::memory_order_relaxed);
        const auto r = mReadCount.load(std::memory_order_acquire);
        const auto writable = mWriteSize - (w - r);
        const auto write_idx = w & mSizeMask;

        const auto wrend = write_idx + writable;
        if(wrend > mSizeMask+1)
        {
            /* Two part vector: the rest of the buffer after the current write
             * ptr, plus some from the start of the buffer.
             */
            return DataPair{std::span{mBuffer}.subspan(write_idx*mElemSize),
                std::span{mBuffer}.first(wrend&mSizeMask)};
        }
        return DataPair{std::span{mBuffer}.subspan(write_idx*mElemSize, writable), {}};
    }

    /** Advance the write pointer `count' places. */
    auto writeAdvance(std::size_t count) noexcept -> void
    {
        const std::size_t w{mWriteCount.load(std::memory_order_relaxed)};
        const std::size_t r{mReadCount.load(std::memory_order_acquire)};
        [[maybe_unused]] const std::size_t writable{mWriteSize - (w - r)};
        assert(writable >= count);
        mWriteCount.store(w+count, std::memory_order_release);
    }

    /** Returns the number of values per element. */
    [[nodiscard]] auto getElemSize() const noexcept -> std::size_t { return mElemSize; }

    /**
     * Create a new ringbuffer to hold at least `sz' elements of `elem_sz'
     * values. The number of elements is rounded up to a power of two. If
     * `limit_writes' is true, the writable space will be limited to `sz'
     * elements regardless of the rounded size.
     */
    [[nodiscard]] static
    auto Create(std::size_t sz, std::size_t elem_sz, bool limit_writes)
        -> std::unique_ptr<RingBuffer2>
    {
        auto power_of_two = 0_uz;
        if(sz > 0)
        {
            power_of_two = sz - 1;
            power_of_two |= power_of_two>>1;
            power_of_two |= power_of_two>>2;
            power_of_two |= power_of_two>>4;
            power_of_two |= power_of_two>>8;
            power_of_two |= power_of_two>>16;
            if constexpr(sizeof(size_t) > sizeof(uint32_t))
                power_of_two |= power_of_two>>32;
        }
        ++power_of_two;
        if(power_of_two < sz || power_of_two > std::numeric_limits<std::size_t>::max()>>1
            || power_of_two > std::numeric_limits<std::size_t>::max()/elem_sz)
            throw std::overflow_error{"Ring buffer size overflow"};

        const auto numvals = power_of_two * elem_sz;
        return std::unique_ptr<RingBuffer2>{new(FamCount(numvals)) RingBuffer2{
            limit_writes ? sz : power_of_two, power_of_two-1, elem_sz, numvals}};
    }

    DEF_FAM_NEWDEL(RingBuffer2, mBuffer)
};
template<typename T>
using RingBuffer2Ptr = std::unique_ptr<RingBuffer2<T>>;


/* A FIFO buffer, modelled after the above but retains type information, and
 * can work with non-trivial types, and does not support multiple values per
 * element. Unreadable elements are in a destructed state.
 */
template<typename T>
class FifoBuffer {
#if defined(__cpp_lib_hardware_interference_size) && !defined(_LIBCPP_VERSION)
    static constexpr auto sCacheAlignment = std::hardware_destructive_interference_size;
#else
    /* Assume a 64-byte cache line, the most common/likely value. */
    static constexpr auto sCacheAlignment = 64_uz;
#endif
    alignas(sCacheAlignment) std::atomic<std::size_t> mWriteCount{0_uz};
    alignas(sCacheAlignment) std::atomic<std::size_t> mReadCount{0_uz};

    alignas(sCacheAlignment) const std::size_t mWriteSize;
    const std::size_t mSizeMask;

    const std::span<T> mStorage;

    FifoBuffer(const std::size_t writesize, const std::size_t mask, const std::span<T> storage)
        : mWriteSize{writesize}, mSizeMask{mask}, mStorage{storage}
    { }

    NOINLINE ~FifoBuffer()
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_relaxed);
        const auto readable = w - r;
        if(w == r) return;

        const auto read_idx = r & mSizeMask;
        const auto rdend = read_idx + readable;
        const auto [n1, n2] = (rdend <= mSizeMask+1) ? std::array{readable, 0_uz}
            : std::array{mSizeMask+1 - read_idx, rdend&mSizeMask};

        std::destroy_n(mStorage.subspan(read_idx).begin(), n1);
        std::destroy_n(mStorage.begin(), n2);
    }

public:
    using value_type = T;

    using DataPair = std::array<std::span<T>,2>;

    /** Reset the read and write pointers to zero. This is not thread safe. */
    NOINLINE auto reset() noexcept -> void
    {
        const auto w = mWriteCount.load(std::memory_order_relaxed);
        const auto r = mReadCount.load(std::memory_order_relaxed);
        const auto readable = w - r;
        if(w == r) return;

        const auto read_idx = r & mSizeMask;
        const auto rdend = read_idx + readable;
        const auto [n1, n2] = (rdend <= mSizeMask+1) ? std::array{readable, 0_uz}
            : std::array{mSizeMask+1 - read_idx, rdend&mSizeMask};

        std::destroy_n(mStorage.subspan(read_idx).begin(), n1);
        std::destroy_n(mStorage.begin(), n2);

        mWriteCount.store(0_uz, std::memory_order_relaxed);
        mReadCount.store(0_uz, std::memory_order_relaxed);
    }

    /**
     * Return the number of elements available for reading. This is the number
     * of elements in front of the read pointer and behind the write pointer.
     */
    [[nodiscard]] auto readSpace() const noexcept -> std::size_t
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_acquire);
        /* mWriteCount is never more than mWriteSize greater than mReadCount. */
        return w - r;
    }

    /**
     * The copying data reader. Move as many elements into `dest' as are
     * available and can fit. Returns the actual number of elements moved.
     */
    [[nodiscard]] NOINLINE auto read(const std::span<T> dest) noexcept -> std::size_t
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_relaxed);
        const auto readable = w - r;
        if(w == r) return 0_uz;

        const auto to_read = std::min(dest.size(), readable);
        const auto read_idx = r & mSizeMask;

        const auto rdend = read_idx + to_read;
        const auto [n1, n2] = (rdend <= mSizeMask+1) ? std::array{to_read, 0_uz}
            : std::array{mSizeMask+1 - read_idx, rdend&mSizeMask};

        auto firstrange = mStorage.subspan(read_idx, n1);
        auto dstiter = std::ranges::move(firstrange, dest.begin()).out;
        std::ranges::move(mStorage.first(n2), dstiter);

        std::destroy_n(firstrange.begin(), n1);
        std::destroy_n(mStorage.begin(), n2);

        mReadCount.store(r+n1+n2, std::memory_order_release);
        return to_read;
    }

    /**
     * The copying data reader w/o read pointer advance. Copy as many elements
     * into `dest' as are available and can fit. Returns the actual number of
     * elements copied.
     */
    [[nodiscard]] NOINLINE auto peek(const std::span<T> dest) noexcept -> std::size_t
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_relaxed);
        const auto readable = w - r;
        if(w == r) return 0_uz;

        const auto to_read = std::min(dest.size(), readable);
        const auto read_idx = r & mSizeMask;

        const auto rdend = read_idx + to_read;
        const auto [n1, n2] = (rdend <= mSizeMask+1) ? std::array{to_read, 0_uz}
            : std::array{mSizeMask+1 - read_idx, rdend&mSizeMask};

        auto firstrange = mStorage.subspan(read_idx, n1);
        auto dstiter = std::ranges::copy(firstrange, dest.begin()).out;
        std::ranges::copy(mStorage.first(n2), dstiter);

        return to_read;
    }

    /**
     * The non-copying data reader. Returns two spans that hold the current
     * readable data. If the readable data is in one segment the second segment
     * has zero length.
     */
    [[nodiscard]] NOINLINE auto getReadVector() noexcept -> DataPair
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_relaxed);
        const auto readable = w - r;
        const auto read_idx = r & mSizeMask;

        const auto rdend = read_idx + readable;
        if(rdend > mSizeMask+1)
        {
            /* Two part vector: the rest of the buffer after the current read
             * ptr, plus some from the start of the buffer.
             */
            return DataPair{mStorage.subspan(read_idx), mStorage.first(rdend&mSizeMask)};
        }
        return DataPair{mStorage.subspan(read_idx, readable), {}};
    }

    /** Advance the read pointer `count' places. */
    auto readAdvance(std::size_t count) noexcept -> void
    {
        const auto w = mWriteCount.load(std::memory_order_acquire);
        const auto r = mReadCount.load(std::memory_order_relaxed);
        const auto readable = w - r;
        assert(readable >= count);

        const auto to_read = std::min(count, readable);
        const auto read_idx = r & mSizeMask;

        const auto rdend = read_idx + to_read;
        const auto [n1, n2] = (rdend <= mSizeMask+1) ? std::array{to_read, 0_uz}
            : std::array{mSizeMask+1 - read_idx, rdend&mSizeMask};

        std::destroy_n(mStorage.subspan(read_idx).begin(), n1);
        std::destroy_n(mStorage.begin(), n2);

        mReadCount.store(r+count, std::memory_order_release);
    }


    /**
     * Return the number of elements available for writing. This is the total
     * number of writable elements excluding what's readable (already written).
     */
    [[nodiscard]] auto writeSpace() const noexcept -> std::size_t
    { return mWriteSize - readSpace(); }

    /**
     * The copying data writer. Copy as many elements from `src' as can fit.
     * Returns the actual number of elements copied.
     */
    [[nodiscard]] NOINLINE auto write(const std::span<const T> src) noexcept -> std::size_t
    {
        const auto w = mWriteCount.load(std::memory_order_relaxed);
        const auto r = mReadCount.load(std::memory_order_acquire);
        const auto writable = mWriteSize - (w - r);
        if(writable == 0) return 0_uz;

        const auto to_write = std::min(src.size(), writable);
        const auto write_idx = w & mSizeMask;

        const auto wrend = write_idx + to_write;
        const auto [n1, n2] = (wrend <= mSizeMask+1) ? std::array{to_write, 0_uz}
            : std::array{mSizeMask+1 - write_idx, wrend&mSizeMask};

        std::ranges::uninitialized_copy(src.first(n1), mStorage.subspan(write_idx));
        std::ranges::uninitialized_copy(src.subspan(n1), mStorage.first(n2));

        mWriteCount.store(w+n1+n2, std::memory_order_release);
        return to_write;
    }

    /**
     * The non-copying data writer. Returns two FIFO buffer spans that hold the
     * current writeable *uninitialized* elements. If the writeable data is all
     * in one segment the second segment has zero length.
     */
    [[nodiscard]] NOINLINE auto getWriteVector() noexcept -> DataPair
    {
        const auto w = mWriteCount.load(std::memory_order_relaxed);
        const auto r = mReadCount.load(std::memory_order_acquire);
        const auto writable = mWriteSize - (w - r);
        const auto write_idx = w & mSizeMask;

        const auto wrend = write_idx + writable;
        if(wrend > mSizeMask+1)
        {
            /* Two part vector: the rest of the buffer after the current write
             * ptr, plus some from the start of the buffer.
             */
            return DataPair{mStorage.subspan(write_idx), mStorage.first(wrend&mSizeMask)};
        }
        return DataPair{mStorage.subspan(write_idx, writable), {}};
    }

    /**
     * Advance the write pointer `count' places. The caller is responsible for
     * having initialized the elements through getWriteVector.
     */
    auto writeAdvance(std::size_t count) noexcept -> void
    {
        const auto w = mWriteCount.load(std::memory_order_relaxed);
        const auto r = mReadCount.load(std::memory_order_acquire);
        const auto writable [[maybe_unused]] = mWriteSize - (w - r);
        assert(writable >= count);
        mWriteCount.store(w+count, std::memory_order_release);
    }


    static void Destroy(gsl::owner<FifoBuffer*> fifo) noexcept
    {
        fifo->~FifoBuffer();
        ::operator delete(fifo, std::align_val_t{alignof(FifoBuffer)});
    }

    struct Deleter {
        void operator()(gsl::owner<FifoBuffer*> ptr) const noexcept { Destroy(ptr); }
    };

    /**
     * Create a new FIFO buffer to hold at least `count' elements of the given
     * type. The number of elements is rounded up to a power of two. If
     * `limit_writes' is true, the writable space will be limited to `count'
     * elements regardless of the rounded size.
     */
    [[nodiscard]] static
    auto Create(std::size_t count, bool limit_writes) -> std::unique_ptr<FifoBuffer,Deleter>
    {
        auto power_of_two = 0_uz;
        if(count > 0)
        {
            power_of_two = count - 1;
            power_of_two |= power_of_two>>1;
            power_of_two |= power_of_two>>2;
            power_of_two |= power_of_two>>4;
            power_of_two |= power_of_two>>8;
            power_of_two |= power_of_two>>16;
            if constexpr(sizeof(size_t) > sizeof(uint32_t))
                power_of_two |= power_of_two>>32;
        }
        ++power_of_two;
        if(power_of_two < count || power_of_two > std::numeric_limits<std::size_t>::max()>>1
            || power_of_two > std::numeric_limits<std::size_t>::max()/sizeof(T))
            throw std::overflow_error{"FIFO buffer size overflow"};

        const auto numbytes = sizeof(FifoBuffer) + power_of_two*sizeof(T);
        auto storage = static_cast<gsl::owner<std::byte*>>(::operator new(numbytes,
            std::align_val_t{alignof(FifoBuffer)}));

        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic) */
        auto extrastore = std::span{reinterpret_cast<T*>(&storage[sizeof(FifoBuffer)]),
            power_of_two};

        return std::unique_ptr<FifoBuffer,Deleter>(::new(storage) FifoBuffer{
            limit_writes ? count : power_of_two, power_of_two-1, extrastore});
    }
};
template<typename T>
using FifoBufferPtr = std::unique_ptr<FifoBuffer<T>, typename FifoBuffer<T>::Deleter>;

#endif /* RINGBUFFER_H */
