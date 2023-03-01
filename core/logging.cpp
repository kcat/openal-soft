
#include "config.h"

#include "logging.h"

#include <cstdarg>
#include <cstdio>
#include <string>

#include "alspan.h"
#include "strutils.h"
#include "vector.h"


#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#endif

void al_print(LogLevel level, FILE *logfile, const char *fmt, ...)
{
    /* Kind of ugly since string literals are const char arrays with a size
     * that includes the null terminator, which we want to exclude from the
     * span.
     */
    auto prefix = al::as_span("[ALSOFT] (--) ").first<14>();
    switch(level)
    {
    case LogLevel::Disable: break;
    case LogLevel::Error: prefix = al::as_span("[ALSOFT] (EE) ").first<14>(); break;
    case LogLevel::Warning: prefix = al::as_span("[ALSOFT] (WW) ").first<14>(); break;
    case LogLevel::Trace: prefix = al::as_span("[ALSOFT] (II) ").first<14>(); break;
    }

    al::vector<char> dynmsg;
    std::array<char,256> stcmsg{};

    char *str{stcmsg.data()};
    auto prefend1 = std::copy_n(prefix.begin(), prefix.size(), stcmsg.begin());
    al::span<char> msg{prefend1, stcmsg.end()};

    std::va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);
    const int msglen{std::vsnprintf(msg.data(), msg.size(), fmt, args)};
    if(msglen >= 0 && static_cast<size_t>(msglen) >= msg.size()) UNLIKELY
    {
        dynmsg.resize(static_cast<size_t>(msglen)+prefix.size() + 1u);

        str = dynmsg.data();
        auto prefend2 = std::copy_n(prefix.begin(), prefix.size(), dynmsg.begin());
        msg = {prefend2, dynmsg.end()};

        std::vsnprintf(msg.data(), msg.size(), fmt, args2);
    }
    va_end(args2);
    va_end(args);

    if(gLogLevel >= level)
    {
        fputs(str, logfile);
        fflush(logfile);
    }
#if defined(_WIN32) && !defined(NDEBUG)
    /* OutputDebugStringW has no 'level' property to distinguish between
     * informational, warning, or error debug messages. So only print them for
     * non-Release builds.
     */
    std::wstring wstr{utf8_to_wstr(str)};
    OutputDebugStringW(wstr.c_str());
#elif defined(__ANDROID__)
    auto android_severity = [](LogLevel l) noexcept
    {
        switch(l)
        {
        case LogLevel::Trace: return ANDROID_LOG_DEBUG;
        case LogLevel::Warning: return ANDROID_LOG_WARN;
        case LogLevel::Error: return ANDROID_LOG_ERROR;
        /* Should not happen. */
        case LogLevel::Disable:
            break;
        }
        return ANDROID_LOG_ERROR;
    };
    __android_log_print(android_severity(level), "openal", "%s", str);
#endif
}
