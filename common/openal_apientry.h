#ifndef COMMON_OPENAL_APIENTRY_H
#define COMMON_OPENAL_APIENTRY_H

#if defined(_WIN32)
#define ALC_APIENTRY __cdecl
#define AL_APIENTRY __cdecl
#else
#define ALC_APIENTRY
#define AL_APIENTRY
#endif

#endif /* COMMON_OPENAL_APIENTRY_H */
