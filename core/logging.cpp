
#include "config.h"

#include "logging.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "alnumeric.h"
#include "alstring.h"
#include "fmt/std.h"
#include "strutils.hpp"


#if defined(_WIN32)
#include <windows.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#endif


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

auto LogCallbackMutex = std::mutex{};
auto gLogState = LogState::FirstRun;

auto gLogFile = std::ofstream{}; /* NOLINT(cert-err58-cpp) */


auto gLogCallback = LogCallbackFunc{};
void *gLogCallbackPtr{};

constexpr auto GetLevelCode(const LogLevel level) noexcept -> std::optional<char>
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

void al_open_logfile(const fs::path &fname)
{
    gLogFile.open(fname);
    if(!gLogFile.is_open())
        ERR("Failed to open log file '{}'", al::u8_as_char(fname.u8string()));
}

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

void al_print_impl(LogLevel level, const std::string_view fmt, std::format_args args)
{
    const auto msg = std::vformat(fmt, std::move(args));

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
        auto &logfile = gLogFile ? gLogFile : std::cerr;
        /* std::vprint_unicode */
        fmt::vprint(logfile, "{}{}\n", fmt::make_format_args(prefix, msg));
        logfile.flush();
    }
#if defined(_WIN32) && !defined(NDEBUG)
    /* OutputDebugStringW has no 'level' property to distinguish between
     * informational, warning, or error debug messages. So only print them for
     * non-Release builds.
     */
    OutputDebugStringW(utf8_to_wstr(std::format("{}{}\n", prefix, msg)).c_str());
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
    __android_log_print(android_severity(level), "openal", "%.*s%s",
        al::saturate_cast<int>(prefix.size()), prefix.data(), msg.c_str());
#endif

    auto cblock = std::lock_guard{LogCallbackMutex};
    if(gLogState != LogState::Disable)
    {
        if(auto logcode = GetLevelCode(level))
        {
            if(gLogCallback)
                gLogCallback(gLogCallbackPtr, *logcode, msg.data(),
                    al::saturate_cast<int>(msg.size()));
            else if(gLogState == LogState::FirstRun)
                gLogState = LogState::Disable;
        }
    }
}
