#ifndef ALHELPERS_H
#define ALHELPERS_H

#include <time.h>

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Some helper functions to get the name from the format enums. */
const char *FormatName(ALenum type);

/* Easy device init/deinit functions. InitAL returns 0 on success. */
int InitAL(char ***argv, int *argc);
void CloseAL(void);

/* Cross-platform timeget and sleep functions. */
#ifndef HAVE_STRUCT_TIMESPEC
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif
#define AL_TIME_UTC 1
int altimespec_get(struct timespec *ts, int base);
void al_nssleep(unsigned long nsec);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* ALHELPERS_H */
