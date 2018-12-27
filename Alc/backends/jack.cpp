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

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <thread>

#include "alMain.h"
#include "alu.h"
#include "alconfig.h"
#include "ringbuffer.h"
#include "threads.h"
#include "compat.h"

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

ALCboolean jack_load(void)
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


struct ALCjackPlayback final : public ALCbackend {
    jack_client_t *mClient{nullptr};
    jack_port_t *mPort[MAX_OUTPUT_CHANNELS]{};

    RingBufferPtr mRing;
    al::semaphore mSem;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    ALCjackPlayback(ALCdevice *device) noexcept : ALCbackend{device} { }
};

int ALCjackPlayback_bufferSizeNotify(jack_nframes_t numframes, void *arg);

int ALCjackPlayback_process(jack_nframes_t numframes, void *arg);
int ALCjackPlayback_mixerProc(ALCjackPlayback *self);

void ALCjackPlayback_Construct(ALCjackPlayback *self, ALCdevice *device);
void ALCjackPlayback_Destruct(ALCjackPlayback *self);
ALCenum ALCjackPlayback_open(ALCjackPlayback *self, const ALCchar *name);
ALCboolean ALCjackPlayback_reset(ALCjackPlayback *self);
ALCboolean ALCjackPlayback_start(ALCjackPlayback *self);
void ALCjackPlayback_stop(ALCjackPlayback *self);
DECLARE_FORWARD2(ALCjackPlayback, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
DECLARE_FORWARD(ALCjackPlayback, ALCbackend, ALCuint, availableSamples)
ClockLatency ALCjackPlayback_getClockLatency(ALCjackPlayback *self);
DECLARE_FORWARD(ALCjackPlayback, ALCbackend, void, lock)
DECLARE_FORWARD(ALCjackPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCjackPlayback)

DEFINE_ALCBACKEND_VTABLE(ALCjackPlayback);


void ALCjackPlayback_Construct(ALCjackPlayback *self, ALCdevice *device)
{
    new (self) ALCjackPlayback{device};
    SET_VTABLE2(ALCjackPlayback, ALCbackend, self);
}

void ALCjackPlayback_Destruct(ALCjackPlayback *self)
{
    if(self->mClient)
    {
        std::for_each(std::begin(self->mPort), std::end(self->mPort),
            [self](jack_port_t *port) -> void
            { if(port) jack_port_unregister(self->mClient, port); }
        );
        std::fill(std::begin(self->mPort), std::end(self->mPort), nullptr);
        jack_client_close(self->mClient);
        self->mClient = nullptr;
    }

    self->~ALCjackPlayback();
}


int ALCjackPlayback_bufferSizeNotify(jack_nframes_t numframes, void *arg)
{
    auto self = static_cast<ALCjackPlayback*>(arg);
    ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};

    ALCjackPlayback_lock(self);
    device->UpdateSize = numframes;
    device->NumUpdates = 2;

    ALuint bufsize{device->UpdateSize};
    if(ConfigValueUInt(device->DeviceName.c_str(), "jack", "buffer-size", &bufsize))
        bufsize = maxu(NextPowerOf2(bufsize), device->UpdateSize);
    device->NumUpdates = (bufsize+device->UpdateSize) / device->UpdateSize;

    TRACE("%u update size x%u\n", device->UpdateSize, device->NumUpdates);

    self->mRing = nullptr;
    self->mRing = CreateRingBuffer(bufsize, device->frameSizeFromFmt(), true);
    if(!self->mRing)
    {
        ERR("Failed to reallocate ringbuffer\n");
        aluHandleDisconnect(device, "Failed to reallocate %u-sample buffer", bufsize);
    }
    ALCjackPlayback_unlock(self);
    return 0;
}


