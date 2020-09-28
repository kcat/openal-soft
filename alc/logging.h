#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

#include "opthelpers.h"


extern FILE *gLogFile;

[[gnu::format(printf,2,3)]] void al_print(FILE *logfile, const char *fmt, ...);
#if !defined(_WIN32)
#define AL_PRINT fprintf
#else
#define AL_PRINT al_print
#endif

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_ANDROID(T, ...) __android_log_print(T, "openal", "AL lib: " __VA_ARGS__)
#else
#define LOG_ANDROID(T, ...) ((void)0)
#endif

enum class LogLevel {
    Disable,
    Error,
    Warning,
    Trace,
    Ref
};
extern LogLevel gLogLevel;

#define TRACE(...) do {                                                       \
    if UNLIKELY(gLogLevel >= LogLevel::Trace)                                 \
        AL_PRINT(gLogFile, "AL lib: (II) " __VA_ARGS__);                      \
    LOG_ANDROID(ANDROID_LOG_DEBUG, __VA_ARGS__);                              \
} while(0)

#define WARN(...) do {                                                        \
    if UNLIKELY(gLogLevel >= LogLevel::Warning)                               \
        AL_PRINT(gLogFile, "AL lib: (WW) " __VA_ARGS__);                      \
    LOG_ANDROID(ANDROID_LOG_WARN, __VA_ARGS__);                               \
} while(0)

#define ERR(...) do {                                                         \
    if UNLIKELY(gLogLevel >= LogLevel::Error)                                 \
        AL_PRINT(gLogFile, "AL lib: (EE) " __VA_ARGS__);                      \
    LOG_ANDROID(ANDROID_LOG_ERROR, __VA_ARGS__);                              \
} while(0)

#endif /* LOGGING_H */
