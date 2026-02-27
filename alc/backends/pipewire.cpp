/**
 * OpenAL cross platform audio library
 * Copyright (C) 2010 by Chris Robinson
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

#include "pipewire.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

#include "alc/alconfig.h"
#include "alc/backends/base.h"
#include "alformat.hpp"
#include "alstring.h"
#include "core/devformat.h"
#include "core/device.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "dynload.h"
#include "fmt/core.h"
#include "fmt/ranges.h"
#include "gsl/gsl"
#include "opthelpers.h"
#include "pragmadefs.h"
#include "ringbuffer.h"

/* Ignore warnings caused by PipeWire headers (lots in standard C++ mode). GCC
 * doesn't support ignoring -Weverything, so we have the list the individual
 * warnings to ignore (and ignoring -Winline doesn't seem to work).
 */
DIAGNOSTIC_PUSH
std_pragma("GCC diagnostic ignored \"-Wpedantic\"")
std_pragma("GCC diagnostic ignored \"-Wconversion\"")
std_pragma("GCC diagnostic ignored \"-Warith-conversion\"")
std_pragma("GCC diagnostic ignored \"-Wfloat-conversion\"")
std_pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
std_pragma("GCC diagnostic ignored \"-Wunused-parameter\"")
std_pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
std_pragma("GCC diagnostic ignored \"-Wsign-compare\"")
std_pragma("GCC diagnostic ignored \"-Winline\"")
std_pragma("GCC diagnostic ignored \"-Wpragmas\"")
std_pragma("GCC diagnostic ignored \"-Wvla\"")
std_pragma("GCC diagnostic ignored \"-Winvalid-constexpr\"")
std_pragma("GCC diagnostic ignored \"-Weverything\"")
#include "pipewire/pipewire.h"
#include "pipewire/extensions/metadata.h"
#include "spa/buffer/buffer.h"
#include "spa/param/audio/format-utils.h"
#include "spa/param/audio/raw.h"
#include "spa/param/format.h"
#include "spa/param/param.h"
#include "spa/pod/builder.h"
#include "spa/utils/json.h"

/* NOLINTBEGIN : All kinds of unsafe C stuff here from PipeWire headers
 * (function-like macros, C style casts in macros, etc), which we can't do
 * anything about except wrap into inline functions.
 */
namespace {
/* Wrap some nasty macros here too... */
template<typename ...Args>
auto ppw_core_add_listener(pw_core *core, Args&& ...args)
{ return pw_core_add_listener(core, std::forward<Args>(args)...); }
template<typename ...Args>
auto ppw_core_sync(pw_core *core, Args&& ...args)
{ return pw_core_sync(core, std::forward<Args>(args)...); }
template<typename ...Args>
auto ppw_registry_add_listener(pw_registry *reg, Args&& ...args)
{ return pw_registry_add_listener(reg, std::forward<Args>(args)...); }
template<typename ...Args>
auto ppw_node_add_listener(pw_node *node, Args&& ...args)
{ return pw_node_add_listener(node, std::forward<Args>(args)...); }
template<typename ...Args>
auto ppw_node_subscribe_params(pw_node *node, Args&& ...args)
{ return pw_node_subscribe_params(node, std::forward<Args>(args)...); }
template<typename ...Args>
auto ppw_metadata_add_listener(pw_metadata *mdata, Args&& ...args)
{ return pw_metadata_add_listener(mdata, std::forward<Args>(args)...); }


constexpr auto get_pod_type(const spa_pod *pod) noexcept
{ return SPA_POD_TYPE(pod); }

template<typename T>
constexpr auto get_pod_body(spa_pod const *const pod, usize const count) noexcept
{ return std::span<T>{static_cast<T*>(SPA_POD_BODY(pod)), count}; }
template<typename T, usize N>
constexpr auto get_pod_body(spa_pod const *const pod) noexcept
{ return std::span<T,N>{static_cast<T*>(SPA_POD_BODY(pod)), N}; }

constexpr auto get_array_value_type(spa_pod const *const pod) noexcept
{ return SPA_POD_ARRAY_VALUE_TYPE(pod); }

constexpr auto make_pod_builder(void *const data, uint32_t const size) noexcept
{ return SPA_POD_BUILDER_INIT(data, size); }

constexpr auto PwIdAny = PW_ID_ANY;

} // namespace
/* NOLINTEND */
DIAGNOSTIC_POP

namespace {

template<typename T> [[nodiscard]] constexpr
auto as_const_ptr(T *ptr) noexcept -> std::add_const_t<T>* { return ptr; }

struct SpaHook : spa_hook {
    SpaHook() : spa_hook{} { }
    ~SpaHook()
    {
        /* Prior to 0.3.57, spa_hook_remove will crash if the spa_hook hasn't
         * been linked with anything, which complicates removing on destruction
         * since the spa_hook object needs to exist before it's linked, but if
         * linking fails, there's no function to test if it can be removed. The
         * PipeWire headers say spa_hook should be treated as opaque, meaning
         * accessing any fields directly risks breaking compilation in the
         * future. So we only peek into the spa_hool to do this check on older
         * versions that need it.
         */
#if !PW_CHECK_VERSION(0,3,57)
        if(this->link.prev != nullptr)
#endif
            spa_hook_remove(this);
    }

    void remove()
    {
#if !PW_CHECK_VERSION(0,3,57)
        if(this->link.prev != nullptr)
#endif
            spa_hook_remove(this);
        static_cast<spa_hook&>(*this) = spa_hook{};
    }

    SpaHook(const SpaHook&) = delete;
    SpaHook(SpaHook&&) = delete;
    auto operator=(const SpaHook&) -> SpaHook& = delete;
    auto operator=(SpaHook&&) -> SpaHook& = delete;
};

struct PodDynamicBuilder {
private:
    std::vector<std::byte> mStorage;
    spa_pod_builder mPod{};

    auto overflow(uint32_t const size) noexcept -> int
    {
        try {
            mStorage.resize(size);
        }
        catch(...) {
            ERR("Failed to resize POD storage");
            return -ENOMEM;
        }
        mPod.data = mStorage.data();
        mPod.size = size;
        return 0;
    }

public:
    explicit PodDynamicBuilder(uint32_t const initSize=1024)
        : mStorage(initSize), mPod{make_pod_builder(mStorage.data(), initSize)}
    {
        static constexpr auto callbacks = spa_pod_builder_callbacks{
            .version = SPA_VERSION_POD_BUILDER_CALLBACKS,
            .overflow = [](void *data, uint32_t const size) noexcept
            { return static_cast<PodDynamicBuilder*>(data)->overflow(size); }
        };

        spa_pod_builder_set_callbacks(&mPod, &callbacks, this);
    }

