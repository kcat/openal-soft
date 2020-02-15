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

#include "backends/jack.h"

#include <cstdlib>
#include <cstdio>
#include <memory.h>

#include <thread>
#include <functional>

#include "alcmain.h"
#include "alu.h"
#include "alconfig.h"
#include "alexcpt.h"
#include "dynload.h"
#include "ringbuffer.h"
#include "threads.h"

#include <jack/jack.h>
#include <jack/ringbuffer.h>


namespace {

constexpr ALCchar jackDevice[] = "JACK Default";


#ifdef HAVE_DYNLOAD
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
JACK_FUNCS(MAKE_FUNC);
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

ALCboolean jack_load()
{
    ALCboolean error = ALC_FALSE;

#ifdef HAVE_DYNLOAD
    if(!jack_handle)
    {
        std::string missing_funcs;

#ifdef _WIN32
#define JACKLIB "libjack.dll"
#else
#define JACKLIB "libjack.so.0"
#endif
        jack_handle = LoadLib(JACKLIB);
        if(!jack_handle)
        {
            WARN("Failed to load %s\n", JACKLIB);
            return ALC_FALSE;
        }

        error = ALC_FALSE;
#define LOAD_FUNC(f) do {                                                     \
    p##f = reinterpret_cast<decltype(p##f)>(GetSymbol(jack_handle, #f));      \
    if(p##f == nullptr) {                                                     \
        error = ALC_TRUE;                                                     \
        missing_funcs += "\n" #f;                                             \
    }                                                                         \
} while(0)
        JACK_FUNCS(LOAD_FUNC);
#undef LOAD_FUNC
        /* Optional symbols. These don't exist in all versions of JACK. */
#define LOAD_SYM(f) p##f = reinterpret_cast<decltype(p##f)>(GetSymbol(jack_handle, #f))
        LOAD_SYM(jack_error_callback);
#undef LOAD_SYM

        if(error)
        {
            WARN("Missing expected functions:%s\n", missing_funcs.c_str());
            CloseLib(jack_handle);
            jack_handle = nullptr;
        }
    }
#endif

    return !error;
}


struct JackPlayback final : public BackendBase {
    JackPlayback(ALCdevice *device) noexcept : BackendBase{device} { }
    ~JackPlayback() override;

    int process(jack_nframes_t numframes) noexcept;
    static int processC(jack_nframes_t numframes, void *arg) noexcept
    { return static_cast<JackPlayback*>(arg)->process(numframes); }

    int mixerProc();

    void open(const ALCchar *name) override;
    bool reset() override;
    bool start() override;
    void stop() override;
    ClockLatency getClockLatency() override;

    jack_client_t *mClient{nullptr};
    jack_port_t *mPort[MAX_OUTPUT_CHANNELS]{};

    std::atomic<bool> mPlaying{false};
    RingBufferPtr mRing;
    al::semaphore mSem;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    DEF_NEWDEL(JackPlayback)
};

JackPlayback::~JackPlayback()
{
    if(!mClient)
        return;

    std::for_each(std::begin(mPort), std::end(mPort),
        [this](jack_port_t *port) -> void
        { if(port) jack_port_unregister(mClient, port); }
    );
    std::fill(std::begin(mPort), std::end(mPort), nullptr);
    jack_client_close(mClient);
    mClient = nullptr;
}


int JackPlayback::process(jack_nframes_t numframes) noexcept
{
    jack_default_audio_sample_t *out[MAX_OUTPUT_CHANNELS];
    size_t numchans{0};
    for(auto port : mPort)
    {
        if(!port) break;
        out[numchans++] = static_cast<float*>(jack_port_get_buffer(port, numframes));
    }

    jack_nframes_t total{0};
    if LIKELY(mPlaying.load(std::memory_order_acquire))
    {
        auto data = mRing->getReadVector();
        jack_nframes_t todo{minu(numframes, static_cast<ALuint>(data.first.len))};
        auto write_first = [&data,numchans,todo](float *outbuf) -> float*
        {
            const float *RESTRICT in = reinterpret_cast<float*>(data.first.buf);
            auto deinterlace_input = [&in,numchans]() noexcept -> float
            {
                float ret{*in};
                in += numchans;
                return ret;
            };
            std::generate_n(outbuf, todo, deinterlace_input);
            data.first.buf += sizeof(float);
            return outbuf + todo;
        };
        std::transform(out, out+numchans, out, write_first);
        total += todo;

        todo = minu(numframes-total, static_cast<ALuint>(data.second.len));
        if(todo > 0)
        {
            auto write_second = [&data,numchans,todo](float *outbuf) -> float*
            {
                const float *RESTRICT in = reinterpret_cast<float*>(data.second.buf);
                auto deinterlace_input = [&in,numchans]() noexcept -> float
                {
                    float ret{*in};
                    in += numchans;
                    return ret;
                };
                std::generate_n(outbuf, todo, deinterlace_input);
                data.second.buf += sizeof(float);
                return outbuf + todo;
            };
            std::transform(out, out+numchans, out, write_second);
            total += todo;
        }

        mRing->readAdvance(total);
        mSem.post();
    }

    if(numframes > total)
    {
        jack_nframes_t todo{numframes - total};
        auto clear_buf = [todo](ALfloat *outbuf) -> void { std::fill_n(outbuf, todo, 0.0f); };
        std::for_each(out, out+numchans, clear_buf);
    }

    return 0;
}

int JackPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    const size_t frame_step{mDevice->channelsFromFmt()};

    std::unique_lock<JackPlayback> dlock{*this};
    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        if(mRing->writeSpace() < mDevice->UpdateSize)
        {
            dlock.unlock();
            mSem.wait();
            dlock.lock();
            continue;
        }

        auto data = mRing->getWriteVector();
        auto todo = static_cast<ALuint>(data.first.len + data.second.len);
        todo -= todo%mDevice->UpdateSize;

        ALuint len1{minu(static_cast<ALuint>(data.first.len), todo)};
        ALuint len2{minu(static_cast<ALuint>(data.second.len), todo-len1)};

        aluMixData(mDevice, data.first.buf, len1, frame_step);
        if(len2 > 0)
            aluMixData(mDevice, data.second.buf, len2, frame_step);
        mRing->writeAdvance(todo);
    }

    return 0;
}


void JackPlayback::open(const ALCchar *name)
{
    if(!name)
        name = jackDevice;
    else if(strcmp(name, jackDevice) != 0)
        throw al::backend_exception{ALC_INVALID_VALUE, "Device name \"%s\" not found", name};

    const char *client_name{"alsoft"};
    jack_status_t status;
    mClient = jack_client_open(client_name, ClientOptions, &status, nullptr);
    if(mClient == nullptr)
        throw al::backend_exception{ALC_INVALID_VALUE, "Failed to open client connection: 0x%02x",
            status};

    if((status&JackServerStarted))
        TRACE("JACK server started\n");
    if((status&JackNameNotUnique))
    {
        client_name = jack_get_client_name(mClient);
        TRACE("Client name not unique, got '%s' instead\n", client_name);
    }

    jack_set_process_callback(mClient, &JackPlayback::processC, this);

    mDevice->DeviceName = name;
}

bool JackPlayback::reset()
{
    std::for_each(std::begin(mPort), std::end(mPort),
        [this](jack_port_t *port) -> void
        { if(port) jack_port_unregister(mClient, port); }
    );
    std::fill(std::begin(mPort), std::end(mPort), nullptr);

    /* Ignore the requested buffer metrics and just keep one JACK-sized buffer
     * ready for when requested.
     */
    mDevice->Frequency = jack_get_sample_rate(mClient);
    mDevice->UpdateSize = jack_get_buffer_size(mClient);
    mDevice->BufferSize = mDevice->UpdateSize * 2;

    const char *devname{mDevice->DeviceName.c_str()};
    ALuint bufsize{ConfigValueUInt(devname, "jack", "buffer-size").value_or(mDevice->UpdateSize)};
    bufsize = maxu(NextPowerOf2(bufsize), mDevice->UpdateSize);
    mDevice->BufferSize = bufsize + mDevice->UpdateSize;

    /* Force 32-bit float output. */
    mDevice->FmtType = DevFmtFloat;

    auto ports_end = std::begin(mPort) + mDevice->channelsFromFmt();
    auto bad_port = std::find_if_not(std::begin(mPort), ports_end,
        [this](jack_port_t *&port) -> bool
        {
            std::string name{"channel_" + std::to_string(&port - mPort + 1)};
            port = jack_port_register(mClient, name.c_str(), JACK_DEFAULT_AUDIO_TYPE,
                JackPortIsOutput, 0);
            return port != nullptr;
        }
    );
    if(bad_port != ports_end)
    {
        ERR("Not enough JACK ports available for %s output\n", DevFmtChannelsString(mDevice->FmtChans));
        if(bad_port == std::begin(mPort)) return false;

        if(bad_port == std::begin(mPort)+1)
            mDevice->FmtChans = DevFmtMono;
        else
        {
            ports_end = mPort+2;
            while(bad_port != ports_end)
            {
                jack_port_unregister(mClient, *(--bad_port));
                *bad_port = nullptr;
            }
            mDevice->FmtChans = DevFmtStereo;
        }
    }

    SetDefaultChannelOrder(mDevice);

    return true;
}

