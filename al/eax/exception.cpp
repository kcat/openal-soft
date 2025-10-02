#include "config.h"

#include "exception.h"

#include <string>


EaxException::EaxException(std::string_view context, std::string_view message)
    : std::runtime_error{make_message(context, message)}
{ }

std::string EaxException::make_message(std::string_view context, std::string_view message)
{
    auto what = std::string{};
    if(context.empty() && message.empty())
        return what;

    what.reserve((!context.empty() ? context.size() + 3 : 0) + message.length() + 1);
    if(!context.empty())
    {
        what += "[";
        what += context;
        what += "] ";
    }
    what += message;

    return what;
}