    auto get() noexcept -> spa_pod_builder* { return &mPod; }
};

/* Added in 0.3.33, but we currently only require 0.3.23. */
#ifndef PW_KEY_NODE_RATE
#define PW_KEY_NODE_RATE "node.rate"
#endif

using namespace std::string_view_literals;
using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

auto *gConfigFileName = gsl::czstring{};


auto check_version(gsl::czstring const version) -> bool
{
    /* There doesn't seem to be a function to get the version as an integer, so
     * instead we have to parse the string, which hopefully won't break in the
     * future.
     */
    auto major = int{};
    auto minor = int{};
    auto revision = int{};
    /* NOLINTNEXTLINE(cert-err34-c,cppcoreguidelines-pro-type-vararg) */
    if(auto const ret = sscanf(version, "%d.%d.%d", &major, &minor, &revision); ret != 3)
        return false;

    /* client-rt.conf is deprecated since PipeWire 1.3.81, and we should just
     * use the default.
     */
    if(!(major > 1 || (major == 1 && minor > 3) || (major == 1 && minor == 3 && revision >= 81)))
        gConfigFileName = "client-rt.conf";

    return major > PW_MAJOR || (major == PW_MAJOR && minor > PW_MINOR)
        || (major == PW_MAJOR && minor == PW_MINOR && revision >= PW_MICRO);
}

#if HAVE_DYNLOAD
#define PWIRE_FUNCS(MAGIC)                                                    \
    MAGIC(pw_context_connect)                                                 \
    MAGIC(pw_context_destroy)                                                 \
    MAGIC(pw_context_new)                                                     \
    MAGIC(pw_core_disconnect)                                                 \
    MAGIC(pw_get_library_version)                                             \
    MAGIC(pw_init)                                                            \
    MAGIC(pw_properties_free)                                                 \
    MAGIC(pw_properties_new)                                                  \
    MAGIC(pw_properties_set)                                                  \
    MAGIC(pw_properties_setf)                                                 \
    MAGIC(pw_proxy_add_object_listener)                                       \
    MAGIC(pw_proxy_destroy)                                                   \
    MAGIC(pw_proxy_get_user_data)                                             \
    MAGIC(pw_stream_add_listener)                                             \
    MAGIC(pw_stream_connect)                                                  \
    MAGIC(pw_stream_dequeue_buffer)                                           \
    MAGIC(pw_stream_destroy)                                                  \
    MAGIC(pw_stream_get_state)                                                \
    MAGIC(pw_stream_new)                                                      \
    MAGIC(pw_stream_queue_buffer)                                             \
    MAGIC(pw_stream_set_active)                                               \
    MAGIC(pw_thread_loop_new)                                                 \
    MAGIC(pw_thread_loop_destroy)                                             \
    MAGIC(pw_thread_loop_get_loop)                                            \
    MAGIC(pw_thread_loop_start)                                               \
    MAGIC(pw_thread_loop_stop)                                                \
    MAGIC(pw_thread_loop_lock)                                                \
    MAGIC(pw_thread_loop_wait)                                                \
    MAGIC(pw_thread_loop_signal)                                              \
    MAGIC(pw_thread_loop_unlock)
#if PW_CHECK_VERSION(0,3,50)
#define PWIRE_FUNCS2(MAGIC)                                                   \
    MAGIC(pw_stream_get_time_n)
#else
#define PWIRE_FUNCS2(MAGIC)                                                   \
    MAGIC(pw_stream_get_time)
#endif

void *pwire_handle;
#define MAKE_FUNC(f) decltype(f) * p##f;
PWIRE_FUNCS(MAKE_FUNC)
PWIRE_FUNCS2(MAKE_FUNC)
#undef MAKE_FUNC

#define PWIRE_LIB "libpipewire-0.3.so.0"

OAL_ELF_NOTE_DLOPEN(
    "backend-pipewire",
    "Support for the PipeWire backend",
    OAL_ELF_NOTE_DLOPEN_PRIORITY_RECOMMENDED,
    PWIRE_LIB
);

auto pwire_load() -> bool
{
    if(pwire_handle)
        return true;

    auto *const pwire_lib = gsl::czstring{PWIRE_LIB};
    if(auto const libresult = LoadLib(pwire_lib))
        pwire_handle = libresult.value();
    else
    {
        WARN("Failed to load {}: {}", pwire_lib, libresult.error());
        return false;
    }

    static constexpr auto load_func = [](auto *&func, gsl::czstring const name) -> bool
    {
        using func_t = std::remove_reference_t<decltype(func)>;
        auto const funcresult = GetSymbol(pwire_handle, name);
        if(!funcresult)
        {
            WARN("Failed to load function {}: {}", name, funcresult.error());
            return false;
        }
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        func = reinterpret_cast<func_t>(funcresult.value());
        return true;
    };
    auto ok = true;
#define LOAD_FUNC(f) ok &= load_func(p##f, #f);
    PWIRE_FUNCS(LOAD_FUNC)
    PWIRE_FUNCS2(LOAD_FUNC)
#undef LOAD_FUNC
    if(!ok)
    {
        CloseLib(pwire_handle);
        pwire_handle = nullptr;
        return false;
    }

    return true;
}

#ifndef IN_IDE_PARSER
#define pw_context_connect ppw_context_connect
#define pw_context_destroy ppw_context_destroy
#define pw_context_new ppw_context_new
#define pw_core_disconnect ppw_core_disconnect
#define pw_get_library_version ppw_get_library_version
#define pw_init ppw_init
#define pw_properties_free ppw_properties_free
#define pw_properties_new ppw_properties_new
#define pw_properties_set ppw_properties_set
#define pw_properties_setf ppw_properties_setf
#define pw_proxy_add_object_listener ppw_proxy_add_object_listener
#define pw_proxy_destroy ppw_proxy_destroy
#define pw_proxy_get_user_data ppw_proxy_get_user_data
#define pw_stream_add_listener ppw_stream_add_listener
#define pw_stream_connect ppw_stream_connect
#define pw_stream_dequeue_buffer ppw_stream_dequeue_buffer
#define pw_stream_destroy ppw_stream_destroy
#define pw_stream_get_state ppw_stream_get_state
#define pw_stream_new ppw_stream_new
#define pw_stream_queue_buffer ppw_stream_queue_buffer
#define pw_stream_set_active ppw_stream_set_active
#define pw_thread_loop_destroy ppw_thread_loop_destroy
#define pw_thread_loop_get_loop ppw_thread_loop_get_loop
#define pw_thread_loop_lock ppw_thread_loop_lock
#define pw_thread_loop_new ppw_thread_loop_new
#define pw_thread_loop_signal ppw_thread_loop_signal
#define pw_thread_loop_start ppw_thread_loop_start
#define pw_thread_loop_stop ppw_thread_loop_stop
#define pw_thread_loop_unlock ppw_thread_loop_unlock
#define pw_thread_loop_wait ppw_thread_loop_wait
#if PW_CHECK_VERSION(0,3,50)
#define pw_stream_get_time_n ppw_stream_get_time_n
#else
inline auto pw_stream_get_time_n(pw_stream *stream, pw_time *ptime, usize /*size*/)
{ return ppw_stream_get_time(stream, ptime); }
#endif
#endif

#else

constexpr bool pwire_load() { return true; }
#endif

/* Helpers for retrieving values from params */
template<uint32_t > struct PodInfo { };

template<>
struct PodInfo<SPA_TYPE_Int> {
    using Type = int32_t;
    static auto get_value(const spa_pod *pod, int32_t *val)
    { return spa_pod_get_int(pod, val); }
};
template<>
struct PodInfo<SPA_TYPE_Id> {
    using Type = uint32_t;
    static auto get_value(const spa_pod *pod, uint32_t *val)
    { return spa_pod_get_id(pod, val); }
};

template<uint32_t T>
using Pod_t = PodInfo<T>::Type;

template<uint32_t T>
auto get_array_span(const spa_pod *pod) -> std::span<const Pod_t<T>>
{
    auto nvals = uint32_t{};
    if(auto *v = spa_pod_get_array(pod, &nvals))
    {
        if(get_array_value_type(pod) == T)
            return {static_cast<const Pod_t<T>*>(v), nvals};
    }
    return {};
}

template<uint32_t T>
auto get_value(const spa_pod *value) -> std::optional<Pod_t<T>>
{
    auto val = Pod_t<T>{};
    if(PodInfo<T>::get_value(value, &val) == 0)
        return val;
    return std::nullopt;
}

/* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
 * Internally, PipeWire types "inherit" from each other, but this is hidden
 * from the API and the caller is expected to C-style cast to inherited types
 * as needed. It's also not made very clear what types a given type can be
 * casted to. To make it a bit safer, this as() method allows casting pw_*
 * types to known inherited types, generating a compile-time error for
 * unexpected/invalid casts.
 */
template<typename To, typename From>
auto as(From) noexcept -> To = delete;

/* pw_proxy
 * - pw_registry
 * - pw_node
 * - pw_metadata
 */
template<> [[nodiscard]]
auto as(pw_registry *reg) noexcept -> pw_proxy* { return reinterpret_cast<pw_proxy*>(reg); }
template<> [[nodiscard]]
auto as(pw_node *node) noexcept -> pw_proxy* { return reinterpret_cast<pw_proxy*>(node); }
template<> [[nodiscard]]
auto as(pw_metadata *mdata) noexcept -> pw_proxy* { return reinterpret_cast<pw_proxy*>(mdata); }
/* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */


using PwContextPtr = std::unique_ptr<pw_context, decltype([](pw_context *context)
    { pw_context_destroy(context); })>;

using PwCorePtr = std::unique_ptr<pw_core, decltype([](pw_core *core)
    { pw_core_disconnect(core); })>;

using PwRegistryPtr = std::unique_ptr<pw_registry, decltype([](pw_registry *reg)
    { pw_proxy_destroy(as<pw_proxy*>(reg)); })>;

using PwNodePtr = std::unique_ptr<pw_node, decltype([](pw_node *node)
    { pw_proxy_destroy(as<pw_proxy*>(node)); })>;

using PwMetadataPtr = std::unique_ptr<pw_metadata, decltype([](pw_metadata *mdata)
    { pw_proxy_destroy(as<pw_proxy*>(mdata)); })>;

using PwStreamPtr = std::unique_ptr<pw_stream, decltype([](pw_stream *stream)
    { pw_stream_destroy(stream); })>;

/* NOLINTBEGIN(*EnumCastOutOfRange) Enums for bitflags... again... *sigh* */
[[nodiscard]] constexpr
auto operator|(pw_stream_flags const lhs, pw_stream_flags const rhs) noexcept -> pw_stream_flags
{ return static_cast<pw_stream_flags>(lhs | al::to_underlying(rhs)); }

constexpr auto operator|=(pw_stream_flags &lhs, pw_stream_flags rhs) noexcept -> pw_stream_flags&
{ lhs = lhs | rhs; return lhs; }
/* NOLINTEND(*EnumCastOutOfRange) */


class ThreadMainloop {
    pw_thread_loop *mLoop{};

public:
    ThreadMainloop() = default;
    ThreadMainloop(const ThreadMainloop&) = delete;
    ThreadMainloop(ThreadMainloop&& rhs) noexcept : mLoop{rhs.mLoop} { rhs.mLoop = nullptr; }
    explicit ThreadMainloop(pw_thread_loop *loop) noexcept : mLoop{loop} { }
    ~ThreadMainloop() { if(mLoop) pw_thread_loop_destroy(mLoop); }

    auto operator=(const ThreadMainloop&) -> ThreadMainloop& = delete;
    auto operator=(ThreadMainloop&& rhs) noexcept -> ThreadMainloop&
    { std::swap(mLoop, rhs.mLoop); return *this; }
    auto operator=(std::nullptr_t) noexcept -> ThreadMainloop&
    {
        if(mLoop)
            pw_thread_loop_destroy(mLoop);
        mLoop = nullptr;
        return *this;
    }

    explicit operator bool() const noexcept { return mLoop != nullptr; }

    [[nodiscard]]
    auto start() const { return pw_thread_loop_start(mLoop); }
    auto stop() const { return pw_thread_loop_stop(mLoop); }

    [[nodiscard]]
    auto getLoop() const { return pw_thread_loop_get_loop(mLoop); }

    auto lock() const { return pw_thread_loop_lock(mLoop); }
    auto unlock() const { return pw_thread_loop_unlock(mLoop); }

    auto signal(bool const wait) const { return pw_thread_loop_signal(mLoop, wait); }

    auto newContext(pw_properties *const props=nullptr, usize const user_data_size=0) const
    { return PwContextPtr{pw_context_new(getLoop(), props, user_data_size)}; }

    static auto Create(gsl::czstring const name, spa_dict *const props=nullptr)
    { return ThreadMainloop{pw_thread_loop_new(name, props)}; }

    friend struct MainloopUniqueLock;
};
struct MainloopUniqueLock : std::unique_lock<ThreadMainloop> {
    using std::unique_lock<ThreadMainloop>::unique_lock;
    MainloopUniqueLock& operator=(MainloopUniqueLock&&) = default;

    auto wait() const -> void
    { pw_thread_loop_wait(mutex()->mLoop); }

    template<typename Predicate>
    auto wait(Predicate done_waiting) const -> void
    { while(!done_waiting()) wait(); }
};
using MainloopLockGuard = std::lock_guard<ThreadMainloop>;


/* There's quite a mess here, but the purpose is to track active devices and
 * their default formats, so playback devices can be configured to match. The
 * device list is updated asynchronously, so it will have the latest list of
 * devices provided by the server.
 */

enum class NodeType : unsigned char {
    Sink, Source, Duplex
};

auto AsString(NodeType const type) noexcept -> std::string_view
{
    switch(type)
    {
    case NodeType::Sink: return "sink"sv;
    case NodeType::Source: return "source"sv;
    case NodeType::Duplex: return "duplex"sv;
    }
    return "<unknown>"sv;
}

/* Enumerated devices. This is updated asynchronously as the app runs, and the
 * gEventHandler thread loop must be locked when accessing the list.
 */
constexpr auto InvalidChannelConfig = gsl::narrow<DevFmtChannels>(255);
struct DeviceNode {
    uint32_t mId{};

    u64 mSerial{};
    std::string mName;
    std::string mDevName;

    NodeType mType{};
    bool mIsHeadphones{};
    bool mIs51Rear{};

    uint32_t mSampleRate{};
    DevFmtChannels mChannels{InvalidChannelConfig};

