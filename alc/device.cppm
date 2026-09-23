module;

#include "alc/device.h"

export module alc.device;

export import core.device;

export {
#if HAVE_CXXMODULES
    using ::eax_x_ram_max_size;
#endif
    using ::ALCdevice;
    namespace al {
        using al::DeviceDeleter;
        using al::Device;
    }
}
