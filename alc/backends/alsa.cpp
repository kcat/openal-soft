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

#include "alsa.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "alc/alconfig.h"
#include "alnumeric.h"
#include "althrd_setname.h"
#include "core/device.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "dynload.h"
#include "gsl/gsl"
#include "ringbuffer.h"

#include <alsa/asoundlib.h>


namespace {

using namespace std::string_literals;
using namespace std::string_view_literals;

[[nodiscard]] constexpr auto GetDefaultName() noexcept { return "ALSA Default"sv; }


#if HAVE_DYNLOAD
#define ALSA_FUNCS(MAGIC)                                                     \
    MAGIC(snd_strerror);                                                      \
    MAGIC(snd_pcm_open);                                                      \
    MAGIC(snd_pcm_close);                                                     \
    MAGIC(snd_pcm_nonblock);                                                  \
    MAGIC(snd_pcm_frames_to_bytes);                                           \
    MAGIC(snd_pcm_bytes_to_frames);                                           \
    MAGIC(snd_pcm_hw_params_malloc);                                          \
    MAGIC(snd_pcm_hw_params_free);                                            \
    MAGIC(snd_pcm_hw_params_any);                                             \
    MAGIC(snd_pcm_hw_params_current);                                         \
    MAGIC(snd_pcm_hw_params_get_access);                                      \
    MAGIC(snd_pcm_hw_params_get_buffer_size);                                 \
    MAGIC(snd_pcm_hw_params_get_buffer_time_min);                             \
    MAGIC(snd_pcm_hw_params_get_buffer_time_max);                             \
    MAGIC(snd_pcm_hw_params_get_channels);                                    \
    MAGIC(snd_pcm_hw_params_get_period_size);                                 \
    MAGIC(snd_pcm_hw_params_get_period_time_max);                             \
    MAGIC(snd_pcm_hw_params_get_period_time_min);                             \
    MAGIC(snd_pcm_hw_params_get_periods);                                     \
    MAGIC(snd_pcm_hw_params_set_access);                                      \
    MAGIC(snd_pcm_hw_params_set_buffer_size_min);                             \
    MAGIC(snd_pcm_hw_params_set_buffer_size_near);                            \
    MAGIC(snd_pcm_hw_params_set_buffer_time_near);                            \
    MAGIC(snd_pcm_hw_params_set_channels);                                    \
    MAGIC(snd_pcm_hw_params_set_channels_near);                               \
    MAGIC(snd_pcm_hw_params_set_format);                                      \
    MAGIC(snd_pcm_hw_params_set_period_time_near);                            \
    MAGIC(snd_pcm_hw_params_set_period_size_near);                            \
    MAGIC(snd_pcm_hw_params_set_periods_near);                                \
    MAGIC(snd_pcm_hw_params_set_rate_near);                                   \
    MAGIC(snd_pcm_hw_params_set_rate);                                        \
    MAGIC(snd_pcm_hw_params_set_rate_resample);                               \
    MAGIC(snd_pcm_hw_params_test_format);                                     \
    MAGIC(snd_pcm_hw_params_test_channels);                                   \
    MAGIC(snd_pcm_hw_params);                                                 \
    MAGIC(snd_pcm_sw_params);                                                 \
    MAGIC(snd_pcm_sw_params_current);                                         \
    MAGIC(snd_pcm_sw_params_free);                                            \
    MAGIC(snd_pcm_sw_params_malloc);                                          \
    MAGIC(snd_pcm_sw_params_set_avail_min);                                   \
    MAGIC(snd_pcm_sw_params_set_stop_threshold);                              \
    MAGIC(snd_pcm_prepare);                                                   \
    MAGIC(snd_pcm_start);                                                     \
    MAGIC(snd_pcm_resume);                                                    \
    MAGIC(snd_pcm_reset);                                                     \
    MAGIC(snd_pcm_wait);                                                      \
    MAGIC(snd_pcm_delay);                                                     \
    MAGIC(snd_pcm_state);                                                     \
    MAGIC(snd_pcm_avail_update);                                              \
    MAGIC(snd_pcm_mmap_begin);                                                \
    MAGIC(snd_pcm_mmap_commit);                                               \
    MAGIC(snd_pcm_readi);                                                     \
    MAGIC(snd_pcm_writei);                                                    \
    MAGIC(snd_pcm_drain);                                                     \
    MAGIC(snd_pcm_drop);                                                      \
    MAGIC(snd_pcm_recover);                                                   \
    MAGIC(snd_pcm_info_malloc);                                               \
    MAGIC(snd_pcm_info_free);                                                 \
    MAGIC(snd_pcm_info_set_device);                                           \
    MAGIC(snd_pcm_info_set_subdevice);                                        \
    MAGIC(snd_pcm_info_set_stream);                                           \
    MAGIC(snd_pcm_info_get_name);                                             \
    MAGIC(snd_ctl_pcm_next_device);                                           \
    MAGIC(snd_ctl_pcm_info);                                                  \
    MAGIC(snd_ctl_open);                                                      \
    MAGIC(snd_ctl_close);                                                     \
    MAGIC(snd_ctl_card_info_malloc);                                          \
    MAGIC(snd_ctl_card_info_free);                                            \
    MAGIC(snd_ctl_card_info);                                                 \
    MAGIC(snd_ctl_card_info_get_name);                                        \
    MAGIC(snd_ctl_card_info_get_id);                                          \
    MAGIC(snd_card_next);                                                     \
    MAGIC(snd_config_update_free_global)

void *alsa_handle;
#define MAKE_FUNC(f) decltype(f) * p##f
ALSA_FUNCS(MAKE_FUNC);
#undef MAKE_FUNC

#ifndef IN_IDE_PARSER
#define snd_strerror psnd_strerror
#define snd_pcm_open psnd_pcm_open
#define snd_pcm_close psnd_pcm_close
#define snd_pcm_nonblock psnd_pcm_nonblock
#define snd_pcm_frames_to_bytes psnd_pcm_frames_to_bytes
#define snd_pcm_bytes_to_frames psnd_pcm_bytes_to_frames
#define snd_pcm_hw_params_malloc psnd_pcm_hw_params_malloc
#define snd_pcm_hw_params_free psnd_pcm_hw_params_free
#define snd_pcm_hw_params_any psnd_pcm_hw_params_any
#define snd_pcm_hw_params_current psnd_pcm_hw_params_current
#define snd_pcm_hw_params_set_access psnd_pcm_hw_params_set_access
#define snd_pcm_hw_params_set_format psnd_pcm_hw_params_set_format
#define snd_pcm_hw_params_set_channels psnd_pcm_hw_params_set_channels
#define snd_pcm_hw_params_set_channels_near psnd_pcm_hw_params_set_channels_near
#define snd_pcm_hw_params_set_periods_near psnd_pcm_hw_params_set_periods_near
#define snd_pcm_hw_params_set_rate_near psnd_pcm_hw_params_set_rate_near
#define snd_pcm_hw_params_set_rate psnd_pcm_hw_params_set_rate
#define snd_pcm_hw_params_set_rate_resample psnd_pcm_hw_params_set_rate_resample
#define snd_pcm_hw_params_set_buffer_time_near psnd_pcm_hw_params_set_buffer_time_near
#define snd_pcm_hw_params_set_period_time_near psnd_pcm_hw_params_set_period_time_near
#define snd_pcm_hw_params_set_buffer_size_near psnd_pcm_hw_params_set_buffer_size_near
#define snd_pcm_hw_params_set_period_size_near psnd_pcm_hw_params_set_period_size_near
#define snd_pcm_hw_params_set_buffer_size_min psnd_pcm_hw_params_set_buffer_size_min
#define snd_pcm_hw_params_get_buffer_time_min psnd_pcm_hw_params_get_buffer_time_min
#define snd_pcm_hw_params_get_buffer_time_max psnd_pcm_hw_params_get_buffer_time_max
#define snd_pcm_hw_params_get_period_time_min psnd_pcm_hw_params_get_period_time_min
#define snd_pcm_hw_params_get_period_time_max psnd_pcm_hw_params_get_period_time_max
#define snd_pcm_hw_params_get_buffer_size psnd_pcm_hw_params_get_buffer_size
#define snd_pcm_hw_params_get_period_size psnd_pcm_hw_params_get_period_size
#define snd_pcm_hw_params_get_access psnd_pcm_hw_params_get_access
#define snd_pcm_hw_params_get_periods psnd_pcm_hw_params_get_periods
#define snd_pcm_hw_params_get_channels psnd_pcm_hw_params_get_channels
#define snd_pcm_hw_params_test_format psnd_pcm_hw_params_test_format
#define snd_pcm_hw_params_test_channels psnd_pcm_hw_params_test_channels
#define snd_pcm_hw_params psnd_pcm_hw_params
#define snd_pcm_sw_params_malloc psnd_pcm_sw_params_malloc
#define snd_pcm_sw_params_current psnd_pcm_sw_params_current
#define snd_pcm_sw_params_set_avail_min psnd_pcm_sw_params_set_avail_min
#define snd_pcm_sw_params_set_stop_threshold psnd_pcm_sw_params_set_stop_threshold
#define snd_pcm_sw_params psnd_pcm_sw_params
#define snd_pcm_sw_params_free psnd_pcm_sw_params_free
#define snd_pcm_prepare psnd_pcm_prepare
#define snd_pcm_start psnd_pcm_start
#define snd_pcm_resume psnd_pcm_resume
#define snd_pcm_reset psnd_pcm_reset
#define snd_pcm_wait psnd_pcm_wait
#define snd_pcm_delay psnd_pcm_delay
#define snd_pcm_state psnd_pcm_state
#define snd_pcm_avail_update psnd_pcm_avail_update
#define snd_pcm_mmap_begin psnd_pcm_mmap_begin
#define snd_pcm_mmap_commit psnd_pcm_mmap_commit
#define snd_pcm_readi psnd_pcm_readi
#define snd_pcm_writei psnd_pcm_writei
#define snd_pcm_drain psnd_pcm_drain
#define snd_pcm_drop psnd_pcm_drop
#define snd_pcm_recover psnd_pcm_recover
#define snd_pcm_info_malloc psnd_pcm_info_malloc
#define snd_pcm_info_free psnd_pcm_info_free
#define snd_pcm_info_set_device psnd_pcm_info_set_device
#define snd_pcm_info_set_subdevice psnd_pcm_info_set_subdevice
#define snd_pcm_info_set_stream psnd_pcm_info_set_stream
#define snd_pcm_info_get_name psnd_pcm_info_get_name
#define snd_ctl_pcm_next_device psnd_ctl_pcm_next_device
#define snd_ctl_pcm_info psnd_ctl_pcm_info
#define snd_ctl_open psnd_ctl_open
#define snd_ctl_close psnd_ctl_close
#define snd_ctl_card_info_malloc psnd_ctl_card_info_malloc
#define snd_ctl_card_info_free psnd_ctl_card_info_free
#define snd_ctl_card_info psnd_ctl_card_info
#define snd_ctl_card_info_get_name psnd_ctl_card_info_get_name
#define snd_ctl_card_info_get_id psnd_ctl_card_info_get_id
#define snd_card_next psnd_card_next
#define snd_config_update_free_global psnd_config_update_free_global
#endif
#endif


using HwParamsPtr = std::unique_ptr<snd_pcm_hw_params_t, decltype([](snd_pcm_hw_params_t *ptr)
    { snd_pcm_hw_params_free(ptr); })>;
auto CreateHwParams() -> HwParamsPtr
{
    auto ret = HwParamsPtr{};
    snd_pcm_hw_params_malloc(al::out_ptr(ret));
    return ret;
}

using SwParamsPtr = std::unique_ptr<snd_pcm_sw_params_t, decltype([](snd_pcm_sw_params_t *ptr)
    { snd_pcm_sw_params_free(ptr); })>;
auto CreateSwParams() -> SwParamsPtr
{
    auto ret = SwParamsPtr{};
    snd_pcm_sw_params_malloc(al::out_ptr(ret));
    return ret;
}

using CtlCardInfoPtr = std::unique_ptr<snd_ctl_card_info_t, decltype([](snd_ctl_card_info_t *ptr)
    { snd_ctl_card_info_free(ptr); })>;
auto CreateCtlCardInfo() -> CtlCardInfoPtr
{
    auto ret = CtlCardInfoPtr{};
    snd_ctl_card_info_malloc(al::out_ptr(ret));
    return ret;
}

using PcmInfoPtr = std::unique_ptr<snd_pcm_info_t, decltype([](snd_pcm_info_t *ptr)
    { snd_pcm_info_free(ptr); })>;
auto CreatePcmInfo() -> PcmInfoPtr
{
    auto ret = PcmInfoPtr{};
    snd_pcm_info_malloc(al::out_ptr(ret));
    return ret;
}

using SndCtlPtr = std::unique_ptr<snd_ctl_t, decltype([](snd_ctl_t *ptr) { snd_ctl_close(ptr); })>;


struct DevMap {
    std::string name;
    std::string device_name;
};

std::vector<DevMap> PlaybackDevices;
std::vector<DevMap> CaptureDevices;


auto prefix_name(snd_pcm_stream_t stream) noexcept -> std::string_view
{
    if(stream == SND_PCM_STREAM_PLAYBACK)
        return "device-prefix"sv;
    return "capture-prefix"sv;
}


auto probe_devices(snd_pcm_stream_t stream) -> std::vector<DevMap>
{
    auto devlist = std::vector<DevMap>{};

    const auto info = CreateCtlCardInfo();
    const auto pcminfo = CreatePcmInfo();

    auto defname = ConfigValueStr({}, "alsa"sv,
        (stream == SND_PCM_STREAM_PLAYBACK) ? "device"sv : "capture"sv);
    devlist.emplace_back(std::string{GetDefaultName()}, defname ? *defname : "default"s);

    if(auto customdevs = ConfigValueStr({}, "alsa"sv,
        (stream == SND_PCM_STREAM_PLAYBACK) ? "custom-devices"sv : "custom-captures"sv))
    {
        auto curpos = customdevs->find_first_not_of(';');
        while(curpos < customdevs->length())
        {
            auto nextpos = customdevs->find(';', curpos+1);
            const auto seppos = customdevs->find('=', curpos);
            if(seppos == curpos || seppos >= nextpos)
            {
                const auto spec = std::string_view{*customdevs}.substr(curpos, nextpos-curpos);
                ERR("Invalid ALSA device specification \"{}\"", spec);
            }
            else
            {
                const auto &entry = devlist.emplace_back(customdevs->substr(curpos, seppos-curpos),
                    customdevs->substr(seppos+1, nextpos-seppos-1));
                TRACE("Got device \"{}\", \"{}\"", entry.name, entry.device_name);
            }

            if(nextpos < customdevs->length())
                nextpos = customdevs->find_first_not_of(';', nextpos+1);
            curpos = nextpos;
        }
    }

    const auto main_prefix = std::string{ConfigValueStr({}, "alsa"sv, prefix_name(stream))
        .value_or("plughw:")};

    auto card = -1;
    auto err = snd_card_next(&card);
    for(;err >= 0 && card >= 0;err = snd_card_next(&card))
    {
        auto handle = SndCtlPtr{};
        err = snd_ctl_open(al::out_ptr(handle), std::format("hw:{}", card).c_str(), 0);
        if(err < 0)
        {
            ERR("control open (hw:{}): {}", card, snd_strerror(err));
            continue;
        }
        err = snd_ctl_card_info(handle.get(), info.get());
        if(err < 0)
        {
            ERR("control hardware info (hw:{}): {}", card, snd_strerror(err));
            continue;
        }

        const auto *cardname = snd_ctl_card_info_get_name(info.get());
        const auto *cardid = snd_ctl_card_info_get_id(info.get());
        auto name = std::format("{}-{}", prefix_name(stream), cardid);
        const auto card_prefix = std::string{ConfigValueStr({}, "alsa"sv, name)
            .value_or(main_prefix)};

        auto dev = -1;
        while(true)
        {
            if(snd_ctl_pcm_next_device(handle.get(), &dev) < 0)
                ERR("snd_ctl_pcm_next_device failed");
            if(dev < 0) break;

            snd_pcm_info_set_device(pcminfo.get(), gsl::narrow_cast<uint>(dev));
            snd_pcm_info_set_subdevice(pcminfo.get(), 0);
            snd_pcm_info_set_stream(pcminfo.get(), stream);
            err = snd_ctl_pcm_info(handle.get(), pcminfo.get());
            if(err < 0)
            {
                if(err != -ENOENT)
                    ERR("control digital audio info (hw:{}): {}", card, snd_strerror(err));
                continue;
            }

            /* "prefix-cardid-dev" */
            name = std::format("{}-{}-{}", prefix_name(stream), cardid, dev);
            const auto device_prefix = std::string{ConfigValueStr({}, "alsa"sv, name)
                .value_or(card_prefix)};

            /* "CardName, PcmName (CARD=cardid,DEV=dev)" */
            name = std::format("{}, {} (CARD={},DEV={})", cardname,
                snd_pcm_info_get_name(pcminfo.get()), cardid, dev);

            /* "devprefixCARD=cardid,DEV=dev" */
            auto device = std::format("{}CARD={},DEV={}", device_prefix, cardid, dev);
            
            const auto &entry = devlist.emplace_back(std::move(name), std::move(device));
            TRACE("Got device \"{}\", \"{}\"", entry.name, entry.device_name);
        }
    }
    if(err < 0)
        ERR("snd_card_next failed: {}", snd_strerror(err));

    return devlist;
}


auto verify_state(snd_pcm_t *handle) -> int
{
    const auto state = snd_pcm_state(handle);
    switch(state)
    {
        case SND_PCM_STATE_OPEN:
        case SND_PCM_STATE_SETUP:
        case SND_PCM_STATE_PREPARED:
        case SND_PCM_STATE_RUNNING:
        case SND_PCM_STATE_DRAINING:
        case SND_PCM_STATE_PAUSED:
            /* All Okay */
            break;

        case SND_PCM_STATE_XRUN:
            if(const auto err = snd_pcm_recover(handle, -EPIPE, 1); err < 0)
                return err;
            break;
        case SND_PCM_STATE_SUSPENDED:
            if(const auto err = snd_pcm_recover(handle, -ESTRPIPE, 1); err < 0)
                return err;
            break;

        case SND_PCM_STATE_DISCONNECTED:
            return -ENODEV;

        /* ALSA headers have made this enum public, leaving us in a bind: use
         * the enum despite being private and internal to the libasound, or
         * ignore when an enum value isn't handled. We can't rely on it being
         * declared either, since older headers don't have it and it could be
         * removed in the future. We can't even really rely on its value, since
         * being private/internal means it's subject to change, but this is the
         * best we can do.
         */
        case 1024 /*SND_PCM_STATE_PRIVATE1*/:
            break;
    }

    return state;
}


struct AlsaPlayback final : public BackendBase {
    explicit AlsaPlayback(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~AlsaPlayback() override;

    void mixerProc();
    void mixerNoMMapProc();

    void open(std::string_view name) override;
    auto reset() -> bool override;
    void start() override;
    void stop() override;

    auto getClockLatency() -> ClockLatency override;

    snd_pcm_t *mPcmHandle{nullptr};

    std::mutex mMutex;

    uint mFrameStep{};
    std::vector<std::byte> mBuffer;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

AlsaPlayback::~AlsaPlayback()
{
    if(mPcmHandle)
        snd_pcm_close(mPcmHandle);
    mPcmHandle = nullptr;
}


void AlsaPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    const auto update_size = snd_pcm_uframes_t{mDevice->mUpdateSize};
    const auto buffer_size = snd_pcm_uframes_t{mDevice->mBufferSize};
    while(!mKillNow.load(std::memory_order_acquire))
    {
        const auto state = verify_state(mPcmHandle);
        if(state < 0)
        {
            ERR("Invalid state detected: {}", snd_strerror(state));
            mDevice->handleDisconnect("Bad state: {}", snd_strerror(state));
            break;
        }

        const auto avails = snd_pcm_avail_update(mPcmHandle);
        if(avails < 0)
        {
            ERR("available update failed: {}", snd_strerror(gsl::narrow_cast<int>(avails)));
            continue;
        }
        auto avail = gsl::narrow_cast<snd_pcm_uframes_t>(avails);

        if(avail > buffer_size)
        {
            WARN("available samples exceeds the buffer size");
            snd_pcm_reset(mPcmHandle);
            continue;
        }

        // make sure there's frames to process
        if(avail < update_size)
        {
            if(state != SND_PCM_STATE_RUNNING)
            {
                if(const auto err = snd_pcm_start(mPcmHandle); err < 0)
                {
                    ERR("start failed: {}", snd_strerror(err));
                    continue;
                }
            }
            if(snd_pcm_wait(mPcmHandle, 1000) == 0)
                ERR("Wait timeout... buffer size too low?");
            continue;
        }
        avail -= avail%update_size;

        // it is possible that contiguous areas are smaller, thus we use a loop
        auto dlock = std::lock_guard{mMutex};
        while(auto frames = avail)
        {
            const snd_pcm_channel_area_t *areas{};
            auto offset = snd_pcm_uframes_t{};
            if(const auto err = snd_pcm_mmap_begin(mPcmHandle, &areas, &offset, &frames); err < 0)
            {
                ERR("mmap begin error: {}", snd_strerror(err));
                break;
            }

            /* NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
            auto *WritePtr = static_cast<char*>(areas->addr) + (offset * areas->step / 8);
            mDevice->renderSamples(WritePtr, gsl::narrow_cast<uint>(frames), mFrameStep);

            const auto commitres = snd_pcm_mmap_commit(mPcmHandle, offset, frames);
            if(std::cmp_not_equal(commitres, frames))
            {
                ERR("mmap commit error: {}",
                    snd_strerror(commitres >= 0 ? -EPIPE : gsl::narrow_cast<int>(commitres)));
                break;
            }

            avail -= frames;
        }
    }
}

void AlsaPlayback::mixerNoMMapProc()
{
    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    const auto update_size = snd_pcm_uframes_t{mDevice->mUpdateSize};
    const auto buffer_size = snd_pcm_uframes_t{mDevice->mBufferSize};
    while(!mKillNow.load(std::memory_order_acquire))
    {
        const auto state = verify_state(mPcmHandle);
        if(state < 0)
        {
            ERR("Invalid state detected: {}", snd_strerror(state));
            mDevice->handleDisconnect("Bad state: {}", snd_strerror(state));
            break;
        }

        auto avail = snd_pcm_avail_update(mPcmHandle);
        if(avail < 0)
        {
            ERR("available update failed: {}", snd_strerror(gsl::narrow_cast<int>(avail)));
            continue;
        }

        if(std::cmp_greater(avail, buffer_size))
        {
            WARN("available samples exceeds the buffer size");
            snd_pcm_reset(mPcmHandle);
            continue;
        }

        if(std::cmp_less(avail, update_size))
        {
            if(state != SND_PCM_STATE_RUNNING)
            {
                if(const auto err = snd_pcm_start(mPcmHandle); err < 0)
                {
                    ERR("start failed: {}", snd_strerror(err));
                    continue;
                }
            }
            if(snd_pcm_wait(mPcmHandle, 1000) == 0)
                ERR("Wait timeout... buffer size too low?");
            continue;
        }

        auto WritePtr = mBuffer.begin();
        avail = snd_pcm_bytes_to_frames(mPcmHandle, std::ssize(mBuffer));
        const auto dlock = std::lock_guard{mMutex};
        mDevice->renderSamples(std::to_address(WritePtr), gsl::narrow_cast<uint>(avail),
            mFrameStep);
        while(avail > 0)
        {
            auto ret = snd_pcm_writei(mPcmHandle, std::to_address(WritePtr),
                gsl::narrow_cast<snd_pcm_uframes_t>(avail));
            switch(ret)
            {
            case -EAGAIN:
                continue;
#if ESTRPIPE != EPIPE
            case -ESTRPIPE:
#endif
            case -EPIPE:
            case -EINTR:
                ret = snd_pcm_recover(mPcmHandle, gsl::narrow_cast<int>(ret), 1);
                if(ret < 0)
                    avail = 0;
                break;
            default:
                if(ret >= 0)
                {
                    WritePtr += snd_pcm_frames_to_bytes(mPcmHandle, ret);
                    avail -= ret;
                }
                break;
            }
            if(ret < 0)
            {
                ret = snd_pcm_prepare(mPcmHandle);
                if(ret < 0) break;
            }
        }
    }
}


void AlsaPlayback::open(std::string_view name)
{
    auto driver = "default"s;
    if(!name.empty())
    {
        if(PlaybackDevices.empty())
            PlaybackDevices = probe_devices(SND_PCM_STREAM_PLAYBACK);

        const auto iter = std::ranges::find(PlaybackDevices, name, &DevMap::name);
        if(iter == PlaybackDevices.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"{}\" not found", name};
        driver = iter->device_name;
    }
    else
    {
        name = GetDefaultName();
        if(auto driveropt = ConfigValueStr({}, "alsa"sv, "device"sv))
            driver = std::move(driveropt).value();
    }
    TRACE("Opening device \"{}\"", driver);

    snd_pcm_t *pcmHandle{};
    if(const auto err = snd_pcm_open(&pcmHandle, driver.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK); err < 0)
        throw al::backend_exception{al::backend_error::NoDevice,
            "Could not open ALSA device \"{}\"", driver};
    if(mPcmHandle)
        snd_pcm_close(mPcmHandle);
    mPcmHandle = pcmHandle;

    /* Free alsa's global config tree. Otherwise valgrind reports a ton of leaks. */
    snd_config_update_free_global();

    mDeviceName = name;
}

auto AlsaPlayback::reset() -> bool
{
    auto format = SND_PCM_FORMAT_UNKNOWN;
    switch(mDevice->FmtType)
    {
    case DevFmtByte: format = SND_PCM_FORMAT_S8; break;
    case DevFmtUByte: format = SND_PCM_FORMAT_U8; break;
    case DevFmtShort: format = SND_PCM_FORMAT_S16; break;
    case DevFmtUShort: format = SND_PCM_FORMAT_U16; break;
    case DevFmtInt: format = SND_PCM_FORMAT_S32; break;
    case DevFmtUInt: format = SND_PCM_FORMAT_U32; break;
    case DevFmtFloat: format = SND_PCM_FORMAT_FLOAT; break;
    }

    auto allowmmap = GetConfigValueBool(mDevice->mDeviceName, "alsa"sv, "mmap"sv, true);
    auto periodLen = gsl::narrow_cast<uint>(mDevice->mUpdateSize * 1000000_u64
        / mDevice->mSampleRate);
    auto bufferLen = gsl::narrow_cast<uint>(mDevice->mBufferSize * 1000000_u64
        / mDevice->mSampleRate);
    auto rate = mDevice->mSampleRate;

    auto hp = CreateHwParams();
#define CHECK(x) do {                                                         \
    if(const auto err = x; err < 0)                                           \
        throw al::backend_exception{al::backend_error::DeviceError, #x " failed: {}", \
            snd_strerror(err)};                                               \
} while(0)
    CHECK(snd_pcm_hw_params_any(mPcmHandle, hp.get()));
    /* set interleaved access */
    if(!allowmmap
        || snd_pcm_hw_params_set_access(mPcmHandle, hp.get(), SND_PCM_ACCESS_MMAP_INTERLEAVED) < 0)
    {
        /* No mmap */
        CHECK(snd_pcm_hw_params_set_access(mPcmHandle, hp.get(), SND_PCM_ACCESS_RW_INTERLEAVED));
    }
    /* test and set format (implicitly sets sample bits) */
    if(snd_pcm_hw_params_test_format(mPcmHandle, hp.get(), format) < 0)
    {
        struct FormatMap {
            snd_pcm_format_t format;
            DevFmtType fmttype;
        };
        static constexpr auto formatlist = std::array{
            FormatMap{SND_PCM_FORMAT_FLOAT, DevFmtFloat },
            FormatMap{SND_PCM_FORMAT_S32,   DevFmtInt   },
            FormatMap{SND_PCM_FORMAT_U32,   DevFmtUInt  },
            FormatMap{SND_PCM_FORMAT_S16,   DevFmtShort },
            FormatMap{SND_PCM_FORMAT_U16,   DevFmtUShort},
            FormatMap{SND_PCM_FORMAT_S8,    DevFmtByte  },
            FormatMap{SND_PCM_FORMAT_U8,    DevFmtUByte },
        };

        for(const auto &fmt : formatlist)
        {
            format = fmt.format;
            if(snd_pcm_hw_params_test_format(mPcmHandle, hp.get(), format) >= 0)
            {
                mDevice->FmtType = fmt.fmttype;
                break;
            }
        }
    }
    CHECK(snd_pcm_hw_params_set_format(mPcmHandle, hp.get(), format));
    /* set channels (implicitly sets frame bits) */
    if(snd_pcm_hw_params_set_channels(mPcmHandle, hp.get(), mDevice->channelsFromFmt()) < 0)
    {
        uint numchans{2u};
        CHECK(snd_pcm_hw_params_set_channels_near(mPcmHandle, hp.get(), &numchans));
        if(numchans < 1)
            throw al::backend_exception{al::backend_error::DeviceError, "Got 0 device channels"};
        if(numchans == 1) mDevice->FmtChans = DevFmtMono;
        else mDevice->FmtChans = DevFmtStereo;
    }
    /* set rate (implicitly constrains period/buffer parameters) */
    if(!GetConfigValueBool(mDevice->mDeviceName, "alsa", "allow-resampler", false)
        || !mDevice->Flags.test(FrequencyRequest))
    {
        if(snd_pcm_hw_params_set_rate_resample(mPcmHandle, hp.get(), 0) < 0)
            WARN("Failed to disable ALSA resampler");
    }
    else if(snd_pcm_hw_params_set_rate_resample(mPcmHandle, hp.get(), 1) < 0)
        WARN("Failed to enable ALSA resampler");
    CHECK(snd_pcm_hw_params_set_rate_near(mPcmHandle, hp.get(), &rate, nullptr));
    /* set period time (implicitly constrains period/buffer parameters) */
    if(const auto err = snd_pcm_hw_params_set_period_time_near(mPcmHandle, hp.get(), &periodLen, nullptr); err < 0)
        ERR("snd_pcm_hw_params_set_period_time_near failed: {}", snd_strerror(err));
    /* set buffer time (implicitly sets buffer size/bytes/time and period size/bytes) */
    if(const auto err = snd_pcm_hw_params_set_buffer_time_near(mPcmHandle, hp.get(), &bufferLen, nullptr); err < 0)
        ERR("snd_pcm_hw_params_set_buffer_time_near failed: {}", snd_strerror(err));
    /* install and prepare hardware configuration */
    CHECK(snd_pcm_hw_params(mPcmHandle, hp.get()));

    /* retrieve configuration info */
    auto periodSizeInFrames = snd_pcm_uframes_t{};
    auto bufferSizeInFrames = snd_pcm_uframes_t{};
    auto access = snd_pcm_access_t{};

    CHECK(snd_pcm_hw_params_get_access(hp.get(), &access));
    CHECK(snd_pcm_hw_params_get_period_size(hp.get(), &periodSizeInFrames, nullptr));
    CHECK(snd_pcm_hw_params_get_buffer_size(hp.get(), &bufferSizeInFrames));
    CHECK(snd_pcm_hw_params_get_channels(hp.get(), &mFrameStep));
    hp = nullptr;

    auto sp = CreateSwParams();
    CHECK(snd_pcm_sw_params_current(mPcmHandle, sp.get()));
    CHECK(snd_pcm_sw_params_set_avail_min(mPcmHandle, sp.get(), periodSizeInFrames));
    CHECK(snd_pcm_sw_params_set_stop_threshold(mPcmHandle, sp.get(), bufferSizeInFrames));
    CHECK(snd_pcm_sw_params(mPcmHandle, sp.get()));
#undef CHECK
    sp = nullptr;

    mDevice->mBufferSize = gsl::narrow_cast<uint>(bufferSizeInFrames);
    mDevice->mUpdateSize = gsl::narrow_cast<uint>(periodSizeInFrames);
    mDevice->mSampleRate = rate;

    setDefaultChannelOrder();

    return true;
}

void AlsaPlayback::start()
{
    auto access = snd_pcm_access_t{};
    auto hp = CreateHwParams();
#define CHECK(x) do {                                                         \
    if(const auto err = x; err < 0)                                           \
        throw al::backend_exception{al::backend_error::DeviceError, #x " failed: {}", \
            snd_strerror(err)};                                               \
} while(0)
    CHECK(snd_pcm_hw_params_current(mPcmHandle, hp.get()));
    /* retrieve configuration info */
    CHECK(snd_pcm_hw_params_get_access(hp.get(), &access));
    hp = nullptr;

    void (AlsaPlayback::*thread_func)(){};
    if(access == SND_PCM_ACCESS_RW_INTERLEAVED)
    {
        auto datalen = snd_pcm_frames_to_bytes(mPcmHandle, mDevice->mUpdateSize);
        mBuffer.resize(gsl::narrow_cast<size_t>(datalen));
        thread_func = &AlsaPlayback::mixerNoMMapProc;
    }
    else
    {
        CHECK(snd_pcm_prepare(mPcmHandle));
        thread_func = &AlsaPlayback::mixerProc;
    }
#undef CHECK

    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{thread_func, this};
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: {}", e.what()};
    }
}

void AlsaPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    mBuffer.clear();
    if(const auto err = snd_pcm_drop(mPcmHandle); err < 0)
        ERR("snd_pcm_drop failed: {}", snd_strerror(err));
}

