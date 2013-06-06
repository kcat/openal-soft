#ifndef EXAMPLES_SDL_SOUND_H
#define EXAMPLES_SDL_SOUND_H

#include "AL/al.h"

#include <SDL_sound.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Opaque handles to files and streams. Apps don't need to concern themselves
 * with the internals */
typedef Sound_Sample *FilePtr;

/* Opens a file with SDL_sound, and specifies the size of the sample buffer in
 * milliseconds. */
FilePtr openAudioFile(const char *fname, size_t buftime_ms);

/* Closes/frees an opened file */
void closeAudioFile(FilePtr file);

/* Returns information about the given audio stream. Returns 0 on success. */
int getAudioInfo(FilePtr file, ALuint *rate, ALenum *channels, ALenum *type);

/* Returns a pointer to the next available chunk of decoded audio. The size (in
 * bytes) of the returned data buffer is stored in 'length', and the returned
 * pointer is only valid until the next call to getAudioData. */
uint8_t *getAudioData(FilePtr file, size_t *length);

/* Decodes all remaining data from the stream and returns a buffer containing
 * the audio data, with the size stored in 'length'. The returned pointer must
 * be freed with a call to free(). Note that since this decodes the whole
 * stream, using it on lengthy streams (eg, music) will use a lot of memory.
 * Such streams are better handled using getAudioData to keep smaller chunks in
 * memory at any given time. */
void *decodeAudioStream(FilePtr, size_t *length);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* EXAMPLES_SDL_SOUND_H */
