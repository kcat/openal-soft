#include "config.h"

#include "exception.h"

#include <string>

#if HAVE_CXXMODULES
import format;
#else
#include "alformat.hpp"
#endif

namespace {

auto make_message(std::string_view const context, std::string_view const message) -> std::string
{
    if(context.empty())
    {
        if(message.empty())
            return std::string{};
        return std::string{message};
    }
    return al::format("[{}] {}", context, message);
}

}

EaxException::EaxException(std::string_view const context, std::string_view const message)
    : std::runtime_error{make_message(context, message)}
{ }
