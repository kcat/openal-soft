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


using LogCallbackFunc = void(*)(void *userptr, LogLevel level, const char *entity, int entityLength, const char *message, int messageLength) noexcept;

void al_set_log_callback(LogCallbackFunc callback, void *userptr);


#ifdef __USE_MINGW_ANSI_STDIO
[[gnu::format(gnu_printf,3,4)]]
#else
[[gnu::format(printf,3,4)]]
#endif
void al_print(LogLevel level, const char *entity, const char *fmt, ...);

#define TRACE_FOR(...) al_print(LogLevel::Trace, __VA_ARGS__)

#define TRACE(...) TRACE_FOR(NULL, __VA_ARGS__)

#define WARN_FOR(...) al_print(LogLevel::Warning, __VA_ARGS__)

#define WARN(...) WARN_FOR(NULL, __VA_ARGS__)

#define ERR_FOR(...) al_print(LogLevel::Error, __VA_ARGS__)

#define ERR(...) ERR_FOR(NULL, __VA_ARGS__)

#endif /* CORE_LOGGING_H */
