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

#include "ringbuffer.h"
#include "atomic.h"
#include "threads.h"
#include "almalloc.h"
#include "compat.h"


/* NOTE: This lockless ringbuffer implementation is copied from JACK, extended
 * to include an element size. Consequently, parameters and return values for a
 * size or count is in 'elements', not bytes. Additionally, it only supports
 * single-consumer/single-provider operation. */
struct ll_ringbuffer {
    std::atomic<size_t> write_ptr;
    std::atomic<size_t> read_ptr;
    size_t size;
    size_t size_mask;
    size_t elem_size;

    alignas(16) char buf[];
};

ll_ringbuffer_t *ll_ringbuffer_create(size_t sz, size_t elem_sz, int limit_writes)
{
    ll_ringbuffer_t *rb;
    size_t power_of_two = 0;

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
    power_of_two++;
    if(power_of_two < sz) return NULL;

    rb = static_cast<ll_ringbuffer_t*>(al_malloc(16, sizeof(*rb) + power_of_two*elem_sz));
    if(!rb) return NULL;

    ATOMIC_INIT(&rb->write_ptr, static_cast<size_t>(0));
    ATOMIC_INIT(&rb->read_ptr, static_cast<size_t>(0));
    rb->size = limit_writes ? sz : power_of_two;
    rb->size_mask = power_of_two - 1;
    rb->elem_size = elem_sz;
    return rb;
}

void ll_ringbuffer_free(ll_ringbuffer_t *rb)
{
    al_free(rb);
}

void ll_ringbuffer_reset(ll_ringbuffer_t *rb)
{
    rb->write_ptr.store(0, std::memory_order_release);
    rb->read_ptr.store(0, std::memory_order_release);
    memset(rb->buf, 0, (rb->size_mask+1)*rb->elem_size);
}


size_t ll_ringbuffer_read_space(const ll_ringbuffer_t *rb)
{
    size_t w = rb->write_ptr.load(std::memory_order_acquire);
    size_t r = rb->read_ptr.load(std::memory_order_acquire);
    return (w-r) & rb->size_mask;
}

size_t ll_ringbuffer_write_space(const ll_ringbuffer_t *rb)
{
    size_t w = rb->write_ptr.load(std::memory_order_acquire);
    size_t r = rb->read_ptr.load(std::memory_order_acquire);
    w = (r-w-1) & rb->size_mask;
    return (w > rb->size) ? rb->size : w;
}


size_t ll_ringbuffer_read(ll_ringbuffer_t *rb, void *dest, size_t cnt)
{
    size_t read_ptr;
    size_t free_cnt;
    size_t cnt2;
    size_t to_read;
    size_t n1, n2;

    free_cnt = ll_ringbuffer_read_space(rb);
    if(free_cnt == 0) return 0;

    to_read = (cnt > free_cnt) ? free_cnt : cnt;
    read_ptr = rb->read_ptr.load(std::memory_order_relaxed) & rb->size_mask;

    cnt2 = read_ptr + to_read;
    if(cnt2 > rb->size_mask+1)
    {
        n1 = rb->size_mask+1 - read_ptr;
        n2 = cnt2 & rb->size_mask;
    }
    else
    {
        n1 = to_read;
        n2 = 0;
    }

    memcpy(dest, &rb->buf[read_ptr*rb->elem_size], n1*rb->elem_size);
    read_ptr += n1;
    if(n2)
    {
        memcpy(static_cast<char*>(dest) + n1*rb->elem_size,
               &rb->buf[(read_ptr&rb->size_mask)*rb->elem_size],
               n2*rb->elem_size);
        read_ptr += n2;
    }
    rb->read_ptr.store(read_ptr, std::memory_order_release);
    return to_read;
}

size_t ll_ringbuffer_peek(ll_ringbuffer_t *rb, void *dest, size_t cnt)
{
    size_t free_cnt;
    size_t cnt2;
    size_t to_read;
    size_t n1, n2;
    size_t read_ptr;

    free_cnt = ll_ringbuffer_read_space(rb);
    if(free_cnt == 0) return 0;

    to_read = (cnt > free_cnt) ? free_cnt : cnt;
    read_ptr = rb->read_ptr.load(std::memory_order_relaxed) & rb->size_mask;

    cnt2 = read_ptr + to_read;
    if(cnt2 > rb->size_mask+1)
    {
        n1 = rb->size_mask+1 - read_ptr;
        n2 = cnt2 & rb->size_mask;
    }
    else
    {
        n1 = to_read;
        n2 = 0;
    }

    memcpy(dest, &rb->buf[read_ptr*rb->elem_size], n1*rb->elem_size);
    if(n2)
    {
        read_ptr += n1;
        memcpy(static_cast<char*>(dest) + n1*rb->elem_size,
               &rb->buf[(read_ptr&rb->size_mask)*rb->elem_size],
               n2*rb->elem_size);
    }
    return to_read;
}