auto AlsaPlayback::getClockLatency() -> ClockLatency
{
    const auto dlock = std::lock_guard{mMutex};
    auto ret = ClockLatency{};
    ret.ClockTime = mDevice->getClockTime();
    auto delay = snd_pcm_sframes_t{};
    if(const auto err = snd_pcm_delay(mPcmHandle, &delay); err < 0)
    {
        ERR("Failed to get pcm delay: {}", snd_strerror(err));
        delay = 0;
    }
    ret.Latency  = std::chrono::seconds{std::max<snd_pcm_sframes_t>(0, delay)};
    ret.Latency /= mDevice->mSampleRate;

    return ret;
}


struct AlsaCapture final : public BackendBase {
    explicit AlsaCapture(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~AlsaCapture() override;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    auto availableSamples() -> uint override;
    auto getClockLatency() -> ClockLatency override;

    snd_pcm_t *mPcmHandle{nullptr};

    std::vector<std::byte> mBuffer;

    bool mDoCapture{false};
    RingBufferPtr<std::byte> mRing;

    snd_pcm_sframes_t mLastAvail{0};
};

AlsaCapture::~AlsaCapture()
{
    if(mPcmHandle)
        snd_pcm_close(mPcmHandle);
    mPcmHandle = nullptr;
}


void AlsaCapture::open(std::string_view name)
{
    auto driver = "default"s;
    if(!name.empty())
    {
        if(CaptureDevices.empty())
            CaptureDevices = probe_devices(SND_PCM_STREAM_CAPTURE);

        const auto iter = std::ranges::find(CaptureDevices, name, &DevMap::name);
        if(iter == CaptureDevices.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"{}\" not found", name};
        driver = iter->device_name;
    }
    else
    {
        name = GetDefaultName();
        if(auto driveropt = ConfigValueStr({}, "alsa"sv, "capture"sv))
            driver = std::move(driveropt).value();
    }

    TRACE("Opening device \"{}\"", driver);
    if(const auto err = snd_pcm_open(&mPcmHandle, driver.c_str(), SND_PCM_STREAM_CAPTURE,
        SND_PCM_NONBLOCK); err < 0)
        throw al::backend_exception{al::backend_error::NoDevice,
            "Could not open ALSA device \"{}\"", driver};

    /* Free alsa's global config tree. Otherwise valgrind reports a ton of leaks. */
    snd_config_update_free_global();

    auto format = SND_PCM_FORMAT_UNKNOWN;
    switch(mDevice->FmtType)
    {
    case DevFmtByte: format = SND_PCM_FORMAT_S8; break;
    case DevFmtUByte: format = SND_PCM_FORMAT_U8; break;
    case DevFmtShort: format = SND_PCM_FORMAT_S16; break;
    case DevFmtUShort: format = SND_PCM_FORMAT_U16; break;
    case DevFmtInt: format = SND_PCM_FORMAT_S32; break;
    case DevFmtUInt: format = SND_PCM_FORMAT_U32; break;
    case DevFmtFloat: format = SND_PCM_FORMAT_FLOAT; break;
    }

    auto bufferSizeInFrames = snd_pcm_uframes_t{std::max(mDevice->mBufferSize,
        100u*mDevice->mSampleRate/1000u)};
    auto periodSizeInFrames = snd_pcm_uframes_t{std::min(mDevice->mBufferSize,
        25u*mDevice->mSampleRate/1000u)};

    auto needring = false;
    auto hp = CreateHwParams();
#define CHECK(x) do {                                                         \
    if(const auto err = x; err < 0)                                           \
        throw al::backend_exception{al::backend_error::DeviceError, #x " failed: {}", \
            snd_strerror(err)};                                               \
} while(0)
    CHECK(snd_pcm_hw_params_any(mPcmHandle, hp.get()));
    /* set interleaved access */
    CHECK(snd_pcm_hw_params_set_access(mPcmHandle, hp.get(), SND_PCM_ACCESS_RW_INTERLEAVED));
    /* set format (implicitly sets sample bits) */
    CHECK(snd_pcm_hw_params_set_format(mPcmHandle, hp.get(), format));
    /* set channels (implicitly sets frame bits) */
    CHECK(snd_pcm_hw_params_set_channels(mPcmHandle, hp.get(), mDevice->channelsFromFmt()));
    /* set rate (implicitly constrains period/buffer parameters) */
    CHECK(snd_pcm_hw_params_set_rate(mPcmHandle, hp.get(), mDevice->mSampleRate, 0));
    /* set buffer size in frame units (implicitly sets period size/bytes/time and buffer time/bytes) */
    if(snd_pcm_hw_params_set_buffer_size_min(mPcmHandle, hp.get(), &bufferSizeInFrames) < 0)
    {
        TRACE("Buffer too large, using intermediate ring buffer");
        needring = true;
        CHECK(snd_pcm_hw_params_set_buffer_size_near(mPcmHandle, hp.get(), &bufferSizeInFrames));
    }
    /* set buffer size in frame units (implicitly sets period size/bytes/time and buffer time/bytes) */
    CHECK(snd_pcm_hw_params_set_period_size_near(mPcmHandle, hp.get(), &periodSizeInFrames, nullptr));
    /* install and prepare hardware configuration */
    CHECK(snd_pcm_hw_params(mPcmHandle, hp.get()));
    /* retrieve configuration info */
    CHECK(snd_pcm_hw_params_get_period_size(hp.get(), &periodSizeInFrames, nullptr));
#undef CHECK
    hp = nullptr;

