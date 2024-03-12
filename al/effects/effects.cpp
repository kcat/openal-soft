#include "config.h"

#include "effects.h"

#include <cstdarg>


effect_exception::effect_exception(ALenum code, const char *msg, ...) : mErrorCode{code}
{
    /* NOLINTBEGIN(*-array-to-pointer-decay) */
    std::va_list args;
    va_start(args, msg);
    setMessage(msg, args);
    va_end(args);
    /* NOLINTEND(*-array-to-pointer-decay) */
}
effect_exception::~effect_exception() = default;
