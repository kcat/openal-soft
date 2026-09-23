#ifndef EAX_ALAPI_HPP
#define EAX_ALAPI_HPP

#include "AL/al.h"


struct _GUID; /* NOLINT(*-reserved-identifier) */

extern "C" auto AL_APIENTRY EAXSet(_GUID const *property_set_id, ALuint property_id,
    ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum;
extern "C" auto AL_APIENTRY EAXGet(_GUID const *property_set_id, ALuint property_id,
    ALuint source_id, ALvoid *value, ALuint value_size) noexcept -> ALenum;


inline auto constexpr AL_EAX_RAM_SIZE =          0x202201;
inline auto constexpr AL_EAX_RAM_FREE =          0x202202;

inline auto constexpr AL_STORAGE_AUTOMATIC =     0x202203;
inline auto constexpr AL_STORAGE_HARDWARE =      0x202204;
inline auto constexpr AL_STORAGE_ACCESSIBLE =    0x202205;

extern "C" auto AL_APIENTRY EAXSetBufferMode(ALsizei n, const ALuint *buffers, ALint value)
    noexcept -> ALboolean;
extern "C" auto AL_APIENTRY EAXGetBufferMode(ALuint buffer, ALint *pReserved) noexcept -> ALenum;

#endif /* EAX_ALAPI_HPP */