int ALCjackPlayback_process(jack_nframes_t numframes, void *arg)
{
    auto self = static_cast<ALCjackPlayback*>(arg);

    jack_default_audio_sample_t *out[MAX_OUTPUT_CHANNELS];
    ALsizei numchans{0};
    for(auto port : self->mPort)
    {
        if(!port) break;
        out[numchans++] = static_cast<float*>(jack_port_get_buffer(port, numframes));
    }

    RingBuffer *ring{self->mRing.get()};
    auto data = ring->getReadVector();
    jack_nframes_t todo{minu(numframes, data.first.len)};
    std::transform(out, out+numchans, out,
        [&data,numchans,todo](ALfloat *outbuf) -> ALfloat*
        {
            const ALfloat *RESTRICT in = reinterpret_cast<ALfloat*>(data.first.buf);
            std::generate_n(outbuf, todo,
                [&in,numchans]() noexcept -> ALfloat
                {
                    ALfloat ret{*in};
                    in += numchans;
                    return ret;
                }
            );
            data.first.buf += sizeof(ALfloat);
            return outbuf + todo;
        }
    );
    jack_nframes_t total{todo};

    todo = minu(numframes-total, data.second.len);
    if(todo > 0)
    {
        std::transform(out, out+numchans, out,
            [&data,numchans,todo](ALfloat *outbuf) -> ALfloat*
            {
                const ALfloat *RESTRICT in = reinterpret_cast<ALfloat*>(data.second.buf);
                std::generate_n(outbuf, todo,
                    [&in,numchans]() noexcept -> ALfloat
                    {
                        ALfloat ret{*in};
                        in += numchans;
                        return ret;
                    }
                );
                data.second.buf += sizeof(ALfloat);
                return outbuf + todo;
            }
        );
        total += todo;
    }

    ring->readAdvance(total);
    self->mSem.post();

    if(numframes > total)
    {
        todo = numframes-total;
        std::transform(out, out+numchans, out,
            [todo](ALfloat *outbuf) -> ALfloat*
            {
                std::fill_n(outbuf, todo, 0.0f);
                return outbuf + todo;
            }
        );
    }

    return 0;
}

int ALCjackPlayback_mixerProc(ALCjackPlayback *self)
{
    ALCdevice *device{self->mDevice};
    RingBuffer *ring{self->mRing.get()};

    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    ALCjackPlayback_lock(self);
    while(!self->mKillNow.load(std::memory_order_acquire) &&
          device->Connected.load(std::memory_order_acquire))
    {
        if(ring->writeSpace() < device->UpdateSize)
        {
            ALCjackPlayback_unlock(self);
            self->mSem.wait();
            ALCjackPlayback_lock(self);
            continue;
        }

        auto data = ring->getWriteVector();
        auto todo = static_cast<ALuint>(data.first.len + data.second.len);
        todo -= todo%device->UpdateSize;

        ALuint len1{minu(data.first.len, todo)};
        ALuint len2{minu(data.second.len, todo-len1)};

        aluMixData(device, data.first.buf, len1);
        if(len2 > 0)
            aluMixData(device, data.second.buf, len2);
        ring->writeAdvance(todo);
    }
    ALCjackPlayback_unlock(self);

    return 0;
}


ALCenum ALCjackPlayback_open(ALCjackPlayback *self, const ALCchar *name)
{
    if(!name)
        name = jackDevice;
    else if(strcmp(name, jackDevice) != 0)
        return ALC_INVALID_VALUE;

    const char *client_name{"alsoft"};
    jack_status_t status;
    self->mClient = jack_client_open(client_name, ClientOptions, &status, nullptr);
    if(self->mClient == nullptr)
    {
        ERR("jack_client_open() failed, status = 0x%02x\n", status);
        return ALC_INVALID_VALUE;
    }
    if((status&JackServerStarted))
        TRACE("JACK server started\n");
    if((status&JackNameNotUnique))
    {
        client_name = jack_get_client_name(self->mClient);
        TRACE("Client name not unique, got `%s' instead\n", client_name);
    }

    jack_set_process_callback(self->mClient, ALCjackPlayback_process, self);
    jack_set_buffer_size_callback(self->mClient, ALCjackPlayback_bufferSizeNotify, self);

    ALCdevice *device{self->mDevice};
    device->DeviceName = name;
    return ALC_NO_ERROR;
}

