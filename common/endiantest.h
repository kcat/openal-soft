#ifndef AL_ENDIANTEST_H
#define AL_ENDIANTEST_H

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#define IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#else
constexpr inline bool EndianTest() noexcept
{
    constexpr int test_val{1};
    return static_cast<const char&>(test_val);
}
#define IS_LITTLE_ENDIAN (EndianTest())
#endif

#endif /* AL_ENDIANTEST_H */
