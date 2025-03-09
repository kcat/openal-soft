#ifndef EAX_UTILS_INCLUDED
#define EAX_UTILS_INCLUDED

#include <string>
#include <string_view>

#include "opthelpers.h"

using EaxDirtyFlags = unsigned int;

struct EaxAlLowPassParam {
    float gain;
    float gain_hf;
};

void eax_log_exception(std::string_view message) noexcept;

template<typename TException, typename TValue>
void eax_validate_range(std::string_view value_name, const TValue& value, const TValue& min_value,
    const TValue& max_value)
{
    if(value >= min_value && value <= max_value) LIKELY
        return;

    const auto message =
        std::string{value_name} +
        " out of range (value: " +
        std::to_string(value) + "; min: " +
        std::to_string(min_value) + "; max: " +
        std::to_string(max_value) + ").";

    throw TException{message.c_str()};
}

#endif // !EAX_UTILS_INCLUDED
