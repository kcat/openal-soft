#ifndef OPTHELPERS_H
#define OPTHELPERS_H

#include <functional>
#include <type_traits>

#include "opthelper_macros.hpp"


namespace al {

template<typename>
concept dependent_false = false;

template<typename T> [[nodiscard]] constexpr
auto to_underlying(T e) noexcept -> std::underlying_type_t<T>
{ return static_cast<std::underlying_type_t<T>>(e); }

template<typename T>
concept dereferenceable = requires(T& p) { *p; };

struct dereference {
    template<dereferenceable T> [[nodiscard]] constexpr
    auto operator()(T&& p) const noexcept(noexcept(*std::forward<T>(p))) -> decltype(auto)
    { return *std::forward<T>(p); }
};

template<typename F>
struct assign_result {
    using result_t = std::invoke_result_t<F>;
    F&& mCallable;

    [[nodiscard]] constexpr explicit(false) /* NOLINTNEXTLINE(*-explicit-constructor) */
    operator result_t() && noexcept { return std::invoke(std::forward<F>(mCallable)); }
};

} // namespace al

#endif /* OPTHELPERS_H */
