#ifndef ALC_DEVICE_H
#define ALC_DEVICE_H

#include <atomic>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <string>
#include <utility>

#include "AL/alc.h"
#include "AL/alext.h"

#include "alconfig.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "core/device.h"
#include "intrusive_ptr.h"
#include "vector.h"

struct ALbuffer;
struct ALeffect;
struct ALfilter;
struct BackendBase;

using uint = unsigned int;


struct BufferSubList {
    uint64_t FreeMask{~0_u64};
    ALbuffer *Buffers{nullptr}; /* 64 */

    BufferSubList() noexcept = default;
    BufferSubList(const BufferSubList&) = delete;
    BufferSubList(BufferSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Buffers{rhs.Buffers}
    { rhs.FreeMask = ~0_u64; rhs.Buffers = nullptr; }
    ~BufferSubList();

    BufferSubList& operator=(const BufferSubList&) = delete;
    BufferSubList& operator=(BufferSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Buffers, rhs.Buffers); return *this; }
};

struct EffectSubList {
    uint64_t FreeMask{~0_u64};
    ALeffect *Effects{nullptr}; /* 64 */

    EffectSubList() noexcept = default;
    EffectSubList(const EffectSubList&) = delete;
    EffectSubList(EffectSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Effects{rhs.Effects}
    { rhs.FreeMask = ~0_u64; rhs.Effects = nullptr; }
    ~EffectSubList();

    EffectSubList& operator=(const EffectSubList&) = delete;
    EffectSubList& operator=(EffectSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Effects, rhs.Effects); return *this; }
};

struct FilterSubList {
    uint64_t FreeMask{~0_u64};
    ALfilter *Filters{nullptr}; /* 64 */

    FilterSubList() noexcept = default;
    FilterSubList(const FilterSubList&) = delete;
    FilterSubList(FilterSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Filters{rhs.Filters}
    { rhs.FreeMask = ~0_u64; rhs.Filters = nullptr; }
    ~FilterSubList();

    FilterSubList& operator=(const FilterSubList&) = delete;
    FilterSubList& operator=(FilterSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Filters, rhs.Filters); return *this; }
};


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
    al::vector<std::string> mHrtfList;
    ALCenum mHrtfStatus{ALC_FALSE};

    std::atomic<ALCenum> LastError{ALC_NO_ERROR};

    // Map of Buffers for this device
    std::mutex BufferLock;
    al::vector<BufferSubList> BufferList;

    // Map of Effects for this device
    std::mutex EffectLock;
    al::vector<EffectSubList> EffectList;

    // Map of Filters for this device
    std::mutex FilterLock;
    al::vector<FilterSubList> FilterList;


    ALCdevice(DeviceType type);
    ~ALCdevice();

    void enumerateHrtfs();

    bool getConfigValueBool(const char *block, const char *key, int def)
    { return GetConfigValueBool(DeviceName.c_str(), block, key, def); }

    template<typename T>
    al::optional<T> configValue(const char *block, const char *key) = delete;

    DEF_NEWDEL(ALCdevice)
};

template<>
inline al::optional<std::string> ALCdevice::configValue(const char *block, const char *key)
{ return ConfigValueStr(DeviceName.c_str(), block, key); }
template<>
inline al::optional<int> ALCdevice::configValue(const char *block, const char *key)
{ return ConfigValueInt(DeviceName.c_str(), block, key); }
template<>
inline al::optional<uint> ALCdevice::configValue(const char *block, const char *key)
{ return ConfigValueUInt(DeviceName.c_str(), block, key); }
template<>
inline al::optional<float> ALCdevice::configValue(const char *block, const char *key)
{ return ConfigValueFloat(DeviceName.c_str(), block, key); }
template<>
inline al::optional<bool> ALCdevice::configValue(const char *block, const char *key)
{ return ConfigValueBool(DeviceName.c_str(), block, key); }

#endif
