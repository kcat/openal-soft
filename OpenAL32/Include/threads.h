#ifndef AL_THREADS_H
#define AL_THREADS_H

#include "alMain.h"

struct althread_info;
typedef struct althread_info* althread_t;

ALboolean StartThread(althread_t *out, ALuint (*func)(ALvoid*), ALvoid *ptr);
ALuint StopThread(althread_t thread);

void SetThreadName(const char *name);

#endif /* AL_THREADS_H */
