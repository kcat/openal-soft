#ifndef EAX_VALIDATOR_HPP
#define EAX_VALIDATOR_HPP

#include <string_view>

#include "alformat.hpp"


template<typename TException, typename TValue> constexpr
auto eax_validate_range(std::string_view const value_name, TValue const& value,
    TValue const& min_value, TValue const& max_value) -> void
{
    if(value >= min_value && value <= max_value) [[likely]]
        return;

    const auto message = al::format("{} out of range (value: {}; min: {}; max: {}).", value_name,
        value, min_value, max_value);
    throw TException{message};
}

#endif /* EAX_VALIDATOR_HPP */
