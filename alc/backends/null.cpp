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

#include "backends/null.h"

#include <exception>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <thread>

#include "alcmain.h"
#include "alexcpt.h"
#include "almalloc.h"
#include "alu.h"
#include "logging.h"
#include "threads.h"


namespace {

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

constexpr ALCchar nullDevice[] = "No Output";


struct NullBackend final : public BackendBase {
    NullBackend(ALCdevice *device) noexcept : BackendBase{device} { }

    int mixerProc();

    void open(const ALCchar *name) override;
    bool reset() override;
    bool start() override;
    void stop() override;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    DEF_NEWDEL(NullBackend)
};

int NullBackend::mixerProc()
{
    const milliseconds restTime{mDevice->UpdateSize*1000/mDevice->Frequency / 2};

    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    int64_t done{0};
    auto start = std::chrono::steady_clock::now();
    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        auto now = std::chrono::steady_clock::now();

        /* This converts from nanoseconds to nanosamples, then to samples. */
        int64_t avail{std::chrono::duration_cast<seconds>((now-start) * mDevice->Frequency).count()};
        if(avail-done < mDevice->UpdateSize)
        {
            std::this_thread::sleep_for(restTime);
            continue;
        }
        while(avail-done >= mDevice->UpdateSize)
        {
            std::lock_guard<NullBackend> _{*this};
            aluMixData(mDevice, nullptr, mDevice->UpdateSize, 0u);
            done += mDevice->UpdateSize;
        }

        /* For every completed second, increment the start time and reduce the
         * samples done. This prevents the difference between the start time
         * and current time from growing too large, while maintaining the
         * correct number of samples to render.
         */
        if(done >= mDevice->Frequency)
        {
            seconds s{done/mDevice->Frequency};
            start += s;
            done -= mDevice->Frequency*s.count();
        }
    }

    return 0;
}


void NullBackend::open(const ALCchar *name)
{
    if(!name)
        name = nullDevice;
    else if(strcmp(name, nullDevice) != 0)
        throw al::backend_exception{ALC_INVALID_VALUE, "Device name \"%s\" not found", name};

    mDevice->DeviceName = name;
}

bool NullBackend::reset()
{
    SetDefaultWFXChannelOrder(mDevice);
    return true;
}

bool NullBackend::start()
{
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&NullBackend::mixerProc), this};
        return true;
    }
    catch(std::exception& e) {
        ERR("Failed to start mixing thread: %s\n", e.what());
    }
    catch(...) {
    }
    return false;
}

void NullBackend::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();
}

} // namespace


bool NullBackendFactory::init()
{ return true; }

bool NullBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback); }

void NullBackendFactory::probe(DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case DevProbe::Playback:
            /* Includes null char. */
            outnames->append(nullDevice, sizeof(nullDevice));
            break;
        case DevProbe::Capture:
            break;
    }
}

BackendPtr NullBackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new NullBackend{device}};
    return nullptr;
}

BackendFactory &NullBackendFactory::getFactory()
{
    static NullBackendFactory factory{};
    return factory;
}