    if(needring)
        mRing = RingBuffer<std::byte>::Create(mDevice->mBufferSize, mDevice->frameSizeFromFmt(),
            false);

    mDeviceName = name;
}


void AlsaCapture::start()
{
    if(const auto err = snd_pcm_prepare(mPcmHandle); err < 0)
        throw al::backend_exception{al::backend_error::DeviceError, "snd_pcm_prepare failed: {}",
            snd_strerror(err)};

    if(const auto err = snd_pcm_start(mPcmHandle); err < 0)
        throw al::backend_exception{al::backend_error::DeviceError, "snd_pcm_start failed: {}",
            snd_strerror(err)};

    mDoCapture = true;
}

void AlsaCapture::stop()
{
    /* OpenAL requires access to unread audio after stopping, but ALSA's
     * snd_pcm_drain is unreliable and snd_pcm_drop drops it. Capture what's
     * available now so it'll be available later after the drop.
     */
    const auto avail = availableSamples();
    if(!mRing && avail > 0)
    {
        /* The ring buffer implicitly captures when checking availability.
         * Direct access needs to explicitly capture it into temp storage.
         */
        auto temp = std::vector<std::byte>(
            as_unsigned(snd_pcm_frames_to_bytes(mPcmHandle, avail)));
        captureSamples(temp);
        mBuffer = std::move(temp);
    }
    if(const auto err = snd_pcm_drop(mPcmHandle); err < 0)
        ERR("snd_pcm_drop failed: {}", snd_strerror(err));
    mDoCapture = false;
}

