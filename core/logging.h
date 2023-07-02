#ifndef CORE_LOGGING_H
#define CORE_LOGGING_H

#include <stdio.h>

#include "opthelpers.h"


enum class LogLevel {
    Disable,
    Error,
    Warning,
    Trace
};
extern LogLevel gLogLevel;

extern FILE *gLogFile;


using LogCallbackFunc = void(*)(void *userptr, char level, const char *message, int length) noexcept;

void al_set_log_callback(LogCallbackFunc callback, void *userptr);


#ifdef __USE_MINGW_ANSI_STDIO
[[gnu::format(gnu_printf,2,3)]]
#else
[[gnu::format(printf,2,3)]]
#endif
void al_print(LogLevel level, const char *fmt, ...);

#define TRACE(...) al_print(LogLevel::Trace, __VA_ARGS__)

#define WARN(...) al_print(LogLevel::Warning, __VA_ARGS__)

#define ERR(...) al_print(LogLevel::Error, __VA_ARGS__)

#endif /* CORE_LOGGING_H */
