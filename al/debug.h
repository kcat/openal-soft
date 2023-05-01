#ifndef AL_DEBUG_H
#define AL_DEBUG_H

#include <stdint.h>


/* Somewhat arbitrary. Avoid letting it get out of control if the app enables
 * logging but never reads it.
 */
constexpr uint8_t MaxDebugLoggedMessages{64};
constexpr uint16_t MaxDebugMessageLength{1024};

#endif /* AL_DEBUG_H */
