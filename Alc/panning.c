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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"
#include "alu.h"
#include "bool.h"


void ComputeAngleGains(const ALCdevice *device, ALfloat angle, ALfloat elevation, ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALfloat dir[3] = {
        sinf(angle) * cosf(elevation),
        sinf(elevation),
        -cosf(angle) * cosf(elevation)
    };
    ComputeDirectionalGains(device, dir, ingain, gains);
}

void ComputeDirectionalGains(const ALCdevice *device, const ALfloat dir[3], ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALfloat coeffs[MAX_AMBI_COEFFS];
    ALuint i, j;
    /* Convert from OpenAL coords to Ambisonics. */
    ALfloat x = -dir[2];
    ALfloat y = -dir[0];
    ALfloat z =  dir[1];

    coeffs[0] = 0.7071f; /* sqrt(1.0 / 2.0) */
    coeffs[1] = x; /* X */
    coeffs[2] = y; /* Y */
    coeffs[3] = z; /* Z */
    coeffs[4] = 0.5f * (3.0f*z*z - 1.0f); /* 0.5 * (3*Z*Z - 1) */
    coeffs[5] = 2.0f * z * x; /* 2*Z*X */
    coeffs[6] = 2.0f * y * z; /* 2*Y*Z */
    coeffs[7] = x*x - y*y; /* X*X - Y*Y */
    coeffs[8] = 2.0f * x * y; /* 2*X*Y */
    coeffs[9] = 0.5f * z * (5.0f*z*z - 3.0f); /* 0.5 * Z * (5*Z*Z - 3) */
    coeffs[10] = 0.7262f * x * (5.0f*z*z - 1.0f); /* sqrt(135.0 / 256.0) * X * (5*Z*Z - 1) */
    coeffs[11] = 0.7262f * y * (5.0f*z*z - 1.0f); /* sqrt(135.0 / 256.0) * Y * (5*Z*Z - 1) */
    coeffs[12] = 2.5981f * z * (x*x - y*y); /* sqrt(27.0 / 4.0) * Z * (X*X - Y*Y) */
    coeffs[13] = 5.1962f * x * y * z; /* sqrt(27) * X * Y * Z */
    coeffs[14] = x * (x*x - 3.0f*y*y); /* X * (X*X - 3*Y*Y) */
    coeffs[15] = y * (3.0f*x*x - y*y); /* Y * (3*X*X - Y*Y) */

    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
    for(i = 0;i < device->NumChannels;i++)
    {
        for(j = 0;j < MAX_AMBI_COEFFS;j++)
            gains[i] += device->Channel[i].HOACoeff[j]*coeffs[j];
        gains[i] *= ingain;
    }
}

void ComputeAmbientGains(const ALCdevice *device, ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALuint i;

    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
    for(i = 0;i < device->NumChannels;i++)
    {
        // The W coefficients are based on a mathematical average of the
        // output, scaled by sqrt(2) to compensate for FuMa-style Ambisonics
        // scaling the W channel input by sqrt(0.5). The square root of the
        // base average provides for a more perceptual average volume, better
        // suited to non-directional gains.
        gains[i] = sqrtf(device->Channel[i].HOACoeff[0]/1.4142f) * ingain;
    }
}

void ComputeBFormatGains(const ALCdevice *device, const ALfloat mtx[4], ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALuint i, j;

    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
    for(i = 0;i < device->NumChannels;i++)
    {
        for(j = 0;j < 4;j++)
            gains[i] += device->Channel[i].FOACoeff[j] * mtx[j];
        gains[i] *= ingain;
    }
}


DECL_CONST static inline const char *GetLabelFromChannel(enum Channel channel)
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

        case TopFrontLeft: return "top-front-left";
        case TopFrontRight: return "top-front-right";
        case TopBackLeft: return "top-back-left";
        case TopBackRight: return "top-back-right";
        case BottomFrontLeft: return "bottom-front-left";
        case BottomFrontRight: return "bottom-front-right";
        case BottomBackLeft: return "bottom-back-left";
        case BottomBackRight: return "bottom-back-right";

        case Aux0: return "aux-0";
        case Aux1: return "aux-1";
        case Aux2: return "aux-2";
        case Aux3: return "aux-3";

        case InvalidChannel: break;
    }
    return "(unknown)";
}


typedef struct ChannelMap {
    enum Channel ChanName;
    ChannelConfig Config;
} ChannelMap;