    void parseSampleRate(const spa_pod *value, bool force_update) noexcept;
    void parsePositions(const spa_pod *value, bool force_update) noexcept;
    void parseChannelCount(const spa_pod *value, bool force_update) noexcept;

    void callEvent(alc::EventType const type, std::string_view const message) const
    {
        /* Source nodes aren't recognized for playback, only Sink and Duplex
         * nodes are. All node types are recognized for capture.
         */
        if(mType != NodeType::Source)
            alc::Event(type, alc::DeviceType::Playback, message);
        alc::Event(type, alc::DeviceType::Capture, message);
    }
};
auto DefaultSinkDevice = std::string{};
auto DefaultSourceDevice = std::string{};


/* A generic PipeWire node proxy object used to track changes to sink and
 * source nodes.
 */
struct NodeProxy {
    uint32_t mId{};
    PwNodePtr mNode;
    SpaHook mListener;

    NodeProxy(uint32_t const id, PwNodePtr&& node) : mId{id}, mNode{std::move(node)}
    {
        static constexpr auto nodeEvents = std::invoke([]() -> pw_node_events
        {
            auto ret = pw_node_events{};
            ret.version = PW_VERSION_NODE_EVENTS;
            ret.info = infoCallback;
            ret.param = [](void *const object_, int const seq_, uint32_t const id_,
                uint32_t const index_, uint32_t const next_, spa_pod const *const param_) noexcept
                -> void
            { static_cast<NodeProxy*>(object_)->paramCallback(seq_, id_, index_, next_, param_); };
            return ret;
        });
        ppw_node_add_listener(mNode.get(), &mListener, &nodeEvents, this);

        /* Track changes to the enumerable and current formats (indicates the
         * default and active format, which is what we're interested in).
         */
        auto fmtids = std::to_array<uint32_t>({SPA_PARAM_EnumFormat, SPA_PARAM_Format});
        ppw_node_subscribe_params(mNode.get(), fmtids.data(), fmtids.size());
    }

    static void infoCallback(void *object, const pw_node_info *info) noexcept;
    void paramCallback(int seq, uint32_t id, uint32_t index, uint32_t next, spa_pod const *param)
        const noexcept;
};

/* A metadata proxy object used to query the default sink and source. */
struct MetadataProxy {
    uint32_t mId{};
    PwMetadataPtr mMetadata;
    SpaHook mListener;

    MetadataProxy(uint32_t const id, PwMetadataPtr&& mdata) : mId{id}, mMetadata{std::move(mdata)}
    {
        static constexpr auto metadataEvents = std::invoke([]() -> pw_metadata_events
        {
            auto ret = pw_metadata_events{};
            ret.version = PW_VERSION_METADATA_EVENTS;
            ret.property = propertyCallback;
            return ret;
        });
        ppw_metadata_add_listener(mMetadata.get(), &mListener, &metadataEvents, this);
    }

    static auto propertyCallback(void *object, uint32_t id, gsl::czstring key, gsl::czstring type,
        gsl::czstring value) noexcept -> int;
};


/* The global thread watching for global events. This particular class responds
 * to objects being added to or removed from the registry.
 */
struct EventManager {
    ThreadMainloop mLoop;
    PwContextPtr mContext;
    PwCorePtr mCore;
    PwRegistryPtr mRegistry;
    SpaHook mRegistryListener;
    SpaHook mCoreListener;

    /* A list of proxy objects watching for events about changes to objects in
     * the registry.
     */
    std::vector<std::unique_ptr<NodeProxy>> mNodeList;
    std::optional<MetadataProxy> mDefaultMetadata;

    /* Initialization handling. When init() is called, mInitSeq is set to a
     * SequenceID that marks the end of populating the registry. As objects of
     * interest are found, events to parse them are generated and mInitSeq is
     * updated with a newer ID. When mInitSeq stops being updated and the event
     * corresponding to it is reached, mInitDone will be set to true.
     */
    std::atomic<bool> mInitDone{false};
    std::atomic<bool> mHasAudio{false};
    int mInitSeq{};

    static auto AddDevice(uint32_t id) -> DeviceNode&;
    static auto FindDevice(uint32_t id) -> DeviceNode*;
    static auto FindDevice(std::string_view devname) -> DeviceNode*;
    static void RemoveDevice(uint32_t id);
    static auto GetDeviceList() noexcept { return std::span{sList}; }

    ~EventManager() { if(mLoop) mLoop.stop(); }

    auto init() -> bool;

    void kill();

    auto lock() const { return mLoop.lock(); }
    auto unlock() const { return mLoop.unlock(); }

    [[nodiscard]]
    auto initIsDone(std::memory_order const m=std::memory_order_seq_cst) const noexcept -> bool
    { return mInitDone.load(m); }

    /**
     * Waits for initialization to finish. The event manager must *NOT* be
     * locked when calling this.
     */
    void waitForInit()
    {
        if(!initIsDone(std::memory_order_acquire)) [[unlikely]]
        {
            auto const plock = MainloopUniqueLock{mLoop};
            plock.wait([this]{ return initIsDone(std::memory_order_acquire); });
        }
    }

    /**
     * Waits for audio support to be detected, or initialization to finish,
     * whichever is first. Returns true if audio support was detected. The
     * event manager must *NOT* be locked when calling this.
     */
    auto waitForAudio() -> bool
    {
        auto const plock = MainloopUniqueLock{mLoop};
        auto has_audio = false;
        plock.wait([this,&has_audio]
        {
            has_audio = mHasAudio.load(std::memory_order_acquire);
            return has_audio || initIsDone(std::memory_order_acquire);
        });
        return has_audio;
    }

    void syncInit()
    {
        /* If initialization isn't done, update the sequence ID so it won't
         * complete until after currently scheduled events.
         */
        if(!initIsDone(std::memory_order_relaxed))
            mInitSeq = ppw_core_sync(mCore.get(), PW_ID_CORE, mInitSeq);
    }

    void addCallback(uint32_t id, uint32_t permissions, gsl::czstring type, uint32_t version,
        spa_dict const *props) noexcept;

    void removeCallback(uint32_t id) noexcept;

