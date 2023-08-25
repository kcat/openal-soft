#include "config.h"

#include "exception.h"

#include <cassert>
#include <string>


EaxException::EaxException(std::string_view context, std::string_view message)
    : std::runtime_error{make_message(context, message)}
{
}
EaxException::~EaxException() = default;


std::string EaxException::make_message(std::string_view context, std::string_view message)
{
    auto what = std::string{};
    if(context.empty() && message.empty())
        return what;

    static constexpr char left_prefix[] = "[";
    static constexpr auto left_prefix_size = std::string::traits_type::length(left_prefix);

    static constexpr char right_prefix[] = "] ";
    static constexpr auto right_prefix_size = std::string::traits_type::length(right_prefix);

    what.reserve((!context.empty() ? left_prefix_size + context.size() + right_prefix_size : 0) +
        message.length() + 1);

    if(!context.empty())
    {
        what.append(left_prefix, left_prefix_size);
        what += context;
        what.append(right_prefix, right_prefix_size);
    }
    what += message;

    return what;
}
