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

#ifdef __USE_MINGW_ANSI_STDIO
[[gnu::format(gnu_printf,3,4)]]
#else
[[gnu::format(printf,3,4)]]
#endif
void al_print(LogLevel level, FILE *logfile, const char *fmt, ...);

#if (!defined(_WIN32) || defined(NDEBUG)) && !defined(__ANDROID__)
#define TRACE(...) do {                                                       \
    if(gLogLevel >= LogLevel::Trace) UNLIKELY                                 \
        al_print(LogLevel::Trace, gLogFile, __VA_ARGS__);                     \
} while(0)

#define WARN(...) do {                                                        \
    if(gLogLevel >= LogLevel::Warning) UNLIKELY                               \
        al_print(LogLevel::Warning, gLogFile, __VA_ARGS__);                   \
} while(0)

#define ERR(...) do {                                                         \
    if(gLogLevel >= LogLevel::Error) UNLIKELY                                 \
        al_print(LogLevel::Error, gLogFile, __VA_ARGS__);                     \
} while(0)

#else

#define TRACE(...) al_print(LogLevel::Trace, gLogFile, __VA_ARGS__)

#define WARN(...) al_print(LogLevel::Warning, gLogFile, __VA_ARGS__)

#define ERR(...) al_print(LogLevel::Error, gLogFile, __VA_ARGS__)
#endif

#endif /* CORE_LOGGING_H */
