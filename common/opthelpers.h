#ifndef OPTHELPERS_H
#define OPTHELPERS_H

#include <utility>


#ifdef __has_builtin
#define HAS_BUILTIN __has_builtin
#else
#define HAS_BUILTIN(x) (0)
#endif

#if defined(__GNUC__) || HAS_BUILTIN(__builtin_expect)
/* likely() optimizes for the case where the condition is true. The condition
 * is not required to be true, but it can result in more optimal code for the
 * true path at the expense of a less optimal false path.
 */
template<typename T>
constexpr bool likely(T&& expr) noexcept
{ return __builtin_expect(static_cast<bool>(std::forward<T>(expr)), true); }
/* The opposite of likely(), optimizing for the case where the condition is
 * false.
 */
template<typename T>
constexpr bool unlikely(T&& expr) noexcept
{ return __builtin_expect(static_cast<bool>(std::forward<T>(expr)), false); }

#else

template<typename T>
constexpr bool likely(T&& expr) noexcept { return static_cast<bool>(std::forward<T>(expr)); }
template<typename T>
constexpr bool unlikely(T&& expr) noexcept { return static_cast<bool>(std::forward<T>(expr)); }
#endif
#define LIKELY(x) (likely(x))
#define UNLIKELY(x) (unlikely(x))

#if HAS_BUILTIN(__builtin_assume)
/* Unlike LIKELY, ASSUME requires the condition to be true or else it invokes
 * undefined behavior. It's essentially an assert without actually checking the
 * condition at run-time, allowing for stronger optimizations than LIKELY.
 */
#define ASSUME __builtin_assume
#elif defined(_MSC_VER)
#define ASSUME __assume
#elif defined(__GNUC__)
#define ASSUME(x) do { if(!(x)) __builtin_unreachable(); } while(0)
#else
#define ASSUME(x) ((void)0)
#endif

#endif /* OPTHELPERS_H */
