
#include "altypes.hpp"
#include "alformat.hpp"

namespace al {

[[noreturn]]
auto throw_narrowing_error(std::string_view const prefix, long long const value,
    std::string_view const type) -> void
{
    throw narrowing_error{al::format("{}: {} narrowed converting to type {}", prefix, value,
        type)};
}

[[noreturn]]
auto throw_narrowing_error(std::string_view const prefix, unsigned long long const value,
    std::string_view const type) -> void
{
    throw narrowing_error{al::format("{}: {} narrowed converting to type {}", prefix, value,
        type)};
}

[[noreturn]]
auto throw_narrowing_error(std::string_view const prefix, long double const value,
    std::string_view const type) -> void
{
    throw narrowing_error{al::format("{}: {} narrowed converting to type {}", prefix, value,
        type)};
}

} /* namespace al */
