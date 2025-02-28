/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "wasapi.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <memory.h>

#include <avrt.h>
#include <wtypes.h>
#include <mmdeviceapi.h>
#include <audiosessiontypes.h>
#include <audioclient.h>
#include <spatialaudioclient.h>
#include <cguid.h>
#include <devpropdef.h>
#include <mmreg.h>
#include <propsys.h>
#include <propkey.h>
#include <devpkey.h>
#ifndef _WAVEFORMATEXTENSIBLE_
#include <ks.h>
#include <ksmedia.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "albit.h"
#include "alc/alconfig.h"
#include "alnumeric.h"
#include "alspan.h"
#include "alstring.h"
#include "althrd_setname.h"
#include "comptr.h"
#include "core/converter.h"
#include "core/device.h"
#include "core/logging.h"
#include "fmt/core.h"
#include "fmt/chrono.h"
#include "ringbuffer.h"
#include "strutils.h"

#if ALSOFT_UWP
#include <winrt/Windows.Media.Core.h> // !!This is important!!
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Devices.h>

#include "alstring.h"
#endif

/* Some headers seem to define these as macros for __uuidof, which is annoying
 * since some headers don't declare them at all. Hopefully the ifdef is enough
 * to tell if they need to be declared.
 */
#ifndef KSDATAFORMAT_SUBTYPE_PCM
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif
#ifndef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif
#if !ALSOFT_UWP
DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80,0x20, 0x67,0xd1,0x46,0xa8,0x50,0xe0, 14);
DEFINE_PROPERTYKEY(PKEY_AudioEndpoint_FormFactor, 0x1da5d803, 0xd492, 0x4edd, 0x8c,0x23, 0xe0,0xc0,0xff,0xee,0x7f,0x0e, 0);
DEFINE_PROPERTYKEY(PKEY_AudioEndpoint_GUID, 0x1da5d803, 0xd492, 0x4edd, 0x8c, 0x23,0xe0, 0xc0,0xff,0xee,0x7f,0x0e, 4 );
#endif

namespace {

#if ALSOFT_UWP
using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Media::Devices;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Media::Devices;
#endif
#ifndef E_NOTFOUND
#define E_NOTFOUND E_NOINTERFACE
#endif

using namespace std::string_view_literals;
using std::chrono::nanoseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;

using ReferenceTime = std::chrono::duration<REFERENCE_TIME,std::ratio<1,10'000'000>>;


#define MONO SPEAKER_FRONT_CENTER
#define STEREO (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)
#define QUAD (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X5DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X5DOT1REAR (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X6DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_CENTER|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1DOT4 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT|SPEAKER_TOP_FRONT_LEFT|SPEAKER_TOP_FRONT_RIGHT|SPEAKER_TOP_BACK_LEFT|SPEAKER_TOP_BACK_RIGHT)

constexpr auto MaskFromTopBits(DWORD b) noexcept -> DWORD
{
    b |= b>>1;
    b |= b>>2;
    b |= b>>4;
    b |= b>>8;
    b |= b>>16;
    return b;
}
constexpr DWORD MonoMask{MaskFromTopBits(MONO)};
constexpr DWORD StereoMask{MaskFromTopBits(STEREO)};
constexpr DWORD QuadMask{MaskFromTopBits(QUAD)};
constexpr DWORD X51Mask{MaskFromTopBits(X5DOT1)};
constexpr DWORD X51RearMask{MaskFromTopBits(X5DOT1REAR)};
constexpr DWORD X61Mask{MaskFromTopBits(X6DOT1)};
constexpr DWORD X71Mask{MaskFromTopBits(X7DOT1)};
constexpr DWORD X714Mask{MaskFromTopBits(X7DOT1DOT4)};


#ifndef _MSC_VER
constexpr AudioObjectType operator|(AudioObjectType lhs, AudioObjectType rhs) noexcept
{ return static_cast<AudioObjectType>(lhs | al::to_underlying(rhs)); }
#endif

constexpr AudioObjectType ChannelMask_Mono{AudioObjectType_FrontCenter};
constexpr AudioObjectType ChannelMask_Stereo{AudioObjectType_FrontLeft
    | AudioObjectType_FrontRight};
constexpr AudioObjectType ChannelMask_Quad{AudioObjectType_FrontLeft | AudioObjectType_FrontRight
    | AudioObjectType_BackLeft | AudioObjectType_BackRight};
constexpr AudioObjectType ChannelMask_X51{AudioObjectType_FrontLeft | AudioObjectType_FrontRight
    | AudioObjectType_FrontCenter | AudioObjectType_LowFrequency | AudioObjectType_SideLeft
    | AudioObjectType_SideRight};
constexpr AudioObjectType ChannelMask_X61{AudioObjectType_FrontLeft | AudioObjectType_FrontRight
    | AudioObjectType_FrontCenter | AudioObjectType_LowFrequency | AudioObjectType_SideLeft
    | AudioObjectType_SideRight | AudioObjectType_BackCenter};
constexpr AudioObjectType ChannelMask_X71{AudioObjectType_FrontLeft | AudioObjectType_FrontRight
    | AudioObjectType_FrontCenter | AudioObjectType_LowFrequency | AudioObjectType_SideLeft
    | AudioObjectType_SideRight | AudioObjectType_BackLeft | AudioObjectType_BackRight};
constexpr AudioObjectType ChannelMask_X714{AudioObjectType_FrontLeft | AudioObjectType_FrontRight
    | AudioObjectType_FrontCenter | AudioObjectType_LowFrequency | AudioObjectType_SideLeft
    | AudioObjectType_SideRight | AudioObjectType_BackLeft | AudioObjectType_BackRight
    | AudioObjectType_TopFrontLeft | AudioObjectType_TopFrontRight | AudioObjectType_TopBackLeft
    | AudioObjectType_TopBackRight};
constexpr AudioObjectType ChannelMask_X7144{AudioObjectType_FrontLeft | AudioObjectType_FrontRight
    | AudioObjectType_FrontCenter | AudioObjectType_LowFrequency | AudioObjectType_SideLeft
    | AudioObjectType_SideRight | AudioObjectType_BackLeft | AudioObjectType_BackRight
    | AudioObjectType_TopFrontLeft | AudioObjectType_TopFrontRight | AudioObjectType_TopBackLeft
    | AudioObjectType_TopBackRight | AudioObjectType_BottomFrontLeft
    | AudioObjectType_BottomFrontRight | AudioObjectType_BottomBackLeft
    | AudioObjectType_BottomBackRight};


template<typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;


template<typename T>
struct CoTaskMemDeleter {
    void operator()(T *ptr) const { CoTaskMemFree(ptr); }
};
template<typename T>
using unique_coptr = std::unique_ptr<T,CoTaskMemDeleter<T>>;


/* Scales the given reftime value, rounding the result. */
constexpr auto RefTime2Samples(const ReferenceTime &val, DWORD srate) noexcept -> uint
{
    const auto retval = (val*srate + ReferenceTime{seconds{1}}/2) / seconds{1};
    return static_cast<uint>(std::min<decltype(retval)>(retval, std::numeric_limits<uint>::max()));
}


class GuidPrinter {
    std::string mMsg;

public:
    explicit GuidPrinter(const GUID &guid)
        : mMsg{fmt::format(
            "{{{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}}}",
            guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7])}
    { }
    [[nodiscard]] auto str() const noexcept -> const std::string& { return mMsg; }
};

struct PropVariant {
    PROPVARIANT mProp{};

public:
    PropVariant() { PropVariantInit(&mProp); }
    PropVariant(const PropVariant &rhs) : PropVariant{} { PropVariantCopy(&mProp, &rhs.mProp); }
    ~PropVariant() { clear(); }

    auto operator=(const PropVariant &rhs) -> PropVariant&
    {
        if(this != &rhs)
            PropVariantCopy(&mProp, &rhs.mProp);
        return *this;
    }

    void clear() { PropVariantClear(&mProp); }

    PROPVARIANT* get() noexcept { return &mProp; }

    /* NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) */
    [[nodiscard]]
    auto type() const noexcept -> VARTYPE { return mProp.vt; }

    template<typename T> [[nodiscard]]
    auto value() const -> T
    {
        if constexpr(std::is_same_v<T,uint>)
        {
            alassert(mProp.vt == VT_UI4 || mProp.vt == VT_UINT);
            return mProp.uintVal;
        }
        else if constexpr(std::is_same_v<T,std::wstring_view> || std::is_same_v<T,std::wstring>
            || std::is_same_v<T,LPWSTR> || std::is_same_v<T,LPCWSTR>)
        {
            alassert(mProp.vt == VT_LPWSTR);
            return mProp.pwszVal;
        }
    }

    void setBlob(const al::span<BYTE> data)
    {
        if constexpr(sizeof(size_t) > sizeof(ULONG))
            alassert(data.size() <= std::numeric_limits<ULONG>::max());
        mProp.vt = VT_BLOB;
        mProp.blob.cbSize = static_cast<ULONG>(data.size());
        mProp.blob.pBlobData = data.data();
    }
    /* NOLINTEND(cppcoreguidelines-pro-type-union-access) */
};

struct DevMap {
    std::string name;
    std::string endpoint_guid; // obtained from PKEY_AudioEndpoint_GUID , set to "Unknown device GUID" if absent.
    std::wstring devid;

    template<typename T0, typename T1, typename T2>
    DevMap(T0&& name_, T1&& guid_, T2&& devid_)
      : name{std::forward<T0>(name_)}
      , endpoint_guid{std::forward<T1>(guid_)}
      , devid{std::forward<T2>(devid_)}
    { }
    /* To prevent GCC from complaining it doesn't want to inline this. */
    ~DevMap();
};
DevMap::~DevMap() = default;

bool checkName(const al::span<DevMap> list, const std::string_view name)
{
    auto match_name = [name](const DevMap &entry) -> bool { return entry.name == name; };
    return std::find_if(list.cbegin(), list.cend(), match_name) != list.cend();
}


struct DeviceList {
    auto lock() noexcept(noexcept(mMutex.lock())) { return mMutex.lock(); }
    auto unlock() noexcept(noexcept(mMutex.unlock())) { return mMutex.unlock(); }

private:
    std::mutex mMutex;
    std::vector<DevMap> mPlayback;
    std::vector<DevMap> mCapture;
    std::wstring mPlaybackDefaultId;
    std::wstring mCaptureDefaultId;

    friend struct DeviceListLock;
};
struct DeviceListLock : public std::unique_lock<DeviceList> {
    using std::unique_lock<DeviceList>::unique_lock;

    [[nodiscard]] auto& getPlaybackList() const noexcept { return mutex()->mPlayback; }
    [[nodiscard]] auto& getCaptureList() const noexcept { return mutex()->mCapture; }

    void setPlaybackDefaultId(std::wstring_view devid) const { mutex()->mPlaybackDefaultId = devid; }
    [[nodiscard]] auto getPlaybackDefaultId() const noexcept -> std::wstring_view { return mutex()->mPlaybackDefaultId; }
    void setCaptureDefaultId(std::wstring_view devid) const { mutex()->mCaptureDefaultId = devid; }
    [[nodiscard]] auto getCaptureDefaultId() const noexcept -> std::wstring_view { return mutex()->mCaptureDefaultId; }
};

DeviceList gDeviceList;
std::condition_variable_any gInitCV;
std::atomic<bool> gInitDone{false};


#ifdef AVRTAPI
struct AvrtHandleCloser {
    void operator()(HANDLE handle) { AvRevertMmThreadCharacteristics(handle); }
};
using AvrtHandlePtr = std::unique_ptr<std::remove_pointer_t<HANDLE>,AvrtHandleCloser>;
#endif

#if ALSOFT_UWP
enum EDataFlow {
    eRender              = 0,
    eCapture             = (eRender + 1),
    eAll                 = (eCapture + 1),
    EDataFlow_enum_count = (eAll + 1)
};
#endif

#if ALSOFT_UWP
using DeviceHandle = Windows::Devices::Enumeration::DeviceInformation;
using EventRegistrationToken = winrt::event_token;
#else
using DeviceHandle = ComPtr<IMMDevice>;
#endif


struct NameGUIDPair { std::string mName; std::string mGuid; };
auto GetDeviceNameAndGuid(const DeviceHandle &device) -> NameGUIDPair
{
    constexpr auto UnknownName = "Unknown Device Name"sv;
    constexpr auto UnknownGuid = "Unknown Device GUID"sv;

#if !ALSOFT_UWP
    auto ps = ComPtr<IPropertyStore>{};
    auto hr = device->OpenPropertyStore(STGM_READ, al::out_ptr(ps));
    if(FAILED(hr))
    {
        WARN("OpenPropertyStore failed: {:#x}", as_unsigned(hr));
        return NameGUIDPair{std::string{UnknownName}, std::string{UnknownGuid}};
    }

    auto ret = NameGUIDPair{};
    auto pvprop = PropVariant{};
    hr = ps->GetValue(al::bit_cast<PROPERTYKEY>(DEVPKEY_Device_FriendlyName), pvprop.get());
    if(FAILED(hr))
        WARN("GetValue Device_FriendlyName failed: {:#x}", as_unsigned(hr));
    else if(pvprop.type() == VT_LPWSTR)
        ret.mName = wstr_to_utf8(pvprop.value<std::wstring_view>());
    else
        WARN("Unexpected Device_FriendlyName PROPVARIANT type: {:#04x}", pvprop.type());

    pvprop.clear();
    hr = ps->GetValue(al::bit_cast<PROPERTYKEY>(PKEY_AudioEndpoint_GUID), pvprop.get());
    if(FAILED(hr))
        WARN("GetValue AudioEndpoint_GUID failed: {:#x}", as_unsigned(hr));
    else if(pvprop.type() == VT_LPWSTR)
        ret.mGuid = wstr_to_utf8(pvprop.value<std::wstring_view>());
    else
        WARN("Unexpected AudioEndpoint_GUID PROPVARIANT type: {:#04x}", pvprop.type());
#else
    auto ret = NameGUIDPair{wstr_to_utf8(device.Name()), {}};

    // device->Id is DeviceInterfacePath: \\?\SWD#MMDEVAPI#{0.0.0.00000000}.{a21c17a0-fc1d-405e-ab5a-b513422b57d1}#{e6327cad-dcec-4949-ae8a-991e976a79d2}
    auto devIfPath = device.Id();
    if(auto devIdStart = wcsstr(devIfPath.data(), L"}."))
    {
        devIdStart += 2;  // L"}."
        if(auto devIdStartEnd = wcschr(devIdStart, L'#'))
        {
            ret.mGuid = wstr_to_utf8(std::wstring_view{devIdStart,
                static_cast<size_t>(devIdStartEnd - devIdStart)});
            std::transform(ret.mGuid.begin(), ret.mGuid.end(), ret.mGuid.begin(),
                [](char ch) { return static_cast<char>(std::toupper(ch)); });
        }
    }
#endif
    if(ret.mName.empty()) ret.mName = UnknownName;
    if(ret.mGuid.empty()) ret.mGuid = UnknownGuid;
    return ret;
}
#if !ALSOFT_UWP
EndpointFormFactor GetDeviceFormfactor(IMMDevice *device)
{
    ComPtr<IPropertyStore> ps;
    HRESULT hr{device->OpenPropertyStore(STGM_READ, al::out_ptr(ps))};
    if(FAILED(hr))
    {
        WARN("OpenPropertyStore failed: {:#x}", as_unsigned(hr));
        return UnknownFormFactor;
    }

    EndpointFormFactor formfactor{UnknownFormFactor};
    PropVariant pvform;
    hr = ps->GetValue(PKEY_AudioEndpoint_FormFactor, pvform.get());
    if(FAILED(hr))
        WARN("GetValue AudioEndpoint_FormFactor failed: {:#x}", as_unsigned(hr));
    else if(pvform.type() == VT_UI4)
        formfactor = static_cast<EndpointFormFactor>(pvform.value<uint>());
    else if(pvform.type() != VT_EMPTY)
        WARN("Unexpected PROPVARIANT type: {:#04x}", pvform.type());
    return formfactor;
}
#endif


