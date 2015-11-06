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

/* NOTE: These are scale factors as applied to Ambisonics content. FuMa
 * decoder coefficients should be divided by these values to get N3D decoder
 * coefficients.
 */
static const ALfloat FuMa2N3DScale[MAX_AMBI_COEFFS] = {
    1.414213562f, /* ACN  0 (W), sqrt(2) */
    1.732050808f, /* ACN  1 (Y), sqrt(3) */
    1.732050808f, /* ACN  2 (Z), sqrt(3) */
    1.732050808f, /* ACN  3 (X), sqrt(3) */
    1.936491673f, /* ACN  4 (V), sqrt(15)/2 */
    1.936491673f, /* ACN  5 (T), sqrt(15)/2 */
    2.236067978f, /* ACN  6 (R), sqrt(5) */
    1.936491673f, /* ACN  7 (S), sqrt(15)/2 */
    1.936491673f, /* ACN  8 (U), sqrt(15)/2 */
    2.091650066f, /* ACN  9 (Q), sqrt(35/8) */
    1.972026594f, /* ACN 10 (O), sqrt(35)/3 */
    2.231093404f, /* ACN 11 (M), sqrt(224/45) */
    2.645751311f, /* ACN 12 (K), sqrt(7) */
    2.231093404f, /* ACN 13 (L), sqrt(224/45) */
    1.972026594f, /* ACN 14 (N), sqrt(35)/3 */
    2.091650066f, /* ACN 15 (P), sqrt(35/8) */
};