ALCboolean ALCjackPlayback_reset(ALCjackPlayback *self)
{
    std::for_each(std::begin(self->mPort), std::end(self->mPort),
        [self](jack_port_t *port) -> void
        { if(port) jack_port_unregister(self->mClient, port); }
    );
    std::fill(std::begin(self->mPort), std::end(self->mPort), nullptr);

    /* Ignore the requested buffer metrics and just keep one JACK-sized buffer
     * ready for when requested.
     */
    ALCdevice *device{self->mDevice};
    device->Frequency = jack_get_sample_rate(self->mClient);
    device->UpdateSize = jack_get_buffer_size(self->mClient);
    device->NumUpdates = 2;

    ALuint bufsize{device->UpdateSize};
    if(ConfigValueUInt(device->DeviceName.c_str(), "jack", "buffer-size", &bufsize))
        bufsize = maxu(NextPowerOf2(bufsize), device->UpdateSize);
    device->NumUpdates = (bufsize+device->UpdateSize) / device->UpdateSize;

    /* Force 32-bit float output. */
    device->FmtType = DevFmtFloat;

    ALsizei numchans{device->channelsFromFmt()};
    auto ports_end = std::begin(self->mPort) + numchans;
    auto bad_port = std::find_if_not(std::begin(self->mPort), ports_end,
        [self](jack_port_t *&port) -> bool
        {
            std::string name{"channel_" + std::to_string(&port - self->mPort + 1)};
            port = jack_port_register(self->mClient, name.c_str(), JACK_DEFAULT_AUDIO_TYPE,
                JackPortIsOutput, 0);
            return port != nullptr;
        }
    );
    if(bad_port != ports_end)
    {
        ERR("Not enough JACK ports available for %s output\n", DevFmtChannelsString(device->FmtChans));
        if(bad_port == std::begin(self->mPort)) return ALC_FALSE;

        if(bad_port == std::begin(self->mPort)+1)
            device->FmtChans = DevFmtMono;
        else
        {
            ports_end = self->mPort+2;
            while(bad_port != ports_end)
            {
                jack_port_unregister(self->mClient, *(--bad_port));
                *bad_port = nullptr;
            }
            device->FmtChans = DevFmtStereo;
        }
        numchans = std::distance(std::begin(self->mPort), bad_port);
    }

    self->mRing = nullptr;
    self->mRing = CreateRingBuffer(bufsize, device->frameSizeFromFmt(), true);
    if(!self->mRing)
    {
        ERR("Failed to allocate ringbuffer\n");
        return ALC_FALSE;
    }

    SetDefaultChannelOrder(device);

    return ALC_TRUE;
}

ALCboolean ALCjackPlayback_start(ALCjackPlayback *self)
{
    if(jack_activate(self->mClient))
    {
        ERR("Failed to activate client\n");
        return ALC_FALSE;
    }

    const char **ports{jack_get_ports(self->mClient, nullptr, nullptr,
        JackPortIsPhysical|JackPortIsInput)};
    if(ports == nullptr)
    {
        ERR("No physical playback ports found\n");
        jack_deactivate(self->mClient);
        return ALC_FALSE;
    }
    std::mismatch(std::begin(self->mPort), std::end(self->mPort), ports,
        [self](const jack_port_t *port, const char *pname) -> bool
        {
            if(!port) return false;
            if(!pname)
            {
                ERR("No physical playback port for \"%s\"\n", jack_port_name(port));
                return false;
            }
            if(jack_connect(self->mClient, jack_port_name(port), pname))
                ERR("Failed to connect output port \"%s\" to \"%s\"\n", jack_port_name(port),
                    pname);
            return true;
        }
    );
    jack_free(ports);

    try {
        self->mKillNow.store(false, std::memory_order_release);
        self->mThread = std::thread(ALCjackPlayback_mixerProc, self);
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Could not create playback thread: %s\n", e.what());
    }
    catch(...) {
    }
    jack_deactivate(self->mClient);
    return ALC_FALSE;
}

void ALCjackPlayback_stop(ALCjackPlayback *self)
{
    if(self->mKillNow.exchange(true, std::memory_order_acq_rel) || !self->mThread.joinable())
        return;

    self->mSem.post();
    self->mThread.join();

    jack_deactivate(self->mClient);
}


ClockLatency ALCjackPlayback_getClockLatency(ALCjackPlayback *self)
{
    ClockLatency ret;

    ALCjackPlayback_lock(self);
    ALCdevice *device{self->mDevice};
    RingBuffer *ring{self->mRing.get()};
    ret.ClockTime = GetDeviceClockTime(device);
    ret.Latency  = std::chrono::seconds{ring->readSpace()};
    ret.Latency /= device->Frequency;
    ALCjackPlayback_unlock(self);

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

void JackBackendFactory::deinit()
{
#ifdef HAVE_DYNLOAD
    if(jack_handle)
        CloseLib(jack_handle);
    jack_handle = nullptr;
#endif
}

bool JackBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback); }

void JackBackendFactory::probe(DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            /* Includes null char. */
            outnames->append(jackDevice, sizeof(jackDevice));
            break;

        case CAPTURE_DEVICE_PROBE:
            break;
    }
}

ALCbackend *JackBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCjackPlayback *backend;
        NEW_OBJ(backend, ALCjackPlayback)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    return nullptr;
}

BackendFactory &JackBackendFactory::getFactory()
{
    static JackBackendFactory factory{};
    return factory;
}
