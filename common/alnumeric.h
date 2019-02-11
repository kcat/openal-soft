#ifndef AL_NUMERIC_H
#define AL_NUMERIC_H

#include <stdint.h>

inline constexpr int64_t operator "" _i64(unsigned long long int n) noexcept { return static_cast<int64_t>(n); }
inline constexpr uint64_t operator "" _u64(unsigned long long int n) noexcept { return static_cast<uint64_t>(n); }

/** Find the next power-of-2 for non-power-of-2 numbers. */
inline uint32_t NextPowerOf2(uint32_t value) noexcept
{
    if(value > 0)
    {
        value--;
        value |= value>>1;
        value |= value>>2;
        value |= value>>4;
        value |= value>>8;
        value |= value>>16;
    }
    return value+1;
}

/** Round up a value to the next multiple. */
inline size_t RoundUp(size_t value, size_t r) noexcept
{
    value += r-1;
    return value - (value%r);
}

#endif /* AL_NUMERIC_H */
