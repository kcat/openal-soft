#ifndef ALC_DEVICE_H
#define ALC_DEVICE_H

#include "config.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "alconfig.h"
#include "core/device.h"
#include "intrusive_ptr.h"

#if ALSOFT_EAX
#include "al/eax/x_ram.h"
#endif // ALSOFT_EAX

struct BackendBase;
struct BufferSubList;
struct EffectSubList;
struct FilterSubList;

using uint = unsigned int;


struct ALCdevice { };

namespace al {
struct Device;

struct DeviceDeleter { void operator()(gsl::owner<Device*> device) const noexcept; };
struct Device final : ALCdevice, intrusive_ref<Device,DeviceDeleter>, DeviceBase {
    /* This lock protects the device state (format, update size, etc.) from
     * being changed in multiple threads, or being accessed while being
     * changed. It's also used to serialize calls to the backend.
     */
    std::mutex StateLock;
    std::unique_ptr<BackendBase> Backend;

    u32 NumMonoSources{};
    u32 NumStereoSources{};

    // Maximum number of sources that can be created
    u32 SourcesMax{};
    // Maximum number of slots that can be created
    u32 AuxiliaryEffectSlotMax{};

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
    auto getOutputMode1() const noexcept -> OutputMode1;

    using OutputMode = OutputMode1;

    std::atomic<ALCenum> mLastError{ALC_NO_ERROR};

    // Map of Buffers for this device
    std::mutex BufferLock;
    std::vector<BufferSubList> BufferList;

    // Map of Effects for this device
    std::mutex EffectLock;
    std::vector<EffectSubList> EffectList;

    // Map of Filters for this device
    std::mutex FilterLock;
    std::vector<FilterSubList> FilterList;

#if ALSOFT_EAX
    u32 eax_x_ram_free_size{eax_x_ram_max_size};
#endif // ALSOFT_EAX


    std::unordered_map<u32, std::string> mBufferNames;
    std::unordered_map<u32, std::string> mEffectNames;
    std::unordered_map<u32, std::string> mFilterNames;

    std::string mVendorOverride;
    std::string mVersionOverride;
    std::string mRendererOverride;

    void enumerateHrtfs();

    auto getConfigValueBool(std::string_view const block, std::string_view const key,
        bool const def) const -> bool
    { return GetConfigValueBool(mDeviceName, block, key, def); }

    template<typename T>
    auto configValue(std::string_view block, std::string_view key) const -> std::optional<T> = delete;

    static auto Create(DeviceType type) -> intrusive_ptr<Device>;

    /** Stores the latest ALC device error. */
    static void SetGlobalError(ALCenum const errorCode) { SetError(nullptr, errorCode); }
    void setError(ALCenum const errorCode) { SetError(this, errorCode); }

    static inline auto sLastGlobalError = std::atomic{ALC_NO_ERROR};
    /* Flag to trap ALC device errors */
    static inline auto sTrapALCError = false;

protected:
    ~Device();

private:
    explicit Device(DeviceType type);

    static void SetError(Device *device, ALCenum errorCode);

    friend DeviceDeleter;
};

template<> inline
auto Device::configValue(std::string_view const block, std::string_view const key) const
    -> std::optional<std::string>
{ return ConfigValueStr(mDeviceName, block, key); }
template<> inline
auto Device::configValue(std::string_view const block, std::string_view const key) const
    -> std::optional<i32>
{ return ConfigValueI32(mDeviceName, block, key); }
template<> inline
auto Device::configValue(std::string_view const block, std::string_view const key) const
    -> std::optional<u32>
{ return ConfigValueU32(mDeviceName, block, key); }
template<> inline
auto Device::configValue(std::string_view const block, std::string_view const key) const
    -> std::optional<f32>
{ return ConfigValueF32(mDeviceName, block, key); }
template<> inline
auto Device::configValue(std::string_view const block, std::string_view const key) const
    -> std::optional<bool>
{ return ConfigValueBool(mDeviceName, block, key); }

} // namespace al

#endif