void AlsaCapture::captureSamples(std::span<std::byte> outbuffer)
{
    if(mRing)
    {
        std::ignore = mRing->read(outbuffer);
        return;
    }

    const auto bpf = snd_pcm_frames_to_bytes(mPcmHandle, 1);
    mLastAvail -= std::ssize(outbuffer) / bpf;
    while(mDevice->Connected.load(std::memory_order_acquire) && !outbuffer.empty())
    {
        if(!mBuffer.empty())
        {
            /* First get any data stored from the last stop */
            std::ranges::copy(mBuffer | std::views::take(outbuffer.size()), outbuffer.begin());

            const auto amt = std::min(std::ssize(outbuffer), std::ssize(mBuffer));
            mBuffer.erase(mBuffer.begin(), mBuffer.begin()+amt);
            outbuffer = outbuffer.subspan(as_unsigned(amt));
            continue;
        }

        auto amt = snd_pcm_sframes_t{0};
        if(mDoCapture)
        {
            amt = std::ssize(outbuffer) / bpf;
            amt = snd_pcm_readi(mPcmHandle, outbuffer.data(), as_unsigned(amt));
        }
        if(amt < 0)
        {
            ERR("read error: {}", snd_strerror(gsl::narrow_cast<int>(amt)));

            if(amt == -EAGAIN)
                continue;
            amt = snd_pcm_recover(mPcmHandle, gsl::narrow_cast<int>(amt), 1);
            if(amt >= 0)
            {
                amt = snd_pcm_start(mPcmHandle);
                if(amt >= 0)
                    amt = snd_pcm_avail_update(mPcmHandle);
            }
            if(amt < 0)
            {
                auto *err = snd_strerror(gsl::narrow_cast<int>(amt));
                ERR("restore error: {}", err);
                mDevice->handleDisconnect("Capture recovery failure: {}", err);
                break;
            }
            /* If the amount available is less than what's asked, we lost it
             * during recovery. So just give silence instead.
             */
            if(amt*bpf < std::ssize(outbuffer))
                break;
            continue;
        }
        outbuffer = outbuffer.subspan(as_unsigned(amt*bpf));
    }
    if(!outbuffer.empty())
        std::ranges::fill(outbuffer, (mDevice->FmtType==DevFmtUByte)?std::byte{0x80}:std::byte{0});
}