void ComputeAmbientGains(const ALCdevice *device, ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALuint i;

    for(i = 0;i < device->NumChannels;i++)
    {
        // The W coefficients are based on a mathematical average of the
        // output. The square root of the base average provides for a more
        // perceptual average volume, better suited to non-directional gains.
        gains[i] = sqrtf(device->AmbiCoeffs[i][0]) * ingain;
    }
    for(;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
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
    coeffs[0]  = 1.0f; /* ACN 0 = 1 */
    /* First-order */
    coeffs[1]  = 1.732050808f * y; /* ACN 1 = sqrt(3) * Y */
    coeffs[2]  = 1.732050808f * z; /* ACN 2 = sqrt(3) * Z */
    coeffs[3]  = 1.732050808f * x; /* ACN 3 = sqrt(3) * X */
    /* Second-order */
    coeffs[4]  = 3.872983346f * x * y;             /* ACN 4 = sqrt(15) * X * Y */
    coeffs[5]  = 3.872983346f * y * z;             /* ACN 5 = sqrt(15) * Y * Z */
    coeffs[6]  = 1.118033989f * (3.0f*z*z - 1.0f); /* ACN 6 = sqrt(5)/2 * (3*Z*Z - 1) */
    coeffs[7]  = 3.872983346f * x * z;             /* ACN 7 = sqrt(15) * X * Z */
    coeffs[8]  = 1.936491673f * (x*x - y*y);       /* ACN 8 = sqrt(15)/2 * (X*X - Y*Y) */
    /* Third-order */
    coeffs[9]  =  2.091650066f * y * (3.0f*x*x - y*y);  /* ACN  9 = sqrt(35/8) * Y * (3*X*X - Y*Y) */
    coeffs[10] = 10.246950766f * z * x * y;             /* ACN 10 = sqrt(105) * Z * X * Y */
    coeffs[11] =  1.620185175f * y * (5.0f*z*z - 1.0f); /* ACN 11 = sqrt(21/8) * Y * (5*Z*Z - 1) */
    coeffs[12] =  1.322875656f * z * (5.0f*z*z - 3.0f); /* ACN 12 = sqrt(7)/2 * Z * (5*Z*Z - 3) */
    coeffs[13] =  1.620185175f * x * (5.0f*z*z - 1.0f); /* ACN 13 = sqrt(21/8) * X * (5*Z*Z - 1) */
    coeffs[14] =  5.123475383f * z * (x*x - y*y);       /* ACN 14 = sqrt(105)/2 * Z * (X*X - Y*Y) */
    coeffs[15] =  2.091650066f * x * (x*x - 3.0f*y*y);  /* ACN 15 = sqrt(35/8) * X * (X*X - 3*Y*Y) */

    for(i = 0;i < device->NumChannels;i++)
    {
        float gain = 0.0f;
        for(j = 0;j < MAX_AMBI_COEFFS;j++)
            gain += device->AmbiCoeffs[i][j]*coeffs[j];
        gains[i] = gain * ingain;
    }
    for(;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
}

void ComputeBFormatGains(const ALCdevice *device, const ALfloat mtx[4], ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALuint i, j;

    for(i = 0;i < device->NumChannels;i++)
    {
        float gain = 0.0f;
        for(j = 0;j < 4;j++)
            gain += device->AmbiCoeffs[i][j] * mtx[j];
        gains[i] = gain * ingain;
    }
    for(;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
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

static void SetChannelMap(ALCdevice *device, const ChannelMap *chanmap, size_t count, ALfloat ambiscale, ALboolean isfuma)
{
    size_t j, k;
    ALuint i;

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
                if(isfuma)
                {
                    /* Reformat FuMa -> ACN/N3D */
                    for(k = 0;k < MAX_AMBI_COEFFS;++k)
                    {
                        ALuint acn = FuMa2ACN[k];
                        device->AmbiCoeffs[i][acn] = chanmap[j].Config[k] / FuMa2N3DScale[acn];
                    }
                }
                else
                {
                    for(k = 0;k < MAX_AMBI_COEFFS;++k)
                        device->AmbiCoeffs[i][k] = chanmap[j].Config[k];
                }
                break;
            }
        }
        if(j == count)
            ERR("Failed to match %s channel (%u) in config\n", GetLabelFromChannel(device->ChannelName[i]), i);
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

        for(j = 0;j < MAX_AMBI_COEFFS;++j)
            chanmap[i].Config[j] = coeffs[j];
    }
    SetChannelMap(device, chanmap, count, ambiscale, isfuma);
    return true;
}

ALvoid aluInitPanning(ALCdevice *device)
{
    /* NOTE: These decoder coefficients are using FuMa channel ordering and
     * normalization, since that's what was produced by the Ambisonic Decoder
     * Toolbox. SetChannelMap will convert them to N3D.
     */
    static const ChannelMap MonoCfg[1] = {
        { FrontCenter, { 1.414213562f } },
    }, StereoCfg[2] = {
        { FrontLeft,   { 0.707106781f, 0.0f,  0.5f, 0.0f } },
        { FrontRight,  { 0.707106781f, 0.0f, -0.5f, 0.0f } },
    }, QuadCfg[4] = {
        { FrontLeft,   { 0.353553f,  0.306184f,  0.306184f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.117186f } },
        { FrontRight,  { 0.353553f,  0.306184f, -0.306184f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.117186f } },
        { BackLeft,    { 0.353553f, -0.306184f,  0.306184f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.117186f } },
        { BackRight,   { 0.353553f, -0.306184f, -0.306184f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.117186f } },
    }, X51SideCfg[5] = {
        { FrontLeft,   { 0.208954f,  0.212846f,  0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f,  0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f,  0.047490f } },
        { FrontRight,  { 0.208954f,  0.212846f, -0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f, -0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f, -0.047490f } },
        { FrontCenter, { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f,  0.000000f } },
        { SideLeft,    { 0.470936f, -0.369626f,  0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f, -0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f, -0.043968f } },
        { SideRight,   { 0.470936f, -0.369626f, -0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f,  0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f,  0.043968f } },
    }, X51RearCfg[5] = {
        { FrontLeft,   { 0.208954f,  0.212846f,  0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f,  0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f,  0.047490f } },
        { FrontRight,  { 0.208954f,  0.212846f, -0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f, -0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f, -0.047490f } },
        { FrontCenter, { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f,  0.000000f } },
        { BackLeft,    { 0.470936f, -0.369626f,  0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f, -0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f, -0.043968f } },
        { BackRight,   { 0.470936f, -0.369626f, -0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f,  0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f,  0.043968f } },
    }, X61Cfg[6] = {
        { FrontLeft,   { 0.167065f,  0.200583f,  0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f,  0.068910f } },
        { FrontRight,  { 0.167065f,  0.200583f, -0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f, -0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f, -0.068910f } },
        { FrontCenter, { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f,  0.000000f } },
        { BackCenter,  { 0.353556f, -0.461940f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.165723f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.000000f } },
        { SideLeft,    { 0.289151f, -0.081301f,  0.401292f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f, -0.071420f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f, -0.032897f } },
        { SideRight,   { 0.289151f, -0.081301f, -0.401292f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f,  0.071420f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f,  0.032897f } },
    }, X71Cfg[7] = {
        { FrontLeft,   { 0.167065f,  0.200583f,  0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f,  0.068910f } },
        { FrontRight,  { 0.167065f,  0.200583f, -0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f, -0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f, -0.068910f } },
        { FrontCenter, { 0.109403f,  0.179490f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f,  0.000000f } },
        { BackLeft,    { 0.224752f, -0.295009f,  0.170325f, 0.0f, 0.0f, 0.0f, 0.0f,  0.105349f, -0.182473f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.065799f } },
        { BackRight,   { 0.224752f, -0.295009f, -0.170325f, 0.0f, 0.0f, 0.0f, 0.0f,  0.105349f,  0.182473f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.065799f } },
        { SideLeft,    { 0.224739f,  0.000000f,  0.340644f, 0.0f, 0.0f, 0.0f, 0.0f, -0.210697f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f, -0.065795f } },
        { SideRight,   { 0.224739f,  0.000000f, -0.340644f, 0.0f, 0.0f, 0.0f, 0.0f, -0.210697f,  0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.065795f } },
    }, BFormat3D[4] = {
        { BFormatW, { 1.0f, 0.0f, 0.0f, 0.0f } },
        { BFormatX, { 0.0f, 1.0f, 0.0f, 0.0f } },
        { BFormatY, { 0.0f, 0.0f, 1.0f, 0.0f } },
        { BFormatZ, { 0.0f, 0.0f, 0.0f, 1.0f } },
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
        SetChannelMap(device, chanmap, count, ambiscale, AL_TRUE);

        for(i = 0;i < 4;++i)
        {
            static const enum Channel inputs[4] = { BFormatW, BFormatX, BFormatY, BFormatZ };
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

    SetChannelMap(device, chanmap, count, ambiscale, AL_TRUE);
}
