#ifndef CORE_LOGGING_H
#define CORE_LOGGING_H

#include <format>
#include <string_view>

#include "alnumeric.h"
#include "filesystem.h"
#include "gsl/gsl"
#include "opthelpers.h"


enum class LogLevel : u8 {
    Disable,
    Error,
    Warning,
    Trace
};
DECL_HIDDEN extern LogLevel gLogLevel;


using LogCallbackFunc = auto(*)(void *userptr, char level, gsl::czstring message, int length)
    noexcept -> void;

void al_set_log_callback(LogCallbackFunc callback, void *userptr);

void al_open_logfile(fs::path const &fname);
void al_print_impl(LogLevel level, std::string_view fmt, std::format_args&& args);

template<typename ...Args>
void al_print(LogLevel const level, std::format_string<Args...> const fmt, Args&& ...args) noexcept
try {
    al_print_impl(level, fmt.get(), std::make_format_args(args...));
} catch(...) { /* Swallow all exceptions */ }

template<typename ...Args>
void TRACE(std::format_string<Args...> const fmt, Args&& ...args) noexcept
{ al_print(LogLevel::Trace, fmt, std::forward<Args>(args)...); }

template<typename ...Args>
void WARN(std::format_string<Args...> const fmt, Args&& ...args) noexcept
{ al_print(LogLevel::Warning, fmt, std::forward<Args>(args)...); }

template<typename ...Args>
void ERR(std::format_string<Args...> const fmt, Args&& ...args) noexcept
{ al_print(LogLevel::Error, fmt, std::forward<Args>(args)...); }

#endif /* CORE_LOGGING_H */