static void SetChannelMap(ALCdevice *device, const ChannelMap *chanmap, size_t count)
{
    size_t i, j;

    for(i = 0;i < MAX_OUTPUT_CHANNELS && device->ChannelName[i] != InvalidChannel;i++)
    {
        if(device->ChannelName[i] == LFE)
        {
            for(j = 0;j < MAX_AMBI_COEFFS;j++)
                device->Channel[i].HOACoeff[j] = 0.0f;
            for(j = 0;j < 4;j++)
                device->Channel[i].FOACoeff[j] = 0.0f;
            continue;
        }

        for(j = 0;j < count;j++)
        {
            if(device->ChannelName[i] == chanmap[j].ChanName)
            {
                device->Channel[i] = chanmap[j].Config;
                break;
            }
        }
        if(j == count)
            ERR("Failed to match %s channel ("SZFMT") in config\n", GetLabelFromChannel(device->ChannelName[i]), i);
    }
    device->NumChannels = i;
}

static bool LoadChannelSetup(ALCdevice *device)
{
    static const enum Channel mono_chans[1] = {
        FrontCenter
    }, stereo_chans[2] = {
        FrontLeft, FrontRight
    }, quad_chans[4] = {
        FrontLeft, FrontRight,
        BackLeft, BackRight
    }, surround51_chans[5] = {
        FrontLeft, FrontRight, FrontCenter,
        SideLeft, SideRight
    }, surround51rear_chans[5] = {
        FrontLeft, FrontRight, FrontCenter,
        BackLeft, BackRight
    }, surround61_chans[6] = {
        FrontLeft, FrontRight,
        FrontCenter, BackCenter,
        SideLeft, SideRight
    }, surround71_chans[7] = {
        FrontLeft, FrontRight, FrontCenter,
        BackLeft, BackRight,
        SideLeft, SideRight
    };
    ChannelMap chanmap[MAX_OUTPUT_CHANNELS];
    const enum Channel *channels = NULL;
    const char *layout = NULL;
    size_t count = 0;
    size_t i;

    switch(device->FmtChans)
    {
        case DevFmtMono:
            layout = "mono";
            channels = mono_chans;
            count = COUNTOF(mono_chans);
            break;
        case DevFmtStereo:
            layout = "stereo";
            channels = stereo_chans;
            count = COUNTOF(stereo_chans);
            break;
        case DevFmtQuad:
            layout = "quad";
            channels = quad_chans;
            count = COUNTOF(quad_chans);
            break;
        case DevFmtX51:
            layout = "surround51";
            channels = surround51_chans;
            count = COUNTOF(surround51_chans);
            break;
        case DevFmtX51Rear:
            layout = "surround51rear";
            channels = surround51rear_chans;
            count = COUNTOF(surround51rear_chans);
            break;
        case DevFmtX61:
            layout = "surround61";
            channels = surround61_chans;
            count = COUNTOF(surround61_chans);
            break;
        case DevFmtX71:
            layout = "surround71";
            channels = surround71_chans;
            count = COUNTOF(surround71_chans);
            break;
        case DevFmtBFormat3D:
            break;
    }

    if(!layout)
        return false;
    else
    {
        char name[32];
        snprintf(name, sizeof(name), "%s/enable", layout);
        if(!GetConfigValueBool("layouts", name, 0))
            return false;
    }

    for(i = 0;i < count;i++)
    {
        const char *channame;
        char chanlayout[32];
        const char *value;
        int props;

        chanmap[i].ChanName = channels[i];
        channame = GetLabelFromChannel(channels[i]);

        snprintf(chanlayout, sizeof(chanlayout), "%s/%s", layout, channame);
        if(!ConfigValueStr("layouts", chanlayout, &value))
        {
            ERR("Missing channel %s\n", channame);
            return false;
        }
        props = sscanf(value, " %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f ; %f %f %f %f",
            &chanmap[i].Config.HOACoeff[0], &chanmap[i].Config.HOACoeff[1], &chanmap[i].Config.HOACoeff[2],
            &chanmap[i].Config.HOACoeff[3], &chanmap[i].Config.HOACoeff[4], &chanmap[i].Config.HOACoeff[5],
            &chanmap[i].Config.HOACoeff[6], &chanmap[i].Config.HOACoeff[7], &chanmap[i].Config.HOACoeff[8],
            &chanmap[i].Config.HOACoeff[9], &chanmap[i].Config.HOACoeff[10], &chanmap[i].Config.HOACoeff[11],
            &chanmap[i].Config.HOACoeff[12], &chanmap[i].Config.HOACoeff[13], &chanmap[i].Config.HOACoeff[14],
            &chanmap[i].Config.HOACoeff[15], &chanmap[i].Config.FOACoeff[0], &chanmap[i].Config.FOACoeff[1],
            &chanmap[i].Config.FOACoeff[2], &chanmap[i].Config.FOACoeff[3]);
        if(props == 0)
        {
            ERR("Failed to parse %s channel's properties\n", channame);
            return false;
        }

        if(props < 20)
        {
            ERR("Failed to parse %s channel's elements (expected 20, got %d)\n", channame, props);
            return false;
        }
    }
    SetChannelMap(device, chanmap, count);
    return true;
}

