
#include "config.h"

#include "except.h"

#include <cstdio>
#include <cstdarg>

#include "opthelpers.h"


namespace al {

base_exception::~base_exception() = default;

void base_exception::setMessage(const char* msg, std::va_list args)
{
    std::va_list args2;
    va_copy(args2, args);
    int msglen{std::vsnprintf(nullptr, 0, msg, args)};
    if(msglen > 0) [[likely]]
    {
        mMessage.resize(static_cast<size_t>(msglen)+1);
        std::vsnprintf(const_cast<char*>(mMessage.data()), mMessage.length(), msg, args2);
        mMessage.pop_back();
    }
    va_end(args2);
}

} // namespace al