    void coreCallback(uint32_t id, int seq) noexcept;

private:
    static inline auto sList = std::vector<DeviceNode>{};
};
using EventWatcherLockGuard = std::lock_guard<EventManager>;

auto gEventHandler = EventManager{}; /* NOLINT(cert-err58-cpp) */


auto EventManager::AddDevice(uint32_t const id) -> DeviceNode&
{
    /* If the node is already in the list, return the existing entry. */
    const auto match = std::ranges::lower_bound(sList, id, std::less{}, &DeviceNode::mId);
    if(match != sList.end() && match->mId == id)
        return *match;

    auto &n = *sList.emplace(match);
    n.mId = id;
    return n;
}

auto EventManager::FindDevice(uint32_t const id) -> DeviceNode*
{
    if(auto const match = std::ranges::find(sList, id, &DeviceNode::mId); match != sList.end())
        return std::to_address(match);
    return nullptr;
}

auto EventManager::FindDevice(std::string_view const devname) -> DeviceNode*
{
    if(auto const match = std::ranges::find(sList, devname, &DeviceNode::mDevName);
        match != sList.end())
        return std::to_address(match);
    return nullptr;
}

void EventManager::RemoveDevice(uint32_t const id)
{
    const auto end = std::ranges::remove_if(sList, [id](DeviceNode &n) noexcept -> bool
    {
        if(n.mId != id)
            return false;
        TRACE("Removing device \"{}\"", n.mDevName);
        if(gEventHandler.initIsDone(std::memory_order_relaxed))
            n.callEvent(alc::EventType::DeviceRemoved, al::format("Device removed: {}", n.mName));
        return true;
    });
    sList.erase(end.begin(), end.end());
}


constexpr auto MonoMap = std::array{
    SPA_AUDIO_CHANNEL_MONO
};
constexpr auto StereoMap = std::array{
    SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR
};
constexpr auto QuadMap = std::array{
    SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR
};
constexpr auto X51Map = std::array{
    SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
    SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR
};
constexpr auto X51RearMap = std::array{
    SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
    SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR
};
constexpr auto X61Map = std::array{
    SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
    SPA_AUDIO_CHANNEL_RC, SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR
};
constexpr auto X71Map = std::array{
    SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
    SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR, SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR
};
constexpr auto X714Map = std::array{
    SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
    SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR, SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR,
    SPA_AUDIO_CHANNEL_TFL, SPA_AUDIO_CHANNEL_TFR, SPA_AUDIO_CHANNEL_TRL, SPA_AUDIO_CHANNEL_TRR
};

/**
 * Checks if every channel in 'map1' exists in 'map0' (that is, map0 is equal
 * to or a superset of map1).
 */
auto MatchChannelMap(std::span<uint32_t const> const map0,
    std::span<spa_audio_channel const> const map1) -> bool
{
    if(map0.size() < map1.size())
        return false;

    return std::ranges::all_of(map1, [map0](spa_audio_channel const chid) -> bool
    { return std::ranges::find(map0, chid) != map0.end(); });
}

void DeviceNode::parseSampleRate(spa_pod const *value, bool const force_update) noexcept
{
    /* TODO: Can this be anything else? Long, Float, Double? */
    auto nvals = uint32_t{};
    auto choiceType = uint32_t{};
    value = spa_pod_get_values(value, &nvals, &choiceType);

    if(const auto podType = get_pod_type(value); podType != SPA_TYPE_Int)
    {
        WARN("  Unhandled sample rate POD type: {}", podType);
        return;
    }

    if(choiceType == SPA_CHOICE_Range)
    {
        if(nvals != 3)
        {
            WARN("  Unexpected SPA_CHOICE_Range count: {}", nvals);
            return;
        }
        auto srates = get_pod_body<int32_t, 3>(value);

        /* [0] is the default, [1] is the min, and [2] is the max. */
        TRACE("  sample rate: {} ({} -> {})", srates[0], srates[1], srates[2]);
        if(!mSampleRate || force_update)
            mSampleRate = gsl::narrow_cast<uint32_t>(std::clamp<int32_t>(srates[0], MinOutputRate,
                MaxOutputRate));
        return;
    }

    if(choiceType == SPA_CHOICE_Enum)
    {
        if(nvals == 0)
        {
            WARN("  Unexpected SPA_CHOICE_Enum count: {}", nvals);
            return;
        }
        auto srates = get_pod_body<int32_t>(value, nvals);

        /* [0] is the default, [1...size()-1] are available selections. */
        TRACE("{}", fmt::format("  sample rate: {} {}", srates[0], srates.subspan(1)));
        /* Pick the first rate listed that's within the allowed range (default
         * rate if possible).
         */
        for(const auto &rate : srates)
        {
            if(rate >= int32_t{MinOutputRate} && rate <= int32_t{MaxOutputRate})
            {
                if(!mSampleRate || force_update)
                    mSampleRate = gsl::narrow_cast<uint32_t>(rate);
                break;
            }
        }
        return;
    }

    if(choiceType == SPA_CHOICE_None)
    {
        if(nvals != 1)
        {
            WARN("  Unexpected SPA_CHOICE_None count: {}", nvals);
            return;
        }
        auto srates = get_pod_body<int32_t, 1>(value);

        TRACE("  sample rate: {}", srates[0]);
        if(!mSampleRate || force_update)
            mSampleRate = gsl::narrow_cast<uint32_t>(std::clamp<int32_t>(srates[0], MinOutputRate,
                MaxOutputRate));
        return;
    }

    WARN("  Unhandled sample rate choice type: {}", choiceType);
}

void DeviceNode::parsePositions(spa_pod const *value, bool const force_update) noexcept
{
    auto choiceCount = uint32_t{};
    auto choiceType = uint32_t{};
    value = spa_pod_get_values(value, &choiceCount, &choiceType);

    if(choiceType != SPA_CHOICE_None || choiceCount != 1)
    {
        ERR("  Unexpected positions choice: type={}, count={}", choiceType, choiceCount);
        return;
    }

    const auto chanmap = get_array_span<SPA_TYPE_Id>(value);
    if(chanmap.empty()) return;

    if(mChannels == InvalidChannelConfig || force_update)
    {
        mIs51Rear = false;

        if(MatchChannelMap(chanmap, X714Map))
            mChannels = DevFmtX714;
        else if(MatchChannelMap(chanmap, X71Map))
            mChannels = DevFmtX71;
        else if(MatchChannelMap(chanmap, X61Map))
            mChannels = DevFmtX61;
        else if(MatchChannelMap(chanmap, X51Map))
            mChannels = DevFmtX51;
        else if(MatchChannelMap(chanmap, X51RearMap))
        {
            mChannels = DevFmtX51;
            mIs51Rear = true;
        }
        else if(MatchChannelMap(chanmap, QuadMap))
            mChannels = DevFmtQuad;
        else if(MatchChannelMap(chanmap, StereoMap))
            mChannels = DevFmtStereo;
        else
            mChannels = DevFmtMono;
    }
    TRACE("  {} position{} for {}{}", chanmap.size(), (chanmap.size()==1)?"":"s",
        DevFmtChannelsString(mChannels), mIs51Rear?"(rear)":"");
}

void DeviceNode::parseChannelCount(spa_pod const *value, bool const force_update) noexcept
{
    /* As a fallback with just a channel count, just assume mono or stereo. */
    auto choiceCount = uint32_t{};
    auto choiceType = uint32_t{};
    value = spa_pod_get_values(value, &choiceCount, &choiceType);

    if(choiceType != SPA_CHOICE_None || choiceCount != 1)
    {
        ERR("  Unexpected positions choice: type={}, count={}", choiceType, choiceCount);
        return;
    }

    const auto chancount = get_value<SPA_TYPE_Int>(value);
    if(!chancount) return;

    if(mChannels == InvalidChannelConfig || force_update)
    {
        mIs51Rear = false;

        if(*chancount >= 2)
            mChannels = DevFmtStereo;
        else if(*chancount >= 1)
            mChannels = DevFmtMono;
    }
    TRACE("  {} channel{} for {}", *chancount, (*chancount==1)?"":"s",
        DevFmtChannelsString(mChannels));
}


[[nodiscard]] constexpr auto GetMonitorPrefix() noexcept { return "Monitor of "sv; }
[[nodiscard]] constexpr auto GetMonitorSuffix() noexcept { return ".monitor"sv; }
[[nodiscard]] constexpr auto GetAudioSinkClassName() noexcept { return "Audio/Sink"sv; }
[[nodiscard]] constexpr auto GetAudioSourceClassName() noexcept { return "Audio/Source"sv; }
[[nodiscard]] constexpr auto GetAudioDuplexClassName() noexcept { return "Audio/Duplex"sv; }
[[nodiscard]] constexpr auto GetAudioSourceVirtualClassName() noexcept
{ return "Audio/Source/Virtual"sv; }


void NodeProxy::infoCallback(void*, const pw_node_info *info) noexcept
{
    /* We only care about property changes here (media class, name/desc).
     * Format changes will automatically invoke the param callback.
     *
     * TODO: Can the media class or name/desc change without being removed and
     * readded?
     */
    if((info->change_mask&PW_NODE_CHANGE_MASK_PROPS))
    {
        /* Can this actually change? */
        auto *media_class = spa_dict_lookup(info->props, PW_KEY_MEDIA_CLASS);
        if(!media_class) [[unlikely]] return;
        const auto className = std::string_view{media_class};

        auto ntype = NodeType{};
        if(al::case_compare(className, GetAudioSinkClassName()) == 0)
            ntype = NodeType::Sink;
        else if(al::case_compare(className, GetAudioSourceClassName()) == 0
            || al::case_compare(className, GetAudioSourceVirtualClassName()) == 0)
            ntype = NodeType::Source;
        else if(al::case_compare(className, GetAudioDuplexClassName()) == 0)
            ntype = NodeType::Duplex;
        else
        {
            TRACE("Dropping device node {} which became type \"{}\"", info->id, media_class);
            EventManager::RemoveDevice(info->id);
            return;
        }

        auto *devName = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);
        auto *nodeName = spa_dict_lookup(info->props, PW_KEY_NODE_DESCRIPTION);
        if(!nodeName || !*nodeName) nodeName = spa_dict_lookup(info->props, PW_KEY_NODE_NICK);
        if(!nodeName || !*nodeName) nodeName = devName;

        auto serial_id = u64{info->id};
#ifdef PW_KEY_OBJECT_SERIAL
        if(auto *serial_str = spa_dict_lookup(info->props, PW_KEY_OBJECT_SERIAL))
        {
            errno = 0;
            auto *serial_end = gsl::zstring{};
            serial_id = u64{std::strtoull(serial_str, &serial_end, 0)};
            if(*serial_end != '\0' || errno == ERANGE)
            {
                ERR("Unexpected object serial: {}", serial_str);
                serial_id = u64{info->id};
            }
        }
#endif

        auto name = std::invoke([nodeName,info]() -> std::string
        {
            if(nodeName && *nodeName)
                return std::string{nodeName};
            return al::format("PipeWire node #{}", info->id);
        });

        auto *form_factor = spa_dict_lookup(info->props, PW_KEY_DEVICE_FORM_FACTOR);
        TRACE("Got {} device \"{}\"{}{}{}", AsString(ntype), devName ? devName : "(nil)",
            form_factor?" (":"", form_factor?form_factor:"", form_factor?")":"");
        TRACE("  \"{}\" = ID {}", name, serial_id);

        auto &node = EventManager::AddDevice(info->id);
        node.mSerial = serial_id;
        /* This method is called both to notify about a new sink/source node,
         * and update properties for the node. It's unclear what properties can
         * change for an existing node without being removed first, so err on
         * the side of caution: send a DeviceRemoved event if it had a name
         * that's being changed, and send a DeviceAdded event when the name
         * differs or it didn't have one.
         *
         * The DeviceRemoved event needs to be called before the potentially
         * new NodeType is set, so the removal event is called for the previous
         * device type, while the DeviceAdded event needs to be called after.
         *
         * This is overkill if the node type, name, and devname can't change.
         */
        auto notifyAdd = false;
        if(node.mName != name)
        {
            if(gEventHandler.initIsDone(std::memory_order_relaxed))
            {
                if(!node.mName.empty())
                {
                    const auto msg = al::format("Device removed: {}", node.mName);
                    node.callEvent(alc::EventType::DeviceRemoved, msg);
                }
                notifyAdd = true;
            }
            node.mName = std::move(name);
        }
        node.mDevName = devName ? devName : "";
        node.mType = ntype;
        node.mIsHeadphones = form_factor && (al::case_compare(form_factor, "headphones"sv) == 0
            || al::case_compare(form_factor, "headset"sv) == 0);
        if(notifyAdd)
        {
            const auto msg = al::format("Device added: {}", node.mName);
            node.callEvent(alc::EventType::DeviceAdded, msg);
        }
    }
}

void NodeProxy::paramCallback(int, uint32_t const id, uint32_t, uint32_t,
    spa_pod const *const param) const noexcept
{
    if(id == SPA_PARAM_EnumFormat || id == SPA_PARAM_Format)
    {
        auto *node = EventManager::FindDevice(mId);
        if(!node) [[unlikely]] return;

        TRACE("Device ID {} {} format{}:", node->mSerial,
            (id == SPA_PARAM_EnumFormat) ? "available" : "current",
            (id == SPA_PARAM_EnumFormat) ? "s" : "");

        const auto force_update = id == SPA_PARAM_Format;
        if(const auto *prop = spa_pod_find_prop(param, nullptr, SPA_FORMAT_AUDIO_rate))
            node->parseSampleRate(&prop->value, force_update);

        if(const auto *prop = spa_pod_find_prop(param, nullptr, SPA_FORMAT_AUDIO_position))
            node->parsePositions(&prop->value, force_update);
        else
        {
            prop = spa_pod_find_prop(param, nullptr, SPA_FORMAT_AUDIO_channels);
            if(prop) node->parseChannelCount(&prop->value, force_update);
        }
    }
}


