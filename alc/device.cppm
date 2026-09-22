module;

#include "alc/device.h"

export module alc.device;

export import core.device;

export {
    using ::ALCdevice;
    namespace al {
        using al::DeviceDeleter;
        using al::Device;
    }
}
