/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2010 by authors.
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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <new>
#include <numeric>
#include <string>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alconfig.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "aloptional.h"
#include "alspan.h"
#include "alstring.h"
#include "alu.h"
#include "ambdec.h"
#include "ambidefs.h"
#include "bformatdec.h"
#include "bs2b.h"
#include "devformat.h"
#include "hrtf.h"
#include "logging.h"
#include "math_defs.h"
#include "opthelpers.h"
#include "uhjfilter.h"


constexpr std::array<float,MAX_AMBI_CHANNELS> AmbiScale::FromN3D;
constexpr std::array<float,MAX_AMBI_CHANNELS> AmbiScale::FromSN3D;
constexpr std::array<float,MAX_AMBI_CHANNELS> AmbiScale::FromFuMa;
constexpr std::array<uint8_t,MAX_AMBI_CHANNELS> AmbiIndex::FromFuMa;
constexpr std::array<uint8_t,MAX_AMBI2D_CHANNELS> AmbiIndex::FromFuMa2D;
constexpr std::array<uint8_t,MAX_AMBI_CHANNELS> AmbiIndex::FromACN;
constexpr std::array<uint8_t,MAX_AMBI2D_CHANNELS> AmbiIndex::From2D;
constexpr std::array<uint8_t,MAX_AMBI_CHANNELS> AmbiIndex::OrderFromChannel;
constexpr std::array<uint8_t,MAX_AMBI2D_CHANNELS> AmbiIndex::OrderFrom2DChannel;