#if ALSOFT_UWP
struct DeviceEnumHelper final : public IActivateAudioInterfaceCompletionHandler {
    DeviceEnumHelper()
    {
        /* TODO: UWP also needs to watch for device added/removed events and
         * dynamically add/remove devices from the lists.
         */
        mActiveClientEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        static constexpr auto playback_cb = [](const IInspectable &sender [[maybe_unused]],
            const DefaultAudioRenderDeviceChangedEventArgs &args)
        {
            if(args.Role() == AudioDeviceRole::Default)
            {
                const auto msg = std::string{"Default playback device changed: " +
                    wstr_to_utf8(args.Id())};
                alc::Event(alc::EventType::DefaultDeviceChanged, alc::DeviceType::Playback, msg);
            }
        };
        mRenderDeviceChangedToken = MediaDevice::DefaultAudioRenderDeviceChanged(playback_cb);

        static constexpr auto capture_cb = [](const IInspectable &sender [[maybe_unused]],
            const DefaultAudioCaptureDeviceChangedEventArgs &args)
        {
            if(args.Role() == AudioDeviceRole::Default)
            {
                const auto msg = std::string{"Default capture device changed: " +
                    wstr_to_utf8(args.Id())};
                alc::Event(alc::EventType::DefaultDeviceChanged, alc::DeviceType::Capture, msg);
            }
        };
        mCaptureDeviceChangedToken = MediaDevice::DefaultAudioCaptureDeviceChanged(capture_cb);
    }

    ~DeviceEnumHelper()
    {
        MediaDevice::DefaultAudioRenderDeviceChanged(mRenderDeviceChangedToken);
        MediaDevice::DefaultAudioCaptureDeviceChanged(mCaptureDeviceChangedToken);

        if(mActiveClientEvent != nullptr)
            CloseHandle(mActiveClientEvent);
        mActiveClientEvent = nullptr;
    }
#else

struct DeviceEnumHelper final : private IMMNotificationClient {
    DeviceEnumHelper() = default;
    ~DeviceEnumHelper()
    {
        if(mEnumerator)
            mEnumerator->UnregisterEndpointNotificationCallback(this);
        mEnumerator = nullptr;
    }
#endif

    template<typename T>
    auto as() noexcept -> T { return T{this}; }

    /** -------------------------- IUnknown ----------------------------- */
    std::atomic<ULONG> mRefCount{1};
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return mRefCount.fetch_add(1u) + 1u; }
    STDMETHODIMP_(ULONG) Release() noexcept override { return mRefCount.fetch_sub(1u) - 1u; }

    STDMETHODIMP QueryInterface(const IID& IId, void **UnknownPtrPtr) noexcept override
    {
        // Three rules of QueryInterface:
        // https://docs.microsoft.com/en-us/windows/win32/com/rules-for-implementing-queryinterface
        // 1. Objects must have identity.
        // 2. The set of interfaces on an object instance must be static.
        // 3. It must be possible to query successfully for any interface on an object from any other interface.

        // If ppvObject(the address) is nullptr, then this method returns E_POINTER.
        if(!UnknownPtrPtr)
            return E_POINTER;

        // https://docs.microsoft.com/en-us/windows/win32/com/implementing-reference-counting
        // Whenever a client calls a method(or API function), such as QueryInterface, that returns a new interface
        // pointer, the method being called is responsible for incrementing the reference count through the returned
        // pointer. For example, when a client first creates an object, it receives an interface pointer to an object
        // that, from the client's point of view, has a reference count of one. If the client then calls AddRef on the
        // interface pointer, the reference count becomes two. The client must call Release twice on the interface
        // pointer to drop all of its references to the object.
#if ALSOFT_UWP
        if(IId == __uuidof(IActivateAudioInterfaceCompletionHandler))
        {
            *UnknownPtrPtr = as<IActivateAudioInterfaceCompletionHandler*>();
            AddRef();
            return S_OK;
        }
#else
        if(IId == __uuidof(IMMNotificationClient))
        {
            *UnknownPtrPtr = as<IMMNotificationClient*>();
            AddRef();
            return S_OK;
        }
#endif
        else if(IId == __uuidof(IAgileObject) || IId == __uuidof(IUnknown))
        {
            *UnknownPtrPtr = as<IUnknown*>();
            AddRef();
            return S_OK;
        }

        // This method returns S_OK if the interface is supported, and E_NOINTERFACE otherwise.
        *UnknownPtrPtr = nullptr;
        return E_NOINTERFACE;
    }

#if ALSOFT_UWP
    /** ----------------------- IActivateAudioInterfaceCompletionHandler ------------ */
    HRESULT ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override
    {
        SetEvent(mActiveClientEvent);

        // Need to return S_OK
        return S_OK;
    }
#else
    /** ----------------------- IMMNotificationClient ------------ */
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) noexcept override
    {
        TRACE("OnDeviceStateChanged({}, {:#x})", deviceId ? wstr_to_utf8(deviceId)
            : std::string{"<null>"}, newState);

        if(!(newState&DEVICE_STATE_ACTIVE))
            return DeviceRemoved(deviceId);
        return DeviceAdded(deviceId);
    }

    STDMETHODIMP OnDeviceAdded(LPCWSTR deviceId) noexcept override
    {
        TRACE("OnDeviceAdded({})", deviceId ? wstr_to_utf8(deviceId) : std::string{"<null>"});
        return DeviceAdded(deviceId);
    }

    STDMETHODIMP OnDeviceRemoved(LPCWSTR deviceId) noexcept override
    {
        TRACE("OnDeviceRemoved({})", deviceId ? wstr_to_utf8(deviceId) : std::string{"<null>"});
        return DeviceRemoved(deviceId);
    }

    /* NOLINTNEXTLINE(clazy-function-args-by-ref) */
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR /*pwstrDeviceId*/, const PROPERTYKEY /*key*/) noexcept override { return S_OK; }

    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR defaultDeviceId) noexcept override
    {
        TRACE("OnDefaultDeviceChanged({}, {}, {})", al::to_underlying(flow),
            al::to_underlying(role), defaultDeviceId ? wstr_to_utf8(defaultDeviceId)
            : std::string{"<null>"});

        if(role != eMultimedia)
            return S_OK;

        const auto devid = defaultDeviceId ? std::wstring_view{defaultDeviceId}
            : std::wstring_view{};
        if(flow == eRender)
        {
            DeviceListLock{gDeviceList}.setPlaybackDefaultId(devid);
            const std::string msg{"Default playback device changed: " + wstr_to_utf8(devid)};
            alc::Event(alc::EventType::DefaultDeviceChanged, alc::DeviceType::Playback, msg);
        }
        else if(flow == eCapture)
        {
            DeviceListLock{gDeviceList}.setCaptureDefaultId(devid);
            const std::string msg{"Default capture device changed: " + wstr_to_utf8(devid)};
            alc::Event(alc::EventType::DefaultDeviceChanged, alc::DeviceType::Capture, msg);
        }
        return S_OK;
    }
#endif

    /** ------------------------ DeviceEnumHelper -------------------------- */
    HRESULT init()
    {
#if !ALSOFT_UWP
        HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator), al::out_ptr(mEnumerator))};
        if(SUCCEEDED(hr))
            mEnumerator->RegisterEndpointNotificationCallback(this);
        else
            WARN("Failed to create IMMDeviceEnumerator instance: {:#x}", as_unsigned(hr));
        return hr;
#else
        return S_OK;
#endif
    }

    std::wstring probeDevices(EDataFlow flowdir, std::vector<DevMap> &list)
    {
        std::wstring defaultId;
        std::vector<DevMap>{}.swap(list);

#if !ALSOFT_UWP
        ComPtr<IMMDeviceCollection> coll;
        HRESULT hr{mEnumerator->EnumAudioEndpoints(flowdir, DEVICE_STATE_ACTIVE,
            al::out_ptr(coll))};
        if(FAILED(hr))
        {
            ERR("Failed to enumerate audio endpoints: {:#x}", as_unsigned(hr));
            return defaultId;
        }

        UINT count{0};
        hr = coll->GetCount(&count);
        if(SUCCEEDED(hr) && count > 0)
            list.reserve(count);

        ComPtr<IMMDevice> device;
        hr = mEnumerator->GetDefaultAudioEndpoint(flowdir, eMultimedia, al::out_ptr(device));
        if(SUCCEEDED(hr))
        {
            auto devid = unique_coptr<WCHAR>{};
            if(auto hr2 = device->GetId(al::out_ptr(devid)); SUCCEEDED(hr2))
                defaultId = devid.get();
            else
                ERR("Failed to get device id: {:#x}", as_unsigned(hr));
            device = nullptr;
        }

        for(UINT i{0};i < count;++i)
        {
            hr = coll->Item(i, al::out_ptr(device));
            if(FAILED(hr))
                continue;

            auto devid = unique_coptr<WCHAR>{};
            if(auto hr2 = device->GetId(al::out_ptr(devid)); SUCCEEDED(hr2))
                std::ignore = AddDevice(device, devid.get(), list);
            else
                ERR("Failed to get device id: {:#x}", as_unsigned(hr));
            device = nullptr;
        }
#else
        const auto deviceRole = Windows::Media::Devices::AudioDeviceRole::Default;
        auto DefaultAudioId   = flowdir == eRender ? MediaDevice::GetDefaultAudioRenderId(deviceRole)
                                                   : MediaDevice::GetDefaultAudioCaptureId(deviceRole);
        if(!DefaultAudioId.empty())
        {
            auto deviceInfo = DeviceInformation::CreateFromIdAsync(DefaultAudioId, nullptr,
                DeviceInformationKind::DeviceInterface).get();
            if(deviceInfo)
                defaultId = deviceInfo.Id().data();
        }

        // Get the string identifier of the audio renderer
        auto AudioSelector = flowdir == eRender ? MediaDevice::GetAudioRenderSelector() : MediaDevice::GetAudioCaptureSelector();

        // Setup the asynchronous callback
        auto&& DeviceInfoCollection = DeviceInformation::FindAllAsync(AudioSelector, /*PropertyList*/nullptr, DeviceInformationKind::DeviceInterface).get();
        if(DeviceInfoCollection)
        {
            try {
                auto deviceCount = DeviceInfoCollection.Size();
                for(unsigned int i{0};i < deviceCount;++i)
                {
                    auto deviceInfo = DeviceInfoCollection.GetAt(i);
                    if(deviceInfo)
                        std::ignore = AddDevice(deviceInfo, deviceInfo.Id().data(), list);
                }
            }
            catch (const winrt::hresult_error& /*ex*/) {
            }
        }
#endif

        return defaultId;
    }

private:
    static bool AddDevice(const DeviceHandle &device, const WCHAR *devid, std::vector<DevMap> &list)
    {
        for(auto &entry : list)
        {
            if(entry.devid == devid)
                return false;
        }

        auto [name, guid] = GetDeviceNameAndGuid(device);
        auto count = 1;
        auto newname = name;
        while(checkName(list, newname))
            newname = fmt::format("{} #{}", name, ++count);
        const auto &newentry = list.emplace_back(std::move(newname), std::move(guid), devid);

        TRACE("Got device \"{}\", \"{}\", \"{}\"", newentry.name, newentry.endpoint_guid,
            wstr_to_utf8(newentry.devid));
        return true;
    }