auto MetadataProxy::propertyCallback(void*, uint32_t id, gsl::czstring key, gsl::czstring type,
    gsl::czstring value) noexcept -> int
{
    if(id != PW_ID_CORE)
        return 0;

    auto isCapture = bool{};
    if("default.audio.sink"sv == key)
        isCapture = false;
    else if("default.audio.source"sv == key)
        isCapture = true;
    else
        return 0;

    if(!type)
    {
        TRACE("Default {} device cleared", isCapture ? "capture" : "playback");
        if(!isCapture) DefaultSinkDevice.clear();
        else DefaultSourceDevice.clear();
        return 0;
    }
    if("Spa:String:JSON"sv != type)
    {
        ERR("Unexpected {} property type: {}", key, type);
        return 0;
    }

    auto it = std::array<spa_json, 2>{};
    spa_json_init(it.data(), value, strlen(value));
    if(spa_json_enter_object(&std::get<0>(it), &std::get<1>(it)) <= 0)
        return 0;

    static constexpr auto get_json_string = [](spa_json *const iter)
    {
        auto str = std::optional<std::string>{};

        const char *val{};
        const auto len = spa_json_next(iter, &val);
        if(len <= 0) return str;

        str.emplace(gsl::narrow_cast<unsigned>(len), '\0');
        if(spa_json_parse_string(val, len, str->data()) <= 0)
            str.reset();
        else while(!str->empty() && str->back() == '\0')
            str->pop_back();
        return str;
    };
    while(auto propKey = get_json_string(&std::get<1>(it)))
    {
        if("name"sv == *propKey)
        {
            auto propValue = get_json_string(&std::get<1>(it));
            if(!propValue) break;

            TRACE("Got default {} device \"{}\"", isCapture ? "capture" : "playback",
                *propValue);
            if(!isCapture && DefaultSinkDevice != *propValue)
            {
                if(gEventHandler.mInitDone.load(std::memory_order_relaxed))
                {
                    auto *entry = EventManager::FindDevice(*propValue);
                    const auto message = al::format("Default playback device changed: {}",
                        entry ? entry->mName : std::string{});
                    alc::Event(alc::EventType::DefaultDeviceChanged, alc::DeviceType::Playback,
                        message);
                }
                DefaultSinkDevice = std::move(*propValue);
            }
            else if(isCapture && DefaultSourceDevice != *propValue)
            {
                if(gEventHandler.mInitDone.load(std::memory_order_relaxed))
                {
                    auto *entry = EventManager::FindDevice(*propValue);
                    const auto message = al::format("Default capture device changed: {}",
                        entry ? entry->mName : std::string{});
                    alc::Event(alc::EventType::DefaultDeviceChanged, alc::DeviceType::Capture,
                        message);
                }
                DefaultSourceDevice = std::move(*propValue);
            }
        }
        else
        {
            const char *v{};
            if(spa_json_next(&std::get<1>(it), &v) <= 0)
                break;
        }
    }
    return 0;
}


auto EventManager::init() -> bool
{
    mLoop = ThreadMainloop::Create("PWEventThread");
    if(!mLoop)
    {
        ERR("Failed to create PipeWire event thread loop (errno: {})", errno);
        return false;
    }

    mContext = mLoop.newContext();
    if(!mContext)
    {
        ERR("Failed to create PipeWire event context (errno: {})", errno);
        return false;
    }

    mCore = PwCorePtr{pw_context_connect(mContext.get(), nullptr, 0)};
    if(!mCore)
    {
        ERR("Failed to connect PipeWire event context (errno: {})", errno);
        return false;
    }

    static constexpr auto coreEvents = std::invoke([]() -> pw_core_events
    {
        auto ret = pw_core_events{};
        ret.version = PW_VERSION_CORE_EVENTS;
        ret.done = [](void *const object, uint32_t const id, int const seq) noexcept -> void
        { static_cast<EventManager*>(object)->coreCallback(id, seq); };
        return ret;
    });
    ppw_core_add_listener(mCore.get(), &mCoreListener, &coreEvents, this);

    mRegistry = PwRegistryPtr{pw_core_get_registry(mCore.get(), PW_VERSION_REGISTRY, 0)};
    if(!mRegistry)
    {
        ERR("Failed to get PipeWire event registry (errno: {})", errno);
        return false;
    }

    static constexpr auto registryEvents = std::invoke([]() -> pw_registry_events
    {
        auto ret = pw_registry_events{};
        ret.version = PW_VERSION_REGISTRY_EVENTS;
        ret.global = [](void *const object, uint32_t const id, uint32_t const permissions,
            gsl::czstring const type, uint32_t const version, spa_dict const *const props) noexcept
            -> void
        { static_cast<EventManager*>(object)->addCallback(id, permissions, type, version, props); };
        ret.global_remove = [](void *const object, uint32_t const id) noexcept -> void
        { static_cast<EventManager*>(object)->removeCallback(id); };
        return ret;
    });
    ppw_registry_add_listener(mRegistry.get(), &mRegistryListener, &registryEvents, this);

    /* Set an initial sequence ID for initialization, to trigger after the
     * registry is first populated.
     */
    mInitSeq = ppw_core_sync(mCore.get(), PW_ID_CORE, 0);

    if(const auto res = mLoop.start())
    {
        ERR("Failed to start PipeWire event thread loop (res: {})", res);
        return false;
    }

    return true;
}

void EventManager::kill()
{
    if(!mLoop)
        return;
    mLoop.stop();

    mDefaultMetadata.reset();
    mNodeList.clear();

    mRegistryListener.remove();
    mRegistry = nullptr;

    mCoreListener.remove();
    mCore = nullptr;

    mContext = nullptr;
    mLoop = nullptr;
}

void EventManager::addCallback(uint32_t const id, uint32_t, gsl::czstring const type,
    uint32_t const version, spa_dict const *const props) noexcept
{
    /* We're only interested in interface nodes. */
    if(std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0)
    {
        auto *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        if(!media_class) return;
        const auto className = std::string_view{media_class};

        /* Specifically, audio sinks and sources (and duplexes). */
        const auto isGood = al::case_compare(className, GetAudioSinkClassName()) == 0
            || al::case_compare(className, GetAudioSourceClassName()) == 0
            || al::case_compare(className, GetAudioSourceVirtualClassName()) == 0
            || al::case_compare(className, GetAudioDuplexClassName()) == 0;
        if(!isGood)
        {
            if(!al::contains(className, "/Video"sv) && !className.starts_with("Stream/"sv))
                TRACE("Ignoring node class {}", media_class);
            return;
        }

        /* Create the proxy object. */
        auto node = PwNodePtr{static_cast<pw_node*>(pw_registry_bind(mRegistry.get(), id, type,
            version, 0))};
        if(!node)
        {
            ERR("Failed to create node proxy object (errno: {})", errno);
            return;
        }

        /* Initialize the NodeProxy to hold the node object, add it to the
         * active node list, and update the sync point.
         */
        mNodeList.emplace_back(std::make_unique<NodeProxy>(id, std::move(node)));
        syncInit();

        /* Signal any waiters that we have found a source or sink for audio
         * support.
         */
        if(!mHasAudio.exchange(true, std::memory_order_acq_rel))
            mLoop.signal(false);
    }
    else if(std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0)
    {
        auto *data_class = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if(!data_class) return;

        if("default"sv != data_class)
        {
            TRACE("Ignoring metadata \"{}\"", data_class);
            return;
        }

        if(mDefaultMetadata)
        {
            ERR("Duplicate default metadata");
            return;
        }

        auto mdata = PwMetadataPtr{static_cast<pw_metadata*>(pw_registry_bind(mRegistry.get(), id,
            type, version, 0))};
        if(!mdata)
        {
            ERR("Failed to create metadata proxy object (errno: {})", errno);
            return;
        }

        mDefaultMetadata.emplace(id, std::move(mdata));
        syncInit();
    }
}

void EventManager::removeCallback(uint32_t const id) noexcept
{
    RemoveDevice(id);

    auto node_end = std::ranges::remove_if(mNodeList, [id](NodeProxy const &node) noexcept
    { return node.mId == id; }, &std::unique_ptr<NodeProxy>::operator*);
    mNodeList.erase(node_end.begin(), node_end.end());

    if(mDefaultMetadata && mDefaultMetadata->mId == id)
        mDefaultMetadata.reset();
}

void EventManager::coreCallback(uint32_t const id, int const seq) noexcept
{
    if(id == PW_ID_CORE && seq == mInitSeq)
    {
        /* Initialization done. Remove this callback and signal anyone that may
         * be waiting.
         */
        mCoreListener.remove();

        mInitDone.store(true);
        mLoop.signal(false);
    }
}


enum use_f32p_e : bool { UseDevType=false, ForceF32Planar=true };
auto make_spa_info(DeviceBase *const device, bool const is51rear, use_f32p_e const use_f32p)
    -> spa_audio_info_raw
{
    auto info = spa_audio_info_raw{};
    if(use_f32p)
    {
        device->FmtType = DevFmtFloat;
        info.format = SPA_AUDIO_FORMAT_F32P;
    }
    else switch(device->FmtType)
    {
    case DevFmtByte: info.format = SPA_AUDIO_FORMAT_S8; break;
    case DevFmtUByte: info.format = SPA_AUDIO_FORMAT_U8; break;
    case DevFmtShort: info.format = SPA_AUDIO_FORMAT_S16; break;
    case DevFmtUShort: info.format = SPA_AUDIO_FORMAT_U16; break;
    case DevFmtInt: info.format = SPA_AUDIO_FORMAT_S32; break;
    case DevFmtUInt: info.format = SPA_AUDIO_FORMAT_U32; break;
    case DevFmtFloat: info.format = SPA_AUDIO_FORMAT_F32; break;
    }

    info.rate = device->mSampleRate;

    auto map = std::span<const spa_audio_channel>{};
    switch(device->FmtChans)
    {
    case DevFmtMono: map = MonoMap; break;
    case DevFmtStereo: map = StereoMap; break;
    case DevFmtQuad: map = QuadMap; break;
    case DevFmtX51:
        if(is51rear) map = X51RearMap;
        else map = X51Map;
        break;
    case DevFmtX61: map = X61Map; break;
    case DevFmtX71: map = X71Map; break;
    case DevFmtX714: map = X714Map; break;
    case DevFmtX3D71: map = X71Map; break;
    case DevFmtX7144:
    case DevFmtAmbi3D:
        info.flags |= SPA_AUDIO_FLAG_UNPOSITIONED;
        info.channels = device->channelsFromFmt();
        break;
    }
    if(!map.empty())
    {
        info.channels = gsl::narrow_cast<uint32_t>(map.size());
        std::ranges::copy(map, std::begin(info.position));
    }

    return info;
}

class PipeWirePlayback final : public BackendBase {
    void stateChangedCallback(pw_stream_state old, pw_stream_state state, gsl::czstring error) const noexcept;
    void ioChangedCallback(uint32_t id, void *area, uint32_t size) noexcept;
    void outputCallback() noexcept;

    void open(std::string_view name) override;
    auto reset() -> bool override;
    void start() override;
    void stop() override;
    auto getClockLatency() -> ClockLatency override;

    u64 mTargetId{PwIdAny};
    nanoseconds mTimeBase{0};
    ThreadMainloop mLoop;
    PwContextPtr mContext;
    PwCorePtr mCore;
    PwStreamPtr mStream;
    SpaHook mStreamListener;
    spa_io_rate_match *mRateMatch{};
    std::vector<void*> mChannelPtrs;

public:
    explicit PipeWirePlayback(gsl::not_null<DeviceBase*> const device) noexcept
        : BackendBase{device}
    { }
    ~PipeWirePlayback() final
    {
        /* Stop the mainloop so the stream can be properly destroyed. */
        if(mLoop) mLoop.stop();
    }
};


