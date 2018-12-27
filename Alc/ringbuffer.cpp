/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <algorithm>

#include "ringbuffer.h"
#include "atomic.h"
#include "threads.h"
#include "almalloc.h"
#include "compat.h"


RingBufferPtr CreateRingBuffer(size_t sz, size_t elem_sz, int limit_writes)
{
    size_t power_of_two{0u};
    if(sz > 0)
    {
        power_of_two = sz;
        power_of_two |= power_of_two>>1;
        power_of_two |= power_of_two>>2;
        power_of_two |= power_of_two>>4;
        power_of_two |= power_of_two>>8;
        power_of_two |= power_of_two>>16;
#if SIZE_MAX > UINT_MAX
        power_of_two |= power_of_two>>32;
#endif
    }
    ++power_of_two;
    if(power_of_two < sz) return nullptr;

    RingBufferPtr rb{new (al_malloc(16, sizeof(*rb) + power_of_two*elem_sz)) RingBuffer{}};
    rb->mSize = limit_writes ? sz : power_of_two;
    rb->mSizeMask = power_of_two - 1;
    rb->mElemSize = elem_sz;

    return rb;
}

void RingBuffer::reset() noexcept
{
    mWritePtr.store(0, std::memory_order_relaxed);
    mReadPtr.store(0, std::memory_order_relaxed);
    std::fill_n(mBuffer, (mSizeMask+1)*mElemSize, 0);
}


size_t RingBuffer::readSpace() const noexcept
{
    size_t w = mWritePtr.load(std::memory_order_acquire);
    size_t r = mReadPtr.load(std::memory_order_acquire);
    return (w-r) & mSizeMask;
}

size_t RingBuffer::writeSpace() const noexcept
{
    size_t w = mWritePtr.load(std::memory_order_acquire);
    size_t r = mReadPtr.load(std::memory_order_acquire);
    w = (r-w-1) & mSizeMask;
    return std::max(w, mSize);
}


size_t RingBuffer::read(void *dest, size_t cnt) noexcept
{
    const size_t free_cnt{readSpace()};
    if(free_cnt == 0) return 0;

    const size_t to_read{std::min(cnt, free_cnt)};
    size_t read_ptr{mReadPtr.load(std::memory_order_relaxed) & mSizeMask};

    size_t n1, n2;
    const size_t cnt2{read_ptr + to_read};
    if(cnt2 > mSizeMask+1)
    {
        n1 = mSizeMask+1 - read_ptr;
        n2 = cnt2 & mSizeMask;
    }
    else
    {
        n1 = to_read;
        n2 = 0;
    }

    memcpy(dest, &mBuffer[read_ptr*mElemSize], n1*mElemSize);
    read_ptr += n1;
    if(n2)
    {
        memcpy(static_cast<char*>(dest) + n1*mElemSize, &mBuffer[(read_ptr&mSizeMask)*mElemSize],
            n2*mElemSize);
        read_ptr += n2;
    }
    mReadPtr.store(read_ptr, std::memory_order_release);
    return to_read;
}

size_t RingBuffer::peek(void *dest, size_t cnt) const noexcept
{
    const size_t free_cnt{readSpace()};
    if(free_cnt == 0) return 0;

    const size_t to_read{std::min(cnt, free_cnt)};
    size_t read_ptr{mReadPtr.load(std::memory_order_relaxed) & mSizeMask};

    size_t n1, n2;
    const size_t cnt2{read_ptr + to_read};
    if(cnt2 > mSizeMask+1)
    {
        n1 = mSizeMask+1 - read_ptr;
        n2 = cnt2 & mSizeMask;
    }
    else
    {
        n1 = to_read;
        n2 = 0;
    }

    memcpy(dest, &mBuffer[read_ptr*mElemSize], n1*mElemSize);
    if(n2)
    {
        read_ptr += n1;
        memcpy(static_cast<char*>(dest) + n1*mElemSize, &mBuffer[(read_ptr&mSizeMask)*mElemSize],
            n2*mElemSize);
    }
    return to_read;
}

