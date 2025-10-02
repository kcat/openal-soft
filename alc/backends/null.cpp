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

#include "null.h"

#include <exception>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include "althrd_setname.h"
#include "core/device.h"
#include "core/helpers.h"


namespace {

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using namespace std::string_view_literals;

[[nodiscard]] constexpr auto GetDeviceName() noexcept { return "No Output"sv; }


struct NullBackend final : public BackendBase {
    explicit NullBackend(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }

    void mixerProc() const;

    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

void NullBackend::mixerProc() const
{
    const milliseconds restTime{mDevice->mUpdateSize*1000/mDevice->mSampleRate / 2};

    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    int64_t done{0};
    auto start = std::chrono::steady_clock::now();
    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        auto now = std::chrono::steady_clock::now();

        /* This converts from nanoseconds to nanosamples, then to samples. */
        const auto avail = int64_t{std::chrono::duration_cast<seconds>((now-start)
            * mDevice->mSampleRate).count()};
        if(avail-done < mDevice->mUpdateSize)
        {
            std::this_thread::sleep_for(restTime);
            continue;
        }
        while(avail-done >= mDevice->mUpdateSize)
        {
            mDevice->renderSamples(nullptr, mDevice->mUpdateSize, 0u);
            done += mDevice->mUpdateSize;
        }

        /* For every completed second, increment the start time and reduce the
         * samples done. This prevents the difference between the start time
         * and current time from growing too large, while maintaining the
         * correct number of samples to render.
         */
        if(done >= mDevice->mSampleRate)
        {
            const auto s = seconds{done/mDevice->mSampleRate};
            start += s;
            done -= mDevice->mSampleRate*s.count();
        }
    }
}


void NullBackend::open(std::string_view name)
{
    if(name.empty())
        name = GetDeviceName();
    else if(name != GetDeviceName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};

    mDeviceName = name;
}

bool NullBackend::reset()
{
    setDefaultWFXChannelOrder();
    return true;
}

void NullBackend::start()
{
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{&NullBackend::mixerProc, this};
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: {}", e.what()};
    }
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

auto NullBackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    switch(type)
    {
    case BackendType::Playback:
        /* Include null char. */
        return std::vector{std::string{GetDeviceName()}};
    case BackendType::Capture:
        break;
    }
    return {};
}

auto NullBackendFactory::createBackend(gsl::not_null<DeviceBase*> device, BackendType type)
    -> BackendPtr
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