void PipeWirePlayback::stateChangedCallback(pw_stream_state, pw_stream_state, gsl::czstring) const
    noexcept
{ mLoop.signal(false); }

void PipeWirePlayback::ioChangedCallback(uint32_t const id, void *const area, uint32_t const size)
    noexcept
{
    switch(id)
    {
    case SPA_IO_RateMatch:
        if(size >= sizeof(spa_io_rate_match))
            mRateMatch = static_cast<spa_io_rate_match*>(area);
        else
            mRateMatch = nullptr;
        break;
    }
}

void PipeWirePlayback::outputCallback() noexcept
{
    auto *const pw_buf = pw_stream_dequeue_buffer(mStream.get());
    if(!pw_buf) [[unlikely]] return;

    auto const datas = std::span{pw_buf->buffer->datas,
        std::min(mChannelPtrs.size(), usize{pw_buf->buffer->n_datas})};
#if PW_CHECK_VERSION(0,3,49)
    /* In 0.3.49, pw_buffer::requested specifies the number of samples needed
     * by the resampler/graph for this audio update.
     */
    auto length = al::saturate_cast<uint32_t>(pw_buf->requested);
#else
    /* In 0.3.48 and earlier, spa_io_rate_match::size apparently has the number
     * of samples per update.
     */
    auto length = mRateMatch ? uint32_t{mRateMatch->size} : uint32_t{0};
#endif
    /* If no length is specified, use the device's update size as a fallback. */
    if(!length) [[unlikely]] length = mDevice->mUpdateSize;

    /* For planar formats, each datas[] seems to contain one channel, so store
     * the pointers in an array. Limit the render length in case the available
     * buffer length in any one channel is smaller than we wanted (shouldn't
     * be, but just in case).
     */
    auto chanptr_end = mChannelPtrs.begin();
    for(const auto &data : datas)
    {
        length = std::min(length, data.maxsize/uint32_t{sizeof(float)});
        *chanptr_end = data.data;
        ++chanptr_end;

        data.chunk->offset = 0;
        data.chunk->stride = sizeof(float);
        data.chunk->size   = length * uint32_t{sizeof(float)};
    }

    mDevice->renderSamples(mChannelPtrs, length);

    pw_buf->size = length;
    pw_stream_queue_buffer(mStream.get(), pw_buf);
}


void PipeWirePlayback::open(std::string_view name)
{
    static auto OpenCount = std::atomic{0u};

    auto targetid = u64{PwIdAny};
    auto devname = std::string{};
    gEventHandler.waitForInit();
    if(name.empty())
    {
        const auto evtlock = EventWatcherLockGuard{gEventHandler};
        auto&& devlist = EventManager::GetDeviceList();

        auto match = devlist.end();
        if(!DefaultSinkDevice.empty())
            match = std::ranges::find(devlist, DefaultSinkDevice, &DeviceNode::mDevName);
        if(match == devlist.end())
        {
            match = std::ranges::find_if(devlist, [](const DeviceNode &n) noexcept -> bool
            { return n.mType != NodeType::Source; });
        }
        if(match == devlist.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "No PipeWire playback device found"};

        targetid = match->mSerial;
        devname = match->mName;
    }
    else
    {
        const auto evtlock = EventWatcherLockGuard{gEventHandler};
        auto&& devlist = EventManager::GetDeviceList();

        auto match = std::ranges::find_if(devlist, [name](const DeviceNode &n) -> bool
        { return n.mType != NodeType::Source && (n.mName == name || n.mDevName == name); });
        if(match == devlist.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"{}\" not found", name};

        targetid = match->mSerial;
        devname = match->mName;
    }

    if(!mLoop)
    {
        const auto count = OpenCount.fetch_add(1u, std::memory_order_relaxed);
        const auto thread_name = al::format("ALSoftP{}", count);
        mLoop = ThreadMainloop::Create(thread_name.c_str());
        if(!mLoop)
            throw al::backend_exception{al::backend_error::DeviceError,
                "Failed to create PipeWire mainloop (errno: {})", errno};
        if(const auto res = mLoop.start())
            throw al::backend_exception{al::backend_error::DeviceError,
                "Failed to start PipeWire mainloop (res: {})", res};
    }
    auto mlock = MainloopUniqueLock{mLoop};
    mContext = mLoop.newContext(!gConfigFileName ? nullptr
        : pw_properties_new(PW_KEY_CONFIG_NAME, gConfigFileName, nullptr));
    if(!mContext)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to create PipeWire event context (errno: {})\n", errno};
    mCore = PwCorePtr{pw_context_connect(mContext.get(), nullptr, 0)};
    if(!mCore)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to connect PipeWire event context (errno: {})\n", errno};
    mlock.unlock();

    /* TODO: Ensure the target ID is still valid/usable and accepts streams. */

    mTargetId = targetid;
    if(!devname.empty())
        mDeviceName = std::move(devname);
    else
        mDeviceName = "PipeWire Output"sv;
}

auto PipeWirePlayback::reset() -> bool
{
    if(mStream)
    {
        auto looplock = MainloopLockGuard{mLoop};
        mStreamListener.remove();
        mStream = nullptr;
    }
    mRateMatch = nullptr;
    mTimeBase = mDevice->getClockTime();

    /* If connecting to a specific device, update various device parameters to
     * match its format.
     */
    auto is51rear = false;
    mDevice->Flags.reset(DirectEar);
    if(mTargetId != PwIdAny)
    {
        const auto evtlock = EventWatcherLockGuard{gEventHandler};
        auto&& devlist = EventManager::GetDeviceList();

        const auto match = std::ranges::find(devlist, mTargetId, &DeviceNode::mSerial);
        if(match != devlist.end())
        {
            if(!mDevice->Flags.test(FrequencyRequest) && match->mSampleRate > 0)
            {
                /* Scale the update size if the sample rate changes. */
                const auto scale = gsl::narrow_cast<double>(match->mSampleRate)
                    / mDevice->mSampleRate;

                /* Don't scale down power-of-two sizes unless it would be more
                 * than halfway to the next lower power-of-two. PipeWire uses
                 * powers of two updates at the graph sample rate, but seems to
                 * always round down streams' non-power-of-two update sizes. So
                 * for instance, with the default 48khz playback rate and 512
                 * update size, if the device is 44.1khz the update size would
                 * be scaled to 470 samples, which gets rounded down to 256
                 * when 512 would be closer to the requested size.
                 */
                if(scale < 0.75 && std::popcount(mDevice->mUpdateSize) == 1)
                {
                    const auto updatesize = std::round(mDevice->mUpdateSize * scale);
                    const auto buffersize = std::round(mDevice->mBufferSize * scale);

                    mDevice->mUpdateSize = gsl::narrow_cast<uint32_t>(std::clamp(updatesize, 64.0,
                        8192.0));
                    mDevice->mBufferSize = gsl::narrow_cast<uint32_t>(std::max(buffersize, 128.0));
                }
                mDevice->mSampleRate = match->mSampleRate;
            }
            if(!mDevice->Flags.test(ChannelsRequest) && match->mChannels != InvalidChannelConfig)
                mDevice->FmtChans = match->mChannels;
            if(match->mChannels == DevFmtStereo && match->mIsHeadphones)
                mDevice->Flags.set(DirectEar);
            is51rear = match->mIs51Rear;
        }
    }
    /* Force planar 32-bit float output for playback. This is what PipeWire
     * handles internally, and it's easier for us too.
     */
    auto info = spa_audio_info_raw{make_spa_info(mDevice, is51rear, ForceF32Planar)};

    auto b = PodDynamicBuilder{};
    auto params = as_const_ptr(spa_format_audio_raw_build(b.get(), SPA_PARAM_EnumFormat, &info));
    if(!params)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to set PipeWire audio format parameters"};

    /* TODO: Which properties are actually needed here? Any others that could
     * be useful?
     */
    auto&& binary = GetProcBinary();
    auto *const appname = !binary.fname.empty() ? binary.fname.c_str() : "OpenAL Soft";
    auto *props = pw_properties_new(PW_KEY_NODE_NAME, appname,
        PW_KEY_NODE_DESCRIPTION, appname,
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Game",
        PW_KEY_NODE_ALWAYS_PROCESS, "true",
        nullptr);
    if(!props)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to create PipeWire stream properties (errno: {})", errno};

    pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%u/%u", mDevice->mUpdateSize,
        mDevice->mSampleRate);
    pw_properties_setf(props, PW_KEY_NODE_RATE, "1/%u", mDevice->mSampleRate);
#ifdef PW_KEY_TARGET_OBJECT
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, std::to_string(mTargetId.c_val).c_str());
#else
    pw_properties_set(props, PW_KEY_NODE_TARGET, std::to_string(mTargetId.c_val).c_str());
#endif

    auto plock = MainloopUniqueLock{mLoop};
    /* The stream takes ownership of 'props', even in the case of failure. */
    mStream = PwStreamPtr{pw_stream_new(mCore.get(), "Playback Stream", props)};
    if(!mStream)
        throw al::backend_exception{al::backend_error::NoDevice,
            "Failed to create PipeWire stream (errno: {})", errno};

    static constexpr auto streamEvents = std::invoke([]() -> pw_stream_events
    {
        auto ret = pw_stream_events{};
        ret.version = PW_VERSION_STREAM_EVENTS;
        ret.state_changed = [](void *const data, pw_stream_state const old,
            pw_stream_state const state, gsl::czstring const error) noexcept -> void
        { static_cast<PipeWirePlayback*>(data)->stateChangedCallback(old, state, error); };
        ret.io_changed = [](void *const data, uint32_t const id, void *const area,
            uint32_t const size) noexcept -> void
        { static_cast<PipeWirePlayback*>(data)->ioChangedCallback(id, area, size); };
        ret.process = [](void *const data) noexcept -> void
        { static_cast<PipeWirePlayback*>(data)->outputCallback(); };
        return ret;
    });
    pw_stream_add_listener(mStream.get(), &mStreamListener, &streamEvents, this);

    auto flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_INACTIVE | PW_STREAM_FLAG_MAP_BUFFERS;
    if(GetConfigValueBool(mDevice->mDeviceName, "pipewire", "rt-mix", false))
        flags |= PW_STREAM_FLAG_RT_PROCESS;
    if(const auto res = pw_stream_connect(mStream.get(), PW_DIRECTION_OUTPUT, PwIdAny, flags,
        &params, 1))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Error connecting PipeWire stream (res: {})", res};

    /* Wait for the stream to become paused (ready to start streaming). */
    plock.wait([stream=mStream.get()]
    {
        auto *error = gsl::czstring{};
        auto const state = pw_stream_get_state(stream, &error);
        if(state == PW_STREAM_STATE_ERROR)
            throw al::backend_exception{al::backend_error::DeviceError,
                "Error connecting PipeWire stream: \"{}\"", error};
        return state == PW_STREAM_STATE_PAUSED;
    });

    /* TODO: Update mDevice->mUpdateSize with the stream's quantum, and
     * mDevice->mBufferSize with the total known buffering delay from the head
     * of this playback stream to the tail of the device output.
     *
     * This info is apparently not available until after the stream starts.
     */
    plock.unlock();

    mChannelPtrs.resize(mDevice->channelsFromFmt());

    setDefaultWFXChannelOrder();

    return true;
}

