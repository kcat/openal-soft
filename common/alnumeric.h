#ifndef AL_NUMERIC_H
#define AL_NUMERIC_H

#include <stdint.h>

inline constexpr int64_t operator "" _i64(unsigned long long int n) noexcept { return static_cast<int64_t>(n); }
inline constexpr uint64_t operator "" _u64(unsigned long long int n) noexcept { return static_cast<uint64_t>(n); }


constexpr inline float minf(float a, float b) noexcept
{ return ((a > b) ? b : a); }
constexpr inline float maxf(float a, float b) noexcept
{ return ((a > b) ? a : b); }
constexpr inline float clampf(float val, float min, float max) noexcept
{ return minf(max, maxf(min, val)); }

constexpr inline double mind(double a, double b) noexcept
{ return ((a > b) ? b : a); }
constexpr inline double maxd(double a, double b) noexcept
{ return ((a > b) ? a : b); }
constexpr inline double clampd(double val, double min, double max) noexcept
{ return mind(max, maxd(min, val)); }

constexpr inline unsigned int minu(unsigned int a, unsigned int b) noexcept
{ return ((a > b) ? b : a); }
constexpr inline unsigned int maxu(unsigned int a, unsigned int b) noexcept
{ return ((a > b) ? a : b); }
constexpr inline unsigned int clampu(unsigned int val, unsigned int min, unsigned int max) noexcept
{ return minu(max, maxu(min, val)); }

constexpr inline int mini(int a, int b) noexcept
{ return ((a > b) ? b : a); }
constexpr inline int maxi(int a, int b) noexcept
{ return ((a > b) ? a : b); }
constexpr inline int clampi(int val, int min, int max) noexcept
{ return mini(max, maxi(min, val)); }

constexpr inline int64_t mini64(int64_t a, int64_t b) noexcept
{ return ((a > b) ? b : a); }
constexpr inline int64_t maxi64(int64_t a, int64_t b) noexcept
{ return ((a > b) ? a : b); }
constexpr inline int64_t clampi64(int64_t val, int64_t min, int64_t max) noexcept
{ return mini64(max, maxi64(min, val)); }

constexpr inline uint64_t minu64(uint64_t a, uint64_t b) noexcept
{ return ((a > b) ? b : a); }
constexpr inline uint64_t maxu64(uint64_t a, uint64_t b) noexcept
{ return ((a > b) ? a : b); }
constexpr inline uint64_t clampu64(uint64_t val, uint64_t min, uint64_t max) noexcept
{ return minu64(max, maxu64(min, val)); }

constexpr inline size_t minz(size_t a, size_t b) noexcept
{ return ((a > b) ? b : a); }
constexpr inline size_t maxz(size_t a, size_t b) noexcept
{ return ((a > b) ? a : b); }
constexpr inline size_t clampz(size_t val, size_t min, size_t max) noexcept
{ return minz(max, maxz(min, val)); }


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
