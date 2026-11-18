#include "config.h"

#include <string>

#if HAVE_CXXMODULES
import common.std;
import eax.exception;
import format;

#else

#include <stdexcept>

#include "al/eax/exception.hpp"
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