void PipeWirePlayback::start()
{
    auto plock = MainloopUniqueLock{mLoop};
    if(const auto res = pw_stream_set_active(mStream.get(), true))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start PipeWire stream (res: {})", res};

    /* Wait for the stream to start playing (would be nice to not, but we need
     * the actual update size which is only available after starting).
     */
    plock.wait([stream=mStream.get()]
    {
        auto *error = gsl::czstring{};
        auto const state = pw_stream_get_state(stream, &error);
        if(state == PW_STREAM_STATE_ERROR)
            throw al::backend_exception{al::backend_error::DeviceError,
                "PipeWire stream error: {}", error ? error : "(unknown)"};
        return state == PW_STREAM_STATE_STREAMING;
    });

    /* HACK: Try to work out the update size and total buffering size. There's
     * no actual query for this, so we have to work it out from the stream time
     * info, and assume it stays accurate with future updates. The stream time
     * info may also not be available right away, so we have to wait until it
     * is (up to about 2 seconds).
     */
    auto wait_count = 100;
    do {
        auto ptime = pw_time{};
        if(const auto res = pw_stream_get_time_n(mStream.get(), &ptime, sizeof(ptime)))
        {
            ERR("Failed to get PipeWire stream time (res: {})", res);
            break;
        }

        /* The rate match size is the update size for each buffer. */
        const auto updatesize = mRateMatch ? mRateMatch->size : uint32_t{0};
#if PW_CHECK_VERSION(0,3,50)
        /* Assume ptime.avail_buffers+ptime.queued_buffers is the target buffer
         * queue size.
         */
        if(ptime.rate.denom > 0 && (ptime.avail_buffers || ptime.queued_buffers) && updatesize > 0)
        {
            const auto totalbuffers = ptime.avail_buffers + ptime.queued_buffers;

            /* Ensure the delay is in sample frames. */
            const auto delay = gsl::narrow_cast<uint64_t>(ptime.delay) * mDevice->mSampleRate *
                ptime.rate.num / ptime.rate.denom;

            mDevice->mUpdateSize = updatesize;
            mDevice->mBufferSize = gsl::narrow_cast<uint32_t>(ptime.buffered + delay +
                uint64_t{totalbuffers}*updatesize);
            break;
        }
#else
        /* Prior to 0.3.50, we can only measure the delay with the update size,
         * assuming one buffer and no resample buffering.
         */
        if(ptime.rate.denom > 0 && updatesize > 0)
        {
            /* Ensure the delay is in sample frames. */
            const auto delay = gsl::narrow_cast<u64>(ptime.delay) * mDevice->mSampleRate *
                ptime.rate.num / ptime.rate.denom;

            mDevice->mUpdateSize = updatesize;
            mDevice->mBufferSize = gsl::narrow_cast<uint32_t>(delay + updatesize);
            break;
        }
#endif
        if(!--wait_count)
        {
            ERR("Timeout getting PipeWire stream buffering info");
            break;
        }

        plock.unlock();
        std::this_thread::sleep_for(milliseconds{20});
        plock.lock();
    } while(pw_stream_get_state(mStream.get(), nullptr) == PW_STREAM_STATE_STREAMING);
}

void PipeWirePlayback::stop()
{
    auto plock = MainloopUniqueLock{mLoop};
    if(int res{pw_stream_set_active(mStream.get(), false)})
        ERR("Failed to stop PipeWire stream (res: {})", res);

    /* Wait for the stream to stop playing. */
    plock.wait([stream=mStream.get()]
    { return pw_stream_get_state(stream, nullptr) != PW_STREAM_STATE_STREAMING; });
}

auto PipeWirePlayback::getClockLatency() -> ClockLatency
{
    /* Given a real-time low-latency output, this is rather complicated to get
     * accurate timing. So, here we go.
     */

    /* First, get the stream time info (tick delay, ticks played, and the
     * CLOCK_MONOTONIC time closest to when that last tick was played).
     */
    auto ptime = pw_time{};
    if(mStream)
    {
        auto looplock = MainloopLockGuard{mLoop};
        if(const auto res = pw_stream_get_time_n(mStream.get(), &ptime, sizeof(ptime)))
            ERR("Failed to get PipeWire stream time (res: {})", res);
    }

    /* Now get the mixer time and the CLOCK_MONOTONIC time atomically (i.e. the
     * monotonic clock closest to 'now', and the last mixer time at 'now').
     */
    auto mixtime = nanoseconds{};
    auto tspec = timespec{};
    auto refcount = unsigned{};
    do {
        refcount = mDevice->waitForMix();
        mixtime = mDevice->getClockTime();
        clock_gettime(CLOCK_MONOTONIC, &tspec);
        std::atomic_thread_fence(std::memory_order_acquire);
    } while(refcount != mDevice->mMixCount.load(std::memory_order_relaxed));

    /* Convert the monotonic clock, stream ticks, and stream delay to
     * nanoseconds.
     */
    auto const monoclock = nanoseconds{seconds{tspec.tv_sec} + nanoseconds{tspec.tv_nsec}};
    auto curtic = nanoseconds{};
    auto delay = nanoseconds{};
    if(ptime.rate.denom < 1) [[unlikely]]
    {
        /* If there's no stream rate, the stream hasn't had a chance to get
         * going and return time info yet. Just use dummy values.
         */
        ptime.now = monoclock.count();
        curtic = mixtime;
        delay = nanoseconds{seconds{mDevice->mBufferSize}} / mDevice->mSampleRate;
    }
    else
    {
        /* The stream gets recreated with each reset, so include the time that
         * had already passed with previous streams.
         */
        curtic = mTimeBase;
        /* More safely scale the ticks to avoid overflowing the pre-division
         * temporary as it gets larger.
         */
        curtic += seconds{ptime.ticks / ptime.rate.denom} * ptime.rate.num;
        curtic += nanoseconds{seconds{ptime.ticks%ptime.rate.denom} * ptime.rate.num} /
            ptime.rate.denom;

        /* The delay should be small enough to not worry about overflow. */
        delay = nanoseconds{seconds{ptime.delay} * ptime.rate.num} / ptime.rate.denom;
    }

    /* If the mixer time is ahead of the stream time, there's that much more
     * delay relative to the stream delay.
     */
    if(mixtime > curtic)
        delay += mixtime - curtic;
    /* Reduce the delay according to how much time has passed since the known
     * stream time. This isn't 100% accurate since the system monotonic clock
     * doesn't tick at the exact same rate as the audio device, but it should
     * be good enough with ptime.now being constantly updated every few
     * milliseconds with ptime.ticks.
     */
    delay -= monoclock - nanoseconds{ptime.now};

    /* Return the mixer time and delay. Clamp the delay to no less than 0,
     * in case timer drift got that severe.
     */
    ClockLatency ret{};
    ret.ClockTime = mixtime;
    ret.Latency = std::max(delay, nanoseconds{});

    return ret;
}


class PipeWireCapture final : public BackendBase {
    void stateChangedCallback(pw_stream_state old, pw_stream_state state, gsl::czstring error) const noexcept;
    void inputCallback() const noexcept;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    auto availableSamples() -> usize override;

    u64 mTargetId{PwIdAny};
    ThreadMainloop mLoop;
    PwContextPtr mContext;
    PwCorePtr mCore;
    PwStreamPtr mStream;
    SpaHook mStreamListener;

    RingBufferPtr<std::byte> mRing;

public:
    explicit PipeWireCapture(gsl::not_null<DeviceBase*> const device) noexcept
        : BackendBase{device}
    { }
    ~PipeWireCapture() final { if(mLoop) mLoop.stop(); }
};


void PipeWireCapture::stateChangedCallback(pw_stream_state, pw_stream_state, gsl::czstring) const noexcept
{ mLoop.signal(false); }

void PipeWireCapture::inputCallback() const noexcept
{
    auto *const pw_buf = pw_stream_dequeue_buffer(mStream.get());
    if(!pw_buf) [[unlikely]] return;

    auto const *const bufdata = pw_buf->buffer->datas;
    auto const offset = bufdata->chunk->offset % bufdata->maxsize;
    auto const input = std::span{static_cast<std::byte const*>(bufdata->data), bufdata->maxsize}
        | std::views::drop(offset) | std::views::take(bufdata->chunk->size);

    std::ignore = mRing->write(input);

    pw_stream_queue_buffer(mStream.get(), pw_buf);
}