size_t RingBuffer::write(const void *src, size_t cnt) noexcept
{
    const size_t free_cnt{writeSpace()};
    if(free_cnt == 0) return 0;

    const size_t to_write{std::min(cnt, free_cnt)};
    size_t write_ptr{mWritePtr.load(std::memory_order_relaxed) & mSizeMask};

    size_t n1, n2;
    const size_t cnt2{write_ptr + to_write};
    if(cnt2 > mSizeMask+1)
    {
        n1 = mSizeMask+1 - write_ptr;
        n2 = cnt2 & mSizeMask;
    }
    else
    {
        n1 = to_write;
        n2 = 0;
    }

    memcpy(&mBuffer[write_ptr*mElemSize], src, n1*mElemSize);
    write_ptr += n1;
    if(n2)
    {
        memcpy(&mBuffer[(write_ptr&mSizeMask)*mElemSize],
            static_cast<const char*>(src) + n1*mElemSize, n2*mElemSize);
        write_ptr += n2;
    }
    mWritePtr.store(write_ptr, std::memory_order_release);
    return to_write;
}


void RingBuffer::readAdvance(size_t cnt) noexcept
{
    mReadPtr.fetch_add(cnt, std::memory_order_acq_rel);
}

void RingBuffer::writeAdvance(size_t cnt) noexcept
{
    mWritePtr.fetch_add(cnt, std::memory_order_acq_rel);
}


ll_ringbuffer_data_pair RingBuffer::getReadVector() const noexcept
{
    ll_ringbuffer_data_pair ret;

    size_t w{mWritePtr.load(std::memory_order_acquire)};
    size_t r{mReadPtr.load(std::memory_order_acquire)};
    w &= mSizeMask;
    r &= mSizeMask;
    const size_t free_cnt{(w-r) & mSizeMask};

    const size_t cnt2{r + free_cnt};
    if(cnt2 > mSizeMask+1)
    {
        /* Two part vector: the rest of the buffer after the current read ptr,
         * plus some from the start of the buffer. */
        ret.first.buf = const_cast<char*>(&mBuffer[r*mElemSize]);
        ret.first.len = mSizeMask+1 - r;
        ret.second.buf = const_cast<char*>(mBuffer);
        ret.second.len = cnt2 & mSizeMask;
    }
    else
    {
        /* Single part vector: just the rest of the buffer */
        ret.first.buf = const_cast<char*>(&mBuffer[r*mElemSize]);
        ret.first.len = free_cnt;
        ret.second.buf = nullptr;
        ret.second.len = 0;
    }

    return ret;
}

ll_ringbuffer_data_pair RingBuffer::getWriteVector() const noexcept
{
    ll_ringbuffer_data_pair ret;

    size_t w{mWritePtr.load(std::memory_order_acquire)};
    size_t r{mReadPtr.load(std::memory_order_acquire)};
    w &= mSizeMask;
    r &= mSizeMask;
    const size_t free_cnt{std::min((r-w-1) & mSizeMask, mSize)};

    const size_t cnt2{w + free_cnt};
    if(cnt2 > mSizeMask+1)
    {
        /* Two part vector: the rest of the buffer after the current write ptr,
         * plus some from the start of the buffer. */
        ret.first.buf = const_cast<char*>(&mBuffer[w*mElemSize]);
        ret.first.len = mSizeMask+1 - w;
        ret.second.buf = const_cast<char*>(mBuffer);
        ret.second.len = cnt2 & mSizeMask;
    }
    else
    {
        ret.first.buf = const_cast<char*>(&mBuffer[w*mElemSize]);
        ret.first.len = free_cnt;
        ret.second.buf = nullptr;
        ret.second.len = 0;
    }

    return ret;
}