auto AlsaCapture::availableSamples() -> uint
{
    auto avail = snd_pcm_sframes_t{0};
    if(mDevice->Connected.load(std::memory_order_acquire) && mDoCapture)
        avail = snd_pcm_avail_update(mPcmHandle);
    if(avail < 0)
    {
        ERR("snd_pcm_avail_update failed: {}", snd_strerror(gsl::narrow_cast<int>(avail)));

        avail = snd_pcm_recover(mPcmHandle, gsl::narrow_cast<int>(avail), 1);
        if(avail >= 0)
        {
            if(mDoCapture)
                avail = snd_pcm_start(mPcmHandle);
            if(avail >= 0)
                avail = snd_pcm_avail_update(mPcmHandle);
        }
        if(avail < 0)
        {
            auto *err = snd_strerror(gsl::narrow_cast<int>(avail));
            ERR("restore error: {}", err);
            mDevice->handleDisconnect("Capture recovery failure: {}", err);
        }
    }

    if(!mRing)
    {
        avail = std::max<snd_pcm_sframes_t>(avail, 0);
        avail += snd_pcm_bytes_to_frames(mPcmHandle, std::ssize(mBuffer));
        mLastAvail = std::max(mLastAvail, avail);
        return gsl::narrow_cast<uint>(mLastAvail);
    }

    while(avail > 0)
    {
        auto vec = mRing->getWriteVector();
        if(vec[0].empty()) break;

        auto amt = snd_pcm_bytes_to_frames(mPcmHandle, std::ssize(vec[0]));
        amt = std::min(amt, avail);
        amt = snd_pcm_readi(mPcmHandle, vec[0].data(), gsl::narrow_cast<snd_pcm_uframes_t>(amt));
        if(amt < 0)
        {
            ERR("read error: {}", snd_strerror(gsl::narrow_cast<int>(amt)));

            if(amt == -EAGAIN)
                continue;
            amt = snd_pcm_recover(mPcmHandle, gsl::narrow_cast<int>(amt), 1);
            if(amt >= 0)
            {
                if(mDoCapture)
                    amt = snd_pcm_start(mPcmHandle);
                if(amt >= 0)
                    amt = snd_pcm_avail_update(mPcmHandle);
            }
            if(amt < 0)
            {
                auto *err = snd_strerror(gsl::narrow_cast<int>(amt));
                ERR("restore error: {}", err);
                mDevice->handleDisconnect("Capture recovery failure: {}", err);
                break;
            }
            avail = amt;
            continue;
        }

        mRing->writeAdvance(gsl::narrow_cast<snd_pcm_uframes_t>(amt));
        avail -= amt;
    }

    return gsl::narrow_cast<uint>(mRing->readSpace());
}

