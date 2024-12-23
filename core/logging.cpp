
#include "config.h"

#include "logging.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "alstring.h"
#include "strutils.h"


#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#endif


FILE *gLogFile{stderr};
#ifdef _DEBUG
LogLevel gLogLevel{LogLevel::Warning};
#else
LogLevel gLogLevel{LogLevel::Error};
#endif


namespace {

using namespace std::string_view_literals;

enum class LogState : uint8_t {
    FirstRun,
    Ready,
    Disable
};

std::mutex LogCallbackMutex;
LogState gLogState{LogState::FirstRun};

LogCallbackFunc gLogCallback{};
void *gLogCallbackPtr{};

constexpr auto GetLevelCode(LogLevel level) noexcept -> std::optional<char>
{
    switch(level)
    {
    case LogLevel::Disable: break;
    case LogLevel::Error: return 'E';
    case LogLevel::Warning: return 'W';
    case LogLevel::Trace: return 'I';
    }
    return std::nullopt;
}

} // namespace

void al_set_log_callback(LogCallbackFunc callback, void *userptr)
{
    auto cblock = std::lock_guard{LogCallbackMutex};
    gLogCallback = callback;
    gLogCallbackPtr = callback ? userptr : nullptr;
    if(gLogState == LogState::FirstRun)
    {
        auto extlogopt = al::getenv("ALSOFT_DISABLE_LOG_CALLBACK");
        if(!extlogopt || *extlogopt != "1")
            gLogState = LogState::Ready;
        else
            gLogState = LogState::Disable;
    }
}

void al_print_impl(LogLevel level, const fmt::string_view fmt, fmt::format_args args)
{
    const auto msg = fmt::vformat(fmt, std::move(args));

    auto prefix = "[ALSOFT] (--) "sv;
    switch(level)
    {
    case LogLevel::Disable: break;
    case LogLevel::Error: prefix = "[ALSOFT] (EE) "sv; break;
    case LogLevel::Warning: prefix = "[ALSOFT] (WW) "sv; break;
    case LogLevel::Trace: prefix = "[ALSOFT] (II) "sv; break;
    }

    if(gLogLevel >= level)
    {
        auto logfile = gLogFile;
        fmt::println(logfile, "{}{}", prefix, msg);
        fflush(logfile);
    }
#if defined(_WIN32) && !defined(NDEBUG)
    /* OutputDebugStringW has no 'level' property to distinguish between
     * informational, warning, or error debug messages. So only print them for
     * non-Release builds.
     */
    OutputDebugStringW(utf8_to_wstr(fmt::format("{}{}\n", prefix, msg)).c_str());
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
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) */
    __android_log_print(android_severity(level), "openal", "%.*s%s", al::sizei(prefix),
        prefix.data(), msg.c_str());
#endif

    auto cblock = std::lock_guard{LogCallbackMutex};
    if(gLogState != LogState::Disable)
    {
        if(auto logcode = GetLevelCode(level))
        {
            if(gLogCallback)
                gLogCallback(gLogCallbackPtr, *logcode, msg.data(), al::sizei(msg));
            else if(gLogState == LogState::FirstRun)
                gLogState = LogState::Disable;
        }
    }
}