ALvoid aluInitPanning(ALCdevice *device)
{
    static const ChannelMap MonoCfg[1] = {
        { FrontCenter, { { 1.4142f }, { 1.4142f } } },
    }, StereoCfg[2] = {
        { FrontLeft,   { { 0.7071f, 0.0f,  0.5f, 0.0f }, { 0.7071f, 0.0f,  0.5f, 0.0f } } },
        { FrontRight,  { { 0.7071f, 0.0f, -0.5f, 0.0f }, { 0.7071f, 0.0f, -0.5f, 0.0f } } },
    }, QuadCfg[4] = {
        { FrontLeft,   { { 0.353558f,  0.306181f,  0.306192f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.117183f }, { 0.353553f,  0.250000f,  0.250000f, 0.0f  } } },
        { FrontRight,  { { 0.353558f,  0.306181f, -0.306192f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.117183f }, { 0.353553f,  0.250000f, -0.250000f, 0.0f  } } },
        { BackLeft,    { { 0.353543f, -0.306192f,  0.306181f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.117193f }, { 0.353553f, -0.250000f,  0.250000f, 0.0f  } } },
        { BackRight,   { { 0.353543f, -0.306192f, -0.306181f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.117193f }, { 0.353553f, -0.250000f, -0.250000f, 0.0f  } } },
    }, X51SideCfg[5] = {
        { FrontLeft,   { { 0.208954f,  0.212846f,  0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f,  0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f,  0.047490f }, { 0.208954f,  0.162905f,  0.182425f, 0.0f } } },
        { FrontRight,  { { 0.208954f,  0.212846f, -0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f, -0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f, -0.047490f }, { 0.208954f,  0.162905f, -0.182425f, 0.0f } } },
        { FrontCenter, { { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f,  0.000000f }, { 0.109403f,  0.137375f,  0.000000f, 0.0f } } },
        { SideLeft,    { { 0.470936f, -0.369626f,  0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f, -0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f, -0.043968f }, { 0.470934f, -0.282903f,  0.267406f, 0.0f } } },
        { SideRight,   { { 0.470936f, -0.369626f, -0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f,  0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f,  0.043968f }, { 0.470934f, -0.282903f, -0.267406f, 0.0f } } },
    }, X51RearCfg[5] = {
        { FrontLeft,   { { 0.208954f,  0.212846f,  0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f,  0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f,  0.047490f }, { 0.208954f,  0.162905f,  0.182425f, 0.0f } } },
        { FrontRight,  { { 0.208954f,  0.212846f, -0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f, -0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f, -0.047490f }, { 0.208954f,  0.162905f, -0.182425f, 0.0f } } },
        { FrontCenter, { { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f,  0.000000f }, { 0.109403f,  0.137375f,  0.000000f, 0.0f } } },
        { BackLeft,    { { 0.470936f, -0.369626f,  0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f, -0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f, -0.043968f }, { 0.470934f, -0.282903f,  0.267406f, 0.0f } } },
        { BackRight,   { { 0.470936f, -0.369626f, -0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f,  0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f,  0.043968f }, { 0.470934f, -0.282903f, -0.267406f, 0.0f } } },
    }, X61Cfg[6] = {
        { FrontLeft,   { { 0.167065f,  0.200583f,  0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f,  0.068910f }, { 0.167065f,  0.153519f,  0.132175f, 0.0f } } },
        { FrontRight,  { { 0.167065f,  0.200583f, -0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f, -0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f, -0.068910f }, { 0.167065f,  0.153519f, -0.132175f, 0.0f } } },
        { FrontCenter, { { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f,  0.000000f }, { 0.109403f,  0.137375f,  0.000000f, 0.0f } } },
        { BackCenter,  { { 0.353556f, -0.461940f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.165723f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.000000f }, { 0.353556f, -0.353554f,  0.000000f, 0.0f } } },
        { SideLeft,    { { 0.289151f, -0.081301f,  0.401292f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f, -0.071420f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f, -0.032897f }, { 0.289151f, -0.062225f,  0.307136f, 0.0f } } },
        { SideRight,   { { 0.289151f, -0.081301f, -0.401292f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f,  0.071420f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f,  0.032897f }, { 0.289151f, -0.062225f, -0.307136f, 0.0f } } },
    }, X71Cfg[7] = {
        { FrontLeft,   { { 0.167065f,  0.200583f,  0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f,  0.068910f }, { 0.167065f,  0.153519f,  0.132175f, 0.0f } } },
        { FrontRight,  { { 0.167065f,  0.200583f, -0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f, -0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f, -0.068910f }, { 0.167065f,  0.153519f, -0.132175f, 0.0f } } },
        { FrontCenter, { { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f,  0.000000f }, { 0.109403f,  0.137375f,  0.000000f, 0.0f } } },
        { BackLeft,    { { 0.224752f, -0.295009f,  0.170325f, 0.0f, 0.0f, 0.0f, 0.0f,  0.105349f, -0.182473f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.065799f }, { 0.224752f, -0.225790f,  0.130361f, 0.0f } } },
        { BackRight,   { { 0.224752f, -0.295009f, -0.170325f, 0.0f, 0.0f, 0.0f, 0.0f,  0.105349f,  0.182473f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.065799f }, { 0.224752f, -0.225790f, -0.130361f, 0.0f } } },
        { SideLeft,    { { 0.224739f,  0.000000f,  0.340644f, 0.0f, 0.0f, 0.0f, 0.0f, -0.210697f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.065795f }, { 0.224739f,  0.000000f,  0.260717f, 0.0f } } },
        { SideRight,   { { 0.224739f,  0.000000f, -0.340644f, 0.0f, 0.0f, 0.0f, 0.0f, -0.210697f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.065795f }, { 0.224739f,  0.000000f, -0.260717f, 0.0f } } },
    }, BFormat3D[4] = {
        { Aux0, { { 1.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f } } },
        { Aux1, { { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f } } },
        { Aux2, { { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } } },
        { Aux3, { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } } },
    };
    const ChannelMap *chanmap = NULL;
    size_t count = 0;

    memset(device->Channel, 0, sizeof(device->Channel));
    device->NumChannels = 0;

    if(device->Hrtf)
    {
        ALuint i;

        count = COUNTOF(BFormat3D);
        chanmap = BFormat3D;

        for(i = 0;i < count;i++)
            device->ChannelName[i] = chanmap[i].ChanName;
        for(;i < MAX_OUTPUT_CHANNELS;i++)
            device->ChannelName[i] = InvalidChannel;
        SetChannelMap(device, chanmap, count);

        {
            ALfloat (*coeffs_list[4])[2] = {
                device->Hrtf_Params[0].Coeffs, device->Hrtf_Params[1].Coeffs,
                device->Hrtf_Params[2].Coeffs, device->Hrtf_Params[3].Coeffs
            };
            ALuint *delay_list[4] = {
                device->Hrtf_Params[0].Delay, device->Hrtf_Params[1].Delay,
                device->Hrtf_Params[2].Delay, device->Hrtf_Params[3].Delay
            };
            GetBFormatHrtfCoeffs(device->Hrtf, 4, coeffs_list, delay_list);
        }

        return;
    }

    if(LoadChannelSetup(device))
        return;

    switch(device->FmtChans)
    {
        case DevFmtMono:
            count = COUNTOF(MonoCfg);
            chanmap = MonoCfg;
            break;

        case DevFmtStereo:
            count = COUNTOF(StereoCfg);
            chanmap = StereoCfg;
            break;

        case DevFmtQuad:
            count = COUNTOF(QuadCfg);
            chanmap = QuadCfg;
            break;

        case DevFmtX51:
            count = COUNTOF(X51SideCfg);
            chanmap = X51SideCfg;
            break;

        case DevFmtX51Rear:
            count = COUNTOF(X51RearCfg);
            chanmap = X51RearCfg;
            break;

        case DevFmtX61:
            count = COUNTOF(X61Cfg);
            chanmap = X61Cfg;
            break;

        case DevFmtX71:
            count = COUNTOF(X71Cfg);
            chanmap = X71Cfg;
            break;

        case DevFmtBFormat3D:
            count = COUNTOF(BFormat3D);
            chanmap = BFormat3D;
            break;
    }

    SetChannelMap(device, chanmap, count);
}
