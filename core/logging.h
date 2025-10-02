#ifndef CORE_LOGGING_H
#define CORE_LOGGING_H

#include <cstdint>
#include <format>
#include <string_view>

#include "filesystem.h"
#include "opthelpers.h"


enum class LogLevel : uint8_t {
    Disable,
    Error,
    Warning,
    Trace
};
DECL_HIDDEN extern LogLevel gLogLevel;


using LogCallbackFunc = auto(*)(void *userptr, char level, const char *message, int length)
    noexcept -> void;

void al_set_log_callback(LogCallbackFunc callback, void *userptr);

void al_open_logfile(const fs::path &fname);
void al_print_impl(LogLevel level, const std::string_view fmt, std::format_args args);

template<typename ...Args>
void al_print(LogLevel level, std::format_string<Args...> fmt, Args&& ...args) noexcept
try {
    al_print_impl(level, fmt.get(), std::make_format_args(args...));
} catch(...) { }

#define TRACE(...) al_print(LogLevel::Trace, __VA_ARGS__)

#define WARN(...) al_print(LogLevel::Warning, __VA_ARGS__)

#define ERR(...) al_print(LogLevel::Error, __VA_ARGS__)

#endif /* CORE_LOGGING_H */
