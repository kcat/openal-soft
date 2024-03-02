#ifndef ALC_DEVICE_H
#define ALC_DEVICE_H

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AL/alc.h"
#include "AL/alext.h"

#include "alconfig.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "core/device.h"
#include "inprogext.h"
#include "intrusive_ptr.h"

#ifdef ALSOFT_EAX
#include "al/eax/x_ram.h"
#endif // ALSOFT_EAX

struct ALbuffer;
struct ALeffect;
struct ALfilter;
struct BackendBase;
struct BufferSubList;
struct EffectSubList;
struct FilterSubList;

using uint = unsigned int;


struct ALCdevice : public al::intrusive_ref<ALCdevice>, DeviceBase {
    /* This lock protects the device state (format, update size, etc) from
     * being from being changed in multiple threads, or being accessed while
     * being changed. It's also used to serialize calls to the backend.
     */
    std::mutex StateLock;
    std::unique_ptr<BackendBase> Backend;

    ALCuint NumMonoSources{};
    ALCuint NumStereoSources{};

    // Maximum number of sources that can be created
    uint SourcesMax{};
    // Maximum number of slots that can be created
    uint AuxiliaryEffectSlotMax{};

    std::string mHrtfName;
    std::vector<std::string> mHrtfList;
    ALCenum mHrtfStatus{ALC_FALSE};

    enum class OutputMode1 : ALCenum {
        Any = ALC_ANY_SOFT,
        Mono = ALC_MONO_SOFT,
        Stereo = ALC_STEREO_SOFT,
        StereoBasic = ALC_STEREO_BASIC_SOFT,
        Uhj2 = ALC_STEREO_UHJ_SOFT,
        Hrtf = ALC_STEREO_HRTF_SOFT,
        Quad = ALC_QUAD_SOFT,
        X51 = ALC_SURROUND_5_1_SOFT,
        X61 = ALC_SURROUND_6_1_SOFT,
        X71 = ALC_SURROUND_7_1_SOFT
    };
    OutputMode1 getOutputMode1() const noexcept;

    using OutputMode = OutputMode1;

    std::atomic<ALCenum> LastError{ALC_NO_ERROR};

    // Map of Buffers for this device
    std::mutex BufferLock;
    std::vector<BufferSubList> BufferList;

    // Map of Effects for this device
    std::mutex EffectLock;
    std::vector<EffectSubList> EffectList;

    // Map of Filters for this device
    std::mutex FilterLock;
    std::vector<FilterSubList> FilterList;

#ifdef ALSOFT_EAX
    ALuint eax_x_ram_free_size{eax_x_ram_max_size};
#endif // ALSOFT_EAX


    std::unordered_map<ALuint,std::string> mBufferNames;
    std::unordered_map<ALuint,std::string> mEffectNames;
    std::unordered_map<ALuint,std::string> mFilterNames;

    ALCdevice(DeviceType type);
    ~ALCdevice();

    void enumerateHrtfs();

    bool getConfigValueBool(const std::string_view block, const std::string_view key, bool def)
    { return GetConfigValueBool(DeviceName, block, key, def); }

    template<typename T>
    inline std::optional<T> configValue(const std::string_view block, const std::string_view key) = delete;
};

template<>
inline std::optional<std::string> ALCdevice::configValue(const std::string_view block, const std::string_view key)
{ return ConfigValueStr(DeviceName, block, key); }
template<>
inline std::optional<int> ALCdevice::configValue(const std::string_view block, const std::string_view key)
{ return ConfigValueInt(DeviceName, block, key); }
template<>
inline std::optional<uint> ALCdevice::configValue(const std::string_view block, const std::string_view key)
{ return ConfigValueUInt(DeviceName, block, key); }
template<>
inline std::optional<float> ALCdevice::configValue(const std::string_view block, const std::string_view key)
{ return ConfigValueFloat(DeviceName, block, key); }
template<>
inline std::optional<bool> ALCdevice::configValue(const std::string_view block, const std::string_view key)
{ return ConfigValueBool(DeviceName, block, key); }

/** Stores the latest ALC device error. */
void alcSetError(ALCdevice *device, ALCenum errorCode);

#endif
