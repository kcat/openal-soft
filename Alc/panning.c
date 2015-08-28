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


#define ZERO_ORDER_SCALE    0.0f
#define FIRST_ORDER_SCALE   1.0f
#define SECOND_ORDER_SCALE  (1.0f / 1.22474f)
#define THIRD_ORDER_SCALE   (1.0f / 1.30657f)


static const ALuint FuMa2ACN[MAX_AMBI_COEFFS] = {
    0,  /* W */
    3,  /* X */
    1,  /* Y */
    2,  /* Z */
    6,  /* R */
    7,  /* S */
    5,  /* T */
    8,  /* U */
    4,  /* V */
    12, /* K */
    13, /* L */
    11, /* M */
    14, /* N */
    10, /* O */
    15, /* P */
    9,  /* Q */
};

static const ALfloat N3D2FuMaScale[MAX_AMBI_COEFFS] = {
    0.7071f, /* ACN  0 (W), sqrt(1/2) */
    0.5774f, /* ACN  1 (Y), sqrt(1/3) */
    0.5774f, /* ACN  2 (Z), sqrt(1/3) */
    0.5774f, /* ACN  3 (X), sqrt(1/3) */
    0.5164f, /* ACN  4 (V), 2/sqrt(15) */
    0.5164f, /* ACN  5 (T), 2/sqrt(15) */
    0.4472f, /* ACN  6 (R), sqrt(1/5) */
    0.5164f, /* ACN  7 (S), 2/sqrt(15) */
    0.5164f, /* ACN  8 (U), 2/sqrt(15) */
    0.4781f, /* ACN  9 (Q), sqrt(8/35) */
    0.5071f, /* ACN 10 (O), 3/sqrt(35) */
    0.4482f, /* ACN 11 (M), sqrt(45/224) */
    0.3780f, /* ACN 12 (K), sqrt(1/7) */
    0.4482f, /* ACN 13 (L), sqrt(45/224) */
    0.5071f, /* ACN 14 (N), 3/sqrt(35) */
    0.4781f, /* ACN 15 (P), sqrt(8/35) */
};


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
        gains[i] = sqrtf(device->AmbiCoeffs[i][0]/1.4142f) * ingain;
    }
}

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

    /* Zeroth-order */
    coeffs[0] = 0.7071f; /* W = sqrt(1.0 / 2.0) */
    /* First-order */
    coeffs[1] = y; /* Y = Y */
    coeffs[2] = z; /* Z = Z */
    coeffs[3] = x; /* X = X */
    /* Second-order */
    coeffs[4] = 2.0f * x * y;             /* V = 2*X*Y */
    coeffs[5] = 2.0f * y * z;             /* T = 2*Y*Z */
    coeffs[6] = 0.5f * (3.0f*z*z - 1.0f); /* R = 0.5 * (3*Z*Z - 1) */
    coeffs[7] = 2.0f * z * x;             /* S = 2*Z*X */
    coeffs[8] = x*x - y*y;                /* U = X*X - Y*Y */
    /* Third-order */
    coeffs[9] = y * (3.0f*x*x - y*y);             /* Q = Y * (3*X*X - Y*Y) */
    coeffs[10] = 5.1962f * x * y * z;             /* O = sqrt(27) * X * Y * Z */
    coeffs[11] = 0.7262f * y * (5.0f*z*z - 1.0f); /* M = sqrt(135.0 / 256.0) * Y * (5*Z*Z - 1) */
    coeffs[12] = 0.5f * z * (5.0f*z*z - 3.0f);    /* K = 0.5 * Z * (5*Z*Z - 3) */
    coeffs[13] = 0.7262f * x * (5.0f*z*z - 1.0f); /* L = sqrt(135.0 / 256.0) * X * (5*Z*Z - 1) */
    coeffs[14] = 2.5981f * z * (x*x - y*y);       /* N = sqrt(27.0 / 4.0) * Z * (X*X - Y*Y) */
    coeffs[15] = x * (x*x - 3.0f*y*y);            /* P = X * (X*X - 3*Y*Y) */

    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
    for(i = 0;i < device->NumChannels;i++)
    {
        float gain = 0.0f;
        for(j = 0;j < MAX_AMBI_COEFFS;j++)
            gain += device->AmbiCoeffs[i][j]*coeffs[j];
        gains[i] = gain * ingain;
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
            gains[i] += device->AmbiCoeffs[i][j] * mtx[j];
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

        case BFormatW: return "bformat-w";
        case BFormatX: return "bformat-x";
        case BFormatY: return "bformat-y";
        case BFormatZ: return "bformat-z";

        case InvalidChannel: break;
    }
    return "(unknown)";
}


