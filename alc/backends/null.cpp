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


struct NullBackend final : BackendBase {
    explicit NullBackend(gsl::not_null<DeviceBase*> const device) noexcept : BackendBase{device}
    { }

    void mixerProc() const;

    void open(std::string_view name) override;
    auto reset() -> bool override;
    void start() override;
    void stop() override;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

void NullBackend::mixerProc() const
{
    auto const restTime = milliseconds{mDevice->mUpdateSize*1000/mDevice->mSampleRate / 2};

    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    auto done = 0_i64;
    auto start = std::chrono::steady_clock::now();
    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        auto now = std::chrono::steady_clock::now();

        /* This converts from nanoseconds to nanosamples, then to samples. */
        const auto avail = i64{std::chrono::duration_cast<seconds>((now-start)
            * mDevice->mSampleRate).count()};
        if(avail-done < mDevice->mUpdateSize)
        {
            std::this_thread::sleep_for(restTime);
            continue;
        }
        while(avail-done >= mDevice->mUpdateSize)
        {
            mDevice->renderSamples(nullptr, mDevice->mUpdateSize, 0u);
            done += i64{mDevice->mUpdateSize};
        }

        /* For every completed second, increment the start time and reduce the
         * samples done. This prevents the difference between the start time
         * and current time from growing too large, while maintaining the
         * correct number of samples to render.
         */
        if(done >= mDevice->mSampleRate)
        {
            const auto s = seconds{(done/i64{mDevice->mSampleRate}).c_val};
            start += s;
            done -= i64{mDevice->mSampleRate*s.count()};
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

auto NullBackend::reset() -> bool
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


auto NullBackendFactory::init() -> bool
{ return true; }

auto NullBackendFactory::querySupport(BackendType const type) -> bool
{ return (type == BackendType::Playback); }

auto NullBackendFactory::enumerate(BackendType const type) -> std::vector<std::string>
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

auto NullBackendFactory::createBackend(gsl::not_null<DeviceBase*> const device,
    BackendType const type) -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new NullBackend{device}};
    return nullptr;
}

auto NullBackendFactory::getFactory() -> BackendFactory&
{
    static NullBackendFactory factory{};
    return factory;
}
