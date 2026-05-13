#ifndef PRAGMADEFS_H
#define PRAGMADEFS_H

#if defined(_MSC_VER)
#define DIAGNOSTIC_PUSH __pragma(warning(push))
#define DIAGNOSTIC_POP __pragma(warning(pop))
#define std_pragma(...)
#define msc_pragma __pragma
#else
#if defined(__GNUC__) || defined(__clang__)
#define DIAGNOSTIC_PUSH _Pragma("GCC diagnostic push")
#define DIAGNOSTIC_POP _Pragma("GCC diagnostic pop")
#else
#define DIAGNOSTIC_PUSH
#define DIAGNOSTIC_POP
#endif
#define std_pragma _Pragma
#define msc_pragma(...)
#endif

#if defined(__clang__) && (__clang_major__ >= (defined(__APPLE__) ? 17 : 20))
#define IGNORE_FUNCTION_EFFECTS DIAGNOSTIC_PUSH \
    std_pragma("clang diagnostic ignored \"-Wfunction-effects\"")
#else
#define IGNORE_FUNCTION_EFFECTS DIAGNOSTIC_PUSH
#endif
#define UNIGNORE_FUNCTION_EFFECTS DIAGNOSTIC_POP

#endif /* PRAGMADEFS_H */
