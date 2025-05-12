
#include "config.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string_view>
#include <tuple>
#include <unordered_map>

#include "AL/alc.h"

#include "alnumeric.h"
#include "alstring.h"
#include "router.h"
#include "strutils.h"


namespace {

using namespace std::string_view_literals;

using LPALCdevice = ALCdevice*;

auto InitOnce = std::once_flag{};
void LoadDrivers() { std::call_once(InitOnce, []{ LoadDriverList(); }); }

struct FuncExportEntry {
    std::string_view funcName;
    void *address;
};
#define DECL(x) FuncExportEntry{ #x##sv, reinterpret_cast<void*>(x) }
const auto alcFunctions = std::array{
    DECL(alcCreateContext),
    DECL(alcMakeContextCurrent),
    DECL(alcProcessContext),
    DECL(alcSuspendContext),
    DECL(alcDestroyContext),
    DECL(alcGetCurrentContext),
    DECL(alcGetContextsDevice),
    DECL(alcOpenDevice),
    DECL(alcCloseDevice),
    DECL(alcGetError),
    DECL(alcIsExtensionPresent),
    DECL(alcGetProcAddress),
    DECL(alcGetEnumValue),
    DECL(alcGetString),
    DECL(alcGetIntegerv),
    DECL(alcCaptureOpenDevice),
    DECL(alcCaptureCloseDevice),
    DECL(alcCaptureStart),
    DECL(alcCaptureStop),
    DECL(alcCaptureSamples),

    DECL(alcSetThreadContext),
    DECL(alcGetThreadContext),

    DECL(alEnable),
    DECL(alDisable),
    DECL(alIsEnabled),
    DECL(alGetString),
    DECL(alGetBooleanv),
    DECL(alGetIntegerv),
    DECL(alGetFloatv),
    DECL(alGetDoublev),
    DECL(alGetBoolean),
    DECL(alGetInteger),
    DECL(alGetFloat),
    DECL(alGetDouble),
    DECL(alGetError),
    DECL(alIsExtensionPresent),
    DECL(alGetProcAddress),
    DECL(alGetEnumValue),
    DECL(alListenerf),
    DECL(alListener3f),
    DECL(alListenerfv),
    DECL(alListeneri),
    DECL(alListener3i),
    DECL(alListeneriv),
    DECL(alGetListenerf),
    DECL(alGetListener3f),
    DECL(alGetListenerfv),
    DECL(alGetListeneri),
    DECL(alGetListener3i),
    DECL(alGetListeneriv),
    DECL(alGenSources),
    DECL(alDeleteSources),
    DECL(alIsSource),
    DECL(alSourcef),
    DECL(alSource3f),
    DECL(alSourcefv),
    DECL(alSourcei),
    DECL(alSource3i),
    DECL(alSourceiv),
    DECL(alGetSourcef),
    DECL(alGetSource3f),
    DECL(alGetSourcefv),
    DECL(alGetSourcei),
    DECL(alGetSource3i),
    DECL(alGetSourceiv),
    DECL(alSourcePlayv),
    DECL(alSourceStopv),
    DECL(alSourceRewindv),
    DECL(alSourcePausev),
    DECL(alSourcePlay),
    DECL(alSourceStop),
    DECL(alSourceRewind),
    DECL(alSourcePause),
    DECL(alSourceQueueBuffers),
    DECL(alSourceUnqueueBuffers),
    DECL(alGenBuffers),
    DECL(alDeleteBuffers),
    DECL(alIsBuffer),
    DECL(alBufferData),
    DECL(alBufferf),
    DECL(alBuffer3f),
    DECL(alBufferfv),
    DECL(alBufferi),
    DECL(alBuffer3i),
    DECL(alBufferiv),
    DECL(alGetBufferf),
    DECL(alGetBuffer3f),
    DECL(alGetBufferfv),
    DECL(alGetBufferi),
    DECL(alGetBuffer3i),
    DECL(alGetBufferiv),
    DECL(alDopplerFactor),
    DECL(alDopplerVelocity),
    DECL(alSpeedOfSound),
    DECL(alDistanceModel),

    /* EFX 1.0 */
    DECL(alGenFilters),
    DECL(alDeleteFilters),
    DECL(alIsFilter),
    DECL(alFilterf),
    DECL(alFilterfv),
    DECL(alFilteri),
    DECL(alFilteriv),
    DECL(alGetFilterf),
    DECL(alGetFilterfv),
    DECL(alGetFilteri),
    DECL(alGetFilteriv),
    DECL(alGenEffects),
    DECL(alDeleteEffects),
    DECL(alIsEffect),
    DECL(alEffectf),
    DECL(alEffectfv),
    DECL(alEffecti),
    DECL(alEffectiv),
    DECL(alGetEffectf),
    DECL(alGetEffectfv),
    DECL(alGetEffecti),
    DECL(alGetEffectiv),
    DECL(alGenAuxiliaryEffectSlots),
    DECL(alDeleteAuxiliaryEffectSlots),
    DECL(alIsAuxiliaryEffectSlot),
    DECL(alAuxiliaryEffectSlotf),
    DECL(alAuxiliaryEffectSlotfv),
    DECL(alAuxiliaryEffectSloti),
    DECL(alAuxiliaryEffectSlotiv),
    DECL(alGetAuxiliaryEffectSlotf),
    DECL(alGetAuxiliaryEffectSlotfv),
    DECL(alGetAuxiliaryEffectSloti),
    DECL(alGetAuxiliaryEffectSlotiv),
};
#undef DECL

struct EnumExportEntry {
    std::string_view enumName;
    ALCenum value;
};
#define DECL(x) EnumExportEntry{ #x##sv, (x) }
const auto alcEnumerations = std::array{
    DECL(ALC_INVALID),
    DECL(ALC_FALSE),
    DECL(ALC_TRUE),

    DECL(ALC_MAJOR_VERSION),
    DECL(ALC_MINOR_VERSION),
    DECL(ALC_ATTRIBUTES_SIZE),
    DECL(ALC_ALL_ATTRIBUTES),
    DECL(ALC_DEFAULT_DEVICE_SPECIFIER),
    DECL(ALC_DEVICE_SPECIFIER),
    DECL(ALC_ALL_DEVICES_SPECIFIER),
    DECL(ALC_DEFAULT_ALL_DEVICES_SPECIFIER),
    DECL(ALC_EXTENSIONS),
    DECL(ALC_FREQUENCY),
    DECL(ALC_REFRESH),
    DECL(ALC_SYNC),
    DECL(ALC_MONO_SOURCES),
    DECL(ALC_STEREO_SOURCES),
    DECL(ALC_CAPTURE_DEVICE_SPECIFIER),
    DECL(ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER),
    DECL(ALC_CAPTURE_SAMPLES),

    DECL(ALC_NO_ERROR),
    DECL(ALC_INVALID_DEVICE),
    DECL(ALC_INVALID_CONTEXT),
    DECL(ALC_INVALID_ENUM),
    DECL(ALC_INVALID_VALUE),
    DECL(ALC_OUT_OF_MEMORY),

    DECL(AL_INVALID),
    DECL(AL_NONE),
    DECL(AL_FALSE),
    DECL(AL_TRUE),

    DECL(AL_SOURCE_RELATIVE),
    DECL(AL_CONE_INNER_ANGLE),
    DECL(AL_CONE_OUTER_ANGLE),
    DECL(AL_PITCH),
    DECL(AL_POSITION),
    DECL(AL_DIRECTION),
    DECL(AL_VELOCITY),
    DECL(AL_LOOPING),
    DECL(AL_BUFFER),
    DECL(AL_GAIN),
    DECL(AL_MIN_GAIN),
    DECL(AL_MAX_GAIN),
    DECL(AL_ORIENTATION),
    DECL(AL_REFERENCE_DISTANCE),
    DECL(AL_ROLLOFF_FACTOR),
    DECL(AL_CONE_OUTER_GAIN),
    DECL(AL_MAX_DISTANCE),
    DECL(AL_SEC_OFFSET),
    DECL(AL_SAMPLE_OFFSET),
    DECL(AL_BYTE_OFFSET),
    DECL(AL_SOURCE_TYPE),
    DECL(AL_STATIC),
    DECL(AL_STREAMING),
    DECL(AL_UNDETERMINED),

    DECL(AL_SOURCE_STATE),
    DECL(AL_INITIAL),
    DECL(AL_PLAYING),
    DECL(AL_PAUSED),
    DECL(AL_STOPPED),

    DECL(AL_BUFFERS_QUEUED),
    DECL(AL_BUFFERS_PROCESSED),

    DECL(AL_FORMAT_MONO8),
    DECL(AL_FORMAT_MONO16),
    DECL(AL_FORMAT_STEREO8),
    DECL(AL_FORMAT_STEREO16),

    DECL(AL_FREQUENCY),
    DECL(AL_BITS),
    DECL(AL_CHANNELS),
    DECL(AL_SIZE),

    DECL(AL_UNUSED),
    DECL(AL_PENDING),
    DECL(AL_PROCESSED),

    DECL(AL_NO_ERROR),
    DECL(AL_INVALID_NAME),
    DECL(AL_INVALID_ENUM),
    DECL(AL_INVALID_VALUE),
    DECL(AL_INVALID_OPERATION),
    DECL(AL_OUT_OF_MEMORY),

    DECL(AL_VENDOR),
    DECL(AL_VERSION),
    DECL(AL_RENDERER),
    DECL(AL_EXTENSIONS),

    DECL(AL_DOPPLER_FACTOR),
    DECL(AL_DOPPLER_VELOCITY),
    DECL(AL_DISTANCE_MODEL),
    DECL(AL_SPEED_OF_SOUND),

    DECL(AL_INVERSE_DISTANCE),
    DECL(AL_INVERSE_DISTANCE_CLAMPED),
    DECL(AL_LINEAR_DISTANCE),
    DECL(AL_LINEAR_DISTANCE_CLAMPED),
    DECL(AL_EXPONENT_DISTANCE),
    DECL(AL_EXPONENT_DISTANCE_CLAMPED),
};
#undef DECL

[[nodiscard]] constexpr auto GetNoErrorString() noexcept { return "No Error"; }
[[nodiscard]] constexpr auto GetInvalidDeviceString() noexcept { return "Invalid Device"; }
[[nodiscard]] constexpr auto GetInvalidContextString() noexcept { return "Invalid Context"; }
[[nodiscard]] constexpr auto GetInvalidEnumString() noexcept { return "Invalid Enum"; }
[[nodiscard]] constexpr auto GetInvalidValueString() noexcept { return "Invalid Value"; }
[[nodiscard]] constexpr auto GetOutOfMemoryString() noexcept { return "Out of Memory"; }
[[nodiscard]] constexpr auto GetExtensionString() noexcept -> const char*
{
    return "ALC_ENUMERATE_ALL_EXT ALC_ENUMERATION_EXT ALC_EXT_CAPTURE "
        "ALC_EXT_thread_local_context";
}

[[nodiscard]] consteval auto GetExtensionArray() noexcept
{
    constexpr auto extlist = std::string_view{GetExtensionString()};
    auto ret = std::array<std::string_view, std::ranges::count(extlist, ' ')+1>{};
    std::ranges::transform(extlist | std::views::split(' '), ret.begin(),
        [](auto&& namerange) { return std::string_view{namerange.begin(), namerange.end()}; });
    return ret;
}

constexpr auto alcMajorVersion = 1;
constexpr auto alcMinorVersion = 1;


auto EnumerationLock = std::recursive_mutex{};
auto ContextSwitchLock = std::mutex{};

auto LastError = std::atomic<ALCenum>{ALC_NO_ERROR};
auto DeviceIfaceMap = std::unordered_map<ALCdevice*,ALCuint>{};
auto ContextIfaceMap = std::unordered_map<ALCcontext*,ALCuint>{};

template<typename T, typename U, typename V> [[nodiscard]]
auto maybe_get(std::unordered_map<T,U> &list, V&& key) -> std::optional<U>
{
    auto iter = list.find(std::forward<V>(key));
    if(iter != list.end()) return iter->second;
    return std::nullopt;
}


struct DeviceList {
    std::vector<std::string_view> mNames;
};

class EnumeratedList {
    std::vector<ALCchar> mNamesStore;
    std::vector<DeviceList> mEnumeratedDevices;

public:
    void clear() noexcept
    {
        mEnumeratedDevices.clear();
        mNamesStore.clear();
    }