bool JackPlayback::start()
{
    if(jack_activate(mClient))
    {
        ERR("Failed to activate client\n");
        return false;
    }

    const char **ports{jack_get_ports(mClient, nullptr, nullptr,
        JackPortIsPhysical|JackPortIsInput)};
    if(ports == nullptr)
    {
        ERR("No physical playback ports found\n");
        jack_deactivate(mClient);
        return false;
    }
    std::mismatch(std::begin(mPort), std::end(mPort), ports,
        [this](const jack_port_t *port, const char *pname) -> bool
        {
            if(!port) return false;
            if(!pname)
            {
                ERR("No physical playback port for \"%s\"\n", jack_port_name(port));
                return false;
            }
            if(jack_connect(mClient, jack_port_name(port), pname))
                ERR("Failed to connect output port \"%s\" to \"%s\"\n", jack_port_name(port),
                    pname);
            return true;
        });
    jack_free(ports);

    /* Reconfigure buffer metrics in case the server changed it since the reset
     * (it won't change again after jack_activate), then allocate the ring
     * buffer with the appropriate size.
     */
    mDevice->Frequency = jack_get_sample_rate(mClient);
    mDevice->UpdateSize = jack_get_buffer_size(mClient);
    mDevice->BufferSize = mDevice->UpdateSize * 2;

    const char *devname{mDevice->DeviceName.c_str()};
    ALuint bufsize{ConfigValueUInt(devname, "jack", "buffer-size").value_or(mDevice->UpdateSize)};
    bufsize = maxu(NextPowerOf2(bufsize), mDevice->UpdateSize);
    mDevice->BufferSize = bufsize + mDevice->UpdateSize;

    mRing = nullptr;
    mRing = RingBuffer::Create(bufsize, mDevice->frameSizeFromFmt(), true);

    try {
        mPlaying.store(true, std::memory_order_release);
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&JackPlayback::mixerProc), this};
        return true;
    }
    catch(std::exception& e) {
        ERR("Could not create playback thread: %s\n", e.what());
    }
    catch(...) {
    }
    jack_deactivate(mClient);
    mPlaying.store(false, std::memory_order_release);
    return false;
}

void JackPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;

    mSem.post();
    mThread.join();

    jack_deactivate(mClient);
    mPlaying.store(false, std::memory_order_release);
}


ClockLatency JackPlayback::getClockLatency()
{
    ClockLatency ret;

    std::lock_guard<JackPlayback> _{*this};
    ret.ClockTime = GetDeviceClockTime(mDevice);
    ret.Latency  = std::chrono::seconds{mRing->readSpace()};
    ret.Latency /= mDevice->Frequency;

    return ret;
}


void jack_msg_handler(const char *message)
{
    WARN("%s\n", message);
}

} // namespace

bool JackBackendFactory::init()
{
    if(!jack_load())
        return false;

    if(!GetConfigValueBool(nullptr, "jack", "spawn-server", 0))
        ClientOptions = static_cast<jack_options_t>(ClientOptions | JackNoStartServer);

    void (*old_error_cb)(const char*){&jack_error_callback ? jack_error_callback : nullptr};
    jack_set_error_function(jack_msg_handler);
    jack_status_t status;
    jack_client_t *client{jack_client_open("alsoft", ClientOptions, &status, nullptr)};
    jack_set_error_function(old_error_cb);
    if(!client)
    {
        WARN("jack_client_open() failed, 0x%02x\n", status);
        if((status&JackServerFailed) && !(ClientOptions&JackNoStartServer))
            ERR("Unable to connect to JACK server\n");
        return false;
    }

    jack_client_close(client);
    return true;
}

bool JackBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback); }

void JackBackendFactory::probe(DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case DevProbe::Playback:
            /* Includes null char. */
            outnames->append(jackDevice, sizeof(jackDevice));
            break;

        case DevProbe::Capture:
            break;
    }
}

BackendPtr JackBackendFactory::createBackend(ALCdevice *device, BackendType type)
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
