#ifndef OPTHELPERS_H
#define OPTHELPERS_H

#include <type_traits>

#ifdef __has_builtin
#define HAS_BUILTIN __has_builtin
#else
#define HAS_BUILTIN(x) (0)
#endif

#ifdef __has_cpp_attribute
#define HAS_ATTRIBUTE __has_cpp_attribute
#else
#define HAS_ATTRIBUTE(x) (0)
#endif

#ifdef __GNUC__
#define force_inline [[gnu::always_inline]] inline
#define NOINLINE [[gnu::noinline]]
#elif defined(_MSC_VER)
#define force_inline __forceinline
#define NOINLINE __declspec(noinline)
#else
#define force_inline inline
#define NOINLINE
#endif

/* Unlike the likely attribute, ASSUME requires the condition to be true or
 * else it invokes undefined behavior. It's essentially an assert without
 * actually checking the condition at run-time, allowing for stronger
 * optimizations than the likely attribute.
 */
#if HAS_BUILTIN(__builtin_assume)
#define ASSUME __builtin_assume
#elif defined(_MSC_VER)
#define ASSUME __assume
#elif __has_attribute(assume)
#define ASSUME(x) [[assume(x)]]
#elif HAS_BUILTIN(__builtin_unreachable)
#define ASSUME(x) do { if(x) break; __builtin_unreachable(); } while(0)
#else
#define ASSUME(x) (static_cast<void>(0))
#endif

#if !defined(_WIN32) && HAS_ATTRIBUTE(gnu::visibility)
#define DECL_HIDDEN [[gnu::visibility("hidden")]]
#else
#define DECL_HIDDEN
#endif

#if HAS_ATTRIBUTE(clang::lifetimebound)
#define LIFETIMEBOUND [[clang::lifetimebound]]
#elif HAS_ATTRIBUTE(msvc::lifetimebound)
#define LIFETIMEBOUND [[msvc::lifetimebound]]
#elif HAS_ATTRIBUTE(lifetimebound)
#define LIFETIMEBOUND [[lifetimebound]]
#else
#define LIFETIMEBOUND
#endif

namespace al {

template<typename T>
constexpr std::underlying_type_t<T> to_underlying(T e) noexcept
{ return static_cast<std::underlying_type_t<T>>(e); }

} // namespace al

#endif /* OPTHELPERS_H */
