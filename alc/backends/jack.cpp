/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
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

#include "jack.h"

#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory.h>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <thread>
#include <vector>

#include "alc/alconfig.h"
#include "alnumeric.h"
#include "althrd_setname.h"
#include "core/device.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "dynload.h"
#include "fmt/format.h"
#include "gsl/gsl"
#include "opthelpers.h"
#include "ringbuffer.h"

#include <jack/jack.h>
#include <jack/ringbuffer.h>


namespace {

using namespace std::string_literals;
using namespace std::string_view_literals;

using voidp = void*;

#if HAVE_DYNLOAD
#define JACK_FUNCS(MAGIC)          \
    MAGIC(jack_client_open);       \
    MAGIC(jack_client_close);      \
    MAGIC(jack_client_name_size);  \
    MAGIC(jack_get_client_name);   \
    MAGIC(jack_connect);           \
    MAGIC(jack_activate);          \
    MAGIC(jack_deactivate);        \
    MAGIC(jack_port_register);     \
    MAGIC(jack_port_unregister);   \
    MAGIC(jack_port_get_buffer);   \
    MAGIC(jack_port_name);         \
    MAGIC(jack_get_ports);         \
    MAGIC(jack_free);              \
    MAGIC(jack_get_sample_rate);   \
    MAGIC(jack_set_error_function); \
    MAGIC(jack_set_process_callback); \
    MAGIC(jack_set_buffer_size_callback); \
    MAGIC(jack_set_buffer_size);   \
    MAGIC(jack_get_buffer_size);

void *jack_handle;
#define MAKE_FUNC(f) decltype(f) * p##f
JACK_FUNCS(MAKE_FUNC)
decltype(jack_error_callback) * pjack_error_callback;
#undef MAKE_FUNC

#ifndef IN_IDE_PARSER
#define jack_client_open pjack_client_open
#define jack_client_close pjack_client_close
#define jack_client_name_size pjack_client_name_size
#define jack_get_client_name pjack_get_client_name
#define jack_connect pjack_connect
#define jack_activate pjack_activate
#define jack_deactivate pjack_deactivate
#define jack_port_register pjack_port_register
#define jack_port_unregister pjack_port_unregister
#define jack_port_get_buffer pjack_port_get_buffer
#define jack_port_name pjack_port_name
#define jack_get_ports pjack_get_ports
#define jack_free pjack_free
#define jack_get_sample_rate pjack_get_sample_rate
#define jack_set_error_function pjack_set_error_function
#define jack_set_process_callback pjack_set_process_callback
#define jack_set_buffer_size_callback pjack_set_buffer_size_callback
#define jack_set_buffer_size pjack_set_buffer_size
#define jack_get_buffer_size pjack_get_buffer_size
#define jack_error_callback (*pjack_error_callback)
#endif
#endif


jack_options_t ClientOptions = JackNullOption;

auto jack_load() -> bool
{
#if HAVE_DYNLOAD
    if(!jack_handle)
    {
#if defined(_WIN64)
#define JACKLIB "libjack64.dll"
#elif defined(_WIN32)
#define JACKLIB "libjack.dll"
#else
#define JACKLIB "libjack.so.0"
#endif
        if(auto libresult = LoadLib(JACKLIB))
            jack_handle = libresult.value();
        else
        {
            WARN("Failed to load {}: {}", JACKLIB, libresult.error());
            return false;
        }

        static constexpr auto load_func = [](auto *&func, const char *name) -> bool
        {
            using func_t = std::remove_reference_t<decltype(func)>;
            auto funcresult = GetSymbol(jack_handle, name);
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
#define LOAD_FUNC(f) ok &= load_func(p##f, #f)
        JACK_FUNCS(LOAD_FUNC)
#undef LOAD_FUNC
        if(!ok)
        {
            CloseLib(jack_handle);
            jack_handle = nullptr;
            return false;
        }

        /* Optional symbols. These don't exist in all versions of JACK. */
#define LOAD_SYM(f) std::ignore = load_func(p##f, #f)
        LOAD_SYM(jack_error_callback);
#undef LOAD_SYM
    }
#endif

    return true;
}


/* NOLINTNEXTLINE(*-avoid-c-arrays) */
using JackPortsPtr = std::unique_ptr<gsl::czstring[], decltype([](gsl::czstring *ptr)
    { jack_free(static_cast<void*>(ptr)); })>;

struct DeviceEntry {
    std::string mName;
    std::string mPattern;

    NOINLINE ~DeviceEntry() = default;
};

std::vector<DeviceEntry> PlaybackList;


void EnumerateDevices(jack_client_t *client, std::vector<DeviceEntry> &list)
{
    std::remove_reference_t<decltype(list)>{}.swap(list);

    if(const auto ports = JackPortsPtr{jack_get_ports(client, nullptr, JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput)})
    {
        for(auto i = 0_uz;ports[i];++i)
        {
            const auto portname = std::string_view{ports[i]};
            const auto seppos = portname.find(':');
            if(seppos == 0 || seppos >= portname.size())
                continue;

            const auto portdev = portname.substr(0, seppos);
            if(std::ranges::find(list, portdev, &DeviceEntry::mName) != list.end())
                continue;

            const auto &entry = list.emplace_back(std::string{portdev},
                fmt::format("{}:", portdev));
            TRACE("Got device: {} = {}", entry.mName, entry.mPattern);
        }
        /* There are ports but couldn't get device names from them. Add a
         * generic entry.
         */
        if(ports[0] && list.empty())
        {
            WARN("No device names found in available ports, adding a generic name.");
            list.emplace_back("JACK"s, ""s);
        }
    }

    if(auto listopt = ConfigValueStr({}, "jack", "custom-devices"))
    {
        for(auto strpos = 0_uz;strpos < listopt->size();)
        {
            auto nextpos = listopt->find(';', strpos);
            const auto seppos = listopt->find('=', strpos);
            if(seppos >= nextpos || seppos == strpos)
            {
                const auto entry = std::string_view{*listopt}.substr(strpos, nextpos-strpos);
                ERR("Invalid device entry: \"{}\"", entry);
                if(nextpos != std::string::npos) ++nextpos;
                strpos = nextpos;
                continue;
            }

            const auto name = std::string_view{*listopt}.substr(strpos, seppos-strpos);
            const auto pattern = std::string_view{*listopt}.substr(seppos+1,
                std::min(nextpos, listopt->size())-(seppos+1));

            /* Check if this custom pattern already exists in the list. */
            auto itemmatch = std::ranges::find(list, pattern, &DeviceEntry::mPattern);
            if(itemmatch != list.end())
            {
                /* If so, replace the name with this custom one. */
                itemmatch->mName = name;
                TRACE("Customized device name: {} = {}", itemmatch->mName, itemmatch->mPattern);
            }
            else
            {
                /* Otherwise, add a new device entry. */
                const auto &entry = list.emplace_back(std::string{name}, std::string{pattern});
                TRACE("Got custom device: {} = {}", entry.mName, entry.mPattern);
            }

            if(nextpos != std::string::npos) ++nextpos;
            strpos = nextpos;
        }
    }

    if(list.size() > 1)
    {
        /* Rename entries that have matching names, by appending '#2', '#3',
         * etc, as needed.
         */
        for(auto curitem = list.begin()+1;curitem != list.end();++curitem)
        {
            const auto subrange = std::span{list.begin(), curitem};
            if(std::ranges::find(subrange, curitem->mName, &DeviceEntry::mName) != subrange.end())
            {
                auto name = std::string{};
                auto count = 1_uz;
                do {
                    name = fmt::format("{} #{}", curitem->mName, ++count);
                } while(std::ranges::find(subrange, name, &DeviceEntry::mName) != subrange.end());
                curitem->mName = std::move(name);
            }
        }
    }
}


struct JackPlayback final : public BackendBase {
    explicit JackPlayback(DeviceBase *device) noexcept : BackendBase{device} { }
    ~JackPlayback() override;

    int processRt(jack_nframes_t numframes) noexcept;
    static int processRtC(jack_nframes_t numframes, void *arg) noexcept
    { return static_cast<JackPlayback*>(arg)->processRt(numframes); }

    int process(jack_nframes_t numframes) noexcept;
    static int processC(jack_nframes_t numframes, void *arg) noexcept
    { return static_cast<JackPlayback*>(arg)->process(numframes); }

    int mixerProc();

    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;
    ClockLatency getClockLatency() override;

    std::string mPortPattern;

    jack_client_t *mClient{nullptr};
    std::vector<jack_port_t*> mPort;

    std::mutex mMutex;

    std::atomic<bool> mPlaying{false};
    bool mRTMixing{false};
    RingBufferPtr<float> mRing;
    std::atomic<bool> mSignal;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

JackPlayback::~JackPlayback()
{
    if(!mClient)
        return;

    std::ranges::for_each(mPort, [this](jack_port_t *port) -> void
    { jack_port_unregister(mClient, port); });

    jack_client_close(mClient);
    mClient = nullptr;
}


int JackPlayback::processRt(jack_nframes_t numframes) noexcept
{
    auto outptrs = std::array<void*,MaxOutputChannels>{};
    std::ranges::transform(mPort, outptrs.begin(), [numframes](jack_port_t *port)
    { return jack_port_get_buffer(port, numframes); });

    const auto dst = std::span{outptrs}.first(mPort.size());
    if(mPlaying.load(std::memory_order_acquire)) [[likely]]
        mDevice->renderSamples(dst, gsl::narrow_cast<uint>(numframes));
    else
    {
        std::ranges::for_each(dst, [numframes](void *outbuf) -> void
        { std::ranges::fill(std::views::counted(static_cast<float*>(outbuf), numframes), 0.0f); });
    }

    return 0;
}


int JackPlayback::process(jack_nframes_t numframes) noexcept
{
    auto out = std::array<std::span<float>,MaxOutputChannels>{};
    std::ranges::transform(mPort, out.begin(), [numframes](jack_port_t *port)
    {
        auto *ptr = static_cast<float*>(jack_port_get_buffer(port, numframes));
        return std::span{ptr, numframes};
    });
    const auto numchans = mPort.size();

    if(mPlaying.load(std::memory_order_acquire)) [[likely]]
    {
        auto data = mRing->getReadVector();

        const auto outlen = size_t{numframes / mDevice->mUpdateSize};
        const auto updates1 = std::min(data[0].size() / mRing->getElemSize(), outlen);
        const auto updates2 = std::min(data[1].size() / mRing->getElemSize(), outlen - updates1);

        auto src = data[0];
        for(auto i = 0_uz;i < updates1;++i)
        {
            for(auto c = 0_uz;c < numchans;++c)
            {
                std::ranges::copy(src.first(mDevice->mUpdateSize), out[c].begin());
                out[c] = out[c].subspan(mDevice->mUpdateSize);
                src = src.subspan(mDevice->mUpdateSize);
            }
        }

        src = data[1];
        for(auto i = 0_uz;i < updates2;++i)
        {
            for(auto c = 0_uz;c < numchans;++c)
            {
                std::ranges::copy(src.first(mDevice->mUpdateSize), out[c].begin());
                out[c] = out[c].subspan(mDevice->mUpdateSize);
                src = src.subspan(mDevice->mUpdateSize);
            }
        }

        mRing->readAdvance(updates1 + updates2);
        mSignal.store(true, std::memory_order_release);
        mSignal.notify_all();
    }
    std::ranges::fill(out | std::views::take(numchans) | std::views::join, 0.0f);

    return 0;
}

int JackPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    const auto update_size = mDevice->mUpdateSize;
    auto outptrs = std::vector<void*>(mPort.size());

    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        if(mRing->writeSpace() == 0)
        {
            mSignal.wait(false, std::memory_order_acquire);
            mSignal.store(false, std::memory_order_release);
            continue;
        }

        auto dlock = std::lock_guard{mMutex};
        auto writevec = mRing->getWriteVector();
        std::ranges::for_each(writevec, [this,update_size,&outptrs](const std::span<float> samples)
        {
            auto bufiter = samples.begin();
            const auto updates = samples.size() / mRing->getElemSize();
            for(auto i = 0_uz;i < updates;++i)
            {
                std::ranges::generate(outptrs, [&bufiter,update_size]
                {
                    auto ret = std::to_address(bufiter);
                    std::advance(bufiter, update_size);
                    return ret;
                });
                mDevice->renderSamples(outptrs, update_size);
            }
            mRing->writeAdvance(updates);
        });
    }

    return 0;
}


void JackPlayback::open(std::string_view name)
{
    if(!mClient)
    {
        auto&& binname = GetProcBinary();
        auto *client_name = binname.fname.empty() ? "alsoft" : binname.fname.c_str();

        auto status = jack_status_t{};
        mClient = jack_client_open(client_name, ClientOptions, &status, nullptr);
        if(mClient == nullptr)
            throw al::backend_exception{al::backend_error::DeviceError,
                "Failed to open client connection: {:#02x}",
                as_unsigned(al::to_underlying(status))};
        if((status&JackServerStarted))
            TRACE("JACK server started");
        if((status&JackNameNotUnique))
        {
            client_name = jack_get_client_name(mClient);
            TRACE("Client name not unique, got '{}' instead", client_name);
        }
    }

    if(PlaybackList.empty())
        EnumerateDevices(mClient, PlaybackList);

    if(name.empty() && !PlaybackList.empty())
    {
        name = PlaybackList[0].mName;
        mPortPattern = PlaybackList[0].mPattern;
    }
    else
    {
        auto iter = std::ranges::find(PlaybackList, name, &DeviceEntry::mName);
        if(iter == PlaybackList.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"{}\" not found", name};
        mPortPattern = iter->mPattern;
    }

    mDeviceName = name;
}

bool JackPlayback::reset()
{
    std::ranges::for_each(mPort, [this](jack_port_t *port) -> void
    { jack_port_unregister(mClient, port); });
    decltype(mPort){}.swap(mPort);

    mRTMixing = GetConfigValueBool(mDevice->mDeviceName, "jack", "rt-mix", true);
    jack_set_process_callback(mClient,
        mRTMixing ? &JackPlayback::processRtC : &JackPlayback::processC, this);

    /* Ignore the requested buffer metrics and just keep one JACK-sized buffer
     * ready for when requested.
     */
    mDevice->mSampleRate = jack_get_sample_rate(mClient);
    mDevice->mUpdateSize = jack_get_buffer_size(mClient);
    if(mRTMixing)
    {
        /* Assume only two periods when directly mixing. Should try to query
         * the total port latency when connected.
         */
        mDevice->mBufferSize = mDevice->mUpdateSize * 2;
    }
    else
    {
        const auto devname = std::string_view{mDevice->mDeviceName};
        auto bufsize = ConfigValueUInt(devname, "jack", "buffer-size")
            .value_or(mDevice->mUpdateSize);
        bufsize = std::max(NextPowerOf2(bufsize), mDevice->mUpdateSize);
        mDevice->mBufferSize = bufsize + mDevice->mUpdateSize;
    }

    /* Force 32-bit float output. */
    mDevice->FmtType = DevFmtFloat;

    try {
        const auto numchans = size_t{mDevice->channelsFromFmt()};
        std::ranges::for_each(std::views::iota(0_uz, numchans), [this](const size_t idx)
        {
            auto name = fmt::format("channel_{}", idx);
            auto &newport = mPort.emplace_back();
            newport = jack_port_register(mClient, name.c_str(), JACK_DEFAULT_AUDIO_TYPE,
                JackPortIsOutput | JackPortIsTerminal, 0);
            if(!newport)
            {
                mPort.pop_back();
                throw std::runtime_error{fmt::format(
                    "Failed to register enough JACK ports for {} output",
                    DevFmtChannelsString(mDevice->FmtChans))};
            }
        });
    }
    catch(std::exception& e) {
        ERR("Exception: {}", e.what());
        if(mPort.size() >= 2)
        {
            std::ranges::for_each(mPort | std::views::drop(2), [this](jack_port_t *port)
            { jack_port_unregister(mClient, port); });
            mPort.resize(2_uz);
            mPort.shrink_to_fit();
            mDevice->FmtChans = DevFmtStereo;
        }
        else if(mPort.size() == 1)
            mDevice->FmtChans = DevFmtMono;
        else
            throw;
    }

    setDefaultChannelOrder();

    return true;
}

void JackPlayback::start()
{
    if(jack_activate(mClient))
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to activate client"};

    const auto devname = std::string_view{mDevice->mDeviceName};
    if(ConfigValueBool(devname, "jack", "connect-ports").value_or(true))
    {
        auto pnamesptr = JackPortsPtr{jack_get_ports(mClient, mPortPattern.c_str(),
            JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput)};
        if(!pnamesptr)
        {
            jack_deactivate(mClient);
            throw al::backend_exception{al::backend_error::DeviceError, "No playback ports found"};
        }

        auto *pnames_end = pnamesptr.get();
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
        while(*pnames_end) ++pnames_end;
        const auto pnames = std::span{pnamesptr.get(), pnames_end};

        std::ranges::mismatch(mPort, pnames, [this](jack_port_t *port, const char *portname)
        {
            if(!portname)
            {
                ERR("No playback port for \"{}\"", jack_port_name(port));
                return false;
            }
            if(jack_connect(mClient, jack_port_name(port), portname))
                ERR("Failed to connect output port \"{}\" to \"{}\"", jack_port_name(port),
                    portname);
            return true;
        });
    }

    /* Reconfigure buffer metrics in case the server changed it since the reset
     * (it won't change again after jack_activate), then allocate the ring
     * buffer with the appropriate size.
     */
    mDevice->mSampleRate = jack_get_sample_rate(mClient);
    mDevice->mUpdateSize = jack_get_buffer_size(mClient);
    mDevice->mBufferSize = mDevice->mUpdateSize * 2;

    mRing = nullptr;
    if(mRTMixing)
        mPlaying.store(true, std::memory_order_release);
    else
    {
        auto bufsize = ConfigValueUInt(devname, "jack", "buffer-size")
            .value_or(mDevice->mUpdateSize);
        bufsize = std::max(NextPowerOf2(bufsize), mDevice->mUpdateSize) / mDevice->mUpdateSize;
        mDevice->mBufferSize = (bufsize+1) * mDevice->mUpdateSize;

        mRing = RingBuffer<float>::Create(bufsize,
            size_t{mDevice->mUpdateSize} * mDevice->channelsFromFmt(), true);

        try {
            mPlaying.store(true, std::memory_order_release);
            mKillNow.store(false, std::memory_order_release);
            mThread = std::thread{&JackPlayback::mixerProc, this};
        }
        catch(std::exception& e) {
            jack_deactivate(mClient);
            mPlaying.store(false, std::memory_order_release);
            throw al::backend_exception{al::backend_error::DeviceError,
                "Failed to start mixing thread: {}", e.what()};
        }
    }
}

void JackPlayback::stop()
{
    if(mPlaying.load(std::memory_order_acquire))
    {
        mKillNow.store(true, std::memory_order_release);
        if(mThread.joinable())
        {
            mSignal.store(true, std::memory_order_release);
            mSignal.notify_all();
            mThread.join();
        }

        jack_deactivate(mClient);
        mPlaying.store(false, std::memory_order_release);
    }
}


ClockLatency JackPlayback::getClockLatency()
{
    auto dlock = std::lock_guard{mMutex};

    auto ret = ClockLatency{};
    ret.ClockTime = mDevice->getClockTime();
    ret.Latency  = std::chrono::seconds{mRing ? mRing->readSpace() : 1_uz} * mDevice->mUpdateSize;
    ret.Latency /= mDevice->mSampleRate;

    return ret;
}


void jack_msg_handler(const char *message)
{
    WARN("{}", message);
}

} // namespace