#if !ALSOFT_UWP
    STDMETHODIMP DeviceAdded(LPCWSTR deviceId) noexcept
    {
        auto device = ComPtr<IMMDevice>{};
        auto hr = mEnumerator->GetDevice(deviceId, al::out_ptr(device));
        if(FAILED(hr))
        {
            ERR("Failed to get device: {:#x}", as_unsigned(hr));
            return S_OK;
        }

        auto state = DWORD{};
        hr = device->GetState(&state);
        if(FAILED(hr))
        {
            ERR("Failed to get device state: {:#x}", as_unsigned(hr));
            return S_OK;
        }
        if(!(state&DEVICE_STATE_ACTIVE))
            return S_OK;

        auto endpoint = ComPtr<IMMEndpoint>{};
        hr = device->QueryInterface(__uuidof(IMMEndpoint), al::out_ptr(endpoint));
        if(FAILED(hr))
        {
            ERR("Failed to get device endpoint: {:#x}", as_unsigned(hr));
            return S_OK;
        }

        auto flowdir = EDataFlow{};
        hr = endpoint->GetDataFlow(&flowdir);
        if(FAILED(hr))
        {
            ERR("Failed to get endpoint data flow: {:#x}", as_unsigned(hr));
            return S_OK;
        }

        auto devlock = DeviceListLock{gDeviceList};
        auto &list = (flowdir == eRender) ? devlock.getPlaybackList() : devlock.getCaptureList();

        if(AddDevice(device, deviceId, list))
        {
            const auto devtype = (flowdir == eRender) ? alc::DeviceType::Playback
                : alc::DeviceType::Capture;
            const auto msg = "Device added: "+list.back().name;
            alc::Event(alc::EventType::DeviceAdded, devtype, msg);
        }

        return S_OK;
    }

    STDMETHODIMP DeviceRemoved(LPCWSTR deviceId) noexcept
    {
        auto devlock = DeviceListLock{gDeviceList};
        for(auto flowdir : std::array{eRender, eCapture})
        {
            auto &list = (flowdir==eRender) ? devlock.getPlaybackList() : devlock.getCaptureList();
            auto devtype = (flowdir==eRender)?alc::DeviceType::Playback : alc::DeviceType::Capture;

            /* Find the ID in the list to remove. */
            auto iter = std::find_if(list.begin(), list.end(),
                [deviceId](const DevMap &entry) noexcept { return deviceId == entry.devid; });
            if(iter == list.end()) continue;

            TRACE("Removing device \"{}\", \"{}\", \"{}\"", iter->name, iter->endpoint_guid,
                wstr_to_utf8(iter->devid));

            std::string msg{"Device removed: "+std::move(iter->name)};
            list.erase(iter);

            alc::Event(alc::EventType::DeviceRemoved, devtype, msg);
        }
        return S_OK;
    }

    ComPtr<IMMDeviceEnumerator> mEnumerator{nullptr};

#else

    HANDLE mActiveClientEvent{nullptr};

    EventRegistrationToken mRenderDeviceChangedToken;
    EventRegistrationToken mCaptureDeviceChangedToken;
#endif

    static inline std::mutex mMsgLock;
    static inline std::condition_variable mMsgCond;
    static inline bool mQuit{false};

    [[nodiscard]]
    static bool quit()
    {
        auto lock = std::unique_lock{mMsgLock};
        mMsgCond.wait(lock, []{return mQuit;});
        return mQuit;
    }

public:
    static void messageHandler(std::promise<HRESULT> *promise);
};

/* Manages a DeviceEnumHelper on its own thread, to track available devices. */
void DeviceEnumHelper::messageHandler(std::promise<HRESULT> *promise)
{
    TRACE("Starting watcher thread");

    ComWrapper com{COINIT_MULTITHREADED};
    if(!com)
    {
        WARN("Failed to initialize COM: {:#x}", as_unsigned(com.status()));
        promise->set_value(com.status());
        return;
    }

    auto helper = std::optional<DeviceEnumHelper>{};
    try {
        auto devlock = DeviceListLock{gDeviceList};

        auto hr = helper.emplace().init();
        promise->set_value(hr);
        promise = nullptr;
        if(FAILED(hr))
            return;

        auto defaultId = helper->probeDevices(eRender, devlock.getPlaybackList());
        if(!defaultId.empty()) devlock.setPlaybackDefaultId(defaultId);

        defaultId = helper->probeDevices(eCapture, devlock.getCaptureList());
        if(!defaultId.empty()) devlock.setCaptureDefaultId(defaultId);

        gInitDone.store(true, std::memory_order_relaxed);
    }
    catch(std::exception &e) {
        ERR("Exception probing devices: {}", e.what());
        if(promise)
            promise->set_value(E_FAIL);
        return;
    }

    TRACE("Watcher thread started");
    gInitCV.notify_all();

    while(!quit()) {
        /* Do nothing. */
    }
}


#if ALSOFT_UWP
struct DeviceHelper final : public IActivateAudioInterfaceCompletionHandler {
    DeviceHelper()
    {
        mActiveClientEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    ~DeviceHelper()
    {
        if(mActiveClientEvent != nullptr)
            CloseHandle(mActiveClientEvent);
        mActiveClientEvent = nullptr;
    }

    template<typename T>
    auto as() noexcept -> T { return T{this}; }

    /** -------------------------- IUnknown ----------------------------- */
    std::atomic<ULONG> mRefCount{1};
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return mRefCount.fetch_add(1u) + 1u; }
    STDMETHODIMP_(ULONG) Release() noexcept override { return mRefCount.fetch_sub(1u) - 1u; }

    STDMETHODIMP QueryInterface(const IID& IId, void **UnknownPtrPtr) noexcept override
    {
        // Three rules of QueryInterface:
        // https://docs.microsoft.com/en-us/windows/win32/com/rules-for-implementing-queryinterface
        // 1. Objects must have identity.
        // 2. The set of interfaces on an object instance must be static.
        // 3. It must be possible to query successfully for any interface on an object from any other interface.

        // If ppvObject(the address) is nullptr, then this method returns E_POINTER.
        if(!UnknownPtrPtr)
            return E_POINTER;

        if(IId == __uuidof(IActivateAudioInterfaceCompletionHandler))
        {
            *UnknownPtrPtr = as<IActivateAudioInterfaceCompletionHandler*>();
            AddRef();
            return S_OK;
        }
        else if(IId == __uuidof(IAgileObject) || IId == __uuidof(IUnknown))
        {
            *UnknownPtrPtr = as<IUnknown*>();
            AddRef();
            return S_OK;
        }

        // This method returns S_OK if the interface is supported, and E_NOINTERFACE otherwise.
        *UnknownPtrPtr = nullptr;
        return E_NOINTERFACE;
    }

    /** ----------------------- IActivateAudioInterfaceCompletionHandler ------------ */
    HRESULT ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override
    {
        SetEvent(mActiveClientEvent);

        // Need to return S_OK
        return S_OK;
    }

    /** -------------------------- DeviceHelper ----------------------------- */
    [[nodiscard]] constexpr
    auto init() -> HRESULT { return S_OK; }

    [[nodiscard]]
    auto openDevice(const std::wstring &devid, EDataFlow flow, DeviceHandle &device) -> HRESULT
    {
        const auto deviceRole = Windows::Media::Devices::AudioDeviceRole::Default;
        auto devIfPath =
            devid.empty() ? (flow == eRender ? MediaDevice::GetDefaultAudioRenderId(deviceRole)
                                             : MediaDevice::GetDefaultAudioCaptureId(deviceRole))
                          : winrt::hstring(devid.c_str());
        if (devIfPath.empty())
            return E_POINTER;

        auto&& deviceInfo = DeviceInformation::CreateFromIdAsync(devIfPath, nullptr,
            DeviceInformationKind::DeviceInterface).get();
        if(!deviceInfo)
            return E_NOINTERFACE;
        device = deviceInfo;
        return S_OK;
    }

    [[nodiscard]]
    auto activateAudioClient(_In_ DeviceHandle &device, _In_ REFIID iid, void **ppv) -> HRESULT
    {
        ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
        HRESULT hr{ActivateAudioInterfaceAsync(device.Id().data(), iid, nullptr, this,
            al::out_ptr(asyncOp))};
        if(FAILED(hr))
            return hr;

        /* I don't like waiting for INFINITE time, but the activate operation
         * can take an indefinite amount of time since it can require user
         * input.
         */
        DWORD res{WaitForSingleObjectEx(mActiveClientEvent, INFINITE, FALSE)};
        if(res != WAIT_OBJECT_0)
        {
            ERR("WaitForSingleObjectEx error: {:#x}", res);
            return E_FAIL;
        }

        HRESULT hrActivateRes{E_FAIL};
        ComPtr<IUnknown> punkAudioIface;
        hr = asyncOp->GetActivateResult(&hrActivateRes, al::out_ptr(punkAudioIface));
        if(SUCCEEDED(hr)) hr = hrActivateRes;
        if(FAILED(hr)) return hr;

        return punkAudioIface->QueryInterface(iid, ppv);
    }

    HANDLE mActiveClientEvent{nullptr};
};

#else

struct DeviceHelper {
    DeviceHelper() = default;
    ~DeviceHelper() = default;

    [[nodiscard]]
    auto init() -> HRESULT
    {
        HRESULT hr{CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator), al::out_ptr(mEnumerator))};
        if(FAILED(hr))
            WARN("Failed to create IMMDeviceEnumerator instance: {:#x}", as_unsigned(hr));
        return hr;
    }

    [[nodiscard]]
    auto openDevice(const std::wstring &devid, EDataFlow flow, DeviceHandle &device) const
        -> HRESULT
    {
        HRESULT hr{E_FAIL};
        if(mEnumerator)
        {
            if(devid.empty())
                hr = mEnumerator->GetDefaultAudioEndpoint(flow, eMultimedia, al::out_ptr(device));
            else
                hr = mEnumerator->GetDevice(devid.c_str(), al::out_ptr(device));
        }
        return hr;
    }

    [[nodiscard]]
    static auto activateAudioClient(_In_ DeviceHandle &device, REFIID iid, void **ppv) -> HRESULT
    {
        if(iid == __uuidof(IAudioClient))
        {
            /* Always (try) to activate an IAudioClient3, even if giving back
             * an IAudioClient iface. This may(?) offer more features even if
             * not using its new methods.
             */
            auto ac3 = ComPtr<IAudioClient3>{};
            const auto hr = device->Activate(__uuidof(IAudioClient3), CLSCTX_INPROC_SERVER,
                nullptr, al::out_ptr(ac3));
            if(SUCCEEDED(hr))
                return ac3->QueryInterface(iid, ppv);
        }
        return device->Activate(iid, CLSCTX_INPROC_SERVER, nullptr, ppv);
    }

    ComPtr<IMMDeviceEnumerator> mEnumerator{nullptr};
};
#endif


bool MakeExtensible(WAVEFORMATEXTENSIBLE *out, const WAVEFORMATEX *in)
{
    *out = WAVEFORMATEXTENSIBLE{};
    if(in->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        *out = *CONTAINING_RECORD(in, const WAVEFORMATEXTENSIBLE, Format);
        out->Format.cbSize = sizeof(*out) - sizeof(out->Format);
    }
    else if(in->wFormatTag == WAVE_FORMAT_PCM)
    {
        out->Format = *in;
        out->Format.cbSize = 0;
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) */
        out->Samples.wValidBitsPerSample = out->Format.wBitsPerSample;
        if(out->Format.nChannels == 1)
            out->dwChannelMask = MONO;
        else if(out->Format.nChannels == 2)
            out->dwChannelMask = STEREO;
        else
            ERR("Unhandled PCM channel count: {}", out->Format.nChannels);
        out->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }
    else if(in->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        out->Format = *in;
        out->Format.cbSize = 0;
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) */
        out->Samples.wValidBitsPerSample = out->Format.wBitsPerSample;
        if(out->Format.nChannels == 1)
            out->dwChannelMask = MONO;
        else if(out->Format.nChannels == 2)
            out->dwChannelMask = STEREO;
        else
            ERR("Unhandled IEEE float channel count: {}", out->Format.nChannels);
        out->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    else
    {
        ERR("Unhandled format tag: {:#06x}", in->wFormatTag);
        return false;
    }
    return true;
}

void TraceFormat(const std::string_view msg, const WAVEFORMATEX *format)
{
    constexpr size_t fmtex_extra_size{sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX)};
    if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= fmtex_extra_size)
    {
        const auto *fmtex = CONTAINING_RECORD(format, const WAVEFORMATEXTENSIBLE, Format);
        /* NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) */
        TRACE("{}:\n"
            "    FormatTag      = {:#06x}\n"
            "    Channels       = {}\n"
            "    SamplesPerSec  = {}\n"
            "    AvgBytesPerSec = {}\n"
            "    BlockAlign     = {}\n"
            "    BitsPerSample  = {}\n"
            "    Size           = {}\n"
            "    Samples        = {}\n"
            "    ChannelMask    = {:#x}\n"
            "    SubFormat      = {}",
            msg, fmtex->Format.wFormatTag, fmtex->Format.nChannels, fmtex->Format.nSamplesPerSec,
            fmtex->Format.nAvgBytesPerSec, fmtex->Format.nBlockAlign, fmtex->Format.wBitsPerSample,
            fmtex->Format.cbSize, fmtex->Samples.wReserved, fmtex->dwChannelMask,
            GuidPrinter{fmtex->SubFormat}.str());
        /* NOLINTEND(cppcoreguidelines-pro-type-union-access) */
    }
    else
        TRACE("{}:\n"
            "    FormatTag      = {:#06x}\n"
            "    Channels       = {}\n"
            "    SamplesPerSec  = {}\n"
            "    AvgBytesPerSec = {}\n"
            "    BlockAlign     = {}\n"
            "    BitsPerSample  = {}\n"
            "    Size           = {}",
            msg, format->wFormatTag, format->nChannels, format->nSamplesPerSec,
            format->nAvgBytesPerSec, format->nBlockAlign, format->wBitsPerSample, format->cbSize);
}


/* Duplicates the first sample of each sample frame to the second sample, at
 * half volume. Essentially converting mono to stereo.
 */
template<typename T>
void DuplicateSamples(al::span<BYTE> insamples, size_t step)
{
    auto samples = al::span{reinterpret_cast<T*>(insamples.data()), insamples.size()/sizeof(T)};
    if constexpr(std::is_floating_point_v<T>)
    {
        for(size_t i{0};i < samples.size();i+=step)
        {
            const auto s = samples[i] * T{0.5};
            samples[i+1] = samples[i] = s;
        }
    }
    else if constexpr(std::is_signed_v<T>)
    {
        for(size_t i{0};i < samples.size();i+=step)
        {
            const auto s = samples[i] / 2;
            samples[i+1] = samples[i] = T(s);
        }
    }
    else
    {
        using ST = std::make_signed_t<T>;
        static constexpr auto SignBit = T{1u << (sizeof(T)*8 - 1)};
        for(size_t i{0};i < samples.size();i+=step)
        {
            const auto s = static_cast<ST>(samples[i]^SignBit) / 2;
            samples[i+1] = samples[i] = T(s)^SignBit;
        }
    }
}

void DuplicateSamples(al::span<BYTE> insamples, DevFmtType sampletype, size_t step)
{
    switch(sampletype)
    {
    case DevFmtByte: return DuplicateSamples<char>(insamples, step);
    case DevFmtUByte: return DuplicateSamples<unsigned char>(insamples, step);
    case DevFmtShort: return DuplicateSamples<short>(insamples, step);
    case DevFmtUShort: return DuplicateSamples<unsigned short>(insamples, step);
    case DevFmtInt: return DuplicateSamples<int>(insamples, step);
    case DevFmtUInt: return DuplicateSamples<unsigned int>(insamples, step);
    case DevFmtFloat: return DuplicateSamples<float>(insamples, step);
    }
}


