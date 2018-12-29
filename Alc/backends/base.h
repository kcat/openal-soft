#ifndef ALC_BACKENDS_BASE_H
#define ALC_BACKENDS_BASE_H

#include <memory>
#include <chrono>
#include <string>
#include <mutex>

#include "alMain.h"


struct ClockLatency {
    std::chrono::nanoseconds ClockTime;
    std::chrono::nanoseconds Latency;
};

/* Helper to get the current clock time from the device's ClockBase, and
 * SamplesDone converted from the sample rate.
 */
inline std::chrono::nanoseconds GetDeviceClockTime(ALCdevice *device)
{
    using std::chrono::seconds;
    using std::chrono::nanoseconds;
    using std::chrono::duration_cast;

    auto ns = duration_cast<nanoseconds>(seconds{device->SamplesDone}) / device->Frequency;
    return device->ClockBase + ns;
}

void ALCdevice_Lock(ALCdevice *device);
void ALCdevice_Unlock(ALCdevice *device);

ClockLatency GetClockLatency(ALCdevice *device);

struct BackendBase {
    virtual ALCenum open(const ALCchar *name) = 0;

    virtual ALCboolean reset();
    virtual ALCboolean start() = 0;
    virtual void stop() = 0;

    virtual ALCenum captureSamples(void *buffer, ALCuint samples);
    virtual ALCuint availableSamples();

    virtual ClockLatency getClockLatency();

    virtual void lock() noexcept;
    virtual void unlock() noexcept;

    ALCdevice *mDevice;

    std::recursive_mutex mMutex;

    BackendBase(ALCdevice *device) noexcept;
    virtual ~BackendBase();
};
using BackendPtr = std::unique_ptr<BackendBase>;

enum class BackendType {
    Playback,
    Capture,
    Loopback
};


struct BackendFactory {
    virtual bool init() = 0;
    virtual void deinit() { }

    virtual bool querySupport(BackendType type) = 0;

    virtual void probe(DevProbe type, std::string *outnames) = 0;

    virtual BackendPtr createBackend(ALCdevice *device, BackendType type) = 0;
};

#endif /* ALC_BACKENDS_BASE_H */
