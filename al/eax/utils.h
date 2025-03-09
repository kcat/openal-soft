#ifndef EAX_UTILS_INCLUDED
#define EAX_UTILS_INCLUDED

#include <string_view>

#include "fmt/core.h"
#include "opthelpers.h"


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

    const auto message = fmt::format("{} out of range (value: {}; min: {}; max: {}).", value_name,
        value, min_value, max_value);
    throw TException{message.c_str()};
}

#endif // !EAX_UTILS_INCLUDED