struct WasapiPlayback final : public BackendBase {
    explicit WasapiPlayback(DeviceBase *device) noexcept : BackendBase{device} { }
    ~WasapiPlayback() override;

    struct PlainDevice {
        ComPtr<IAudioClient> mClient{nullptr};
        ComPtr<IAudioRenderClient> mRender{nullptr};
    };
    struct SpatialDevice {
        ComPtr<ISpatialAudioClient> mClient{nullptr};
        ComPtr<ISpatialAudioObjectRenderStream> mRender{nullptr};
        AudioObjectType mStaticMask{};
    };

    void mixerProc(PlainDevice &audio);
    void mixerProc(SpatialDevice &audio);

    auto openProxy(const std::string_view name, DeviceHelper &helper, DeviceHandle &mmdev)
        -> HRESULT;
    void finalizeFormat(WAVEFORMATEXTENSIBLE &OutputType);
    auto initSpatial(DeviceHelper &helper, DeviceHandle &mmdev, SpatialDevice &audio) -> bool;
    auto resetProxy(DeviceHelper &helper, DeviceHandle &mmdev,
        std::variant<PlainDevice,SpatialDevice> &audiodev) -> HRESULT;

    void proc_thread(std::string&& name);


    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    ClockLatency getClockLatency() override;


    std::thread mProcThread;
    std::mutex mProcMutex;
    std::condition_variable mProcCond;
    HRESULT mProcResult{E_FAIL};

    enum class ThreadState : uint8_t {
        Initializing,
        Waiting,
        Playing,
        Done
    };
    ThreadState mState{ThreadState::Initializing};

    enum class ThreadAction : uint8_t {
        Nothing,
        Configure,
        Play,
        Quit
    };
    ThreadAction mAction{ThreadAction::Nothing};


    static inline DWORD sAvIndex{};

    HANDLE mNotifyEvent{nullptr};

    UINT32 mOutBufferSize{}, mOutUpdateSize{};
    std::vector<char> mResampleBuffer;
    uint mBufferFilled{0};
    SampleConverterPtr mResampler;
    bool mMonoUpsample{false};
    bool mExclusiveMode{false};

    WAVEFORMATEXTENSIBLE mFormat{};
    std::atomic<UINT32> mPadding{0u};

    std::mutex mMutex;
    std::atomic<bool> mKillNow{true};
};

WasapiPlayback::~WasapiPlayback()
{
    if(mProcThread.joinable())
    {
        {
            auto plock = std::lock_guard{mProcMutex};
            mKillNow = true;
            mAction = ThreadAction::Quit;
        }
        mProcCond.notify_all();
        mProcThread.join();
    }

    if(mNotifyEvent != nullptr)
        CloseHandle(mNotifyEvent);
    mNotifyEvent = nullptr;
}


