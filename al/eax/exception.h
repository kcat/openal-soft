#ifndef EAX_EXCEPTION_INCLUDED
#define EAX_EXCEPTION_INCLUDED

#include <stdexcept>
#include <string>
#include <string_view>

#include "opthelpers.h"

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
class EaxException : public std::runtime_error {
    static std::string make_message(std::string_view context, std::string_view message);

public:
    EaxException() = delete;
    EaxException(const EaxException&) = default;
    EaxException(EaxException&&) = default;
    EaxException(std::string_view context, std::string_view message);
    NOINLINE ~EaxException() override = default;

    auto operator=(const EaxException&) -> EaxException& = default;
    auto operator=(EaxException&&) -> EaxException& = default;
};

#endif /* EAX_EXCEPTION_INCLUDED */
