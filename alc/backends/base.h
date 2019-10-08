#ifndef ALC_BACKENDS_BASE_H
#define ALC_BACKENDS_BASE_H

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "AL/alc.h"

#include "alcmain.h"
#include "albyte.h"


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

    auto ns = nanoseconds{seconds{device->SamplesDone}} / device->Frequency;
    return device->ClockBase + ns;
}

ClockLatency GetClockLatency(ALCdevice *device);

struct BackendBase {
    virtual void open(const ALCchar *name) = 0;

    virtual bool reset();
    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual ALCenum captureSamples(al::byte *buffer, ALCuint samples);
    virtual ALCuint availableSamples();

    virtual ClockLatency getClockLatency();

    virtual void lock() { mMutex.lock(); }
    virtual void unlock() { mMutex.unlock(); }

    ALCdevice *mDevice;

    std::recursive_mutex mMutex;

    BackendBase(ALCdevice *device) noexcept;
    virtual ~BackendBase();
};
using BackendPtr = std::unique_ptr<BackendBase>;
using BackendUniqueLock = std::unique_lock<BackendBase>;
using BackendLockGuard = std::lock_guard<BackendBase>;

enum class BackendType {
    Playback,
    Capture
};

enum class DevProbe {
    Playback,
    Capture
};


struct BackendFactory {
    virtual bool init() = 0;

    virtual bool querySupport(BackendType type) = 0;

    virtual void probe(DevProbe type, std::string *outnames) = 0;

    virtual BackendPtr createBackend(ALCdevice *device, BackendType type) = 0;

protected:
    virtual ~BackendFactory() = default;
};

#endif /* ALC_BACKENDS_BASE_H */