FORCE_ALIGN void WasapiPlayback::mixerProc(PlainDevice &audio)
{
    class PriorityControl {
        const int mOldPriority;

    public:
        PriorityControl() : mOldPriority{GetThreadPriority(GetCurrentThread())}
        {
            if(!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
                ERR("Failed to set priority level for thread");
        }
        ~PriorityControl()
        { SetThreadPriority(GetCurrentThread(), mOldPriority); }
    };
    auto prioctrl = PriorityControl{};

    const uint frame_size{mFormat.Format.nChannels * mFormat.Format.wBitsPerSample / 8u};
    const UINT32 buffer_len{mOutBufferSize};
    const void *resbufferptr{};

    assert(buffer_len > 0);

#ifdef AVRTAPI
    /* TODO: "Audio" or "Pro Audio"? The suggestion is to use "Pro Audio" for
     * device periods less than 10ms, and "Audio" for greater than or equal to
     * 10ms.
     */
    auto taskname = (mOutUpdateSize < mFormat.Format.nSamplesPerSec/100) ? L"Pro Audio" : L"Audio";
    auto avhandle = AvrtHandlePtr{AvSetMmThreadCharacteristicsW(taskname, &sAvIndex)};
#endif

    auto prefilling = true;
    mBufferFilled = 0;
    while(!mKillNow.load(std::memory_order_relaxed))
    {
        /* For exclusive mode, assume buffer_len sample frames are writable.
         * The first pass will be a prefill of the buffer, while subsequent
         * passes will only occur after notify events.
         * IAudioClient::GetCurrentPadding shouldn't be used with exclusive
         * streams that use event notifications, according to the docs, we
         * should just assume a buffer length is writable after notification.
         */
        auto written = UINT32{};
        if(!mExclusiveMode)
        {
            if(auto hr = audio.mClient->GetCurrentPadding(&written); FAILED(hr))
            {
                ERR("Failed to get padding: {:#x}", as_unsigned(hr));
                mDevice->handleDisconnect("Failed to retrieve buffer padding: {:#x}",
                    as_unsigned(hr));
                break;
            }
            mPadding.store(written, std::memory_order_relaxed);
        }

        if(const auto len = uint{buffer_len - written})
        {
            auto buffer = LPBYTE{};
            auto hr = audio.mRender->GetBuffer(len, &buffer);
            if(SUCCEEDED(hr))
            {
                if(mResampler)
                {
                    auto dlock = std::lock_guard{mMutex};
                    auto dst = al::span{buffer, size_t{len}*frame_size};
                    for(UINT32 done{0};done < len;)
                    {
                        if(mBufferFilled == 0)
                        {
                            mDevice->renderSamples(mResampleBuffer.data(), mDevice->mUpdateSize,
                                mFormat.Format.nChannels);
                            resbufferptr = mResampleBuffer.data();
                            mBufferFilled = mDevice->mUpdateSize;
                        }

                        const auto got = mResampler->convert(&resbufferptr, &mBufferFilled,
                            dst.data(), len-done);
                        dst = dst.subspan(size_t{got}*frame_size);
                        done += got;
                    }
                    mPadding.store(written + len, std::memory_order_relaxed);
                }
                else
                {
                    auto dlock = std::lock_guard{mMutex};
                    mDevice->renderSamples(buffer, len, mFormat.Format.nChannels);
                    mPadding.store(written + len, std::memory_order_relaxed);
                }

                if(mMonoUpsample)
                {
                    DuplicateSamples(al::span{buffer, size_t{len}*frame_size}, mDevice->FmtType,
                        mFormat.Format.nChannels);
                }

                hr = audio.mRender->ReleaseBuffer(len, 0);
            }
            if(FAILED(hr))
            {
                ERR("Failed to buffer data: {:#x}", as_unsigned(hr));
                mDevice->handleDisconnect("Failed to send playback samples: {:#x}",
                    as_unsigned(hr));
                break;
            }
        }

        if(prefilling)
        {
            prefilling = false;
            ResetEvent(mNotifyEvent);
            if(auto hr = audio.mClient->Start(); FAILED(hr))
            {
                ERR("Failed to start audio client: {:#x}", as_unsigned(hr));
                mDevice->handleDisconnect("Failed to start audio client: {:#x}",
                    as_unsigned(hr));
                break;
            }
        }

        if(DWORD res{WaitForSingleObjectEx(mNotifyEvent, 2000, FALSE)}; res != WAIT_OBJECT_0)
            ERR("WaitForSingleObjectEx error: {:#x}", res);
    }
    mPadding.store(0u, std::memory_order_release);
    audio.mClient->Stop();
}

FORCE_ALIGN void WasapiPlayback::mixerProc(SpatialDevice &audio)
{
    class PriorityControl {
        int mOldPriority;
    public:
        PriorityControl() : mOldPriority{GetThreadPriority(GetCurrentThread())}
        {
            if(!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
                ERR("Failed to set priority level for thread");
        }
        ~PriorityControl()
        { SetThreadPriority(GetCurrentThread(), mOldPriority); }
    };
    auto prioctrl = PriorityControl{};

#ifdef AVRTAPI
    auto taskname = (mOutUpdateSize < mFormat.Format.nSamplesPerSec/100) ? L"Pro Audio" : L"Audio";
    auto avhandle = AvrtHandlePtr{AvSetMmThreadCharacteristicsW(taskname, &sAvIndex)};
#endif

    std::vector<ComPtr<ISpatialAudioObject>> channels;
    std::vector<void*> buffers;
    std::vector<void*> resbuffers;
    std::vector<const void*> tmpbuffers;

    /* TODO: Set mPadding appropriately. There doesn't seem to be a way to
     * update it dynamically based on the stream, so a fixed size may be the
     * best we can do.
     */
    mPadding.store(mOutBufferSize-mOutUpdateSize, std::memory_order_release);

    mBufferFilled = 0;
    auto firstupdate = true;
    while(!mKillNow.load(std::memory_order_relaxed))
    {
        UINT32 dynamicCount{}, framesToDo{};
        HRESULT hr{audio.mRender->BeginUpdatingAudioObjects(&dynamicCount, &framesToDo)};
        if(SUCCEEDED(hr))
        {
            if(channels.empty()) UNLIKELY
            {
                auto flags = as_unsigned(al::to_underlying(audio.mStaticMask));
                channels.reserve(as_unsigned(al::popcount(flags)));
                while(flags)
                {
                    auto id = decltype(flags){1} << al::countr_zero(flags);
                    flags &= ~id;

                    audio.mRender->ActivateSpatialAudioObject(static_cast<AudioObjectType>(id),
                        al::out_ptr(channels.emplace_back()));
                }
                buffers.resize(channels.size());
                if(mResampler)
                {
                    tmpbuffers.resize(buffers.size());
                    resbuffers.resize(buffers.size());
                    auto bufptr = mResampleBuffer.begin();
                    for(size_t i{0};i < tmpbuffers.size();++i)
                    {
                        resbuffers[i] = al::to_address(bufptr);
                        bufptr += ptrdiff_t(mDevice->mUpdateSize*sizeof(float));
                    }
                }
            }

            /* We have to call to get each channel's buffer individually every
             * update, unfortunately.
             */
            std::transform(channels.cbegin(), channels.cend(), buffers.begin(),
                [](const ComPtr<ISpatialAudioObject> &obj) -> void*
                {
                    auto buffer = LPBYTE{};
                    auto size = UINT32{};
                    obj->GetBuffer(&buffer, &size);
                    return buffer;
                });

            if(!mResampler)
                mDevice->renderSamples(buffers, framesToDo);
            else
            {
                std::lock_guard<std::mutex> dlock{mMutex};
                for(UINT32 pos{0};pos < framesToDo;)
                {
                    if(mBufferFilled == 0)
                    {
                        mDevice->renderSamples(resbuffers, mDevice->mUpdateSize);
                        std::copy(resbuffers.cbegin(), resbuffers.cend(), tmpbuffers.begin());
                        mBufferFilled = mDevice->mUpdateSize;
                    }

                    const uint got{mResampler->convertPlanar(tmpbuffers.data(), &mBufferFilled,
                        buffers.data(), framesToDo-pos)};
                    for(auto &buf : buffers)
                        buf = static_cast<float*>(buf) + got; /* NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
                    pos += got;
                }
            }

            hr = audio.mRender->EndUpdatingAudioObjects();
        }

        if(firstupdate)
        {
            firstupdate = false;
            ResetEvent(mNotifyEvent);
            hr = audio.mRender->Start();
            if(FAILED(hr))
            {
                ERR("Failed to start spatial audio stream: {:#x}", as_unsigned(hr));
                mDevice->handleDisconnect("Failed to start spatial audio stream: {:#x}",
                    as_unsigned(hr));
                return;
            }
        }

        if(DWORD res{WaitForSingleObjectEx(mNotifyEvent, 1000, FALSE)}; res != WAIT_OBJECT_0)
        {
            ERR("WaitForSingleObjectEx error: {:#x}", res);

            hr = audio.mRender->Reset();
            if(FAILED(hr))
            {
                ERR("ISpatialAudioObjectRenderStream::Reset failed: {:#x}", as_unsigned(hr));
                mDevice->handleDisconnect("Device lost: {:#x}", as_unsigned(hr));
                break;
            }
            firstupdate = true;
        }

        if(FAILED(hr))
            ERR("Failed to update playback objects: {:#x}", as_unsigned(hr));
    }
    mPadding.store(0u, std::memory_order_release);
    audio.mRender->Stop();
    audio.mRender->Reset();
}


void WasapiPlayback::proc_thread(std::string&& name)
try {
    auto com = ComWrapper{COINIT_MULTITHREADED};
    if(!com)
    {
        const auto hr = as_unsigned(com.status());
        ERR("CoInitializeEx(nullptr, COINIT_MULTITHREADED) failed: {:#x}", hr);
        mDevice->handleDisconnect("COM init failed: {:#x}", hr);

        auto plock = std::lock_guard{mProcMutex};
        mProcResult = com.status();
        mState = ThreadState::Done;
        mProcCond.notify_all();
        return;
    }

    if(!gInitDone.load(std::memory_order_relaxed))
    {
        auto devlock = DeviceListLock{gDeviceList};
        gInitCV.wait(devlock, []() -> bool { return gInitDone; });
    }

    auto helper = DeviceHelper{};
    if(HRESULT hr{helper.init()}; FAILED(hr))
    {
        mDevice->handleDisconnect("Helper init failed: {:#x}", as_unsigned(hr));

        auto plock = std::lock_guard{mProcMutex};
        mProcResult = hr;
        mState = ThreadState::Done;
        mProcCond.notify_all();
        return;
    }

    althrd_setname(GetMixerThreadName());

    auto mmdev = DeviceHandle{nullptr};
    if(auto hr = openProxy(name, helper, mmdev); FAILED(hr))
    {
        auto plock = std::lock_guard{mProcMutex};
        mProcResult = hr;
        mState = ThreadState::Done;
        mProcCond.notify_all();
        return;
    }

    auto audiodev = std::variant<PlainDevice,SpatialDevice>{};

    auto plock = std::unique_lock{mProcMutex};
    mProcResult = S_OK;
    while(mState != ThreadState::Done)
    {
        mAction = ThreadAction::Nothing;
        mState = ThreadState::Waiting;
        mProcCond.notify_all();

        mProcCond.wait(plock, [this]() noexcept { return mAction != ThreadAction::Nothing; });
        switch(mAction)
        {
        case ThreadAction::Nothing:
            break;

        case ThreadAction::Configure:
            {
                plock.unlock();
                const auto hr = resetProxy(helper, mmdev, audiodev);
                plock.lock();
                mProcResult = hr;
            }
            break;

        case ThreadAction::Play:
            mKillNow.store(false, std::memory_order_release);

            mAction = ThreadAction::Nothing;
            mState = ThreadState::Playing;
            mProcResult = S_OK;
            plock.unlock();
            mProcCond.notify_all();

            std::visit([this](auto &audio) -> void { mixerProc(audio); }, audiodev);

            plock.lock();
            break;

        case ThreadAction::Quit:
            mAction = ThreadAction::Nothing;
            mState = ThreadState::Done;
            mProcCond.notify_all();
            break;
        }
    }
}
catch(...) {
    auto plock = std::lock_guard{mProcMutex};
    mProcResult = E_FAIL;
    mAction = ThreadAction::Nothing;
    mState = ThreadState::Done;
    mProcCond.notify_all();
}


void WasapiPlayback::open(std::string_view name)
{
    mNotifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if(mNotifyEvent == nullptr)
    {
        ERR("Failed to create notify events: {}", GetLastError());
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to create notify events"};
    }

    mProcThread = std::thread{&WasapiPlayback::proc_thread, this, std::string{name}};

    auto plock = std::unique_lock{mProcMutex};
    mProcCond.wait(plock, [this]() noexcept { return mState != ThreadState::Initializing; });

    if(mProcResult == E_NOTFOUND)
        throw al::backend_exception{al::backend_error::NoDevice, "Device \"{}\" not found", name};
    if(FAILED(mProcResult) || mState == ThreadState::Done)
        throw al::backend_exception{al::backend_error::DeviceError, "Device init failed: {:#x}",
            as_unsigned(mProcResult)};
}

auto WasapiPlayback::openProxy(const std::string_view name, DeviceHelper &helper,
    DeviceHandle &mmdev) -> HRESULT
{
    auto devname = std::string{};
    auto devid = std::wstring{};
    if(!name.empty())
    {
        auto devlock = DeviceListLock{gDeviceList};
        auto list = al::span{devlock.getPlaybackList()};
        auto iter = std::find_if(list.cbegin(), list.cend(),
            [name](const DevMap &entry) -> bool
            { return entry.name == name || al::case_compare(entry.endpoint_guid, name) == 0; });
        if(iter == list.cend())
        {
            const std::wstring wname{utf8_to_wstr(name)};
            iter = std::find_if(list.cbegin(), list.cend(),
                [&wname](const DevMap &entry) -> bool
                { return al::case_compare(entry.devid, wname) == 0; });
        }
        if(iter == list.cend())
        {
            WARN("Failed to find device name matching \"{}\"", name);
            return E_NOTFOUND;
        }
        devname = iter->name;
        devid = iter->devid;
    }

    if(HRESULT hr{helper.openDevice(devid, eRender, mmdev)}; FAILED(hr))
    {
        WARN("Failed to open device \"{}\": {:#x}", devname.empty()
            ? "(default)"sv : std::string_view{devname}, as_unsigned(hr));
        return hr;
    }
    if(!devname.empty())
        mDeviceName = std::move(devname);
    else
        mDeviceName = GetDeviceNameAndGuid(mmdev).mName;

    return S_OK;
}


void WasapiPlayback::finalizeFormat(WAVEFORMATEXTENSIBLE &OutputType)
{
    if(!GetConfigValueBool(mDevice->mDeviceName, "wasapi", "allow-resampler", true))
        mDevice->mSampleRate = uint(OutputType.Format.nSamplesPerSec);
    else
        mDevice->mSampleRate = std::min(mDevice->mSampleRate,
            uint(OutputType.Format.nSamplesPerSec));

    const uint32_t chancount{OutputType.Format.nChannels};
    const DWORD chanmask{OutputType.dwChannelMask};
    /* Don't update the channel format if the requested format fits what's
     * supported.
     */
    bool chansok{false};
    if(mDevice->Flags.test(ChannelsRequest))
    {
        /* When requesting a channel configuration, make sure it fits the
         * mask's lsb (to ensure no gaps in the output channels). If there's no
         * mask, assume the request fits with enough channels.
         */
        switch(mDevice->FmtChans)
        {
        case DevFmtMono:
            chansok = (chancount >= 1 && ((chanmask&MonoMask) == MONO || !chanmask));
            if(!chansok && chancount >= 2 && (chanmask&StereoMask) == STEREO)
            {
                /* Mono rendering with stereo+ output is handled specially. */
                chansok = true;
                mMonoUpsample = true;
            }
            break;
        case DevFmtStereo:
            chansok = (chancount >= 2 && ((chanmask&StereoMask) == STEREO || !chanmask));
            break;
        case DevFmtQuad:
            chansok = (chancount >= 4 && ((chanmask&QuadMask) == QUAD || !chanmask));
            break;
        case DevFmtX51:
            chansok = (chancount >= 6 && ((chanmask&X51Mask) == X5DOT1
                    || (chanmask&X51RearMask) == X5DOT1REAR || !chanmask));
            break;
        case DevFmtX61:
            chansok = (chancount >= 7 && ((chanmask&X61Mask) == X6DOT1 || !chanmask));
            break;
        case DevFmtX71:
        case DevFmtX3D71:
            chansok = (chancount >= 8 && ((chanmask&X71Mask) == X7DOT1 || !chanmask));
            break;
        case DevFmtX714:
            chansok = (chancount >= 12 && ((chanmask&X714Mask) == X7DOT1DOT4 || !chanmask));
        case DevFmtX7144:
        case DevFmtAmbi3D:
            break;
        }
    }
    if(!chansok)
    {
        if(chancount >= 12 && (chanmask&X714Mask) == X7DOT1DOT4)
            mDevice->FmtChans = DevFmtX714;
        else if(chancount >= 8 && (chanmask&X71Mask) == X7DOT1)
            mDevice->FmtChans = DevFmtX71;
        else if(chancount >= 7 && (chanmask&X61Mask) == X6DOT1)
            mDevice->FmtChans = DevFmtX61;
        else if(chancount >= 6 && ((chanmask&X51Mask) == X5DOT1
            || (chanmask&X51RearMask) == X5DOT1REAR))
            mDevice->FmtChans = DevFmtX51;
        else if(chancount >= 4 && (chanmask&QuadMask) == QUAD)
            mDevice->FmtChans = DevFmtQuad;
        else if(chancount >= 2 && ((chanmask&StereoMask) == STEREO || !chanmask))
            mDevice->FmtChans = DevFmtStereo;
        else if(chancount >= 1 && ((chanmask&MonoMask) == MONO || !chanmask))
            mDevice->FmtChans = DevFmtMono;
        else
        {
            ERR("Unhandled extensible channels: {} -- {:#08x}", OutputType.Format.nChannels,
                OutputType.dwChannelMask);
            mDevice->FmtChans = DevFmtStereo;
            OutputType.Format.nChannels = 2;
            OutputType.dwChannelMask = STEREO;
        }
    }

    if(IsEqualGUID(OutputType.SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
    {
        if(OutputType.Format.wBitsPerSample == 8)
            mDevice->FmtType = DevFmtUByte;
        else if(OutputType.Format.wBitsPerSample == 16)
            mDevice->FmtType = DevFmtShort;
        else if(OutputType.Format.wBitsPerSample == 32)
            mDevice->FmtType = DevFmtInt;
        else
        {
            mDevice->FmtType = DevFmtShort;
            OutputType.Format.wBitsPerSample = 16;
        }
    }
    else if(IsEqualGUID(OutputType.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
        mDevice->FmtType = DevFmtFloat;
        OutputType.Format.wBitsPerSample = 32;
    }
    else
    {
        ERR("Unhandled format sub-type: {}", GuidPrinter{OutputType.SubFormat}.str());
        mDevice->FmtType = DevFmtShort;
        if(OutputType.Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
            OutputType.Format.wFormatTag = WAVE_FORMAT_PCM;
        OutputType.Format.wBitsPerSample = 16;
        OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) */
    OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
}


auto WasapiPlayback::initSpatial(DeviceHelper &helper, DeviceHandle &mmdev, SpatialDevice &audio)
    -> bool
{
    HRESULT hr{helper.activateAudioClient(mmdev, __uuidof(ISpatialAudioClient),
        al::out_ptr(audio.mClient))};
    if(FAILED(hr))
    {
        ERR("Failed to activate spatial audio client: {:#x}", as_unsigned(hr));
        return false;
    }

    ComPtr<IAudioFormatEnumerator> fmtenum;
    hr = audio.mClient->GetSupportedAudioObjectFormatEnumerator(al::out_ptr(fmtenum));
    if(FAILED(hr))
    {
        ERR("Failed to get format enumerator: {:#x}", as_unsigned(hr));
        return false;
    }

    UINT32 fmtcount{};
    hr = fmtenum->GetCount(&fmtcount);
    if(FAILED(hr) || fmtcount == 0)
    {
        ERR("Failed to get format count: {:#08x}", as_unsigned(hr));
        return false;
    }

    WAVEFORMATEX *preferredFormat{};
    hr = fmtenum->GetFormat(0, &preferredFormat);
    if(FAILED(hr))
    {
        ERR("Failed to get preferred format: {:#x}", as_unsigned(hr));
        return false;
    }
    TraceFormat("Preferred mix format", preferredFormat);

    UINT32 maxFrames{};
    hr = audio.mClient->GetMaxFrameCount(preferredFormat, &maxFrames);
    if(FAILED(hr))
        ERR("Failed to get max frames: {:#x}", as_unsigned(hr));
    else
        TRACE("Max sample frames: {}", maxFrames);
    for(UINT32 i{1};i < fmtcount;++i)
    {
        WAVEFORMATEX *otherFormat{};
        hr = fmtenum->GetFormat(i, &otherFormat);
        if(FAILED(hr))
            ERR("Failed to get format {}: {:#x}", i+1, as_unsigned(hr));
        else
        {
            TraceFormat("Other mix format", otherFormat);
            UINT32 otherMaxFrames{};
            hr = audio.mClient->GetMaxFrameCount(otherFormat, &otherMaxFrames);
            if(FAILED(hr))
                ERR("Failed to get max frames: {:#x}", as_unsigned(hr));
            else
                TRACE("Max sample frames: {}", otherMaxFrames);
        }
    }

    WAVEFORMATEXTENSIBLE OutputType;
    if(!MakeExtensible(&OutputType, preferredFormat))
        return false;

    /* This seems to be the format of each "object", which should be mono. */
    if(!(OutputType.Format.nChannels == 1
        && (OutputType.dwChannelMask == MONO || !OutputType.dwChannelMask)))
        ERR("Unhandled channel config: {} -- {:#08x}", OutputType.Format.nChannels,
            OutputType.dwChannelMask);

    /* Force 32-bit float. This is currently required for planar output. */
    if(OutputType.Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE
        && OutputType.Format.wFormatTag != WAVE_FORMAT_IEEE_FLOAT)
    {
        OutputType.Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        OutputType.Format.cbSize = 0;
    }
    if(OutputType.Format.wBitsPerSample != 32)
    {
        OutputType.Format.nAvgBytesPerSec = OutputType.Format.nAvgBytesPerSec * 32u
            / OutputType.Format.wBitsPerSample;
        OutputType.Format.nBlockAlign = static_cast<WORD>(OutputType.Format.nBlockAlign * 32
            / OutputType.Format.wBitsPerSample);
        OutputType.Format.wBitsPerSample = 32;
    }
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) */
    OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
    OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    /* Match the output rate if not requesting anything specific. */
    if(!mDevice->Flags.test(FrequencyRequest))
        mDevice->mSampleRate = OutputType.Format.nSamplesPerSec;

    auto getTypeMask = [](DevFmtChannels chans) noexcept
    {
        switch(chans)
        {
        case DevFmtMono: return ChannelMask_Mono;
        case DevFmtStereo: return ChannelMask_Stereo;
        case DevFmtQuad: return ChannelMask_Quad;
        case DevFmtX51: return ChannelMask_X51;
        case DevFmtX61: return ChannelMask_X61;
        case DevFmtX3D71: [[fallthrough]];
        case DevFmtX71: return ChannelMask_X71;
        case DevFmtX714: return ChannelMask_X714;
        case DevFmtX7144: return ChannelMask_X7144;
        case DevFmtAmbi3D:
            break;
        }
        return ChannelMask_Stereo;
    };

    SpatialAudioObjectRenderStreamActivationParams streamParams{};
    streamParams.ObjectFormat = &OutputType.Format;
    streamParams.StaticObjectTypeMask = getTypeMask(mDevice->FmtChans);
    streamParams.Category = AudioCategory_Media;
    streamParams.EventHandle = mNotifyEvent;

    PropVariant paramProp{};
    paramProp.setBlob({reinterpret_cast<BYTE*>(&streamParams), sizeof(streamParams)});

    hr = audio.mClient->ActivateSpatialAudioStream(paramProp.get(),
        __uuidof(ISpatialAudioObjectRenderStream), al::out_ptr(audio.mRender));
    if(FAILED(hr))
    {
        ERR("Failed to activate spatial audio stream: {:#x}", as_unsigned(hr));
        return false;
    }

    audio.mStaticMask = streamParams.StaticObjectTypeMask;
    mFormat = OutputType;

    mDevice->FmtType = DevFmtFloat;
    mDevice->Flags.reset(DirectEar).set(Virtualization);
    if(streamParams.StaticObjectTypeMask == ChannelMask_Stereo)
        mDevice->FmtChans = DevFmtStereo;
    if(!GetConfigValueBool(mDevice->mDeviceName, "wasapi", "allow-resampler", true))
        mDevice->mSampleRate = uint(OutputType.Format.nSamplesPerSec);
    else
        mDevice->mSampleRate = std::min(mDevice->mSampleRate,
            uint(OutputType.Format.nSamplesPerSec));

    setDefaultWFXChannelOrder();

    /* TODO: ISpatialAudioClient::GetMaxFrameCount returns the maximum number
     * of frames per processing pass, which is ostensibly the period size. This
     * should be checked on a real Windows system.
     *
     * In either case, this won't get the buffer size of the
     * ISpatialAudioObjectRenderStream, so we only assume there's two periods.
     */
    mOutUpdateSize = maxFrames;
    mOutBufferSize = mOutUpdateSize*2;

    mDevice->mUpdateSize = static_cast<uint>((uint64_t{mOutUpdateSize}*mDevice->mSampleRate
        + (mFormat.Format.nSamplesPerSec-1)) / mFormat.Format.nSamplesPerSec);
    mDevice->mBufferSize = mDevice->mUpdateSize*2;

    mResampler = nullptr;
    mResampleBuffer.clear();
    mResampleBuffer.shrink_to_fit();
    mBufferFilled = 0;
    if(mDevice->mSampleRate != mFormat.Format.nSamplesPerSec)
    {
        const auto flags = as_unsigned(al::to_underlying(streamParams.StaticObjectTypeMask));
        const auto channelCount = as_unsigned(al::popcount(flags));
        mResampler = SampleConverter::Create(mDevice->FmtType, mDevice->FmtType, channelCount,
            mDevice->mSampleRate, mFormat.Format.nSamplesPerSec, Resampler::FastBSinc24);
        mResampleBuffer.resize(size_t{mDevice->mUpdateSize} * channelCount *
            mFormat.Format.wBitsPerSample / 8);

        TRACE("Created converter for {}/{} format, dst: {}hz ({}), src: {}hz ({})",
            DevFmtChannelsString(mDevice->FmtChans), DevFmtTypeString(mDevice->FmtType),
            mFormat.Format.nSamplesPerSec, mOutUpdateSize, mDevice->mSampleRate,
            mDevice->mUpdateSize);
    }

    return true;
}

bool WasapiPlayback::reset()
{
    auto plock = std::unique_lock{mProcMutex};
    if(mState != ThreadState::Waiting)
        throw al::backend_exception{al::backend_error::DeviceError, "Invalid state: {}",
            unsigned{al::to_underlying(mState)}};

    mAction = ThreadAction::Configure;
    mProcCond.notify_all();
    mProcCond.wait(plock, [this]() noexcept { return mAction != ThreadAction::Configure; });

    if(FAILED(mProcResult) || mState != ThreadState::Waiting)
        throw al::backend_exception{al::backend_error::DeviceError, "Device init failed: {:#x}",
            as_unsigned(mProcResult)};
    return true;
}

auto WasapiPlayback::resetProxy(DeviceHelper &helper, DeviceHandle &mmdev,
    std::variant<PlainDevice,SpatialDevice> &audiodev) -> HRESULT
{
    if(GetConfigValueBool(mDevice->mDeviceName, "wasapi", "spatial-api", false))
    {
        if(initSpatial(helper, mmdev, audiodev.emplace<SpatialDevice>()))
            return S_OK;
    }

    mDevice->Flags.reset(Virtualization);
    mMonoUpsample = false;
    mExclusiveMode = false;

    auto &audio = audiodev.emplace<PlainDevice>();
    auto hr = helper.activateAudioClient(mmdev, __uuidof(IAudioClient),
        al::out_ptr(audio.mClient));
    if(FAILED(hr))
    {
        ERR("Failed to reactivate audio client: {:#x}", as_unsigned(hr));
        return hr;
    }

    auto wfx = unique_coptr<WAVEFORMATEX>{};
    hr = audio.mClient->GetMixFormat(al::out_ptr(wfx));
    if(FAILED(hr))
    {
        ERR("Failed to get mix format: {:#x}", as_unsigned(hr));
        return hr;
    }
    TraceFormat("Device mix format", wfx.get());

    auto OutputType = WAVEFORMATEXTENSIBLE{};
    if(!MakeExtensible(&OutputType, wfx.get()))
        return E_FAIL;
    wfx = nullptr;

    /* Get the buffer and update sizes as a ReferenceTime before potentially
     * altering the sample rate.
     */
    const auto buf_time = ReferenceTime{seconds{mDevice->mBufferSize}} / mDevice->mSampleRate;
    const auto per_time = ReferenceTime{seconds{mDevice->mUpdateSize}} / mDevice->mSampleRate;

    /* Update the mDevice format for non-requested properties. */
    bool isRear51{false};
    if(!mDevice->Flags.test(FrequencyRequest))
        mDevice->mSampleRate = OutputType.Format.nSamplesPerSec;
    if(!mDevice->Flags.test(ChannelsRequest))
    {
        /* If not requesting a channel configuration, auto-select given what
         * fits the mask's lsb (to ensure no gaps in the output channels). If
         * there's no mask, we can only assume mono or stereo.
         */
        const uint32_t chancount{OutputType.Format.nChannels};
        const DWORD chanmask{OutputType.dwChannelMask};
        if(chancount >= 12 && (chanmask&X714Mask) == X7DOT1DOT4)
            mDevice->FmtChans = DevFmtX714;
        else if(chancount >= 8 && (chanmask&X71Mask) == X7DOT1)
            mDevice->FmtChans = DevFmtX71;
        else if(chancount >= 7 && (chanmask&X61Mask) == X6DOT1)
            mDevice->FmtChans = DevFmtX61;
        else if(chancount >= 6 && (chanmask&X51Mask) == X5DOT1)
            mDevice->FmtChans = DevFmtX51;
        else if(chancount >= 6 && (chanmask&X51RearMask) == X5DOT1REAR)
        {
            mDevice->FmtChans = DevFmtX51;
            isRear51 = true;
        }
        else if(chancount >= 4 && (chanmask&QuadMask) == QUAD)
            mDevice->FmtChans = DevFmtQuad;
        else if(chancount >= 2 && ((chanmask&StereoMask) == STEREO || !chanmask))
            mDevice->FmtChans = DevFmtStereo;
        else if(chancount >= 1 && ((chanmask&MonoMask) == MONO || !chanmask))
            mDevice->FmtChans = DevFmtMono;
        else
            ERR("Unhandled channel config: {} -- {:#08x}", chancount, chanmask);
    }
    else
    {
        const uint32_t chancount{OutputType.Format.nChannels};
        const DWORD chanmask{OutputType.dwChannelMask};
        isRear51 = (chancount == 6 && (chanmask&X51RearMask) == X5DOT1REAR);
    }

    /* Request a format matching the mDevice. */
    OutputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    switch(mDevice->FmtChans)
    {
    case DevFmtMono:
        OutputType.Format.nChannels = 1;
        OutputType.dwChannelMask = MONO;
        break;
    case DevFmtAmbi3D:
        mDevice->FmtChans = DevFmtStereo;
        [[fallthrough]];
    case DevFmtStereo:
        OutputType.Format.nChannels = 2;
        OutputType.dwChannelMask = STEREO;
        break;
    case DevFmtQuad:
        OutputType.Format.nChannels = 4;
        OutputType.dwChannelMask = QUAD;
        break;
    case DevFmtX51:
        OutputType.Format.nChannels = 6;
        OutputType.dwChannelMask = isRear51 ? X5DOT1REAR : X5DOT1;
        break;
    case DevFmtX61:
        OutputType.Format.nChannels = 7;
        OutputType.dwChannelMask = X6DOT1;
        break;
    case DevFmtX71:
    case DevFmtX3D71:
        OutputType.Format.nChannels = 8;
        OutputType.dwChannelMask = X7DOT1;
        break;
    case DevFmtX7144:
        mDevice->FmtChans = DevFmtX714;
        [[fallthrough]];
    case DevFmtX714:
        OutputType.Format.nChannels = 12;
        OutputType.dwChannelMask = X7DOT1DOT4;
        break;
    }
    switch(mDevice->FmtType)
    {
    case DevFmtByte:
        mDevice->FmtType = DevFmtUByte;
        [[fallthrough]];
    case DevFmtUByte:
        OutputType.Format.wBitsPerSample = 8;
        OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        break;
    case DevFmtUShort:
        mDevice->FmtType = DevFmtShort;
        [[fallthrough]];
    case DevFmtShort:
        OutputType.Format.wBitsPerSample = 16;
        OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        break;
    case DevFmtUInt:
        mDevice->FmtType = DevFmtInt;
        [[fallthrough]];
    case DevFmtInt:
        OutputType.Format.wBitsPerSample = 32;
        OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        break;
    case DevFmtFloat:
        OutputType.Format.wBitsPerSample = 32;
        OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        break;
    }
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) */
    OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
    OutputType.Format.nSamplesPerSec = mDevice->mSampleRate;
    OutputType.Format.nBlockAlign = static_cast<WORD>(OutputType.Format.nChannels
        * OutputType.Format.wBitsPerSample / 8);
    OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec
        * OutputType.Format.nBlockAlign;

    const auto sharemode =
        GetConfigValueBool(mDevice->mDeviceName, "wasapi", "exclusive-mode", false)
        ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
    mExclusiveMode = (sharemode == AUDCLNT_SHAREMODE_EXCLUSIVE);

    TraceFormat("Requesting playback format", &OutputType.Format);
    hr = audio.mClient->IsFormatSupported(sharemode, &OutputType.Format, al::out_ptr(wfx));
    if(FAILED(hr))
    {
        if(sharemode == AUDCLNT_SHAREMODE_EXCLUSIVE)
        {
            /* For exclusive mode, IAudioClient::IsFormatSupported won't give
             * back a supported format. However, a common failure is an
             * unsupported sample type, so try a fallback to 16-bit int.
             */
            if(hr == AUDCLNT_E_UNSUPPORTED_FORMAT && mDevice->FmtType != DevFmtShort)
            {
                mDevice->FmtType = DevFmtShort;

                OutputType.Format.wBitsPerSample = 16;
                OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
                /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) */
                OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
                OutputType.Format.nBlockAlign = static_cast<WORD>(OutputType.Format.nChannels
                    * OutputType.Format.wBitsPerSample / 8);
                OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec
                    * OutputType.Format.nBlockAlign;

                hr = audio.mClient->IsFormatSupported(sharemode, &OutputType.Format,
                    al::out_ptr(wfx));
            }
        }
        else
        {
            WARN("Failed to check format support: {:#x}", as_unsigned(hr));
            hr = audio.mClient->GetMixFormat(al::out_ptr(wfx));
        }
    }
    if(FAILED(hr))
    {
        ERR("Failed to find a supported format: {:#x}", as_unsigned(hr));
        return hr;
    }

    if(wfx)
    {
        TraceFormat("Got playback format", wfx.get());
        if(!MakeExtensible(&OutputType, wfx.get()))
            return E_FAIL;
        wfx = nullptr;

        finalizeFormat(OutputType);
    }
    mFormat = OutputType;

#if !ALSOFT_UWP
    const EndpointFormFactor formfactor{GetDeviceFormfactor(mmdev.get())};
    mDevice->Flags.set(DirectEar, (formfactor == Headphones || formfactor == Headset));
#else
    mDevice->Flags.set(DirectEar, false);
#endif
    setDefaultWFXChannelOrder();

    if(sharemode == AUDCLNT_SHAREMODE_EXCLUSIVE)
    {
        auto period_time = per_time;
        auto min_period = ReferenceTime{};
        hr = audio.mClient->GetDevicePeriod(nullptr,
            &reinterpret_cast<REFERENCE_TIME&>(min_period));
        if(FAILED(hr))
            ERR("Failed to get minimum period time: {:#x}", as_unsigned(hr));
        else if(min_period > period_time)
        {
            period_time = min_period;
            WARN("Clamping to minimum period time, {}", nanoseconds{min_period});
        }

        hr = audio.mClient->Initialize(sharemode, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            period_time.count(), period_time.count(), &OutputType.Format, nullptr);
        if(hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
        {
            auto newsize = UINT32{};
            hr = audio.mClient->GetBufferSize(&newsize);
            if(SUCCEEDED(hr))
            {
                period_time = ReferenceTime{seconds{newsize}}
                    / OutputType.Format.nSamplesPerSec;
                WARN("Adjusting to supported period time, {}", nanoseconds{period_time});

                audio.mClient = nullptr;
                hr = helper.activateAudioClient(mmdev, __uuidof(IAudioClient),
                    al::out_ptr(audio.mClient));
                if(FAILED(hr))
                {
                    ERR("Failed to reactivate audio client: {:#x}", as_unsigned(hr));
                    return hr;
                }
                hr = audio.mClient->Initialize(sharemode, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    period_time.count(), period_time.count(), &OutputType.Format, nullptr);
            }
        }
    }
    else
        hr = audio.mClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            buf_time.count(), 0, &OutputType.Format, nullptr);
    if(FAILED(hr))
    {
        ERR("Failed to initialize audio client: {:#x}", as_unsigned(hr));
        return hr;
    }

    auto buffer_len = UINT32{};
    auto period_time = ReferenceTime{};
    hr = audio.mClient->GetDevicePeriod(&reinterpret_cast<REFERENCE_TIME&>(period_time), nullptr);
    if(SUCCEEDED(hr))
        hr = audio.mClient->GetBufferSize(&buffer_len);
    if(FAILED(hr))
    {
        ERR("Failed to get audio buffer info: {:#x}", as_unsigned(hr));
        return hr;
    }

    hr = audio.mClient->SetEventHandle(mNotifyEvent);
    if(FAILED(hr))
    {
        ERR("Failed to set event handle: {:#x}", as_unsigned(hr));
        return hr;
    }

    hr = audio.mClient->GetService(__uuidof(IAudioRenderClient), al::out_ptr(audio.mRender));
    if(FAILED(hr))
    {
        ERR("Failed to get IAudioRenderClient: {:#x}", as_unsigned(hr));
        return hr;
    }

    mOutBufferSize = buffer_len;
    if(sharemode == AUDCLNT_SHAREMODE_EXCLUSIVE)
    {
        /* For exclusive mode, the buffer size is the update size, and there's
         * implicitly two update periods on the device.
         */
        mOutUpdateSize = buffer_len;
        mDevice->mUpdateSize = static_cast<uint>(uint64_t{buffer_len} * mDevice->mSampleRate /
            mFormat.Format.nSamplesPerSec);
        mDevice->mBufferSize = mDevice->mUpdateSize * 2;
    }
    else
    {
        mOutUpdateSize = RefTime2Samples(period_time, mFormat.Format.nSamplesPerSec);

        mDevice->mBufferSize = static_cast<uint>(uint64_t{buffer_len} * mDevice->mSampleRate /
            mFormat.Format.nSamplesPerSec);
        mDevice->mUpdateSize = std::min(RefTime2Samples(period_time, mDevice->mSampleRate),
            mDevice->mBufferSize/2u);
    }

    mResampler = nullptr;
    mResampleBuffer.clear();
    mResampleBuffer.shrink_to_fit();
    mBufferFilled = 0;
    if(mDevice->mSampleRate != mFormat.Format.nSamplesPerSec)
    {
        mResampler = SampleConverter::Create(mDevice->FmtType, mDevice->FmtType,
            mFormat.Format.nChannels, mDevice->mSampleRate, mFormat.Format.nSamplesPerSec,
            Resampler::FastBSinc24);
        mResampleBuffer.resize(size_t{mDevice->mUpdateSize} * mFormat.Format.nChannels *
            mFormat.Format.wBitsPerSample / 8);

        TRACE("Created converter for {}/{} format, dst: {}hz ({}), src: {}hz ({})",
            DevFmtChannelsString(mDevice->FmtChans), DevFmtTypeString(mDevice->FmtType),
            mFormat.Format.nSamplesPerSec, mOutUpdateSize, mDevice->mSampleRate,
            mDevice->mUpdateSize);
    }

    return hr;
}


void WasapiPlayback::start()
{
    auto plock = std::unique_lock{mProcMutex};
    if(mState != ThreadState::Waiting)
        throw al::backend_exception{al::backend_error::DeviceError, "Invalid state: {}",
            unsigned{al::to_underlying(mState)}};

    mAction = ThreadAction::Play;
    mProcCond.notify_all();
    mProcCond.wait(plock, [this]() noexcept { return mAction != ThreadAction::Play; });

    if(FAILED(mProcResult) || mState != ThreadState::Playing)
        throw al::backend_exception{al::backend_error::DeviceError, "Device playback failed: {:#x}",
            as_unsigned(mProcResult)};
}

void WasapiPlayback::stop()
{
    auto plock = std::unique_lock{mProcMutex};
    if(mState == ThreadState::Playing)
    {
        mKillNow = true;
        mProcCond.wait(plock, [this]() noexcept { return mState != ThreadState::Playing; });
    }
}


ClockLatency WasapiPlayback::getClockLatency()
{
    std::lock_guard<std::mutex> dlock{mMutex};
    ClockLatency ret{};
    ret.ClockTime = mDevice->getClockTime();
    ret.Latency  = seconds{mPadding.load(std::memory_order_relaxed)};
    ret.Latency /= mFormat.Format.nSamplesPerSec;
    if(mResampler)
    {
        auto extra = mResampler->currentInputDelay();
        ret.Latency += std::chrono::duration_cast<nanoseconds>(extra) / mDevice->mSampleRate;
        ret.Latency += nanoseconds{seconds{mBufferFilled}} / mDevice->mSampleRate;
    }

    return ret;
}


struct WasapiCapture final : public BackendBase {
    explicit WasapiCapture(DeviceBase *device) noexcept : BackendBase{device} { }
    ~WasapiCapture() override;

    void recordProc(IAudioClient *client, IAudioCaptureClient *capture);

    void proc_thread(std::string&& name);

    auto openProxy(const std::string_view name, DeviceHelper &helper, DeviceHandle &mmdev)
        -> HRESULT;
    auto resetProxy(DeviceHelper &helper, DeviceHandle &mmdev, ComPtr<IAudioClient> &client,
        ComPtr<IAudioCaptureClient> &capture) -> HRESULT;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::byte *buffer, uint samples) override;
    uint availableSamples() override;


    std::thread mProcThread;
    std::mutex mProcMutex;
    std::condition_variable mProcCond;
    HRESULT mProcResult{E_FAIL};

    enum class ThreadState : uint8_t {
        Initializing,
        Waiting,
        Recording,
        Done
    };
    ThreadState mState{ThreadState::Initializing};

    enum class ThreadAction : uint8_t {
        Nothing,
        Record,
        Quit
    };
    ThreadAction mAction{ThreadAction::Nothing};


    HANDLE mNotifyEvent{nullptr};

    ChannelConverter mChannelConv{};
    SampleConverterPtr mSampleConv;
    RingBufferPtr mRing;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

WasapiCapture::~WasapiCapture()
{
    if(mProcThread.joinable())
    {
        {
            auto plock = std::lock_guard{mProcMutex};
            mKillNow = true;
            mAction = ThreadAction::Quit;
        }
        mProcCond.notify_all();
        mProcThread.join();
    }

    if(mNotifyEvent != nullptr)
        CloseHandle(mNotifyEvent);
    mNotifyEvent = nullptr;
}


FORCE_ALIGN void WasapiCapture::recordProc(IAudioClient *client, IAudioCaptureClient *capture)
{
    ResetEvent(mNotifyEvent);
    if(HRESULT hr{client->Start()}; FAILED(hr))
    {
        ERR("Failed to start audio client: {:#x}", as_unsigned(hr));
        mDevice->handleDisconnect("Failed to start audio client: {:#x}", as_unsigned(hr));
        return;
    }

    std::vector<float> samples;
    while(!mKillNow.load(std::memory_order_relaxed))
    {
        auto avail = UINT32{};
        auto hr = capture->GetNextPacketSize(&avail);
        if(FAILED(hr))
            ERR("Failed to get next packet size: {:#x}", as_unsigned(hr));
        else if(avail > 0)
        {
            auto numsamples = UINT32{};
            auto flags = DWORD{};
            BYTE *rdata{};

            hr = capture->GetBuffer(&rdata, &numsamples, &flags, nullptr, nullptr);
            if(FAILED(hr))
                ERR("Failed to get capture buffer: {:#x}", as_unsigned(hr));
            else
            {
                if(mChannelConv.is_active())
                {
                    samples.resize(numsamples*2_uz);
                    mChannelConv.convert(rdata, samples.data(), numsamples);
                    rdata = reinterpret_cast<BYTE*>(samples.data());
                }

                auto data = mRing->getWriteVector();

                size_t dstframes;
                if(mSampleConv)
                {
                    static constexpr auto lenlimit = size_t{std::numeric_limits<int>::max()};
                    const void *srcdata{rdata};
                    uint srcframes{numsamples};

                    dstframes = mSampleConv->convert(&srcdata, &srcframes, data[0].buf,
                        static_cast<uint>(std::min(data[0].len, lenlimit)));
                    if(srcframes > 0 && dstframes == data[0].len && data[1].len > 0)
                    {
                        /* If some source samples remain, all of the first dest
                         * block was filled, and there's space in the second
                         * dest block, do another run for the second block.
                         */
                        dstframes += mSampleConv->convert(&srcdata, &srcframes, data[1].buf,
                            static_cast<uint>(std::min(data[1].len, lenlimit)));
                    }
                }
                else
                {
                    const uint framesize{mDevice->frameSizeFromFmt()};
                    auto dst = al::span{rdata, size_t{numsamples}*framesize};
                    size_t len1{std::min(data[0].len, size_t{numsamples})};
                    size_t len2{std::min(data[1].len, numsamples-len1)};

                    memcpy(data[0].buf, dst.data(), len1*framesize);
                    if(len2 > 0)
                    {
                        dst = dst.subspan(len1*framesize);
                        memcpy(data[1].buf, dst.data(), len2*framesize);
                    }
                    dstframes = len1 + len2;
                }

                mRing->writeAdvance(dstframes);

                hr = capture->ReleaseBuffer(numsamples);
                if(FAILED(hr)) ERR("Failed to release capture buffer: {:#x}", as_unsigned(hr));
            }
        }

        if(FAILED(hr))
        {
            mDevice->handleDisconnect("Failed to capture samples: {:#x}", as_unsigned(hr));
            break;
        }

        if(DWORD res{WaitForSingleObjectEx(mNotifyEvent, 2000, FALSE)}; res != WAIT_OBJECT_0)
            ERR("WaitForSingleObjectEx error: {:#x}", res);
    }

    client->Stop();
    client->Reset();
}


void WasapiCapture::proc_thread(std::string&& name)
try {
    auto com = ComWrapper{COINIT_MULTITHREADED};
    if(!com)
    {
        const auto hr = as_unsigned(com.status());
        ERR("CoInitializeEx(nullptr, COINIT_MULTITHREADED) failed: {:#x}", hr);
        mDevice->handleDisconnect("COM init failed: {:#x}", hr);

        auto plock = std::lock_guard{mProcMutex};
        mProcResult = com.status();
        mState = ThreadState::Done;
        mProcCond.notify_all();
        return;
    }

    if(!gInitDone.load(std::memory_order_relaxed))
    {
        auto devlock = DeviceListLock{gDeviceList};
        gInitCV.wait(devlock, []() -> bool { return gInitDone; });
    }

    auto helper = DeviceHelper{};
    if(HRESULT hr{helper.init()}; FAILED(hr))
    {
        mDevice->handleDisconnect("Helper init failed: {:#x}", as_unsigned(hr));

        auto plock = std::lock_guard{mProcMutex};
        mProcResult = hr;
        mState = ThreadState::Done;
        mProcCond.notify_all();
        return;
    }

    althrd_setname(GetRecordThreadName());

    auto mmdev = DeviceHandle{nullptr};
    if(auto hr = openProxy(name, helper, mmdev); FAILED(hr))
    {
        auto plock = std::lock_guard{mProcMutex};
        mProcResult = hr;
        mState = ThreadState::Done;
        mProcCond.notify_all();
        return;
    }

    auto client = ComPtr<IAudioClient>{};
    auto capture = ComPtr<IAudioCaptureClient>{};
    if(auto hr = resetProxy(helper, mmdev, client, capture); FAILED(hr))
    {
        auto plock = std::lock_guard{mProcMutex};
        mProcResult = hr;
        mState = ThreadState::Done;
        mProcCond.notify_all();
        return;
    }

    auto plock = std::unique_lock{mProcMutex};
    mProcResult = S_OK;
    while(mState != ThreadState::Done)
    {
        mAction = ThreadAction::Nothing;
        mState = ThreadState::Waiting;
        mProcCond.notify_all();

        mProcCond.wait(plock, [this]() noexcept { return mAction != ThreadAction::Nothing; });
        switch(mAction)
        {
        case ThreadAction::Nothing:
            break;

        case ThreadAction::Record:
            mKillNow.store(false, std::memory_order_release);

            mAction = ThreadAction::Nothing;
            mState = ThreadState::Recording;
            mProcResult = S_OK;
            plock.unlock();
            mProcCond.notify_all();

            recordProc(client.get(), capture.get());

            plock.lock();
            break;

        case ThreadAction::Quit:
            mAction = ThreadAction::Nothing;
            mState = ThreadState::Done;
            mProcCond.notify_all();
            break;
        }
    }
}
catch(...) {
    auto plock = std::lock_guard{mProcMutex};
    mProcResult = E_FAIL;
    mAction = ThreadAction::Nothing;
    mState = ThreadState::Done;
    mProcCond.notify_all();
}


void WasapiCapture::open(std::string_view name)
{
    mNotifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if(mNotifyEvent == nullptr)
    {
        ERR("Failed to create notify events: {}", GetLastError());
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to create notify events"};
    }

    mProcThread = std::thread{&WasapiCapture::proc_thread, this, std::string{name}};

    auto plock = std::unique_lock{mProcMutex};
    mProcCond.wait(plock, [this]() noexcept { return mState != ThreadState::Initializing; });

    if(mProcResult == E_NOTFOUND)
        throw al::backend_exception{al::backend_error::NoDevice, "Device \"{}\" not found", name};
    if(mProcResult == E_OUTOFMEMORY)
        throw al::backend_exception{al::backend_error::OutOfMemory, "Out of memory"};
    if(FAILED(mProcResult) || mState == ThreadState::Done)
        throw al::backend_exception{al::backend_error::DeviceError, "Device init failed: {:#x}",
            as_unsigned(mProcResult)};
}

auto WasapiCapture::openProxy(const std::string_view name, DeviceHelper &helper,
    DeviceHandle &mmdev) -> HRESULT
{
    auto devname = std::string{};
    auto devid = std::wstring{};
    if(!name.empty())
    {
        auto devlock = DeviceListLock{gDeviceList};
        auto devlist = al::span{devlock.getCaptureList()};
        auto iter = std::find_if(devlist.cbegin(), devlist.cend(),
            [name](const DevMap &entry) -> bool
            { return entry.name == name || al::case_compare(entry.endpoint_guid, name) == 0; });
        if(iter == devlist.cend())
        {
            const std::wstring wname{utf8_to_wstr(name)};
            iter = std::find_if(devlist.cbegin(), devlist.cend(),
                [&wname](const DevMap &entry) -> bool
                { return al::case_compare(entry.devid, wname) == 0; });
        }
        if(iter == devlist.cend())
        {
            WARN("Failed to find device name matching \"{}\"", name);
            return E_NOTFOUND;
        }
        devname = iter->name;
        devid = iter->devid;
    }

    auto hr = helper.openDevice(devid, eCapture, mmdev);
    if(FAILED(hr))
    {
        WARN("Failed to open device \"{}\": {:#x}", devname.empty()
            ? "(default)"sv : std::string_view{devname}, as_unsigned(hr));
        return hr;
    }
    if(!devname.empty())
        mDeviceName = std::move(devname);
    else
        mDeviceName = GetDeviceNameAndGuid(mmdev).mName;

    return S_OK;
}

auto WasapiCapture::resetProxy(DeviceHelper &helper, DeviceHandle &mmdev,
    ComPtr<IAudioClient> &client, ComPtr<IAudioCaptureClient> &capture) -> HRESULT
{
    capture = nullptr;
    client = nullptr;

    auto hr = helper.activateAudioClient(mmdev, __uuidof(IAudioClient), al::out_ptr(client));
    if(FAILED(hr))
    {
        ERR("Failed to reactivate audio client: {:#x}", as_unsigned(hr));
        return hr;
    }

    auto wfx = unique_coptr<WAVEFORMATEX>{};
    hr = client->GetMixFormat(al::out_ptr(wfx));
    if(FAILED(hr))
    {
        ERR("Failed to get capture format: {:#x}", as_unsigned(hr));
        return hr;
    }
    TraceFormat("Device capture format", wfx.get());

    auto InputType = WAVEFORMATEXTENSIBLE{};
    if(!MakeExtensible(&InputType, wfx.get()))
        return E_FAIL;
    wfx = nullptr;

    const bool isRear51{InputType.Format.nChannels == 6
        && (InputType.dwChannelMask&X51RearMask) == X5DOT1REAR};

    // Make sure buffer is at least 100ms in size
    ReferenceTime buf_time{ReferenceTime{seconds{mDevice->mBufferSize}} / mDevice->mSampleRate};
    buf_time = std::max(buf_time, ReferenceTime{milliseconds{100}});

    InputType = {};
    InputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    switch(mDevice->FmtChans)
    {
    case DevFmtMono:
        InputType.Format.nChannels = 1;
        InputType.dwChannelMask = MONO;
        break;
    case DevFmtStereo:
        InputType.Format.nChannels = 2;
        InputType.dwChannelMask = STEREO;
        break;
    case DevFmtQuad:
        InputType.Format.nChannels = 4;
        InputType.dwChannelMask = QUAD;
        break;
    case DevFmtX51:
        InputType.Format.nChannels = 6;
        InputType.dwChannelMask = isRear51 ? X5DOT1REAR : X5DOT1;
        break;
    case DevFmtX61:
        InputType.Format.nChannels = 7;
        InputType.dwChannelMask = X6DOT1;
        break;
    case DevFmtX71:
        InputType.Format.nChannels = 8;
        InputType.dwChannelMask = X7DOT1;
        break;
    case DevFmtX714:
        InputType.Format.nChannels = 12;
        InputType.dwChannelMask = X7DOT1DOT4;
        break;

    case DevFmtX7144:
    case DevFmtX3D71:
    case DevFmtAmbi3D:
        return E_FAIL;
    }
    switch(mDevice->FmtType)
    {
    /* NOTE: Signedness doesn't matter, the converter will handle it. */
    case DevFmtByte:
    case DevFmtUByte:
        InputType.Format.wBitsPerSample = 8;
        InputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        break;
    case DevFmtShort:
    case DevFmtUShort:
        InputType.Format.wBitsPerSample = 16;
        InputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        break;
    case DevFmtInt:
    case DevFmtUInt:
        InputType.Format.wBitsPerSample = 32;
        InputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        break;
    case DevFmtFloat:
        InputType.Format.wBitsPerSample = 32;
        InputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        break;
    }
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) */
    InputType.Samples.wValidBitsPerSample = InputType.Format.wBitsPerSample;
    InputType.Format.nSamplesPerSec = mDevice->mSampleRate;

    InputType.Format.nBlockAlign = static_cast<WORD>(InputType.Format.nChannels *
        InputType.Format.wBitsPerSample / 8);
    InputType.Format.nAvgBytesPerSec = InputType.Format.nSamplesPerSec *
        InputType.Format.nBlockAlign;
    InputType.Format.cbSize = sizeof(InputType) - sizeof(InputType.Format);

    TraceFormat("Requesting capture format", &InputType.Format);
    hr = client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &InputType.Format, al::out_ptr(wfx));
    if(FAILED(hr))
    {
        WARN("Failed to check capture format support: {:#x}", as_unsigned(hr));
        hr = client->GetMixFormat(al::out_ptr(wfx));
    }
    if(FAILED(hr))
    {
        ERR("Failed to find a supported capture format: {:#x}", as_unsigned(hr));
        return hr;
    }

    mSampleConv = nullptr;
    mChannelConv = {};

    if(wfx != nullptr)
    {
        TraceFormat("Got capture format", wfx.get());
        if(!MakeExtensible(&InputType, wfx.get()))
            return E_FAIL;
        wfx = nullptr;

        auto validate_fmt = [](DeviceBase *device, uint32_t chancount, DWORD chanmask) noexcept
            -> bool
        {
            switch(device->FmtChans)
            {
            /* If the device wants mono, we can handle any input. */
            case DevFmtMono:
                return true;
            /* If the device wants stereo, we can handle mono or stereo input. */
            case DevFmtStereo:
                return (chancount == 2 && (chanmask == 0 || (chanmask&StereoMask) == STEREO))
                    || (chancount == 1 && (chanmask&MonoMask) == MONO);
            /* Otherwise, the device must match the input type. */
            case DevFmtQuad:
                return (chancount == 4 && (chanmask == 0 || (chanmask&QuadMask) == QUAD));
            /* 5.1 (Side) and 5.1 (Rear) are interchangeable here. */
            case DevFmtX51:
                return (chancount == 6 && (chanmask == 0 || (chanmask&X51Mask) == X5DOT1
                        || (chanmask&X51RearMask) == X5DOT1REAR));
            case DevFmtX61:
                return (chancount == 7 && (chanmask == 0 || (chanmask&X61Mask) == X6DOT1));
            case DevFmtX71:
            case DevFmtX3D71:
                return (chancount == 8 && (chanmask == 0 || (chanmask&X71Mask) == X7DOT1));
            case DevFmtX714:
                return (chancount == 12 && (chanmask == 0 || (chanmask&X714Mask) == X7DOT1DOT4));
            case DevFmtX7144:
                return (chancount == 16 && chanmask == 0);
            case DevFmtAmbi3D:
                return (chanmask == 0 && chancount == device->channelsFromFmt());
            }
            return false;
        };
        if(!validate_fmt(mDevice, InputType.Format.nChannels, InputType.dwChannelMask))
        {
            ERR("Failed to match format, wanted: {} {} {}hz, got: {:#08x} mask {} channel{} {}-bit {}hz",
                DevFmtChannelsString(mDevice->FmtChans), DevFmtTypeString(mDevice->FmtType),
                mDevice->mSampleRate, InputType.dwChannelMask, InputType.Format.nChannels,
                (InputType.Format.nChannels==1)?"":"s", InputType.Format.wBitsPerSample,
                InputType.Format.nSamplesPerSec);
            return E_FAIL;
        }
    }

    DevFmtType srcType{};
    if(IsEqualGUID(InputType.SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
    {
        if(InputType.Format.wBitsPerSample == 8)
            srcType = DevFmtUByte;
        else if(InputType.Format.wBitsPerSample == 16)
            srcType = DevFmtShort;
        else if(InputType.Format.wBitsPerSample == 32)
            srcType = DevFmtInt;
        else
        {
            ERR("Unhandled integer bit depth: {}", InputType.Format.wBitsPerSample);
            return E_FAIL;
        }
    }
    else if(IsEqualGUID(InputType.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
        if(InputType.Format.wBitsPerSample == 32)
            srcType = DevFmtFloat;
        else
        {
            ERR("Unhandled float bit depth: {}", InputType.Format.wBitsPerSample);
            return E_FAIL;
        }
    }
    else
    {
        ERR("Unhandled format sub-type: {}", GuidPrinter{InputType.SubFormat}.str());
        return E_FAIL;
    }

    if(mDevice->FmtChans == DevFmtMono && InputType.Format.nChannels != 1)
    {
        uint chanmask{(1u<<InputType.Format.nChannels) - 1u};
        /* Exclude LFE from the downmix. */
        if((InputType.dwChannelMask&SPEAKER_LOW_FREQUENCY))
        {
            constexpr auto lfemask = MaskFromTopBits(SPEAKER_LOW_FREQUENCY);
            const int lfeidx{al::popcount(InputType.dwChannelMask&lfemask) - 1};
            chanmask &= ~(1u << lfeidx);
        }

        mChannelConv = ChannelConverter{srcType, InputType.Format.nChannels, chanmask,
            mDevice->FmtChans};
        TRACE("Created {} multichannel-to-mono converter", DevFmtTypeString(srcType));
        /* The channel converter always outputs float, so change the input type
         * for the resampler/type-converter.
         */
        srcType = DevFmtFloat;
    }
    else if(mDevice->FmtChans == DevFmtStereo && InputType.Format.nChannels == 1)
    {
        mChannelConv = ChannelConverter{srcType, 1, 0x1, mDevice->FmtChans};
        TRACE("Created {} mono-to-stereo converter", DevFmtTypeString(srcType));
        srcType = DevFmtFloat;
    }

    if(mDevice->mSampleRate != InputType.Format.nSamplesPerSec || mDevice->FmtType != srcType)
    {
        mSampleConv = SampleConverter::Create(srcType, mDevice->FmtType,
            mDevice->channelsFromFmt(), InputType.Format.nSamplesPerSec, mDevice->mSampleRate,
            Resampler::FastBSinc24);
        if(!mSampleConv)
        {
            ERR("Failed to create converter for {} format, dst: {} {}hz, src: {} {}hz",
                DevFmtChannelsString(mDevice->FmtChans), DevFmtTypeString(mDevice->FmtType),
                mDevice->mSampleRate, DevFmtTypeString(srcType), InputType.Format.nSamplesPerSec);
            return E_FAIL;
        }
        TRACE("Created converter for {} format, dst: {} {}hz, src: {} {}hz",
            DevFmtChannelsString(mDevice->FmtChans), DevFmtTypeString(mDevice->FmtType),
            mDevice->mSampleRate, DevFmtTypeString(srcType), InputType.Format.nSamplesPerSec);
    }

    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        buf_time.count(), 0, &InputType.Format, nullptr);
    if(FAILED(hr))
    {
        ERR("Failed to initialize audio client: {:#x}", as_unsigned(hr));
        return hr;
    }

    hr = client->GetService(__uuidof(IAudioCaptureClient), al::out_ptr(capture));
    if(FAILED(hr))
    {
        ERR("Failed to get IAudioCaptureClient: {:#x}", as_unsigned(hr));
        return hr;
    }

    UINT32 buffer_len{};
    ReferenceTime min_per{};
    hr = client->GetDevicePeriod(&reinterpret_cast<REFERENCE_TIME&>(min_per), nullptr);
    if(SUCCEEDED(hr))
        hr = client->GetBufferSize(&buffer_len);
    if(FAILED(hr))
    {
        ERR("Failed to get buffer size: {:#x}", as_unsigned(hr));
        return hr;
    }
    mDevice->mUpdateSize = RefTime2Samples(min_per, mDevice->mSampleRate);
    mDevice->mBufferSize = buffer_len;

    mRing = RingBuffer::Create(buffer_len, mDevice->frameSizeFromFmt(), false);

    hr = client->SetEventHandle(mNotifyEvent);
    if(FAILED(hr))
    {
        ERR("Failed to set event handle: {:#x}", as_unsigned(hr));
        return hr;
    }

    return hr;
}


void WasapiCapture::start()
{
    auto plock = std::unique_lock{mProcMutex};
    if(mState != ThreadState::Waiting)
        throw al::backend_exception{al::backend_error::DeviceError, "Invalid state: {}",
            unsigned{al::to_underlying(mState)}};

    mAction = ThreadAction::Record;
    mProcCond.notify_all();
    mProcCond.wait(plock, [this]() noexcept { return mAction != ThreadAction::Record; });

    if(FAILED(mProcResult) || mState != ThreadState::Recording)
        throw al::backend_exception{al::backend_error::DeviceError, "Device capture failed: {:#x}",
            as_unsigned(mProcResult)};
}

void WasapiCapture::stop()
{
    auto plock = std::unique_lock{mProcMutex};
    if(mState == ThreadState::Recording)
    {
        mKillNow = true;
        mProcCond.wait(plock, [this]() noexcept { return mState != ThreadState::Recording; });
    }
}


void WasapiCapture::captureSamples(std::byte *buffer, uint samples)
{ std::ignore = mRing->read(buffer, samples); }

uint WasapiCapture::availableSamples()
{ return static_cast<uint>(mRing->readSpace()); }

} // namespace


bool WasapiBackendFactory::init()
{
    static HRESULT InitResult{E_FAIL};
    if(FAILED(InitResult)) try
    {
        std::promise<HRESULT> promise;
        auto future = promise.get_future();

        std::thread{&DeviceEnumHelper::messageHandler, &promise}.detach();
        InitResult = future.get();
    }
    catch(...) {
    }

    return SUCCEEDED(InitResult);
}

bool WasapiBackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback || type == BackendType::Capture; }

auto WasapiBackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    std::vector<std::string> outnames;

    auto devlock = DeviceListLock{gDeviceList};
    switch(type)
    {
    case BackendType::Playback:
        {
            auto defaultId = devlock.getPlaybackDefaultId();
            auto &devlist = devlock.getPlaybackList();

            outnames.reserve(devlist.size());
            std::transform(devlist.cbegin(), devlist.cend(), std::back_inserter(outnames),
                std::mem_fn(&DevMap::name));

            /* Default device goes first. */
            const auto defiter = std::find_if(devlist.cbegin(), devlist.cend(),
                [defaultId](const DevMap &entry) -> bool { return entry.devid == defaultId; });
            if(defiter != devlist.cend())
            {
                const auto defname = outnames.begin() + std::distance(devlist.cbegin(), defiter);
                std::rotate(outnames.begin(), defname, defname+1);
            }
        }
        break;

    case BackendType::Capture:
        {
            auto defaultId = devlock.getCaptureDefaultId();
            auto &devlist = devlock.getCaptureList();

            outnames.reserve(devlist.size());
            std::transform(devlist.cbegin(), devlist.cend(), std::back_inserter(outnames),
                std::mem_fn(&DevMap::name));

            /* Default device goes first. */
            const auto defiter = std::find_if(devlist.cbegin(), devlist.cend(),
                [defaultId](const DevMap &entry) -> bool { return entry.devid == defaultId; });
            if(defiter != devlist.cend())
            {
                const auto defname = outnames.begin() + std::distance(devlist.cbegin(), defiter);
                std::rotate(outnames.begin(), defname, defname+1);
            }
        }
        break;
    }

    return outnames;
}

BackendPtr WasapiBackendFactory::createBackend(DeviceBase *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new WasapiPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new WasapiCapture{device}};
    return nullptr;
}

BackendFactory &WasapiBackendFactory::getFactory()
{
    static WasapiBackendFactory factory{};
    return factory;
}

alc::EventSupport WasapiBackendFactory::queryEventSupport(alc::EventType eventType, BackendType)
{
    switch(eventType)
    {
    case alc::EventType::DefaultDeviceChanged:
        return alc::EventSupport::FullSupport;

    case alc::EventType::DeviceAdded:
    case alc::EventType::DeviceRemoved:
#if !ALSOFT_UWP
        return alc::EventSupport::FullSupport;
#endif

    case alc::EventType::Count:
        break;
    }
    return alc::EventSupport::NoSupport;
}
