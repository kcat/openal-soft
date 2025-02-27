#ifndef CORE_LOGGING_H
#define CORE_LOGGING_H

#include <cstdio>

#include "fmt/core.h"
#include "opthelpers.h"


enum class LogLevel {
    Disable,
    Error,
    Warning,
    Trace
};
DECL_HIDDEN extern LogLevel gLogLevel;

DECL_HIDDEN extern FILE *gLogFile;


using LogCallbackFunc = void(*)(void *userptr, char level, const char *message, int length) noexcept;

void al_set_log_callback(LogCallbackFunc callback, void *userptr);


void al_print_impl(LogLevel level, const fmt::string_view fmt, fmt::format_args args);

template<typename ...Args>
void al_print(LogLevel level, fmt::format_string<Args...> fmt, Args&& ...args) noexcept
try {
    al_print_impl(level, fmt, fmt::make_format_args(args...));
} catch(...) { }

#define TRACE(...) al_print(LogLevel::Trace, __VA_ARGS__)

#define WARN(...) al_print(LogLevel::Warning, __VA_ARGS__)

#define ERR(...) al_print(LogLevel::Error, __VA_ARGS__)

#endif /* CORE_LOGGING_H */
