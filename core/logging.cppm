module;

#include <cstdint>
#include <string_view>

#include "opthelpers.h"


export module logging;

import filesystem;
import format;
import gsl;

export {

enum class LogLevel : std::uint8_t {
    Disable,
    Error,
    Warning,
    Trace
};

using LogCallbackFunc = auto(*)(void *userptr, char level, gsl::czstring message, int length)
    noexcept -> void;

extern "C++" {
DECL_HIDDEN extern LogLevel gLogLevel;

void al_set_log_callback(LogCallbackFunc callback, void *userptr);

void al_open_logfile(fs::path const &fname);
void al_print(LogLevel level, al::string_view fmt, al::format_args&& args) noexcept;
} /* extern "C++" */

template<typename ...Args>
void TRACE(al::format_string<Args...> const fmt, Args&& ...args) noexcept
{ al_print(LogLevel::Trace, fmt.get(), al::make_format_args(args...)); }

template<typename ...Args>
void WARN(al::format_string<Args...> const fmt, Args&& ...args) noexcept
{ al_print(LogLevel::Warning, fmt.get(), al::make_format_args(args...)); }

template<typename ...Args>
void ERR(al::format_string<Args...> const fmt, Args&& ...args) noexcept
{ al_print(LogLevel::Error, fmt.get(), al::make_format_args(args...)); }

}
