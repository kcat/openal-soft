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
        gains[i] = maxf(gains[i], 0.0f) * ingain;
    }
}

void ComputeAmbientGains(const ALCdevice *device, ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALuint i;

    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
    for(i = 0;i < device->NumChannels;i++)
        gains[i] = device->Channel[i].HOACoeff[0] * ingain;
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


typedef struct ChannelMap {
    enum Channel ChanName;
    ChannelConfig Config;
} ChannelMap;

static bool LoadChannelSetup(ALCdevice *device)
{
    static const char *mono_names[1] = {
        "front-center"
    }, *stereo_names[2] = {
        "front-left", "front-right"
    }, *quad_names[4] = {
        "front-left", "front-right",
        "back-left", "back-right"
    }, *surround51_names[6] = {
        "front-left", "front-right",
        "front-center", "lfe",
        "side-left", "side-right"
    }, *surround51rear_names[6] = {
        "front-left", "front-right",
        "front-center", "lfe",
        "back-left", "back-right"
    }, *surround61_names[7] = {
        "front-left", "front-right",
        "front-center", "lfe", "back-center",
        "side-left", "side-right"
    }, *surround71_names[8] = {
        "front-left", "front-right",
        "front-center", "lfe",
        "back-left", "back-right",
        "side-left", "side-right"
    };
    ChannelMap chanmap[MAX_OUTPUT_CHANNELS] = {};
    const char **channames = NULL;
    const char *layout = NULL;
    size_t count = 0;
    size_t i;

    switch(device->FmtChans)
    {
        case DevFmtMono:
            channames = mono_names;
            layout = "mono";
            count = 1;
            break;
        case DevFmtStereo:
            channames = stereo_names;
            layout = "stereo";
            count = 2;
            break;
        case DevFmtQuad:
            channames = quad_names;
            layout = "quad";
            count = 4;
            break;
        case DevFmtX51:
            channames = surround51_names;
            layout = "surround51";
            count = 6;
            break;
        case DevFmtX51Rear:
            channames = surround51rear_names;
            layout = "surround51rear";
            count = 6;
            break;
        case DevFmtX61:
            channames = surround61_names;
            layout = "surround61";
            count = 7;
            break;
        case DevFmtX71:
            channames = surround71_names;
            layout = "surround71";
            count = 8;
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
        const char *channame = channames[i];
        char chanlayout[32];
        const char *value;
        int props;
        size_t j;

        snprintf(chanlayout, sizeof(chanlayout), "%s/%s", layout, channame);
        if(!ConfigValueStr("layouts", chanlayout, &value))
        {
            ERR("Missing channel %s\n", channame);
            return false;
        }
        props = sscanf(value, " %f %f ; %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f ; %f %f %f %f",
            &chanmap[i].Config.Angle, &chanmap[i].Config.Elevation,
            &chanmap[i].Config.HOACoeff[0], &chanmap[i].Config.HOACoeff[1], &chanmap[i].Config.HOACoeff[2],
            &chanmap[i].Config.HOACoeff[3], &chanmap[i].Config.HOACoeff[4], &chanmap[i].Config.HOACoeff[5],
            &chanmap[i].Config.HOACoeff[6], &chanmap[i].Config.HOACoeff[7], &chanmap[i].Config.HOACoeff[8],
            &chanmap[i].Config.HOACoeff[9], &chanmap[i].Config.HOACoeff[10], &chanmap[i].Config.HOACoeff[11],
            &chanmap[i].Config.HOACoeff[12], &chanmap[i].Config.HOACoeff[13], &chanmap[i].Config.HOACoeff[14],
            &chanmap[i].Config.HOACoeff[15], &chanmap[i].Config.FOACoeff[0], &chanmap[i].Config.FOACoeff[1],
            &chanmap[i].Config.FOACoeff[2], &chanmap[i].Config.FOACoeff[3]);
        if(props == 0)
        {
            ERR("Failed to parse channel "SZFMT" properties\n", i);
            return false;
        }

        if(strcmp(channame, "front-left") == 0)
            chanmap[i].ChanName = FrontLeft;
        else if(strcmp(channame, "front-right") == 0)
            chanmap[i].ChanName = FrontRight;
        else if(strcmp(channame, "front-center") == 0)
            chanmap[i].ChanName = FrontCenter;
        else if(strcmp(channame, "lfe") == 0)
            chanmap[i].ChanName = LFE;
        else if(strcmp(channame, "back-left") == 0)
            chanmap[i].ChanName = BackLeft;
        else if(strcmp(channame, "back-right") == 0)
            chanmap[i].ChanName = BackRight;
        else if(strcmp(channame, "back-center") == 0)
            chanmap[i].ChanName = BackCenter;
        else if(strcmp(channame, "side-left") == 0)
            chanmap[i].ChanName = SideLeft;
        else if(strcmp(channame, "side-right") == 0)
            chanmap[i].ChanName = SideRight;

        if(chanmap[i].ChanName == LFE && props < 22)
        {
            if(props > 1 || chanmap[i].Config.Angle != 0.0f)
                WARN("Unexpected elements for LFE channel\n");
            chanmap[i].Config.Angle = 0.0f;
            chanmap[i].Config.Elevation = 0.0f;
            for(j = 0;j < MAX_AMBI_COEFFS;j++)
                chanmap[i].Config.HOACoeff[j] = 0.0f;
            for(j = 0;j < 4;j++)
                chanmap[i].Config.FOACoeff[j] = 0.0f;
        }
        else if(props < 22)
        {
            ERR("Failed to parse channel %s elements (expected 22, got %d\n", channame, props);
            return false;
        }
        else
        {
            if(!(chanmap[i].Config.Angle >= -180.0f && chanmap[i].Config.Angle <= 180.0f))
            {
                ERR("Channel %s angle is out of range (%f not within -180...+180 degrees)\n", channame, chanmap[i].Config.Angle);
                return false;
            }
            chanmap[i].Config.Angle = DEG2RAD(chanmap[i].Config.Angle);

            if(!(chanmap[i].Config.Elevation >= -180.0f && chanmap[i].Config.Elevation <= 180.0f))
            {
                ERR("Channel %s elevation is out of range (%f not within -180...+180 degrees)\n", channame, chanmap[i].Config.Angle);
                return false;
            }
            chanmap[i].Config.Elevation = DEG2RAD(chanmap[i].Config.Elevation);
        }
    }

    for(i = 0;i < MAX_OUTPUT_CHANNELS && device->ChannelName[i] != InvalidChannel;i++)
    {
        size_t j;
        for(j = 0;j < count;j++)
        {
            if(device->ChannelName[i] == chanmap[j].ChanName)
            {
                device->Channel[i] = chanmap[j].Config;
                break;
            }
        }
        if(j == count)
            ERR("Failed to match channel "SZFMT" (label %d) in config\n", i, device->ChannelName[i]);
    }
    device->NumChannels = i;
    return true;
}

ALvoid aluInitPanning(ALCdevice *device)
{
    static const ChannelMap MonoCfg[1] = {
        { FrontCenter, { DEG2RAD(0.0f), DEG2RAD(0.0f), { 1.4142f }, { 1.4142f } } },
    }, StereoCfg[2] = {
        { FrontLeft,  { DEG2RAD(-90.0f), DEG2RAD(0.0f), { 0.7071f, 0.0f,  0.5f, 0.0f }, { 0.7071f, 0.0f,  0.5f, 0.0f } } },
        { FrontRight, { DEG2RAD( 90.0f), DEG2RAD(0.0f), { 0.7071f, 0.0f, -0.5f, 0.0f }, { 0.7071f, 0.0f, -0.5f, 0.0f } } },
    }, QuadCfg[4] = {
        { FrontLeft,  { DEG2RAD( -45.0f), DEG2RAD(0.0f), { 0.353558f,  0.306181f,  0.306192f, 0.0f, 0.0f, 0.0f, 0.0f, 0.000000f,  0.117183f }, { 0.353553f,  0.250000f,  0.250000f, 0.0f  } } },
        { FrontRight, { DEG2RAD(  45.0f), DEG2RAD(0.0f), { 0.353543f,  0.306181f, -0.306192f, 0.0f, 0.0f, 0.0f, 0.0f, 0.000000f, -0.117193f }, { 0.353553f,  0.250000f, -0.250000f, 0.0f  } } },
        { BackLeft,   { DEG2RAD(-135.0f), DEG2RAD(0.0f), { 0.353543f, -0.306192f,  0.306181f, 0.0f, 0.0f, 0.0f, 0.0f, 0.000000f, -0.117193f }, { 0.353553f, -0.250000f,  0.250000f, 0.0f  } } },
        { BackRight,  { DEG2RAD( 135.0f), DEG2RAD(0.0f), { 0.353558f, -0.306192f, -0.306181f, 0.0f, 0.0f, 0.0f, 0.0f, 0.000000f,  0.117183f }, { 0.353553f, -0.250000f, -0.250000f, 0.0f  } } },
    }, X51SideCfg[6] = {
        { FrontLeft,   { DEG2RAD( -30.0f), DEG2RAD(0.0f), { 0.208954f,  0.212846f,  0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f,  0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f,  0.047490f }, { 0.208954f,  0.162905f,  0.182425f, 0.0f } } },
        { FrontRight,  { DEG2RAD(  30.0f), DEG2RAD(0.0f), { 0.208954f,  0.212846f, -0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f, -0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f, -0.047490f }, { 0.208954f,  0.162905f, -0.182425f, 0.0f } } },
        { FrontCenter, { DEG2RAD(   0.0f), DEG2RAD(0.0f), { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f, -0.000001f }, { 0.109403f,  0.137375f,  0.000000f, 0.0f } } },
        { LFE,         { 0.0f, 0.0f, { 0.0f }, { 0.0f } } },
        { SideLeft,    { DEG2RAD(-110.0f), DEG2RAD(0.0f), { 0.470936f, -0.369626f,  0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f, -0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f, -0.043968f }, { 0.470934f, -0.282903f,  0.267406f, 0.0f } } },
        { SideRight,   { DEG2RAD( 110.0f), DEG2RAD(0.0f), { 0.470936f, -0.369626f, -0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f,  0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f,  0.043968f }, { 0.470934f, -0.282903f, -0.267406f, 0.0f } } },
    }, X51RearCfg[6] = {
        { FrontLeft,   { DEG2RAD( -30.0f), DEG2RAD(0.0f), { 0.208954f,  0.212846f,  0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f,  0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f,  0.047490f }, { 0.208954f,  0.162905f,  0.182425f, 0.0f } } },
        { FrontRight,  { DEG2RAD(  30.0f), DEG2RAD(0.0f), { 0.208954f,  0.212846f, -0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f, -0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f, -0.047490f }, { 0.208954f,  0.162905f, -0.182425f, 0.0f } } },
        { FrontCenter, { DEG2RAD(   0.0f), DEG2RAD(0.0f), { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f, -0.000001f }, { 0.109403f,  0.137375f,  0.000000f, 0.0f } } },
        { LFE,         { 0.0f, 0.0f, { 0.0f }, { 0.0f } } },
        { BackLeft,    { DEG2RAD(-110.0f), DEG2RAD(0.0f), { 0.470936f, -0.369626f,  0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f, -0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f, -0.043968f }, { 0.470934f, -0.282903f,  0.267406f, 0.0f } } },
        { BackRight,   { DEG2RAD( 110.0f), DEG2RAD(0.0f), { 0.470936f, -0.369626f, -0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f,  0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f,  0.043968f }, { 0.470934f, -0.282903f, -0.267406f, 0.0f } } },
    }, X61Cfg[7] = {
        { FrontLeft,   { DEG2RAD(-30.0f), DEG2RAD(0.0f), { 0.167065f,  0.200583f,  0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f,  0.068910f }, { 0.167065f,  0.153519f,  0.132175f, 0.0f } } },
        { FrontRight,  { DEG2RAD( 30.0f), DEG2RAD(0.0f), { 0.167065f,  0.200583f, -0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f, -0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f, -0.068910f }, { 0.167065f,  0.153519f, -0.132175f, 0.0f } } },
        { FrontCenter, { DEG2RAD(  0.0f), DEG2RAD(0.0f), { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f, -0.000001f }, { 0.109403f,  0.137375f,  0.000000f, 0.0f } } },
        { LFE,         { 0.0f, 0.0f, { 0.0f }, { 0.0f } } },
        { BackCenter,  { DEG2RAD(180.0f), DEG2RAD(0.0f), { 0.353556f, -0.461940f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.165723f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.000005f }, { 0.353556f, -0.353554f,  0.000000f, 0.0f } } },
        { SideLeft,    { DEG2RAD(-90.0f), DEG2RAD(0.0f), { 0.289151f, -0.081301f,  0.401292f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f, -0.071420f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f, -0.032897f }, { 0.289151f, -0.062225f,  0.307136f, 0.0f } } },
        { SideRight,   { DEG2RAD( 90.0f), DEG2RAD(0.0f), { 0.289151f, -0.081301f, -0.401292f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f,  0.071420f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f,  0.032897f }, { 0.289151f, -0.062225f, -0.307136f, 0.0f } } },
    }, X71Cfg[8] = {
        { FrontLeft,   { DEG2RAD( -30.0f), DEG2RAD(0.0f), { 0.167065f,  0.200583f,  0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f,  0.068910f }, { 0.167065f,  0.153519f,  0.132175f, 0.0f } } },
        { FrontRight,  { DEG2RAD(  30.0f), DEG2RAD(0.0f), { 0.167065f,  0.200583f, -0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f, -0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f, -0.068910f }, { 0.167065f,  0.153519f, -0.132175f, 0.0f } } },
        { FrontCenter, { DEG2RAD(   0.0f), DEG2RAD(0.0f), { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f,  0.000000f }, { 0.109403f,  0.137375f,  0.000000f, 0.0f } } },
        { LFE,         { 0.0f, 0.0f, { 0.0f }, { 0.0f } } },
        { BackLeft,    { DEG2RAD(-150.0f), DEG2RAD(0.0f), { 0.224752f, -0.295009f,  0.170325f, 0.0f, 0.0f, 0.0f, 0.0f,  0.105349f, -0.182473f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.065799f }, { 0.224752f, -0.225790f,  0.130361f, 0.0f } } },
        { BackRight,   { DEG2RAD( 150.0f), DEG2RAD(0.0f), { 0.224752f, -0.295009f, -0.170325f, 0.0f, 0.0f, 0.0f, 0.0f,  0.105349f,  0.182473f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.065799f }, { 0.224752f, -0.225790f, -0.130361f, 0.0f } } },
        { SideLeft,    { DEG2RAD( -90.0f), DEG2RAD(0.0f), { 0.224739f,  0.000002f,  0.340644f, 0.0f, 0.0f, 0.0f, 0.0f, -0.210697f,  0.000002f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.065795f }, { 0.224739f,  0.000000f,  0.260717f, 0.0f } } },
        { SideRight,   { DEG2RAD(  90.0f), DEG2RAD(0.0f), { 0.224739f,  0.000002f, -0.340644f, 0.0f, 0.0f, 0.0f, 0.0f, -0.210697f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.065795f }, { 0.224739f,  0.000000f, -0.260717f, 0.0f } } },
    };
    const ChannelMap *chanmap = NULL;
    size_t count = 0;
    ALuint i, j;

    memset(device->Channel, 0, sizeof(device->Channel));
    device->NumChannels = 0;

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
    }
    for(i = 0;i < MAX_OUTPUT_CHANNELS && device->ChannelName[i] != InvalidChannel;i++)
    {
        for(j = 0;j < count;j++)
        {
            if(device->ChannelName[i] == chanmap[j].ChanName)
            {
                device->Channel[i] = chanmap[j].Config;
                break;
            }
        }
        if(j == count)
            ERR("Failed to match channel %u (label %d) in config\n", i, device->ChannelName[i]);
    }
    device->NumChannels = i;
}
