#ifndef CORE_EXCEPT_H
#define CORE_EXCEPT_H

#include <exception>
#include <string>
#include <type_traits>

#include "opthelpers.h"


namespace al {

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
class base_exception : public std::exception {
    std::string mMessage;

public:
    base_exception() = default;
    template<typename T> requires(std::is_constructible_v<std::string,T>)
    explicit base_exception(T&& msg) : mMessage{std::forward<T>(msg)} { }
    base_exception(const base_exception&) = default;
    base_exception(base_exception&&) = default;
    NOINLINE ~base_exception() override = default;

    auto operator=(const base_exception&) & -> base_exception& = default;
    auto operator=(base_exception&&) & -> base_exception& = default;

    [[nodiscard]] auto what() const noexcept LIFETIMEBOUND -> const char* override
    { return mMessage.c_str(); }
};

} // namespace al

#endif /* CORE_EXCEPT_H */