namespace {

using namespace std::placeholders;
using std::chrono::seconds;
using std::chrono::nanoseconds;

inline const char *GetLabelFromChannel(Channel channel)
{
    switch(channel)
    {
        case FrontLeft: return "front-left";
        case FrontRight: return "front-right";
        case FrontCenter: return "front-center";
        case LFE: return "lfe";
        case BackLeft: return "back-left";
        case BackRight: return "back-right";
        case BackCenter: return "back-center";
        case SideLeft: return "side-left";
        case SideRight: return "side-right";

        case UpperFrontLeft: return "upper-front-left";
        case UpperFrontRight: return "upper-front-right";
        case UpperBackLeft: return "upper-back-left";
        case UpperBackRight: return "upper-back-right";
        case LowerFrontLeft: return "lower-front-left";
        case LowerFrontRight: return "lower-front-right";
        case LowerBackLeft: return "lower-back-left";
        case LowerBackRight: return "lower-back-right";

        case Aux0: return "aux-0";
        case Aux1: return "aux-1";
        case Aux2: return "aux-2";
        case Aux3: return "aux-3";
        case Aux4: return "aux-4";
        case Aux5: return "aux-5";
        case Aux6: return "aux-6";
        case Aux7: return "aux-7";
        case Aux8: return "aux-8";
        case Aux9: return "aux-9";
        case Aux10: return "aux-10";
        case Aux11: return "aux-11";
        case Aux12: return "aux-12";
        case Aux13: return "aux-13";
        case Aux14: return "aux-14";
        case Aux15: return "aux-15";

        case MaxChannels: break;
    }
    return "(unknown)";
}


void AllocChannels(ALCdevice *device, const ALuint main_chans, const ALuint real_chans)
{
    TRACE("Channel config, Main: %u, Real: %u\n", main_chans, real_chans);

    /* Allocate extra channels for any post-filter output. */
    const ALuint num_chans{main_chans + real_chans};

    TRACE("Allocating %u channels, %zu bytes\n", num_chans,
        num_chans*sizeof(device->MixBuffer[0]));
    device->MixBuffer.resize(num_chans);
    al::span<FloatBufferLine> buffer{device->MixBuffer.data(), device->MixBuffer.size()};

    device->Dry.Buffer = buffer.first(main_chans);
    buffer = buffer.subspan(main_chans);
    if(real_chans != 0)
    {
        device->RealOut.Buffer = buffer.first(real_chans);
        buffer = buffer.subspan(real_chans);
    }
    else
        device->RealOut.Buffer = device->Dry.Buffer;
}


struct ChannelMap {
    Channel ChanName;
    ALfloat Config[MAX_AMBI2D_CHANNELS];
};

bool MakeSpeakerMap(ALCdevice *device, const AmbDecConf *conf, ALuint (&speakermap)[MAX_OUTPUT_CHANNELS])
{
    auto map_spkr = [device](const AmbDecConf::SpeakerConf &speaker) -> ALuint
    {
        /* NOTE: AmbDec does not define any standard speaker names, however
         * for this to work we have to by able to find the output channel
         * the speaker definition corresponds to. Therefore, OpenAL Soft
         * requires these channel labels to be recognized:
         *
         * LF = Front left
         * RF = Front right
         * LS = Side left
         * RS = Side right
         * LB = Back left
         * RB = Back right
         * CE = Front center
         * CB = Back center
         *
         * Additionally, surround51 will acknowledge back speakers for side
         * channels, and surround51rear will acknowledge side speakers for
         * back channels, to avoid issues with an ambdec expecting 5.1 to
         * use the side channels when the device is configured for back,
         * and vice-versa.
         */
        Channel ch{};
        if(speaker.Name == "LF")
            ch = FrontLeft;
        else if(speaker.Name == "RF")
            ch = FrontRight;
        else if(speaker.Name == "CE")
            ch = FrontCenter;
        else if(speaker.Name == "LS")
        {
            if(device->FmtChans == DevFmtX51Rear)
                ch = BackLeft;
            else
                ch = SideLeft;
        }
        else if(speaker.Name == "RS")
        {
            if(device->FmtChans == DevFmtX51Rear)
                ch = BackRight;
            else
                ch = SideRight;
        }
        else if(speaker.Name == "LB")
        {
            if(device->FmtChans == DevFmtX51)
                ch = SideLeft;
            else
                ch = BackLeft;
        }
        else if(speaker.Name == "RB")
        {
            if(device->FmtChans == DevFmtX51)
                ch = SideRight;
            else
                ch = BackRight;
        }
        else if(speaker.Name == "CB")
            ch = BackCenter;
        else
        {
            const char *name{speaker.Name.c_str()};
            unsigned int n;
            char c;

            if(sscanf(name, "AUX%u%c", &n, &c) == 1 && n < 16)
                ch = static_cast<Channel>(Aux0+n);
            else
            {
                ERR("AmbDec speaker label \"%s\" not recognized\n", name);
                return INVALID_CHANNEL_INDEX;
            }
        }
        const ALuint chidx{GetChannelIdxByName(device->RealOut, ch)};
        if(chidx == INVALID_CHANNEL_INDEX)
            ERR("Failed to lookup AmbDec speaker label %s\n", speaker.Name.c_str());
        return chidx;
    };
    std::transform(conf->Speakers.begin(), conf->Speakers.end(), std::begin(speakermap), map_spkr);
    /* Return success if no invalid entries are found. */
    auto spkrmap_end = std::begin(speakermap) + conf->Speakers.size();
    return std::find(std::begin(speakermap), spkrmap_end, INVALID_CHANNEL_INDEX) == spkrmap_end;
}


constexpr ChannelMap MonoCfg[1] = {
    { FrontCenter, { 1.0f } },
}, StereoCfg[2] = {
    { FrontLeft,   { 5.00000000e-1f,  2.88675135e-1f,  5.52305643e-2f } },
    { FrontRight,  { 5.00000000e-1f, -2.88675135e-1f,  5.52305643e-2f } },
}, QuadCfg[4] = {
    { BackLeft,    { 3.53553391e-1f,  2.04124145e-1f, -2.04124145e-1f } },
    { FrontLeft,   { 3.53553391e-1f,  2.04124145e-1f,  2.04124145e-1f } },
    { FrontRight,  { 3.53553391e-1f, -2.04124145e-1f,  2.04124145e-1f } },
    { BackRight,   { 3.53553391e-1f, -2.04124145e-1f, -2.04124145e-1f } },
}, X51SideCfg[4] = {
    { SideLeft,    { 3.33000782e-1f,  1.89084803e-1f, -2.00042375e-1f, -2.12307769e-2f, -1.14579885e-2f } },
    { FrontLeft,   { 1.88542860e-1f,  1.27709292e-1f,  1.66295695e-1f,  7.30571517e-2f,  2.10901184e-2f } },
    { FrontRight,  { 1.88542860e-1f, -1.27709292e-1f,  1.66295695e-1f, -7.30571517e-2f,  2.10901184e-2f } },
    { SideRight,   { 3.33000782e-1f, -1.89084803e-1f, -2.00042375e-1f,  2.12307769e-2f, -1.14579885e-2f } },
}, X51RearCfg[4] = {
    { BackLeft,    { 3.33000782e-1f,  1.89084803e-1f, -2.00042375e-1f, -2.12307769e-2f, -1.14579885e-2f } },
    { FrontLeft,   { 1.88542860e-1f,  1.27709292e-1f,  1.66295695e-1f,  7.30571517e-2f,  2.10901184e-2f } },
    { FrontRight,  { 1.88542860e-1f, -1.27709292e-1f,  1.66295695e-1f, -7.30571517e-2f,  2.10901184e-2f } },
    { BackRight,   { 3.33000782e-1f, -1.89084803e-1f, -2.00042375e-1f,  2.12307769e-2f, -1.14579885e-2f } },
}, X61Cfg[6] = {
    { SideLeft,    { 2.04460341e-1f,  2.17177926e-1f, -4.39996780e-2f, -2.60790269e-2f, -6.87239792e-2f } },
    { FrontLeft,   { 1.58923161e-1f,  9.21772680e-2f,  1.59658796e-1f,  6.66278083e-2f,  3.84686854e-2f } },
    { FrontRight,  { 1.58923161e-1f, -9.21772680e-2f,  1.59658796e-1f, -6.66278083e-2f,  3.84686854e-2f } },
    { SideRight,   { 2.04460341e-1f, -2.17177926e-1f, -4.39996780e-2f,  2.60790269e-2f, -6.87239792e-2f } },
    { BackCenter,  { 2.50001688e-1f,  0.00000000e+0f, -2.50000094e-1f,  0.00000000e+0f,  6.05133395e-2f } },
}, X71Cfg[6] = {
    { BackLeft,    { 2.04124145e-1f,  1.08880247e-1f, -1.88586120e-1f, -1.29099444e-1f,  7.45355993e-2f,  3.73460789e-2f,  0.00000000e+0f } },
    { SideLeft,    { 2.04124145e-1f,  2.17760495e-1f,  0.00000000e+0f,  0.00000000e+0f, -1.49071198e-1f, -3.73460789e-2f,  0.00000000e+0f } },
    { FrontLeft,   { 2.04124145e-1f,  1.08880247e-1f,  1.88586120e-1f,  1.29099444e-1f,  7.45355993e-2f,  3.73460789e-2f,  0.00000000e+0f } },
    { FrontRight,  { 2.04124145e-1f, -1.08880247e-1f,  1.88586120e-1f, -1.29099444e-1f,  7.45355993e-2f, -3.73460789e-2f,  0.00000000e+0f } },
    { SideRight,   { 2.04124145e-1f, -2.17760495e-1f,  0.00000000e+0f,  0.00000000e+0f, -1.49071198e-1f,  3.73460789e-2f,  0.00000000e+0f } },
    { BackRight,   { 2.04124145e-1f, -1.08880247e-1f, -1.88586120e-1f,  1.29099444e-1f,  7.45355993e-2f, -3.73460789e-2f,  0.00000000e+0f } },
};

void InitNearFieldCtrl(ALCdevice *device, ALfloat ctrl_dist, ALuint order, bool is3d)
{
    /* NFC is only used when AvgSpeakerDist is greater than 0. */
    const char *devname{device->DeviceName.c_str()};
    if(!GetConfigValueBool(devname, "decoder", "nfc", 0) || !(ctrl_dist > 0.0f))
        return;

    device->AvgSpeakerDist = clampf(ctrl_dist, 0.1f, 10.0f);
    TRACE("Using near-field reference distance: %.2f meters\n", device->AvgSpeakerDist);

    static const ALuint chans_per_order2d[MAX_AMBI_ORDER+1]{ 1, 2, 2, 2 };
    static const ALuint chans_per_order3d[MAX_AMBI_ORDER+1]{ 1, 3, 5, 7 };
    const al::span<const ALuint> chans_per_order{is3d ? chans_per_order3d : chans_per_order2d,
        order+1u};

    auto iter = std::copy(chans_per_order.begin(), chans_per_order.end(),
        std::begin(device->NumChannelsPerOrder));
    std::fill(iter, std::end(device->NumChannelsPerOrder), 0u);
}

void InitDistanceComp(ALCdevice *device, const AmbDecConf *conf,
    const ALuint (&speakermap)[MAX_OUTPUT_CHANNELS])
{
    auto get_max = std::bind(maxf, _1,
        std::bind(std::mem_fn(&AmbDecConf::SpeakerConf::Distance), _2));
    const ALfloat maxdist{
        std::accumulate(conf->Speakers.begin(), conf->Speakers.end(), float{0.0f}, get_max)};

    const char *devname{device->DeviceName.c_str()};
    if(!GetConfigValueBool(devname, "decoder", "distance-comp", 1) || !(maxdist > 0.0f))
        return;

    const auto distSampleScale = static_cast<ALfloat>(device->Frequency)/SPEEDOFSOUNDMETRESPERSEC;
    const auto ChanDelay = device->ChannelDelay.as_span();
    size_t total{0u};
    for(size_t i{0u};i < conf->Speakers.size();i++)
    {
        const AmbDecConf::SpeakerConf &speaker = conf->Speakers[i];
        const ALuint chan{speakermap[i]};

        /* Distance compensation only delays in steps of the sample rate. This
         * is a bit less accurate since the delay time falls to the nearest
         * sample time, but it's far simpler as it doesn't have to deal with
         * phase offsets. This means at 48khz, for instance, the distance delay
         * will be in steps of about 7 millimeters.
         */
        ALfloat delay{std::floor((maxdist - speaker.Distance)*distSampleScale + 0.5f)};
        if(delay > ALfloat{MAX_DELAY_LENGTH-1})
        {
            ERR("Delay for speaker \"%s\" exceeds buffer length (%f > %d)\n",
                speaker.Name.c_str(), delay, MAX_DELAY_LENGTH-1);
            delay = ALfloat{MAX_DELAY_LENGTH-1};
        }

        ChanDelay[chan].Length = static_cast<ALuint>(delay);
        ChanDelay[chan].Gain = speaker.Distance / maxdist;
        TRACE("Channel %u \"%s\" distance compensation: %u samples, %f gain\n", chan,
            speaker.Name.c_str(), ChanDelay[chan].Length, ChanDelay[chan].Gain);

        /* Round up to the next 4th sample, so each channel buffer starts
         * 16-byte aligned.
         */
        total += RoundUp(ChanDelay[chan].Length, 4);
    }

    if(total > 0)
    {
        device->ChannelDelay.setSampleCount(total);
        ChanDelay[0].Buffer = device->ChannelDelay.getSamples();
        auto set_bufptr = [](const DistanceComp::DistData &last, const DistanceComp::DistData &cur) -> DistanceComp::DistData
        {
            DistanceComp::DistData ret{cur};
            ret.Buffer = last.Buffer + RoundUp(last.Length, 4);
            return ret;
        };
        std::partial_sum(ChanDelay.begin(), ChanDelay.end(), ChanDelay.begin(), set_bufptr);
    }
}


auto GetAmbiScales(AmbiNorm scaletype) noexcept -> const std::array<float,MAX_AMBI_CHANNELS>&
{
    if(scaletype == AmbiNorm::FuMa) return AmbiScale::FromFuMa;
    if(scaletype == AmbiNorm::SN3D) return AmbiScale::FromSN3D;
    return AmbiScale::FromN3D;
}

auto GetAmbiLayout(AmbiLayout layouttype) noexcept -> const std::array<uint8_t,MAX_AMBI_CHANNELS>&
{
    if(layouttype == AmbiLayout::FuMa) return AmbiIndex::FromFuMa;
    return AmbiIndex::FromACN;
}


void InitPanning(ALCdevice *device)
{
    al::span<const ChannelMap> chanmap;
    ALuint coeffcount{};

    switch(device->FmtChans)
    {
        case DevFmtMono:
            chanmap = MonoCfg;
            coeffcount = 1;
            break;

        case DevFmtStereo:
            chanmap = StereoCfg;
            coeffcount = 3;
            break;

        case DevFmtQuad:
            chanmap = QuadCfg;
            coeffcount = 3;
            break;

        case DevFmtX51:
            chanmap = X51SideCfg;
            coeffcount = 5;
            break;

        case DevFmtX51Rear:
            chanmap = X51RearCfg;
            coeffcount = 5;
            break;

        case DevFmtX61:
            chanmap = X61Cfg;
            coeffcount = 5;
            break;

        case DevFmtX71:
            chanmap = X71Cfg;
            coeffcount = 7;
            break;

        case DevFmtAmbi3D:
            break;
    }

    if(device->FmtChans == DevFmtAmbi3D)
    {
        const char *devname{device->DeviceName.c_str()};
        const std::array<uint8_t,MAX_AMBI_CHANNELS> &acnmap = GetAmbiLayout(device->mAmbiLayout);
        const std::array<float,MAX_AMBI_CHANNELS> &n3dscale = GetAmbiScales(device->mAmbiScale);

        /* For DevFmtAmbi3D, the ambisonic order is already set. */
        const size_t count{AmbiChannelsFromOrder(device->mAmbiOrder)};
        std::transform(acnmap.begin(), acnmap.begin()+count, std::begin(device->Dry.AmbiMap),
            [&n3dscale](const uint8_t &acn) noexcept -> BFChannelConfig
            { return BFChannelConfig{1.0f/n3dscale[acn], acn}; }
        );
        AllocChannels(device, static_cast<ALuint>(count), 0);

        ALfloat nfc_delay{ConfigValueFloat(devname, "decoder", "nfc-ref-delay").value_or(0.0f)};
        if(nfc_delay > 0.0f)
            InitNearFieldCtrl(device, nfc_delay * SPEEDOFSOUNDMETRESPERSEC, device->mAmbiOrder,
                true);
    }
    else
    {
        ChannelDec chancoeffs[MAX_OUTPUT_CHANNELS]{};
        ALuint outcount{0};
        for(size_t i{0u};i < chanmap.size();++i)
        {
            const ALuint idx{GetChannelIdxByName(device->RealOut, chanmap[i].ChanName)};
            if(idx == INVALID_CHANNEL_INDEX)
            {
                ERR("Failed to find %s channel in device\n",
                    GetLabelFromChannel(chanmap[i].ChanName));
                continue;
            }
            outcount = maxu(outcount, idx+1u);
            std::copy_n(chanmap[i].Config, coeffcount, chancoeffs[idx]);
        }

        /* For non-DevFmtAmbi3D, set the ambisonic order given the mixing
         * channel count. Built-in speaker decoders are always 2D, so just
         * reverse that calculation.
         */
        device->mAmbiOrder = (coeffcount-1) / 2;

        std::transform(AmbiIndex::From2D.begin(), AmbiIndex::From2D.begin()+coeffcount,
            std::begin(device->Dry.AmbiMap),
            [](const uint8_t &index) noexcept { return BFChannelConfig{1.0f, index}; }
        );
        AllocChannels(device, coeffcount, device->channelsFromFmt());

        TRACE("Enabling %s-order%s ambisonic decoder\n",
            (coeffcount > 5) ? "third" :
            (coeffcount > 3) ? "second" : "first",
            "");
        device->AmbiDecoder = BFormatDec::Create(coeffcount,
            al::span<const ChannelDec>{chancoeffs, outcount});
    }
}

void InitCustomPanning(ALCdevice *device, bool hqdec, const AmbDecConf *conf,
    const ALuint (&speakermap)[MAX_OUTPUT_CHANNELS])
{
    if(!hqdec && conf->FreqBands != 1)
        ERR("Basic renderer uses the high-frequency matrix as single-band (xover_freq = %.0fhz)\n",
            conf->XOverFreq);

    const ALuint order{(conf->ChanMask > AMBI_2ORDER_MASK) ? 3u :
        (conf->ChanMask > AMBI_1ORDER_MASK) ? 2u : 1u};
    device->mAmbiOrder = order;

    ALuint count;
    if((conf->ChanMask&AMBI_PERIPHONIC_MASK))
    {
        count = static_cast<ALuint>(AmbiChannelsFromOrder(order));
        std::transform(AmbiIndex::FromACN.begin(), AmbiIndex::FromACN.begin()+count,
            std::begin(device->Dry.AmbiMap),
            [](const uint8_t &index) noexcept { return BFChannelConfig{1.0f, index}; }
        );
    }
    else
    {
        count = static_cast<ALuint>(Ambi2DChannelsFromOrder(order));
        std::transform(AmbiIndex::From2D.begin(), AmbiIndex::From2D.begin()+count,
            std::begin(device->Dry.AmbiMap),
            [](const uint8_t &index) noexcept { return BFChannelConfig{1.0f, index}; }
        );
    }
    AllocChannels(device, count, device->channelsFromFmt());

    TRACE("Enabling %s-band %s-order%s ambisonic decoder\n",
        (!hqdec || conf->FreqBands == 1) ? "single" : "dual",
        (conf->ChanMask > AMBI_2ORDER_MASK) ? "third" :
        (conf->ChanMask > AMBI_1ORDER_MASK) ? "second" : "first",
        (conf->ChanMask&AMBI_PERIPHONIC_MASK) ? " periphonic" : ""
    );
    device->AmbiDecoder = BFormatDec::Create(conf, hqdec, count, device->Frequency, speakermap);

    auto accum_spkr_dist = std::bind(std::plus<float>{}, _1,
        std::bind(std::mem_fn(&AmbDecConf::SpeakerConf::Distance), _2));
    const float avg_dist{
        std::accumulate(conf->Speakers.begin(), conf->Speakers.end(), 0.0f, accum_spkr_dist) /
        static_cast<float>(conf->Speakers.size())};
    InitNearFieldCtrl(device, avg_dist, order, !!(conf->ChanMask&AMBI_PERIPHONIC_MASK));

    InitDistanceComp(device, conf, speakermap);
}

void InitHrtfPanning(ALCdevice *device)
{
    constexpr float PI{al::MathDefs<float>::Pi()};
    constexpr float PI_2{PI / 2.0f};
    constexpr float PI_4{PI_2 / 2.0f};
    constexpr float PI3_4{PI_4 * 3.0f};
    static const float CornerElev{static_cast<float>(std::atan2(1.0, std::sqrt(2.0)))};
    static const AngularPoint AmbiPoints1O[]{
        { EvRadians{ CornerElev}, AzRadians{ -PI_4} },
        { EvRadians{ CornerElev}, AzRadians{-PI3_4} },
        { EvRadians{ CornerElev}, AzRadians{  PI_4} },
        { EvRadians{ CornerElev}, AzRadians{ PI3_4} },
        { EvRadians{-CornerElev}, AzRadians{ -PI_4} },
        { EvRadians{-CornerElev}, AzRadians{-PI3_4} },
        { EvRadians{-CornerElev}, AzRadians{  PI_4} },
        { EvRadians{-CornerElev}, AzRadians{ PI3_4} },
    }, AmbiPoints2O[]{
        { EvRadians{      -CornerElev}, AzRadians{            -PI_4} },
        { EvRadians{      -CornerElev}, AzRadians{           -PI3_4} },
        { EvRadians{       CornerElev}, AzRadians{           -PI3_4} },
        { EvRadians{       CornerElev}, AzRadians{            PI3_4} },
        { EvRadians{       CornerElev}, AzRadians{             PI_4} },
        { EvRadians{      -CornerElev}, AzRadians{             PI_4} },
        { EvRadians{      -CornerElev}, AzRadians{            PI3_4} },
        { EvRadians{       CornerElev}, AzRadians{            -PI_4} },
        { EvRadians{-1.205932499e+00f}, AzRadians{            -PI_2} },
        { EvRadians{ 1.205932499e+00f}, AzRadians{             PI_2} },
        { EvRadians{-1.205932499e+00f}, AzRadians{             PI_2} },
        { EvRadians{ 1.205932499e+00f}, AzRadians{            -PI_2} },
        { EvRadians{             0.0f}, AzRadians{-1.205932499e+00f} },
        { EvRadians{             0.0f}, AzRadians{-1.935660155e+00f} },
        { EvRadians{             0.0f}, AzRadians{ 1.205932499e+00f} },
        { EvRadians{             0.0f}, AzRadians{ 1.935660155e+00f} },
        { EvRadians{-3.648638281e-01f}, AzRadians{               PI} },
        { EvRadians{ 3.648638281e-01f}, AzRadians{               PI} },
        { EvRadians{ 3.648638281e-01f}, AzRadians{             0.0f} },
        { EvRadians{-3.648638281e-01f}, AzRadians{             0.0f} },
    };
    static const float AmbiMatrix1O[][MAX_AMBI_CHANNELS]{
        { 1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f },
        { 1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f },
        { 1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f,  1.250000000e-01f },
        { 1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f },
        { 1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f },
        { 1.250000000e-01f,  1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f },
        { 1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f,  1.250000000e-01f },
        { 1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f, -1.250000000e-01f },
    }, AmbiMatrix2O[][MAX_AMBI_CHANNELS]{
        { 5.000000000e-02f,  5.000000000e-02f, -5.000000000e-02f,  5.000000000e-02f,  6.454972244e-02f, -6.454972244e-02f,  0.000000000e+00f, -6.454972244e-02f,  0.000000000e+00f },
        { 5.000000000e-02f,  5.000000000e-02f, -5.000000000e-02f, -5.000000000e-02f, -6.454972244e-02f, -6.454972244e-02f,  0.000000000e+00f,  6.454972244e-02f,  0.000000000e+00f },
        { 5.000000000e-02f,  5.000000000e-02f,  5.000000000e-02f, -5.000000000e-02f, -6.454972244e-02f,  6.454972244e-02f,  0.000000000e+00f, -6.454972244e-02f,  0.000000000e+00f },
        { 5.000000000e-02f, -5.000000000e-02f,  5.000000000e-02f, -5.000000000e-02f,  6.454972244e-02f, -6.454972244e-02f,  0.000000000e+00f, -6.454972244e-02f,  0.000000000e+00f },
        { 5.000000000e-02f, -5.000000000e-02f,  5.000000000e-02f,  5.000000000e-02f, -6.454972244e-02f, -6.454972244e-02f,  0.000000000e+00f,  6.454972244e-02f,  0.000000000e+00f },
        { 5.000000000e-02f, -5.000000000e-02f, -5.000000000e-02f,  5.000000000e-02f, -6.454972244e-02f,  6.454972244e-02f,  0.000000000e+00f, -6.454972244e-02f,  0.000000000e+00f },
        { 5.000000000e-02f, -5.000000000e-02f, -5.000000000e-02f, -5.000000000e-02f,  6.454972244e-02f,  6.454972244e-02f,  0.000000000e+00f,  6.454972244e-02f,  0.000000000e+00f },
        { 5.000000000e-02f,  5.000000000e-02f,  5.000000000e-02f,  5.000000000e-02f,  6.454972244e-02f,  6.454972244e-02f,  0.000000000e+00f,  6.454972244e-02f,  0.000000000e+00f },
        { 5.000000000e-02f,  3.090169944e-02f, -8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -6.454972244e-02f,  9.045084972e-02f,  0.000000000e+00f, -1.232790000e-02f },
        { 5.000000000e-02f, -3.090169944e-02f,  8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -6.454972244e-02f,  9.045084972e-02f,  0.000000000e+00f, -1.232790000e-02f },
        { 5.000000000e-02f, -3.090169944e-02f, -8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f,  6.454972244e-02f,  9.045084972e-02f,  0.000000000e+00f, -1.232790000e-02f },
        { 5.000000000e-02f,  3.090169944e-02f,  8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f,  6.454972244e-02f,  9.045084972e-02f,  0.000000000e+00f, -1.232790000e-02f },
        { 5.000000000e-02f,  8.090169944e-02f,  0.000000000e+00f,  3.090169944e-02f,  6.454972244e-02f,  0.000000000e+00f, -5.590169944e-02f,  0.000000000e+00f, -7.216878365e-02f },
        { 5.000000000e-02f,  8.090169944e-02f,  0.000000000e+00f, -3.090169944e-02f, -6.454972244e-02f,  0.000000000e+00f, -5.590169944e-02f,  0.000000000e+00f, -7.216878365e-02f },
        { 5.000000000e-02f, -8.090169944e-02f,  0.000000000e+00f,  3.090169944e-02f, -6.454972244e-02f,  0.000000000e+00f, -5.590169944e-02f,  0.000000000e+00f, -7.216878365e-02f },
        { 5.000000000e-02f, -8.090169944e-02f,  0.000000000e+00f, -3.090169944e-02f,  6.454972244e-02f,  0.000000000e+00f, -5.590169944e-02f,  0.000000000e+00f, -7.216878365e-02f },
        { 5.000000000e-02f,  0.000000000e+00f, -3.090169944e-02f, -8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -3.454915028e-02f,  6.454972244e-02f,  8.449668365e-02f },
        { 5.000000000e-02f,  0.000000000e+00f,  3.090169944e-02f, -8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -3.454915028e-02f, -6.454972244e-02f,  8.449668365e-02f },
        { 5.000000000e-02f,  0.000000000e+00f,  3.090169944e-02f,  8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -3.454915028e-02f,  6.454972244e-02f,  8.449668365e-02f },
        { 5.000000000e-02f,  0.000000000e+00f, -3.090169944e-02f,  8.090169944e-02f,  0.000000000e+00f,  0.000000000e+00f, -3.454915028e-02f, -6.454972244e-02f,  8.449668365e-02f },
    };
    static const float AmbiOrderHFGain1O[MAX_AMBI_ORDER+1]{
        2.000000000e+00f, 1.154700538e+00f
    }, AmbiOrderHFGain2O[MAX_AMBI_ORDER+1]{
        2.357022604e+00f, 1.825741858e+00f, 9.428090416e-01f
    };

    static_assert(al::size(AmbiPoints1O) == al::size(AmbiMatrix1O), "First-Order Ambisonic HRTF mismatch");
    static_assert(al::size(AmbiPoints2O) == al::size(AmbiMatrix2O), "Second-Order Ambisonic HRTF mismatch");

    /* Don't bother with HOA when using full HRTF rendering. Nothing needs it,
     * and it eases the CPU/memory load.
     */
    device->mRenderMode = HrtfRender;
    ALuint ambi_order{1};
    if(auto modeopt = ConfigValueStr(device->DeviceName.c_str(), nullptr, "hrtf-mode"))
    {
        struct HrtfModeEntry {
            char name[8];
            RenderMode mode;
            ALuint order;
        };
        static const HrtfModeEntry hrtf_modes[]{
            { "full", HrtfRender, 1 },
            { "ambi1", NormalRender, 1 },
            { "ambi2", NormalRender, 2 },
        };

        const char *mode{modeopt->c_str()};
        if(al::strcasecmp(mode, "basic") == 0 || al::strcasecmp(mode, "ambi3") == 0)
        {
            ERR("HRTF mode \"%s\" deprecated, substituting \"%s\"\n", mode, "ambi2");
            mode = "ambi2";
        }

        auto match_entry = [mode](const HrtfModeEntry &entry) -> bool
        { return al::strcasecmp(mode, entry.name) == 0; };
        auto iter = std::find_if(std::begin(hrtf_modes), std::end(hrtf_modes), match_entry);
        if(iter == std::end(hrtf_modes))
            ERR("Unexpected hrtf-mode: %s\n", mode);
        else
        {
            device->mRenderMode = iter->mode;
            ambi_order = iter->order;
        }
    }
    TRACE("%u%s order %sHRTF rendering enabled, using \"%s\"\n", ambi_order,
        (((ambi_order%100)/10) == 1) ? "th" :
        ((ambi_order%10) == 1) ? "st" :
        ((ambi_order%10) == 2) ? "nd" :
        ((ambi_order%10) == 3) ? "rd" : "th",
        (device->mRenderMode == HrtfRender) ? "+ Full " : "",
        device->HrtfName.c_str());

    al::span<const AngularPoint> AmbiPoints{AmbiPoints1O};
    const float (*AmbiMatrix)[MAX_AMBI_CHANNELS]{AmbiMatrix1O};
    al::span<const float,MAX_AMBI_ORDER+1> AmbiOrderHFGain{AmbiOrderHFGain1O};
    if(ambi_order >= 2)
    {
        AmbiPoints = AmbiPoints2O;
        AmbiMatrix = AmbiMatrix2O;
        AmbiOrderHFGain = AmbiOrderHFGain2O;
    }
    device->mAmbiOrder = ambi_order;

    const size_t count{AmbiChannelsFromOrder(ambi_order)};
    device->mHrtfState = DirectHrtfState::Create(count);

    std::transform(AmbiIndex::FromACN.begin(), AmbiIndex::FromACN.begin()+count,
        std::begin(device->Dry.AmbiMap),
        [](const uint8_t &index) noexcept { return BFChannelConfig{1.0f, index}; }
    );
    AllocChannels(device, static_cast<ALuint>(count), device->channelsFromFmt());

    BuildBFormatHrtf(device->mHrtf, device->mHrtfState.get(), AmbiPoints, AmbiMatrix,
        AmbiOrderHFGain);

    HrtfStore *Hrtf{device->mHrtf};
    InitNearFieldCtrl(device, Hrtf->field[0].distance, ambi_order, true);
}

void InitUhjPanning(ALCdevice *device)
{
    /* UHJ is always 2D first-order. */
    constexpr size_t count{Ambi2DChannelsFromOrder(1)};

    device->mAmbiOrder = 1;

    auto acnmap_end = AmbiIndex::FromFuMa.begin() + count;
    std::transform(AmbiIndex::FromFuMa.begin(), acnmap_end, std::begin(device->Dry.AmbiMap),
        [](const uint8_t &acn) noexcept -> BFChannelConfig
        { return BFChannelConfig{1.0f/AmbiScale::FromFuMa[acn], acn}; }
    );
    AllocChannels(device, ALuint{count}, device->channelsFromFmt());
}

} // namespace