bool JackBackendFactory::init()
{
    if(!jack_load())
        return false;

    if(!GetConfigValueBool({}, "jack", "spawn-server", false))
        ClientOptions = gsl::narrow_cast<jack_options_t>(ClientOptions | JackNoStartServer);

    auto&& binname = GetProcBinary();
    auto *client_name = binname.fname.empty() ? "alsoft" : binname.fname.c_str();

    void (*old_error_cb)(const char*){&jack_error_callback ? jack_error_callback : nullptr};
    jack_set_error_function(jack_msg_handler);
    auto status = jack_status_t{};
    auto *client = jack_client_open(client_name, ClientOptions, &status, nullptr);
    jack_set_error_function(old_error_cb);
    if(!client)
    {
        WARN("jack_client_open() failed, {:#02x}", as_unsigned(al::to_underlying(status)));
        if((status&JackServerFailed) && !(ClientOptions&JackNoStartServer))
            ERR("Unable to connect to JACK server");
        return false;
    }

    jack_client_close(client);
    return true;
}

bool JackBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback); }

auto JackBackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    auto outnames = std::vector<std::string>{};

    auto&& binname = GetProcBinary();
    auto *client_name = binname.fname.empty() ? "alsoft" : binname.fname.c_str();
    auto status = jack_status_t{};
    switch(type)
    {
    case BackendType::Playback:
        if(auto *client = jack_client_open(client_name, ClientOptions, &status, nullptr))
        {
            EnumerateDevices(client, PlaybackList);
            jack_client_close(client);
        }
        else
            WARN("jack_client_open() failed, {:#02x}", as_unsigned(al::to_underlying(status)));

        outnames.reserve(PlaybackList.size());
        std::ranges::transform(PlaybackList, std::back_inserter(outnames), &DeviceEntry::mName);
        break;

    case BackendType::Capture:
        break;
    }

    return outnames;
}

BackendPtr JackBackendFactory::createBackend(DeviceBase *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new JackPlayback{device}};
    return nullptr;
}

BackendFactory &JackBackendFactory::getFactory()
{
    static JackBackendFactory factory{};
    return factory;
}