auto AlsaCapture::getClockLatency() -> ClockLatency
{
    auto ret = ClockLatency{};
    ret.ClockTime = mDevice->getClockTime();
    auto delay = snd_pcm_sframes_t{};
    if(const auto err = snd_pcm_delay(mPcmHandle, &delay); err < 0)
    {
        ERR("Failed to get pcm delay: {}", snd_strerror(err));
        delay = 0;
    }
    ret.Latency  = std::chrono::seconds{std::max<snd_pcm_sframes_t>(0, delay)};
    ret.Latency /= mDevice->mSampleRate;

    return ret;
}

} // namespace


auto AlsaBackendFactory::init() -> bool
{
#if HAVE_DYNLOAD
    if(!alsa_handle)
    {
        if(auto libresult = LoadLib("libasound.so.2"))
            alsa_handle = libresult.value();
        else
        {
            WARN("Failed to load {}: {}", "libasound.so.2", libresult.error());
            return false;
        }

        static constexpr auto load_func = [](auto *&func, const char *name) -> bool
        {
            using func_t = std::remove_reference_t<decltype(func)>;
            auto funcresult = GetSymbol(alsa_handle, name);
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
        ALSA_FUNCS(LOAD_FUNC);
#undef LOAD_FUNC
        if(!ok)
        {
            CloseLib(alsa_handle);
            alsa_handle = nullptr;
            return false;
        }
    }
#endif

    return true;
}

bool AlsaBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback || type == BackendType::Capture); }

auto AlsaBackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    auto outnames = std::vector<std::string>{};
    auto add_device = [&outnames](const DevMap &entry) -> void
    { outnames.emplace_back(entry.name); };

    switch(type)
    {
    case BackendType::Playback:
        PlaybackDevices = probe_devices(SND_PCM_STREAM_PLAYBACK);
        outnames.reserve(PlaybackDevices.size());
        std::ranges::for_each(PlaybackDevices, add_device);
        break;

    case BackendType::Capture:
        CaptureDevices = probe_devices(SND_PCM_STREAM_CAPTURE);
        outnames.reserve(CaptureDevices.size());
        std::ranges::for_each(CaptureDevices, add_device);
        break;
    }

    return outnames;
}

auto AlsaBackendFactory::createBackend(gsl::not_null<DeviceBase*> device, BackendType type)
    -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new AlsaPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new AlsaCapture{device}};
    return nullptr;
}

BackendFactory &AlsaBackendFactory::getFactory()
{
    static AlsaBackendFactory factory{};
    return factory;
}
