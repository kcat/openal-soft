#ifndef AL_DYNLOAD_H
#define AL_DYNLOAD_H

#if defined(_WIN32) || defined(HAVE_DLFCN_H)

#define HAVE_DYNLOAD 1

void *LoadLib(const char *name);
void CloseLib(void *handle);
void *GetSymbol(void *handle, const char *name);

#else

#define HAVE_DYNLOAD 0

#endif

#endif /* AL_DYNLOAD_H */
