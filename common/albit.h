#ifndef AL_BIT_H
#define AL_BIT_H

namespace al {

#ifdef __BYTE_ORDER__
enum class endian {
    little = __ORDER_LITTLE_ENDIAN__,
    big = __ORDER_BIG_ENDIAN__,
    native = __BYTE_ORDER__
};

#else

/* This doesn't support mixed-endian. */
namespace detail_ {
constexpr inline bool EndianTest() noexcept
{
    static_assert(sizeof(char) < sizeof(int), "char is too big");

    constexpr int test_val{1};
    return static_cast<const char&>(test_val);
}
} // namespace detail_

enum class endian {
    little = 0,
    big = 1,
    native = detail_::EndianTest() ? little : big
};
#endif

} // namespace al

#endif /* AL_BIT_H */
