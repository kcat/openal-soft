#ifndef CORE_LOGGING_H
#define CORE_LOGGING_H

#include <string_view>

#include "alformat.hpp"
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
void al_print_impl(LogLevel level, al::string_view fmt, al::format_args&& args);

template<typename ...Args>
void al_print(LogLevel const level, al::format_string<Args...> const fmt, Args&& ...args) noexcept
try {
    al_print_impl(level, fmt.get(), al::make_format_args(args...));
} catch(...) { /* Swallow all exceptions */ }

template<typename ...Args>
void TRACE(al::format_string<Args...> const fmt, Args&& ...args) noexcept
{ al_print(LogLevel::Trace, fmt, std::forward<Args>(args)...); }

template<typename ...Args>
void WARN(al::format_string<Args...> const fmt, Args&& ...args) noexcept
{ al_print(LogLevel::Warning, fmt, std::forward<Args>(args)...); }

template<typename ...Args>
void ERR(al::format_string<Args...> const fmt, Args&& ...args) noexcept
{ al_print(LogLevel::Error, fmt, std::forward<Args>(args)...); }

#endif /* CORE_LOGGING_H */