void aluInitRenderer(ALCdevice *device, ALint hrtf_id, HrtfRequestMode hrtf_appreq, HrtfRequestMode hrtf_userreq)
{
    /* Hold the HRTF the device last used, in case it's used again. */
    HrtfStore *old_hrtf{device->mHrtf};

    device->mHrtfState = nullptr;
    device->mHrtf = nullptr;
    device->HrtfName.clear();
    device->mRenderMode = NormalRender;

    if(device->FmtChans != DevFmtStereo)
    {
        if(old_hrtf)
            old_hrtf->DecRef();
        old_hrtf = nullptr;
        if(hrtf_appreq == Hrtf_Enable)
            device->HrtfStatus = ALC_HRTF_UNSUPPORTED_FORMAT_SOFT;

        const char *layout{nullptr};
        switch(device->FmtChans)
        {
            case DevFmtQuad: layout = "quad"; break;
            case DevFmtX51: /* fall-through */
            case DevFmtX51Rear: layout = "surround51"; break;
            case DevFmtX61: layout = "surround61"; break;
            case DevFmtX71: layout = "surround71"; break;
            /* Mono, Stereo, and Ambisonics output don't use custom decoders. */
            case DevFmtMono:
            case DevFmtStereo:
            case DevFmtAmbi3D:
                break;
        }

        const char *devname{device->DeviceName.c_str()};
        ALuint speakermap[MAX_OUTPUT_CHANNELS];
        AmbDecConf *pconf{nullptr};
        AmbDecConf conf{};
        if(layout)
        {
            if(auto decopt = ConfigValueStr(devname, "decoder", layout))
            {
                if(!conf.load(decopt->c_str()))
                    ERR("Failed to load layout file %s\n", decopt->c_str());
                else if(conf.Speakers.size() > MAX_OUTPUT_CHANNELS)
                    ERR("Unsupported speaker count %zu (max %d)\n", conf.Speakers.size(),
                        MAX_OUTPUT_CHANNELS);
                else if(conf.ChanMask > AMBI_3ORDER_MASK)
                    ERR("Unsupported channel mask 0x%04x (max 0x%x)\n", conf.ChanMask,
                        AMBI_3ORDER_MASK);
                else if(MakeSpeakerMap(device, &conf, speakermap))
                    pconf = &conf;
            }
        }

        if(!pconf)
            InitPanning(device);
        else
        {
            int hqdec{GetConfigValueBool(devname, "decoder", "hq-mode", 1)};
            InitCustomPanning(device, !!hqdec, pconf, speakermap);
        }
        if(device->AmbiDecoder)
            device->PostProcess = &ALCdevice::ProcessAmbiDec;
        return;
    }

    bool headphones{device->IsHeadphones != AL_FALSE};
    if(device->Type != Loopback)
    {
        if(auto modeopt = ConfigValueStr(device->DeviceName.c_str(), nullptr, "stereo-mode"))
        {
            const char *mode{modeopt->c_str()};
            if(al::strcasecmp(mode, "headphones") == 0)
                headphones = true;
            else if(al::strcasecmp(mode, "speakers") == 0)
                headphones = false;
            else if(al::strcasecmp(mode, "auto") != 0)
                ERR("Unexpected stereo-mode: %s\n", mode);
        }
    }

    if(hrtf_userreq == Hrtf_Default)
    {
        bool usehrtf = (headphones && hrtf_appreq != Hrtf_Disable) ||
                       (hrtf_appreq == Hrtf_Enable);
        if(!usehrtf) goto no_hrtf;

        device->HrtfStatus = ALC_HRTF_ENABLED_SOFT;
        if(headphones && hrtf_appreq != Hrtf_Disable)
            device->HrtfStatus = ALC_HRTF_HEADPHONES_DETECTED_SOFT;
    }
    else
    {
        if(hrtf_userreq != Hrtf_Enable)
        {
            if(hrtf_appreq == Hrtf_Enable)
                device->HrtfStatus = ALC_HRTF_DENIED_SOFT;
            goto no_hrtf;
        }
        device->HrtfStatus = ALC_HRTF_REQUIRED_SOFT;
    }

    if(device->HrtfList.empty())
        device->HrtfList = EnumerateHrtf(device->DeviceName.c_str());

    if(hrtf_id >= 0 && static_cast<ALuint>(hrtf_id) < device->HrtfList.size())
    {
        const char *devname{device->DeviceName.c_str()};
        const std::string &hrtfname = device->HrtfList[static_cast<ALuint>(hrtf_id)];
        if(HrtfStore *hrtf{GetLoadedHrtf(hrtfname, devname, device->Frequency)})
        {
            device->mHrtf = hrtf;
            device->HrtfName = hrtfname;
        }
    }

    if(!device->mHrtf)
    {
        const char *devname{device->DeviceName.c_str()};
        auto find_hrtf = [device,devname](const std::string &hrtfname) -> bool
        {
            HrtfStore *hrtf{GetLoadedHrtf(hrtfname, devname, device->Frequency)};
            if(!hrtf) return false;
            device->mHrtf = hrtf;
            device->HrtfName = hrtfname;
            return true;
        };
        std::find_if(device->HrtfList.cbegin(), device->HrtfList.cend(), find_hrtf);
    }

    if(device->mHrtf)
    {
        if(old_hrtf)
            old_hrtf->DecRef();
        old_hrtf = nullptr;

        InitHrtfPanning(device);
        device->PostProcess = &ALCdevice::ProcessHrtf;
        return;
    }
    device->HrtfStatus = ALC_HRTF_UNSUPPORTED_FORMAT_SOFT;

no_hrtf:
    if(old_hrtf)
        old_hrtf->DecRef();
    old_hrtf = nullptr;

    device->mRenderMode = StereoPair;

    if(device->Type != Loopback)
    {
        if(auto cflevopt = ConfigValueInt(device->DeviceName.c_str(), nullptr, "cf_level"))
        {
            if(*cflevopt > 0 && *cflevopt <= 6)
            {
                device->Bs2b = al::make_unique<bs2b>();
                bs2b_set_params(device->Bs2b.get(), *cflevopt,
                    static_cast<int>(device->Frequency));
                TRACE("BS2B enabled\n");
                InitPanning(device);
                device->PostProcess = &ALCdevice::ProcessBs2b;
                return;
            }
        }
    }

    if(auto encopt = ConfigValueStr(device->DeviceName.c_str(), nullptr, "stereo-encoding"))
    {
        const char *mode{encopt->c_str()};
        if(al::strcasecmp(mode, "uhj") == 0)
            device->mRenderMode = NormalRender;
        else if(al::strcasecmp(mode, "panpot") != 0)
            ERR("Unexpected stereo-encoding: %s\n", mode);
    }
    if(device->mRenderMode == NormalRender)
    {
        device->Uhj_Encoder = al::make_unique<Uhj2Encoder>();
        TRACE("UHJ enabled\n");
        InitUhjPanning(device);
        device->PostProcess = &ALCdevice::ProcessUhj;
        return;
    }

    TRACE("Stereo rendering\n");
    InitPanning(device);
    device->PostProcess = &ALCdevice::ProcessAmbiDec;
}


