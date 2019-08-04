
#include "config.h"

#include "alexcpt.h"

#include <cstdio>
#include <cstdarg>

#include "opthelpers.h"


namespace al {

backend_exception::backend_exception(ALCenum code, const char *msg, ...) : mErrorCode{code}
{
    va_list args, args2;
    va_start(args, msg);
    va_copy(args2, args);
    int msglen{std::vsnprintf(nullptr, 0, msg, args)};
    if LIKELY(msglen > 0)
    {
        mMessage.resize(static_cast<size_t>(msglen)+1);
        std::vsnprintf(&mMessage[0], mMessage.length(), msg, args2);
        mMessage.pop_back();
    }
    va_end(args2);
    va_end(args);
}

} // namespace al
