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

#include <cstdlib>
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#include <chrono>
#include <thread>
#include <functional>

#include "alMain.h"
#include "alu.h"
#include "compat.h"


namespace {

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

constexpr ALCchar nullDevice[] = "No Output";


struct NullBackend final : public BackendBase {
    NullBackend(ALCdevice *device) noexcept : BackendBase{device} { }

    int mixerProc();

    ALCenum open(const ALCchar *name) override;
    ALCboolean reset() override;
    ALCboolean start() override;
    void stop() override;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    static constexpr inline const char *CurrentPrefix() noexcept { return "NullBackend::"; }
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
            lock();
            aluMixData(mDevice, nullptr, mDevice->UpdateSize);
            unlock();
            done += mDevice->UpdateSize;
        }

        /* For every completed second, increment the start time and reduce the
         * samples done. This prevents the difference between the start time
         * and current time from growing too large, while maintaining the
         * correct number of samples to render.
         */
        if(done >= mDevice->Frequency && mDevice->Frequency != 0)
        {
            seconds s{done/mDevice->Frequency};
            start += s;
            done -= mDevice->Frequency*s.count();
        }
    }

    return 0;
}


ALCenum NullBackend::open(const ALCchar *name)
{
    if(!name)
        name = nullDevice;
    else if(strcmp(name, nullDevice) != 0)
        return ALC_INVALID_VALUE;

    mDevice->DeviceName = name;

    return ALC_NO_ERROR;
}

ALCboolean NullBackend::reset()
{
    SetDefaultWFXChannelOrder(mDevice);
    return ALC_TRUE;
}

ALCboolean NullBackend::start()
{
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&NullBackend::mixerProc), this};
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Failed to start mixing thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
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
        case ALL_DEVICE_PROBE:
            /* Includes null char. */
            outnames->append(nullDevice, sizeof(nullDevice));
            break;
        case CAPTURE_DEVICE_PROBE:
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
