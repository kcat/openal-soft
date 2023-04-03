#ifndef AL_BUFFER_H
#define AL_BUFFER_H

#include <atomic>

#include "AL/al.h"

#include "albyte.h"
#include "alc/inprogext.h"
#include "almalloc.h"
#include "atomic.h"
#include "core/buffer_storage.h"
#include "vector.h"

#ifdef ALSOFT_EAX
#include "eax/x_ram.h"

enum class EaxStorage : uint8_t {
    Automatic,
    Accessible,
    Hardware
};
#endif // ALSOFT_EAX


struct ALbuffer : public BufferStorage {
    ALbitfieldSOFT Access{0u};

    al::vector<al::byte,16> mDataStorage;

    ALuint OriginalSize{0};

    ALuint UnpackAlign{0};
    ALuint PackAlign{0};
    ALuint UnpackAmbiOrder{1};

    ALbitfieldSOFT MappedAccess{0u};
    ALsizei MappedOffset{0};
    ALsizei MappedSize{0};

    ALuint mLoopStart{0u};
    ALuint mLoopEnd{0u};

    /* Number of times buffer was attached to a source (deletion can only occur when 0) */
    RefCount ref{0u};

    /* Self ID */
    ALuint id{0};

    DISABLE_ALLOC()

#ifdef ALSOFT_EAX
    EaxStorage eax_x_ram_mode{EaxStorage::Automatic};
    bool eax_x_ram_is_hardware{};
#endif // ALSOFT_EAX
};

#endif
