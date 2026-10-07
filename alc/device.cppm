module;

#include "alc/device.h"

export module alc.device;

export import core.device;

export {
#if ALSOFT_EAX
    using ::eax_x_ram_max_size;
#endif
    using ::ALCdevice;
    namespace al {
        using al::DeviceDeleter;
        using al::Device;
    }
    using ::PlaybackFactory;
    using ::CaptureFactory;
}