void PipeWireCapture::open(std::string_view name)
{
    static auto OpenCount = std::atomic{0u};

    auto targetid = u64{PwIdAny};
    auto devname = std::string{};
    gEventHandler.waitForInit();
    if(name.empty())
    {
        const auto evtlock = EventWatcherLockGuard{gEventHandler};
        auto&& devlist = EventManager::GetDeviceList();

        auto match = devlist.end();
        if(!DefaultSourceDevice.empty())
            match = std::ranges::find(devlist, DefaultSourceDevice, &DeviceNode::mDevName);
        if(match == devlist.end())
        {
            match = std::ranges::find_if(devlist, [](const DeviceNode &n) noexcept -> bool
            { return n.mType != NodeType::Sink; });
        }
        if(match == devlist.end())
        {
            match = devlist.begin();
            if(match == devlist.end())
                throw al::backend_exception{al::backend_error::NoDevice,
                    "No PipeWire capture device found"};
        }

        targetid = match->mSerial;
        if(match->mType != NodeType::Sink) devname = match->mName;
        else devname = std::string{GetMonitorPrefix()}+match->mName;
    }
    else
    {
        const auto evtlock = EventWatcherLockGuard{gEventHandler};
        auto&& devlist = EventManager::GetDeviceList();
        constexpr auto prefix = GetMonitorPrefix();
        constexpr auto suffix = GetMonitorSuffix();

        auto match = std::ranges::find_if(devlist, [name](const DeviceNode &n) -> bool
        { return n.mType != NodeType::Sink && n.mName == name; });
        if(match == devlist.end() && name.starts_with(prefix))
        {
            const auto sinkname = name.substr(prefix.length());
            match = std::ranges::find_if(devlist, [sinkname](const DeviceNode &n) -> bool
            { return n.mType == NodeType::Sink && n.mName == sinkname; });
        }
        else if(match == devlist.end() && name.ends_with(suffix))
        {
            const auto sinkname = name.substr(0, name.size()-suffix.size());
            match = std::ranges::find_if(devlist, [sinkname](const DeviceNode &n) -> bool
            { return n.mType == NodeType::Sink && n.mDevName == sinkname; });
        }
        if(match == devlist.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"{}\" not found", name};

        targetid = match->mSerial;
        if(match->mType != NodeType::Sink) devname = match->mName;
        else devname = std::string{GetMonitorPrefix()}+match->mName;
    }

    if(!mLoop)
    {
        const auto count = OpenCount.fetch_add(1u, std::memory_order_relaxed);
        const auto thread_name = al::format("ALSoftC{}", count);
        mLoop = ThreadMainloop::Create(thread_name.c_str());
        if(!mLoop)
            throw al::backend_exception{al::backend_error::DeviceError,
                "Failed to create PipeWire mainloop (errno: {})", errno};
        if(int res{mLoop.start()})
            throw al::backend_exception{al::backend_error::DeviceError,
                "Failed to start PipeWire mainloop (res: {})", res};
    }
    auto mlock = MainloopUniqueLock{mLoop};
    mContext = mLoop.newContext(!gConfigFileName ? nullptr
        : pw_properties_new(PW_KEY_CONFIG_NAME, gConfigFileName, nullptr));
    if(!mContext)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to create PipeWire event context (errno: {})\n", errno};
    mCore = PwCorePtr{pw_context_connect(mContext.get(), nullptr, 0)};
    if(!mCore)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to connect PipeWire event context (errno: {})\n", errno};
    mlock.unlock();

    /* TODO: Ensure the target ID is still valid/usable and accepts streams. */

    mTargetId = targetid;
    if(!devname.empty())
        mDeviceName = std::move(devname);
    else
        mDeviceName = "PipeWire Input"sv;


    auto is51rear = false;
    if(mTargetId != PwIdAny)
    {
        const auto evtlock = EventWatcherLockGuard{gEventHandler};
        auto&& devlist = EventManager::GetDeviceList();

        auto match = std::ranges::find(devlist, mTargetId, &DeviceNode::mSerial);
        if(match != devlist.end())
            is51rear = match->mIs51Rear;
    }
    auto info = spa_audio_info_raw{make_spa_info(mDevice, is51rear, UseDevType)};

    auto b = PodDynamicBuilder{};
    auto params = as_const_ptr(spa_format_audio_raw_build(b.get(), SPA_PARAM_EnumFormat, &info));
    if(!params)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to set PipeWire audio format parameters"};

    auto&& binary = GetProcBinary();
    auto *appname = !binary.fname.empty() ? binary.fname.c_str() : "OpenAL Soft";
    pw_properties *props{pw_properties_new(
        PW_KEY_NODE_NAME, appname,
        PW_KEY_NODE_DESCRIPTION, appname,
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Game",
        PW_KEY_NODE_ALWAYS_PROCESS, "true",
        nullptr)};
    if(!props)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to create PipeWire stream properties (errno: {})", errno};

    /* We don't actually care what the latency/update size is, as long as it's
     * reasonable. Unfortunately, when unspecified PipeWire seems to default to
     * around 40ms, which isn't great. So request 20ms instead.
     */
    pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%u/%u", (mDevice->mSampleRate+25) / 50,
        mDevice->mSampleRate);
    pw_properties_setf(props, PW_KEY_NODE_RATE, "1/%u", mDevice->mSampleRate);
#ifdef PW_KEY_TARGET_OBJECT
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, std::to_string(mTargetId.c_val).c_str());
#else
    pw_properties_set(props, PW_KEY_NODE_TARGET, std::to_string(mTargetId.c_val).c_str());
#endif

    auto plock = MainloopUniqueLock{mLoop};
    mStream = PwStreamPtr{pw_stream_new(mCore.get(), "Capture Stream", props)};
    if(!mStream)
        throw al::backend_exception{al::backend_error::NoDevice,
            "Failed to create PipeWire stream (errno: {})", errno};

    static constexpr auto streamEvents = std::invoke([]() -> pw_stream_events
    {
        auto ret = pw_stream_events{};
        ret.version = PW_VERSION_STREAM_EVENTS;
        ret.state_changed = [](void *const data, pw_stream_state const old,
            pw_stream_state const state, gsl::czstring const error) noexcept -> void
        { static_cast<PipeWireCapture*>(data)->stateChangedCallback(old, state, error); };
        ret.process = [](void *const data) noexcept -> void
        { static_cast<PipeWireCapture*>(data)->inputCallback(); };
        return ret;
    });
    pw_stream_add_listener(mStream.get(), &mStreamListener, &streamEvents, this);

    static constexpr auto Flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_INACTIVE
        | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS;
    if(const auto res = pw_stream_connect(mStream.get(), PW_DIRECTION_INPUT, PwIdAny, Flags,
        &params, 1))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Error connecting PipeWire stream (res: {})", res};

    /* Wait for the stream to become paused (ready to start streaming). */
    plock.wait([stream=mStream.get()]
    {
        auto *error = gsl::czstring{};
        auto const state = pw_stream_get_state(stream, &error);
        if(state == PW_STREAM_STATE_ERROR)
            throw al::backend_exception{al::backend_error::DeviceError,
                "Error connecting PipeWire stream: \"{}\"", error};
        return state == PW_STREAM_STATE_PAUSED;
    });
    plock.unlock();

    setDefaultWFXChannelOrder();

    /* Ensure at least a 100ms capture buffer. */
    mRing = RingBuffer<std::byte>::Create(
        std::max(mDevice->mSampleRate/10u, mDevice->mBufferSize), mDevice->frameSizeFromFmt(),
        false);
}


void PipeWireCapture::start()
{
    const auto plock = MainloopUniqueLock{mLoop};
    if(const auto res = pw_stream_set_active(mStream.get(), true))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start PipeWire stream (res: {})", res};

    plock.wait([stream=mStream.get()]
    {
        auto *error = gsl::czstring{};
        auto const state = pw_stream_get_state(stream, &error);
        if(state == PW_STREAM_STATE_ERROR)
            throw al::backend_exception{al::backend_error::DeviceError,
                "PipeWire stream error: {}", error ? error : "(unknown)"};
        return state == PW_STREAM_STATE_STREAMING;
    });
}

void PipeWireCapture::stop()
{
    const auto plock = MainloopUniqueLock{mLoop};
    if(const auto res = pw_stream_set_active(mStream.get(), false))
        ERR("Failed to stop PipeWire stream (res: {})", res);

    plock.wait([stream=mStream.get()]
    { return pw_stream_get_state(stream, nullptr) != PW_STREAM_STATE_STREAMING; });
}

auto PipeWireCapture::availableSamples() -> usize
{ return mRing->readSpace(); }

void PipeWireCapture::captureSamples(std::span<std::byte> const outbuffer)
{ std::ignore = mRing->read(outbuffer); }

} // namespace


bool PipeWireBackendFactory::init()
{
    if(!pwire_load())
        return false;

    auto const version = pw_get_library_version();
    if(!check_version(version))
    {
        WARN("PipeWire version \"{}\" too old ({} or newer required)", version,
            pw_get_headers_version());
        return false;
    }
    TRACE("Found PipeWire version \"{}\" ({} or newer)", version, pw_get_headers_version());

    pw_init(nullptr, nullptr);
    if(!gEventHandler.init())
        return false;

    if(!GetConfigValueBool({}, "pipewire", "assume-audio", false)
        && !gEventHandler.waitForAudio())
    {
        gEventHandler.kill();
        /* TODO: Temporary warning, until PipeWire gets a proper way to report
         * audio support.
         */
        WARN("No audio support detected in PipeWire. See the PipeWire options in alsoftrc.sample if this is wrong.");
        return false;
    }
    return true;
}

auto PipeWireBackendFactory::querySupport(BackendType const type) -> bool
{ return type == BackendType::Playback || type == BackendType::Capture; }

auto PipeWireBackendFactory::enumerate(BackendType const type) -> std::vector<std::string>
{
    auto outnames = std::vector<std::string>{};

    gEventHandler.waitForInit();
    auto const evtlock = EventWatcherLockGuard{gEventHandler};
    auto&& devlist = EventManager::GetDeviceList();

    auto defmatch = devlist.begin();
    switch(type)
    {
    case BackendType::Playback:
        defmatch = std::ranges::find(devlist, DefaultSinkDevice, &DeviceNode::mDevName);
        if(defmatch != devlist.end())
            outnames.emplace_back(defmatch->mName);
        for(auto iter = devlist.begin();iter != devlist.end();++iter)
        {
            if(iter != defmatch && iter->mType != NodeType::Source)
                outnames.emplace_back(iter->mName);
        }
        break;
    case BackendType::Capture:
        outnames.reserve(devlist.size());
        defmatch = std::ranges::find(devlist, DefaultSourceDevice, &DeviceNode::mDevName);
        if(defmatch != devlist.end())
        {
            if(defmatch->mType == NodeType::Sink)
                outnames.emplace_back(std::string{GetMonitorPrefix()}+defmatch->mName);
            else
                outnames.emplace_back(defmatch->mName);
        }
        for(auto iter = devlist.begin();iter != devlist.end();++iter)
        {
            if(iter != defmatch)
            {
                if(iter->mType == NodeType::Sink)
                    outnames.emplace_back(std::string{GetMonitorPrefix()}+iter->mName);
                else
                    outnames.emplace_back(iter->mName);
            }
        }
        break;
    }

    return outnames;
}


auto PipeWireBackendFactory::createBackend(gsl::not_null<DeviceBase*> const device,
    BackendType const type) -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new PipeWirePlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new PipeWireCapture{device}};
    return nullptr;
}

auto PipeWireBackendFactory::getFactory() -> BackendFactory&
{
    static auto factory = PipeWireBackendFactory{};
    return factory;
}

auto PipeWireBackendFactory::queryEventSupport(alc::EventType const eventType, BackendType)
    -> alc::EventSupport
{
    switch(eventType)
    {
    case alc::EventType::DefaultDeviceChanged:
    case alc::EventType::DeviceAdded:
    case alc::EventType::DeviceRemoved:
        return alc::EventSupport::FullSupport;

    case alc::EventType::Count:
        break;
    }
    return alc::EventSupport::NoSupport;
}
