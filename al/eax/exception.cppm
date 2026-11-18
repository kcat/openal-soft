module;

#include <stdexcept>
#include <string_view>

#include "opthelper_macros.hpp"

export module eax.exception;

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
export extern "C++" class EaxException : public std::runtime_error {
public:
    EaxException() = delete;
    EaxException(const EaxException&) = default;
    EaxException(EaxException&&) = default;
    EaxException(std::string_view context, std::string_view message);
    NOINLINE ~EaxException() override = default;

    auto operator=(const EaxException&) -> EaxException& = default;
    auto operator=(EaxException&&) -> EaxException& = default;
};