typedef struct ChannelMap {
    enum Channel ChanName;
    ChannelConfig Config;
} ChannelMap;

static void SetChannelMap(ALCdevice *device, const ChannelMap *chanmap, size_t count, ALfloat ambiscale)
{
    size_t i, j, k;

    device->AmbiScale = ambiscale;
    for(i = 0;i < MAX_OUTPUT_CHANNELS && device->ChannelName[i] != InvalidChannel;i++)
    {
        if(device->ChannelName[i] == LFE)
        {
            for(j = 0;j < MAX_AMBI_COEFFS;j++)
                device->AmbiCoeffs[i][j] = 0.0f;
            continue;
        }

        for(j = 0;j < count;j++)
        {
            if(device->ChannelName[i] == chanmap[j].ChanName)
            {
                for(k = 0;k < MAX_AMBI_COEFFS;++k)
                    device->AmbiCoeffs[i][k] = chanmap[j].Config[k];
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
    ALfloat ambiscale = 1.0f;
    size_t count = 0;
    int isfuma;
    int order;
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
        char name[32] = {0};
        const char *type;
        char eol;

        snprintf(name, sizeof(name), "%s/type", layout);
        if(!ConfigValueStr(al_string_get_cstr(device->DeviceName), "layouts", name, &type))
            return false;

        if(sscanf(type, " %31[^: ] : %d%c", name, &order, &eol) != 2)
        {
            ERR("Invalid type value '%s' (expected name:order) for layout %s\n", type, layout);
            return false;
        }

        if(strcasecmp(name, "fuma") == 0)
            isfuma = 1;
        else if(strcasecmp(name, "n3d") == 0)
            isfuma = 0;
        else
        {
            ERR("Unhandled type name '%s' (expected FuMa or N3D) for layout %s\n", name, layout);
            return false;
        }

        if(order == 3)
            ambiscale = THIRD_ORDER_SCALE;
        else if(order == 2)
            ambiscale = SECOND_ORDER_SCALE;
        else if(order == 1)
            ambiscale = FIRST_ORDER_SCALE;
        else if(order == 0)
            ambiscale = ZERO_ORDER_SCALE;
        else
        {
            ERR("Unhandled type order %d (expected 0, 1, 2, or 3) for layout %s\n", order, layout);
            return false;
        }
    }

    for(i = 0;i < count;i++)
    {
        float coeffs[MAX_AMBI_COEFFS] = {0.0f};
        const char *channame;
        char chanlayout[32];
        const char *value;
        int props = 0;
        char eol = 0;
        int j;

        chanmap[i].ChanName = channels[i];
        channame = GetLabelFromChannel(channels[i]);

        snprintf(chanlayout, sizeof(chanlayout), "%s/%s", layout, channame);
        if(!ConfigValueStr(al_string_get_cstr(device->DeviceName), "layouts", chanlayout, &value))
        {
            ERR("Missing channel %s\n", channame);
            return false;
        }
        if(order == 3)
            props = sscanf(value, " %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %c",
                &coeffs[0],  &coeffs[1],  &coeffs[2],  &coeffs[3],
                &coeffs[4],  &coeffs[5],  &coeffs[6],  &coeffs[7],
                &coeffs[8],  &coeffs[9],  &coeffs[10], &coeffs[11],
                &coeffs[12], &coeffs[13], &coeffs[14], &coeffs[15],
                &eol
            );
        else if(order == 2)
            props = sscanf(value, " %f %f %f %f %f %f %f %f %f %c",
                &coeffs[0], &coeffs[1], &coeffs[2],
                &coeffs[3], &coeffs[4], &coeffs[5],
                &coeffs[6], &coeffs[7], &coeffs[8],
                &eol
            );
        else if(order == 1)
            props = sscanf(value, " %f %f %f %f %c",
                &coeffs[0], &coeffs[1],
                &coeffs[2], &coeffs[3],
                &eol
            );
        else if(order == 0)
            props = sscanf(value, " %f %c", &coeffs[0], &eol);
        if(props == 0)
        {
            ERR("Failed to parse option %s properties\n", chanlayout);
            return false;
        }

        if(props > (order+1)*(order+1))
        {
            ERR("Excess elements in option %s (expected %d)\n", chanlayout, (order+1)*(order+1));
            return false;
        }

        if(isfuma)
        {
            /* Reorder FuMa -> ACN */
            for(j = 0;j < MAX_AMBI_COEFFS;++j)
                chanmap[i].Config[FuMa2ACN[j]] = coeffs[j];
        }
        else
        {
            /* Rescale N3D -> FuMa */
            for(j = 0;j < MAX_AMBI_COEFFS;++j)
                chanmap[i].Config[j] = coeffs[j] * N3D2FuMaScale[j];
        }
    }
    SetChannelMap(device, chanmap, count, ambiscale);
    return true;
}

ALvoid aluInitPanning(ALCdevice *device)
{
    static const ChannelMap MonoCfg[1] = {
        { FrontCenter, { 1.4142f } },
    }, StereoCfg[2] = {
        { FrontLeft,   { 0.7071f,  0.5f, 0.0f, 0.0f } },
        { FrontRight,  { 0.7071f, -0.5f, 0.0f, 0.0f } },
    }, QuadCfg[4] = {
        { FrontLeft,   { 0.353553f,  0.306184f, 0.0f,  0.306184f,  0.117186f, 0.0f, 0.0f, 0.0f,  0.000000f } },
        { FrontRight,  { 0.353553f, -0.306184f, 0.0f,  0.306184f, -0.117186f, 0.0f, 0.0f, 0.0f,  0.000000f } },
        { BackLeft,    { 0.353553f,  0.306184f, 0.0f, -0.306184f, -0.117186f, 0.0f, 0.0f, 0.0f,  0.000000f } },
        { BackRight,   { 0.353553f, -0.306184f, 0.0f, -0.306184f,  0.117186f, 0.0f, 0.0f, 0.0f,  0.000000f } },
    }, X51SideCfg[5] = {
        { FrontLeft,   { 0.208954f,  0.238350f, 0.0f,  0.212846f,  0.204014f, 0.0f, 0.0f, 0.0f, -0.017738f,  0.047490f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f } },
        { FrontRight,  { 0.208954f, -0.238350f, 0.0f,  0.212846f, -0.204014f, 0.0f, 0.0f, 0.0f, -0.017738f, -0.047490f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f } },
        { FrontCenter, { 0.109403f,  0.000000f, 0.0f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f } },
        { SideLeft,    { 0.470936f,  0.349386f, 0.0f, -0.369626f, -0.058144f, 0.0f, 0.0f, 0.0f, -0.031375f, -0.043968f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f } },
        { SideRight,   { 0.470936f, -0.349386f, 0.0f, -0.369626f,  0.058144f, 0.0f, 0.0f, 0.0f, -0.031375f,  0.043968f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f } },
    }, X51RearCfg[5] = {
        { FrontLeft,   { 0.208954f,  0.238350f, 0.0f,  0.212846f,  0.204014f, 0.0f, 0.0f, 0.0f, -0.017738f,  0.047490f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f } },
        { FrontRight,  { 0.208954f, -0.238350f, 0.0f,  0.212846f, -0.204014f, 0.0f, 0.0f, 0.0f, -0.017738f, -0.047490f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f } },
        { FrontCenter, { 0.109403f,  0.000000f, 0.0f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f } },
        { BackLeft,    { 0.470936f,  0.349386f, 0.0f, -0.369626f, -0.058144f, 0.0f, 0.0f, 0.0f, -0.031375f, -0.043968f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f } },
        { BackRight,   { 0.470936f, -0.349386f, 0.0f, -0.369626f,  0.058144f, 0.0f, 0.0f, 0.0f, -0.031375f,  0.043968f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f } },
    }, X61Cfg[6] = {
        { FrontLeft,   { 0.167065f,  0.172695f, 0.0f,  0.200583f,  0.186407f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.068910f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f } },
        { FrontRight,  { 0.167065f, -0.172695f, 0.0f,  0.200583f, -0.186407f, 0.0f, 0.0f, 0.0f,  0.029855f, -0.068910f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f } },
        { FrontCenter, { 0.109403f,  0.000000f, 0.0f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f } },
        { BackCenter,  { 0.353556f,  0.000000f, 0.0f, -0.461940f,  0.000000f, 0.0f, 0.0f, 0.0f,  0.165723f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f } },
        { SideLeft,    { 0.289151f,  0.401292f, 0.0f, -0.081301f, -0.071420f, 0.0f, 0.0f, 0.0f, -0.188208f, -0.032897f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f } },
        { SideRight,   { 0.289151f, -0.401292f, 0.0f, -0.081301f,  0.071420f, 0.0f, 0.0f, 0.0f, -0.188208f,  0.032897f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f } },
    }, X71Cfg[7] = {
        { FrontLeft,   { 0.167065f,  0.172695f, 0.0f,  0.200583f,  0.186407f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.068910f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f } },
        { FrontRight,  { 0.167065f, -0.172695f, 0.0f,  0.200583f, -0.186407f, 0.0f, 0.0f, 0.0f,  0.029855f, -0.068910f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f } },
        { FrontCenter, { 0.109403f,  0.000000f, 0.0f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f } },
        { BackLeft,    { 0.224752f,  0.170325f, 0.0f, -0.295009f, -0.182473f, 0.0f, 0.0f, 0.0f,  0.105349f,  0.065799f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f } },
        { BackRight,   { 0.224752f, -0.170325f, 0.0f, -0.295009f,  0.182473f, 0.0f, 0.0f, 0.0f,  0.105349f, -0.065799f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f } },
        { SideLeft,    { 0.224739f,  0.340644f, 0.0f,  0.000000f,  0.000000f, 0.0f, 0.0f, 0.0f, -0.210697f, -0.065795f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f } },
        { SideRight,   { 0.224739f, -0.340644f, 0.0f,  0.000000f,  0.000000f, 0.0f, 0.0f, 0.0f, -0.210697f,  0.065795f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f } },
    }, BFormat3D[4] = {
        { BFormatW, { 1.0f, 0.0f, 0.0f, 0.0f } },
        { BFormatX, { 0.0f, 0.0f, 0.0f, 1.0f } },
        { BFormatY, { 0.0f, 1.0f, 0.0f, 0.0f } },
        { BFormatZ, { 0.0f, 0.0f, 1.0f, 0.0f } },
    };
    const ChannelMap *chanmap = NULL;
    ALfloat ambiscale = 1.0f;
    size_t count = 0;

    device->AmbiScale = 1.0f;
    memset(device->AmbiCoeffs, 0, sizeof(device->AmbiCoeffs));
    device->NumChannels = 0;

    if(device->Hrtf)
    {
        ALfloat (*coeffs_list[4])[2];
        ALuint *delay_list[4];
        ALuint i;

        count = COUNTOF(BFormat3D);
        chanmap = BFormat3D;
        ambiscale = 1.0f;

        for(i = 0;i < count;i++)
            device->ChannelName[i] = chanmap[i].ChanName;
        for(;i < MAX_OUTPUT_CHANNELS;i++)
            device->ChannelName[i] = InvalidChannel;
        SetChannelMap(device, chanmap, count, ambiscale);

        for(i = 0;i < 4;++i)
        {
            static const enum Channel inputs[4] = { BFormatW, BFormatY, BFormatZ, BFormatX };
            int chan = GetChannelIdxByName(device, inputs[i]);
            coeffs_list[i] = device->Hrtf_Params[chan].Coeffs;
            delay_list[i] = device->Hrtf_Params[chan].Delay;
        }
        GetBFormatHrtfCoeffs(device->Hrtf, 4, coeffs_list, delay_list);

        return;
    }

    if(LoadChannelSetup(device))
        return;

    switch(device->FmtChans)
    {
        case DevFmtMono:
            count = COUNTOF(MonoCfg);
            chanmap = MonoCfg;
            ambiscale = ZERO_ORDER_SCALE;
            break;

        case DevFmtStereo:
            count = COUNTOF(StereoCfg);
            chanmap = StereoCfg;
            ambiscale = FIRST_ORDER_SCALE;
            break;

        case DevFmtQuad:
            count = COUNTOF(QuadCfg);
            chanmap = QuadCfg;
            ambiscale = SECOND_ORDER_SCALE;
            break;

        case DevFmtX51:
            count = COUNTOF(X51SideCfg);
            chanmap = X51SideCfg;
            ambiscale = THIRD_ORDER_SCALE;
            break;

        case DevFmtX51Rear:
            count = COUNTOF(X51RearCfg);
            chanmap = X51RearCfg;
            ambiscale = THIRD_ORDER_SCALE;
            break;

        case DevFmtX61:
            count = COUNTOF(X61Cfg);
            chanmap = X61Cfg;
            ambiscale = THIRD_ORDER_SCALE;
            break;

        case DevFmtX71:
            count = COUNTOF(X71Cfg);
            chanmap = X71Cfg;
            ambiscale = THIRD_ORDER_SCALE;
            break;

        case DevFmtBFormat3D:
            count = COUNTOF(BFormat3D);
            chanmap = BFormat3D;
            ambiscale = 1.0f;
            break;
    }

    SetChannelMap(device, chanmap, count, ambiscale);
}