size_t ll_ringbuffer_write(ll_ringbuffer_t *rb, const void *src, size_t cnt)
{
    size_t write_ptr;
    size_t free_cnt;
    size_t cnt2;
    size_t to_write;
    size_t n1, n2;

    free_cnt = ll_ringbuffer_write_space(rb);
    if(free_cnt == 0) return 0;

    to_write = (cnt > free_cnt) ? free_cnt : cnt;
    write_ptr = rb->write_ptr.load(std::memory_order_relaxed) & rb->size_mask;

    cnt2 = write_ptr + to_write;
    if(cnt2 > rb->size_mask+1)
    {
        n1 = rb->size_mask+1 - write_ptr;
        n2 = cnt2 & rb->size_mask;
    }
    else
    {
        n1 = to_write;
        n2 = 0;
    }

    memcpy(&rb->buf[write_ptr*rb->elem_size], src, n1*rb->elem_size);
    write_ptr += n1;
    if(n2)
    {
        memcpy(&rb->buf[(write_ptr&rb->size_mask)*rb->elem_size],
               static_cast<const char*>(src) + n1*rb->elem_size,
               n2*rb->elem_size);
        write_ptr += n2;
    }
    rb->write_ptr.store(write_ptr, std::memory_order_release);
    return to_write;
}


void ll_ringbuffer_read_advance(ll_ringbuffer_t *rb, size_t cnt)
{
    rb->read_ptr.fetch_add(cnt, std::memory_order_acq_rel);
}

void ll_ringbuffer_write_advance(ll_ringbuffer_t *rb, size_t cnt)
{
    rb->write_ptr.fetch_add(cnt, std::memory_order_acq_rel);
}


ll_ringbuffer_data_pair ll_ringbuffer_get_read_vector(const ll_ringbuffer_t *rb)
{
    ll_ringbuffer_data_pair ret;
    size_t free_cnt;
    size_t cnt2;

    size_t w = rb->write_ptr.load(std::memory_order_acquire);
    size_t r = rb->read_ptr.load(std::memory_order_acquire);
    w &= rb->size_mask;
    r &= rb->size_mask;
    free_cnt = (w-r) & rb->size_mask;

    cnt2 = r + free_cnt;
    if(cnt2 > rb->size_mask+1)
    {
        /* Two part vector: the rest of the buffer after the current write ptr,
         * plus some from the start of the buffer. */
        ret.first.buf = const_cast<char*>(&rb->buf[r*rb->elem_size]);
        ret.first.len = rb->size_mask+1 - r;
        ret.second.buf = const_cast<char*>(rb->buf);
        ret.second.len = cnt2 & rb->size_mask;
    }
    else
    {
        /* Single part vector: just the rest of the buffer */
        ret.first.buf = const_cast<char*>(&rb->buf[r*rb->elem_size]);
        ret.first.len = free_cnt;
        ret.second.buf = nullptr;
        ret.second.len = 0;
    }

    return ret;
}

ll_ringbuffer_data_pair ll_ringbuffer_get_write_vector(const ll_ringbuffer_t *rb)
{
    ll_ringbuffer_data_pair ret;
    size_t free_cnt;
    size_t cnt2;

    size_t w = rb->write_ptr.load(std::memory_order_acquire);
    size_t r = rb->read_ptr.load(std::memory_order_acquire);
    w &= rb->size_mask;
    r &= rb->size_mask;
    free_cnt = (r-w-1) & rb->size_mask;
    if(free_cnt > rb->size) free_cnt = rb->size;

    cnt2 = w + free_cnt;
    if(cnt2 > rb->size_mask+1)
    {
        /* Two part vector: the rest of the buffer after the current write ptr,
         * plus some from the start of the buffer. */
        ret.first.buf = const_cast<char*>(&rb->buf[w*rb->elem_size]);
        ret.first.len = rb->size_mask+1 - w;
        ret.second.buf = const_cast<char*>(rb->buf);
        ret.second.len = cnt2 & rb->size_mask;
    }
    else
    {
        ret.first.buf = const_cast<char*>(&rb->buf[w*rb->elem_size]);
        ret.first.len = free_cnt;
        ret.second.buf = nullptr;
        ret.second.len = 0;
    }

    return ret;
}