void aluInitEffectPanning(ALeffectslot *slot, ALCdevice *device)
{
    const size_t count{AmbiChannelsFromOrder(device->mAmbiOrder)};
    slot->MixBuffer.resize(count);
    slot->MixBuffer.shrink_to_fit();

    auto acnmap_end = AmbiIndex::FromACN.begin() + count;
    auto iter = std::transform(AmbiIndex::FromACN.begin(), acnmap_end, slot->Wet.AmbiMap.begin(),
        [](const uint8_t &acn) noexcept -> BFChannelConfig
        { return BFChannelConfig{1.0f, acn}; }
    );
    std::fill(iter, slot->Wet.AmbiMap.end(), BFChannelConfig{});
    slot->Wet.Buffer = {slot->MixBuffer.data(), slot->MixBuffer.size()};
}


void CalcAmbiCoeffs(const float y, const float z, const float x, const float spread,
    const al::span<float,MAX_AMBI_CHANNELS> coeffs)
{
    /* Zeroth-order */
    coeffs[0]  = 1.0f; /* ACN 0 = 1 */
    /* First-order */
    coeffs[1]  = 1.732050808f * y; /* ACN 1 = sqrt(3) * Y */
    coeffs[2]  = 1.732050808f * z; /* ACN 2 = sqrt(3) * Z */
    coeffs[3]  = 1.732050808f * x; /* ACN 3 = sqrt(3) * X */
    /* Second-order */
    const float xx{x*x}, yy{y*y}, zz{z*z};
    coeffs[4]  = 3.872983346f * x * y;            /* ACN 4 = sqrt(15) * X * Y */
    coeffs[5]  = 3.872983346f * y * z;            /* ACN 5 = sqrt(15) * Y * Z */
    coeffs[6]  = 1.118033989f * (3.0f*zz - 1.0f); /* ACN 6 = sqrt(5)/2 * (3*Z*Z - 1) */
    coeffs[7]  = 3.872983346f * x * z;            /* ACN 7 = sqrt(15) * X * Z */
    coeffs[8]  = 1.936491673f * (xx - yy);        /* ACN 8 = sqrt(15)/2 * (X*X - Y*Y) */
    /* Third-order */
    coeffs[9]  =  2.091650066f * y * (3.0f*xx - yy);   /* ACN  9 = sqrt(35/8) * Y * (3*X*X - Y*Y) */
    coeffs[10] = 10.246950766f * z * x * y;            /* ACN 10 = sqrt(105) * Z * X * Y */
    coeffs[11] =  1.620185175f * y * (5.0f*zz - 1.0f); /* ACN 11 = sqrt(21/8) * Y * (5*Z*Z - 1) */
    coeffs[12] =  1.322875656f * z * (5.0f*zz - 3.0f); /* ACN 12 = sqrt(7)/2 * Z * (5*Z*Z - 3) */
    coeffs[13] =  1.620185175f * x * (5.0f*zz - 1.0f); /* ACN 13 = sqrt(21/8) * X * (5*Z*Z - 1) */
    coeffs[14] =  5.123475383f * z * (xx - yy);        /* ACN 14 = sqrt(105)/2 * Z * (X*X - Y*Y) */
    coeffs[15] =  2.091650066f * x * (xx - 3.0f*yy);   /* ACN 15 = sqrt(35/8) * X * (X*X - 3*Y*Y) */
    /* Fourth-order */
    /* ACN 16 = sqrt(35)*3/2 * X * Y * (X*X - Y*Y) */
    /* ACN 17 = sqrt(35/2)*3/2 * (3*X*X - Y*Y) * Y * Z */
    /* ACN 18 = sqrt(5)*3/2 * X * Y * (7*Z*Z - 1) */
    /* ACN 19 = sqrt(5/2)*3/2 * Y * Z * (7*Z*Z - 3)  */
    /* ACN 20 = 3/8 * (35*Z*Z*Z*Z - 30*Z*Z + 3) */
    /* ACN 21 = sqrt(5/2)*3/2 * X * Z * (7*Z*Z - 3) */
    /* ACN 22 = sqrt(5)*3/4 * (X*X - Y*Y) * (7*Z*Z - 1) */
    /* ACN 23 = sqrt(35/2)*3/2 * (X*X - 3*Y*Y) * X * Z */
    /* ACN 24 = sqrt(35)*3/8 * (X*X*X*X - 6*X*X*Y*Y + Y*Y*Y*Y) */

    if(spread > 0.0f)
    {
        /* Implement the spread by using a spherical source that subtends the
         * angle spread. See:
         * http://www.ppsloan.org/publications/StupidSH36.pdf - Appendix A3
         *
         * When adjusted for N3D normalization instead of SN3D, these
         * calculations are:
         *
         * ZH0 = -sqrt(pi) * (-1+ca);
         * ZH1 =  0.5*sqrt(pi) * sa*sa;
         * ZH2 = -0.5*sqrt(pi) * ca*(-1+ca)*(ca+1);
         * ZH3 = -0.125*sqrt(pi) * (-1+ca)*(ca+1)*(5*ca*ca - 1);
         * ZH4 = -0.125*sqrt(pi) * ca*(-1+ca)*(ca+1)*(7*ca*ca - 3);
         * ZH5 = -0.0625*sqrt(pi) * (-1+ca)*(ca+1)*(21*ca*ca*ca*ca - 14*ca*ca + 1);
         *
         * The gain of the source is compensated for size, so that the
         * loudness doesn't depend on the spread. Thus:
         *
         * ZH0 = 1.0f;
         * ZH1 = 0.5f * (ca+1.0f);
         * ZH2 = 0.5f * (ca+1.0f)*ca;
         * ZH3 = 0.125f * (ca+1.0f)*(5.0f*ca*ca - 1.0f);
         * ZH4 = 0.125f * (ca+1.0f)*(7.0f*ca*ca - 3.0f)*ca;
         * ZH5 = 0.0625f * (ca+1.0f)*(21.0f*ca*ca*ca*ca - 14.0f*ca*ca + 1.0f);
         */
        const float ca{std::cos(spread * 0.5f)};
        /* Increase the source volume by up to +3dB for a full spread. */
        const float scale{std::sqrt(1.0f + spread/al::MathDefs<float>::Tau())};

        const float ZH0_norm{scale};
        const float ZH1_norm{scale * 0.5f * (ca+1.f)};
        const float ZH2_norm{scale * 0.5f * (ca+1.f)*ca};
        const float ZH3_norm{scale * 0.125f * (ca+1.f)*(5.f*ca*ca-1.f)};

        /* Zeroth-order */
        coeffs[0]  *= ZH0_norm;
        /* First-order */
        coeffs[1]  *= ZH1_norm;
        coeffs[2]  *= ZH1_norm;
        coeffs[3]  *= ZH1_norm;
        /* Second-order */
        coeffs[4]  *= ZH2_norm;
        coeffs[5]  *= ZH2_norm;
        coeffs[6]  *= ZH2_norm;
        coeffs[7]  *= ZH2_norm;
        coeffs[8]  *= ZH2_norm;
        /* Third-order */
        coeffs[9]  *= ZH3_norm;
        coeffs[10] *= ZH3_norm;
        coeffs[11] *= ZH3_norm;
        coeffs[12] *= ZH3_norm;
        coeffs[13] *= ZH3_norm;
        coeffs[14] *= ZH3_norm;
        coeffs[15] *= ZH3_norm;
    }
}

void ComputePanGains(const MixParams *mix, const float*RESTRICT coeffs, const float ingain,
    const al::span<float,MAX_OUTPUT_CHANNELS> gains)
{
    auto ambimap = mix->AmbiMap.cbegin();

    auto iter = std::transform(ambimap, ambimap+mix->Buffer.size(), gains.begin(),
        [coeffs,ingain](const BFChannelConfig &chanmap) noexcept -> float
        { return chanmap.Scale * coeffs[chanmap.Index] * ingain; }
    );
    std::fill(iter, gains.end(), 0.0f);
}
