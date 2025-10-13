
#include "config.h"

#include "logging.h"

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

using lpvoid = void*;

enum class LogState : u8 {
    FirstRun,
    Ready,
    Disable
};

auto LogCallbackMutex = std::mutex{};
auto gLogState = LogState::FirstRun;

auto gLogFile = std::ofstream{}; /* NOLINT(cert-err58-cpp) */


auto gLogCallback = LogCallbackFunc{};
auto gLogCallbackPtr = lpvoid{};

constexpr auto GetLevelCode(LogLevel const level) noexcept -> std::optional<char>
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

void al_open_logfile(fs::path const &fname)
{
    gLogFile.open(fname);
    if(!gLogFile.is_open())
        ERR("Failed to open log file '{}'", al::u8_as_char(fname.u8string()));
}

void al_set_log_callback(LogCallbackFunc const callback, void *const userptr)
{
    auto const cblock = std::lock_guard{LogCallbackMutex};
    gLogCallback = callback;
    gLogCallbackPtr = callback ? userptr : nullptr;
    if(gLogState == LogState::FirstRun)
    {
        if(auto const extlogopt = al::getenv("ALSOFT_DISABLE_LOG_CALLBACK");
            !extlogopt || *extlogopt != "1")
            gLogState = LogState::Ready;
        else
            gLogState = LogState::Disable;
    }
}

void al_print_impl(LogLevel const level, std::string_view const fmt, std::format_args&& args)
{
    const auto msg = std::vformat(fmt, std::move(args));

    auto const prefix = std::invoke([level]() -> std::string_view
    {
        switch(level)
        {
        case LogLevel::Trace: return "[ALSOFT] (II) "sv;
        case LogLevel::Warning: return "[ALSOFT] (WW) "sv;
        case LogLevel::Error: return "[ALSOFT] (EE) "sv;
        case LogLevel::Disable: break;
        }
        return "[ALSOFT] (--) "sv;
    });

    if(gLogLevel >= level)
    {
        auto &logfile = gLogFile.is_open() ? gLogFile : std::cerr;
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

    auto const cblock = std::lock_guard{LogCallbackMutex};
    if(gLogState != LogState::Disable)
    {
        if(auto const logcode = GetLevelCode(level))
        {
            if(gLogCallback)
                gLogCallback(gLogCallbackPtr, *logcode, msg.data(),
                    al::saturate_cast<int>(msg.size()));
            else if(gLogState == LogState::FirstRun)
                gLogState = LogState::Disable;
        }
    }
}