    explicit operator bool() const noexcept { return mEnumeratedDevices.empty(); }

    void reserveDeviceCount(const size_t count) { mEnumeratedDevices.reserve(count); }
    void appendDeviceList(const ALCchar *names, ALCuint idx);
    void finishEnumeration();

    [[nodiscard]]
    auto getDriverIndexForName(const std::string_view name) const -> std::optional<ALCuint>;

    [[nodiscard]]
    auto getNameData() const noexcept -> const char* { return mNamesStore.data(); }
};
auto DevicesList = EnumeratedList{};
auto AllDevicesList = EnumeratedList{};
auto CaptureDevicesList = EnumeratedList{};

void EnumeratedList::appendDeviceList(const ALCchar *names, ALCuint idx)
{
    auto *name_end = names;
    if(!name_end) return;

    auto count = 0_uz;
    while(*name_end)
    {
        TRACE("Enumerated \"{}\", driver {}", name_end, idx);
        ++count;
        name_end += strlen(name_end)+1; /* NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
    }

    const auto namespan = std::span{names, name_end};
    if(namespan.empty())
        return;

    mNamesStore.insert(mNamesStore.cend(), namespan.begin(), namespan.end());

    if(idx >= mEnumeratedDevices.size())
        mEnumeratedDevices.resize(idx+1);

    mEnumeratedDevices[idx].mNames.resize(count);
}

void EnumeratedList::finishEnumeration()
{
    /* Ensure the list is double-null termianted. */
    if(mNamesStore.empty())
        mNamesStore.emplace_back('\0');
    mNamesStore.emplace_back('\0');

    auto base = 0_uz;
    std::ranges::for_each(mEnumeratedDevices, [this,&base](DeviceList &list)
    {
        std::ranges::transform(mNamesStore | std::views::split('\0') | std::views::drop(base)
            | std::views::take(list.mNames.size()), list.mNames.begin(), [](auto&& namerange)
        { return std::string_view{namerange.begin(), namerange.end()}; });
        base += list.mNames.size();
    });
}

auto EnumeratedList::getDriverIndexForName(const std::string_view name) const
    -> std::optional<ALCuint>
{
    auto idx = 0u;
    std::ignore = std::ranges::any_of(mEnumeratedDevices
        | std::views::transform(&DeviceList::mNames),
        [name,&idx](const std::span<const std::string_view> names) -> bool
    {
        if(std::ranges::find(names, name) != names.end())
            return true;
        ++idx;
        return false;
    });
    if(idx < mEnumeratedDevices.size())
        return idx;
    return std::nullopt;
}


void InitCtxFuncs(DriverIface &iface)
{
    auto *device = iface.alcGetContextsDevice(iface.alcGetCurrentContext());

#define LOAD_PROC(x) do {                                                     \
    iface.x = reinterpret_cast<decltype(iface.x)>(iface.alGetProcAddress(#x));\
    if(!iface.x)                                                              \
        ERR("Failed to find entry point for {} in {}", #x,                    \
            wstr_to_utf8(iface.Name));                                        \
} while(0)
    if(iface.alcIsExtensionPresent(device, "ALC_EXT_EFX"))
    {
        LOAD_PROC(alGenFilters);
        LOAD_PROC(alDeleteFilters);
        LOAD_PROC(alIsFilter);
        LOAD_PROC(alFilterf);
        LOAD_PROC(alFilterfv);
        LOAD_PROC(alFilteri);
        LOAD_PROC(alFilteriv);
        LOAD_PROC(alGetFilterf);
        LOAD_PROC(alGetFilterfv);
        LOAD_PROC(alGetFilteri);
        LOAD_PROC(alGetFilteriv);
        LOAD_PROC(alGenEffects);
        LOAD_PROC(alDeleteEffects);
        LOAD_PROC(alIsEffect);
        LOAD_PROC(alEffectf);
        LOAD_PROC(alEffectfv);
        LOAD_PROC(alEffecti);
        LOAD_PROC(alEffectiv);
        LOAD_PROC(alGetEffectf);
        LOAD_PROC(alGetEffectfv);
        LOAD_PROC(alGetEffecti);
        LOAD_PROC(alGetEffectiv);
        LOAD_PROC(alGenAuxiliaryEffectSlots);
        LOAD_PROC(alDeleteAuxiliaryEffectSlots);
        LOAD_PROC(alIsAuxiliaryEffectSlot);
        LOAD_PROC(alAuxiliaryEffectSlotf);
        LOAD_PROC(alAuxiliaryEffectSlotfv);
        LOAD_PROC(alAuxiliaryEffectSloti);
        LOAD_PROC(alAuxiliaryEffectSlotiv);
        LOAD_PROC(alGetAuxiliaryEffectSlotf);
        LOAD_PROC(alGetAuxiliaryEffectSlotfv);
        LOAD_PROC(alGetAuxiliaryEffectSloti);
        LOAD_PROC(alGetAuxiliaryEffectSlotiv);
    }
#undef LOAD_PROC
}

} /* namespace */


ALC_API auto ALC_APIENTRY alcOpenDevice(const ALCchar *devicename) noexcept -> ALCdevice*
{
    LoadDrivers();

    auto *device = LPALCdevice{nullptr};
    auto idx = std::optional<ALCuint>{};

    /* Prior to the enumeration extension, apps would hardcode these names as a
     * quality hint for the wrapper driver. Ignore them since there's no sane
     * way to map them.
     */
    if(devicename && *devicename != '\0' && devicename != "DirectSound3D"sv
        && devicename != "DirectSound"sv && devicename != "MMSYSTEM"sv)
    {
        {
            auto enumlock = std::lock_guard{EnumerationLock};
            if(!DevicesList)
                std::ignore = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
            idx = DevicesList.getDriverIndexForName(devicename);
            if(!idx)
            {
                if(!AllDevicesList)
                    std::ignore = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
                idx = AllDevicesList.getDriverIndexForName(devicename);
            }
        }

        if(!idx)
        {
            LastError.store(ALC_INVALID_VALUE);
            TRACE("Failed to find driver for name \"{}\"", devicename);
            return nullptr;
        }
        TRACE("Found driver {} for name \"{}\"", *idx, devicename);
        device = DriverList[*idx]->alcOpenDevice(devicename);
    }
    else
    {
        auto drvidx = 0u;
        std::ignore = std::ranges::any_of(DriverList | std::views::transform(&DriverIfacePtr::get),
            [&device,&idx,&drvidx](const DriverIface *drv) -> bool
        {
            if(drv->ALCVer >= MakeALCVer(1, 1)
                || drv->alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT"))
            {
                TRACE("Using default device from driver {}", drvidx);
                device = drv->alcOpenDevice(nullptr);
                idx = drvidx;
                return true;
            }
            ++drvidx;
            return false;
        });
    }

    if(device)
    {
        try {
            DeviceIfaceMap.emplace(device, idx.value());
        }
        catch(...) {
            DriverList[idx.value()]->alcCloseDevice(device);
            device = nullptr;
        }
    }

    return device;
}

ALC_API auto ALC_APIENTRY alcCloseDevice(ALCdevice *device) noexcept -> ALCboolean
{
    if(const auto idx = maybe_get(DeviceIfaceMap, device))
    {
        if(!DriverList[*idx]->alcCloseDevice(device))
            return ALC_FALSE;
        DeviceIfaceMap.erase(device);
        return ALC_TRUE;
    }

    LastError.store(ALC_INVALID_DEVICE);
    return ALC_FALSE;
}


ALC_API auto ALC_APIENTRY alcCreateContext(ALCdevice *device, const ALCint *attrlist) noexcept
    -> ALCcontext*
{
    const auto idx = maybe_get(DeviceIfaceMap, device);
    if(!idx)
    {
        LastError.store(ALC_INVALID_DEVICE);
        return nullptr;
    }

    auto *context = DriverList[*idx]->alcCreateContext(device, attrlist);
    if(context)
    {
        try {
            ContextIfaceMap.emplace(context, *idx);
        }
        catch(...) {
            DriverList[*idx]->alcDestroyContext(context);
            context = nullptr;
        }
    }

    return context;
}

ALC_API auto ALC_APIENTRY alcMakeContextCurrent(ALCcontext *context) noexcept -> ALCboolean
{
    auto ctxlock = std::lock_guard{ContextSwitchLock};

    auto idx = std::optional<ALCuint>{};
    if(context)
    {
        idx = maybe_get(ContextIfaceMap, context);
        if(!idx)
        {
            LastError.store(ALC_INVALID_CONTEXT);
            return ALC_FALSE;
        }
        if(!DriverList[*idx]->alcMakeContextCurrent(context))
            return ALC_FALSE;

        std::call_once(DriverList[*idx]->InitOnceCtx, [idx]{ InitCtxFuncs(*DriverList[*idx]); });
    }

    /* Unset the context from the old driver if it's different from the new
     * current one.
     */
    if(!idx)
    {
        auto *oldiface = GetThreadDriver();
        if(oldiface) oldiface->alcSetThreadContext(nullptr);
        oldiface = CurrentCtxDriver.exchange(nullptr);
        if(oldiface) oldiface->alcMakeContextCurrent(nullptr);
    }
    else
    {
        auto *oldiface = GetThreadDriver();
        if(oldiface && oldiface != DriverList[*idx].get())
            oldiface->alcSetThreadContext(nullptr);
        oldiface = CurrentCtxDriver.exchange(DriverList[*idx].get());
        if(oldiface && oldiface != DriverList[*idx].get())
            oldiface->alcMakeContextCurrent(nullptr);
    }
    SetThreadDriver(nullptr);

    return ALC_TRUE;
}

ALC_API void ALC_APIENTRY alcProcessContext(ALCcontext *context) noexcept
{
    if(const auto idx = maybe_get(ContextIfaceMap, context))
        return DriverList[*idx]->alcProcessContext(context);

    LastError.store(ALC_INVALID_CONTEXT);
}

ALC_API void ALC_APIENTRY alcSuspendContext(ALCcontext *context) noexcept
{
    if(const auto idx = maybe_get(ContextIfaceMap, context))
        return DriverList[*idx]->alcSuspendContext(context);

    LastError.store(ALC_INVALID_CONTEXT);
}

ALC_API void ALC_APIENTRY alcDestroyContext(ALCcontext *context) noexcept
{
    if(const auto idx = maybe_get(ContextIfaceMap, context))
    {
        DriverList[*idx]->alcDestroyContext(context);
        ContextIfaceMap.erase(context);
        return;
    }
    LastError.store(ALC_INVALID_CONTEXT);
}

ALC_API auto ALC_APIENTRY alcGetCurrentContext() noexcept -> ALCcontext*
{
    auto *iface = GetThreadDriver();
    if(!iface) iface = CurrentCtxDriver.load();
    return iface ? iface->alcGetCurrentContext() : nullptr;
}

ALC_API auto ALC_APIENTRY alcGetContextsDevice(ALCcontext *context) noexcept -> ALCdevice*
{
    if(const auto idx = maybe_get(ContextIfaceMap, context))
        return DriverList[*idx]->alcGetContextsDevice(context);

    LastError.store(ALC_INVALID_CONTEXT);
    return nullptr;
}


ALC_API auto ALC_APIENTRY alcGetError(ALCdevice *device) noexcept -> ALCenum
{
    if(device)
    {
        if(const auto idx = maybe_get(DeviceIfaceMap, device))
            return DriverList[*idx]->alcGetError(device);
        return ALC_INVALID_DEVICE;
    }
    return LastError.exchange(ALC_NO_ERROR);
}

ALC_API auto ALC_APIENTRY alcIsExtensionPresent(ALCdevice *device, const ALCchar *extname) noexcept
    -> ALCboolean
{
    if(device)
    {
        if(const auto idx = maybe_get(DeviceIfaceMap, device))
            return DriverList[*idx]->alcIsExtensionPresent(device, extname);

        LastError.store(ALC_INVALID_DEVICE);
        return ALC_FALSE;
    }

    const auto matchext = [tofind = std::string_view{extname}](const std::string_view entry)
    { return tofind.size() == entry.size() && al::case_compare(tofind, entry) == 0; };
    return std::ranges::any_of(GetExtensionArray(), matchext) ? ALC_TRUE : ALC_FALSE;
}

ALC_API auto ALC_APIENTRY alcGetProcAddress(ALCdevice *device, const ALCchar *funcname) noexcept
    -> void*
{
    if(device)
    {
        if(const auto idx = maybe_get(DeviceIfaceMap, device))
            return DriverList[*idx]->alcGetProcAddress(device, funcname);

        LastError.store(ALC_INVALID_DEVICE);
        return nullptr;
    }

    const auto funcnames = alcFunctions | std::views::transform(&FuncExportEntry::funcName);
    const auto iter = std::ranges::find(funcnames, std::string_view{funcname});
    return (iter != funcnames.end()) ? iter.base()->address : nullptr;
}

ALC_API auto ALC_APIENTRY alcGetEnumValue(ALCdevice *device, const ALCchar *enumname) noexcept
    -> ALCenum
{
    if(device)
    {
        if(const auto idx = maybe_get(DeviceIfaceMap, device))
            return DriverList[*idx]->alcGetEnumValue(device, enumname);

        LastError.store(ALC_INVALID_DEVICE);
        return 0;
    }

    const auto enumnames = alcEnumerations | std::views::transform(&EnumExportEntry::enumName);
    const auto iter = std::ranges::find(enumnames, std::string_view{enumname});
    return (iter != enumnames.end()) ? iter.base()->value : 0;
}

ALC_API auto ALC_APIENTRY alcGetString(ALCdevice *device, ALCenum param) noexcept -> const ALCchar*
{
    LoadDrivers();

    if(device)
    {
        if(const auto idx = maybe_get(DeviceIfaceMap, device))
            return DriverList[*idx]->alcGetString(device, param);

        LastError.store(ALC_INVALID_DEVICE);
        return nullptr;
    }

    switch(param)
    {
    case ALC_NO_ERROR: return GetNoErrorString();
    case ALC_INVALID_ENUM: return GetInvalidEnumString();
    case ALC_INVALID_VALUE: return GetInvalidValueString();
    case ALC_INVALID_DEVICE: return GetInvalidDeviceString();
    case ALC_INVALID_CONTEXT: return GetInvalidContextString();
    case ALC_OUT_OF_MEMORY: return GetOutOfMemoryString();
    case ALC_EXTENSIONS: return GetExtensionString();

    case ALC_DEVICE_SPECIFIER:
    {
        auto enumlock = std::lock_guard{EnumerationLock};
        DevicesList.clear();
        DevicesList.reserveDeviceCount(DriverList.size());
        auto idx = 0u;
        std::ranges::for_each(DriverList | std::views::transform(&DriverIfacePtr::get),
            [&idx](const DriverIface *drv)
        {
            /* Only enumerate names from drivers that support it. */
            if(drv->ALCVer >= MakeALCVer(1, 1)
                || drv->alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT"))
                DevicesList.appendDeviceList(drv->alcGetString(nullptr,ALC_DEVICE_SPECIFIER), idx);
            ++idx;
        });
        DevicesList.finishEnumeration();
        return DevicesList.getNameData();
    }

    case ALC_ALL_DEVICES_SPECIFIER:
    {
        auto enumlock = std::lock_guard{EnumerationLock};
        AllDevicesList.clear();
        AllDevicesList.reserveDeviceCount(DriverList.size());
        auto idx = 0u;
        std::ranges::for_each(DriverList | std::views::transform(&DriverIfacePtr::get),
            [&idx](const DriverIface *drv)
        {
            /* If the driver doesn't support ALC_ENUMERATE_ALL_EXT, substitute
             * standard enumeration.
             */
            if(drv->alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT"))
                AllDevicesList.appendDeviceList(
                    drv->alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER), idx);
            else if(drv->ALCVer >= MakeALCVer(1, 1)
                || drv->alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT"))
                AllDevicesList.appendDeviceList(
                    drv->alcGetString(nullptr, ALC_DEVICE_SPECIFIER), idx);
            ++idx;
        });
        AllDevicesList.finishEnumeration();
        return AllDevicesList.getNameData();
    }

    case ALC_CAPTURE_DEVICE_SPECIFIER:
    {
        auto enumlock = std::lock_guard{EnumerationLock};
        CaptureDevicesList.clear();
        CaptureDevicesList.reserveDeviceCount(DriverList.size());
        auto idx = 0u;
        std::ranges::for_each(DriverList | std::views::transform(&DriverIfacePtr::get),
            [&idx](const DriverIface *drv)
        {
            if(drv->ALCVer >= MakeALCVer(1, 1)
                || drv->alcIsExtensionPresent(nullptr, "ALC_EXT_CAPTURE"))
                CaptureDevicesList.appendDeviceList(
                    drv->alcGetString(nullptr, ALC_CAPTURE_DEVICE_SPECIFIER), idx);
            ++idx;
        });
        CaptureDevicesList.finishEnumeration();
        return CaptureDevicesList.getNameData();
    }

    case ALC_DEFAULT_DEVICE_SPECIFIER:
    {
        const auto iter = std::ranges::find_if(DriverList, [](const DriverIfacePtr &drv)
        {
            return drv->ALCVer >= MakeALCVer(1, 1)
                || drv->alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT");
        });
        if(iter != DriverList.end())
            return (*iter)->alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER);
        return "";
    }

    case ALC_DEFAULT_ALL_DEVICES_SPECIFIER:
    {
        const auto iter = std::ranges::find_if(DriverList, [](const DriverIfacePtr &drv)
        { return drv->alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT") != ALC_FALSE; });
        if(iter != DriverList.end())
            return (*iter)->alcGetString(nullptr, ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
        return "";
    }

    case ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER:
    {
        const auto iter = std::ranges::find_if(DriverList, [](const DriverIfacePtr &drv)
        {
            return drv->ALCVer >= MakeALCVer(1, 1)
                || drv->alcIsExtensionPresent(nullptr, "ALC_EXT_CAPTURE");
        });
        if(iter != DriverList.end())
            return (*iter)->alcGetString(nullptr, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
        return "";
    }

    default:
        LastError.store(ALC_INVALID_ENUM);
        break;
    }
    return nullptr;
}

ALC_API void ALC_APIENTRY alcGetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size,
    ALCint *values) noexcept
{
    if(device)
    {
        if(const auto idx = maybe_get(DeviceIfaceMap, device))
            return DriverList[*idx]->alcGetIntegerv(device, param, size, values);

        LastError.store(ALC_INVALID_DEVICE);
        return;
    }

    if(size <= 0 || values == nullptr)
    {
        LastError.store(ALC_INVALID_VALUE);
        return;
    }

    switch(param)
    {
    case ALC_MAJOR_VERSION:
        if(size >= 1)
        {
            *values = alcMajorVersion;
            return;
        }
        LastError.store(ALC_INVALID_VALUE);
        return;
    case ALC_MINOR_VERSION:
        if(size >= 1)
        {
            *values = alcMinorVersion;
            return;
        }
        LastError.store(ALC_INVALID_VALUE);
        return;

    case ALC_ATTRIBUTES_SIZE:
    case ALC_ALL_ATTRIBUTES:
    case ALC_FREQUENCY:
    case ALC_REFRESH:
    case ALC_SYNC:
    case ALC_MONO_SOURCES:
    case ALC_STEREO_SOURCES:
    case ALC_CAPTURE_SAMPLES:
        LastError.store(ALC_INVALID_DEVICE);
        return;

    default:
        LastError.store(ALC_INVALID_ENUM);
        return;
    }
}


ALC_API auto ALC_APIENTRY alcCaptureOpenDevice(const ALCchar *devicename, ALCuint frequency,
    ALCenum format, ALCsizei buffersize) noexcept -> ALCdevice*
{
    LoadDrivers();

    auto *device = LPALCdevice{nullptr};
    auto idx = std::optional<ALCuint>{};

    if(devicename && *devicename != '\0')
    {
        {
            auto enumlock = std::lock_guard{EnumerationLock};
            if(!CaptureDevicesList)
                std::ignore = alcGetString(nullptr, ALC_CAPTURE_DEVICE_SPECIFIER);
            idx = CaptureDevicesList.getDriverIndexForName(devicename);
        }

        if(!idx)
        {
            LastError.store(ALC_INVALID_VALUE);
            TRACE("Failed to find driver for name \"{}\"", devicename);
            return nullptr;
        }
        TRACE("Found driver {} for name \"{}\"", *idx, devicename);
        device = DriverList[*idx]->alcCaptureOpenDevice(devicename, frequency, format, buffersize);
    }
    else
    {
        auto drvidx = 0u;
        std::ignore = std::ranges::any_of(DriverList | std::views::transform(&DriverIfacePtr::get),
            [frequency,format,buffersize,&device,&idx,&drvidx](const DriverIface *drv) -> bool
        {
            if(drv->ALCVer >= MakeALCVer(1, 1)
                || drv->alcIsExtensionPresent(nullptr, "ALC_EXT_CAPTURE"))
            {
                TRACE("Using default capture device from driver {}", drvidx);
                device = drv->alcCaptureOpenDevice(nullptr, frequency, format, buffersize);
                idx = drvidx;
                return true;
            }
            ++drvidx;
            return false;
        });
    }

    if(device)
    {
        try {
            DeviceIfaceMap.emplace(device, idx.value());
        }
        catch(...) {
            DriverList[idx.value()]->alcCaptureCloseDevice(device);
            device = nullptr;
        }
    }

    return device;
}

ALC_API auto ALC_APIENTRY alcCaptureCloseDevice(ALCdevice *device) noexcept -> ALCboolean
{
    if(const auto idx = maybe_get(DeviceIfaceMap, device))
    {
        if(!DriverList[*idx]->alcCaptureCloseDevice(device))
            return ALC_FALSE;
        DeviceIfaceMap.erase(device);
        return ALC_TRUE;
    }

    LastError.store(ALC_INVALID_DEVICE);
    return ALC_FALSE;
}

ALC_API void ALC_APIENTRY alcCaptureStart(ALCdevice *device) noexcept
{
    if(const auto idx = maybe_get(DeviceIfaceMap, device))
        return DriverList[*idx]->alcCaptureStart(device);
    LastError.store(ALC_INVALID_DEVICE);
}

ALC_API void ALC_APIENTRY alcCaptureStop(ALCdevice *device) noexcept
{
    if(const auto idx = maybe_get(DeviceIfaceMap, device))
        return DriverList[*idx]->alcCaptureStop(device);
    LastError.store(ALC_INVALID_DEVICE);
}

ALC_API void ALC_APIENTRY alcCaptureSamples(ALCdevice *device, ALCvoid *buffer, ALCsizei samples)
    noexcept
{
    if(const auto idx = maybe_get(DeviceIfaceMap, device))
        return DriverList[*idx]->alcCaptureSamples(device, buffer, samples);
    LastError.store(ALC_INVALID_DEVICE);
}


ALC_API auto ALC_APIENTRY alcSetThreadContext(ALCcontext *context) noexcept -> ALCboolean
{
    if(!context)
    {
        auto *oldiface = GetThreadDriver();
        if(oldiface && !oldiface->alcSetThreadContext(nullptr))
            return ALC_FALSE;
        SetThreadDriver(nullptr);
        return ALC_TRUE;
    }

    auto err = ALCenum{ALC_INVALID_CONTEXT};
    if(const auto idx = maybe_get(ContextIfaceMap, context))
    {
        if(DriverList[*idx]->alcSetThreadContext(context))
        {
            std::call_once(DriverList[*idx]->InitOnceCtx, [idx]{InitCtxFuncs(*DriverList[*idx]);});

            auto *oldiface = GetThreadDriver();
            if(oldiface != DriverList[*idx].get())
            {
                SetThreadDriver(DriverList[*idx].get());
                if(oldiface) oldiface->alcSetThreadContext(nullptr);
            }
            return ALC_TRUE;
        }
        err = DriverList[*idx]->alcGetError(nullptr);
    }
    LastError.store(err);
    return ALC_FALSE;
}

ALC_API auto ALC_APIENTRY alcGetThreadContext() noexcept -> ALCcontext*
{
    if(auto *iface = GetThreadDriver())
        return iface->alcGetThreadContext();
    return nullptr;
}
