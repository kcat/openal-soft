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
#include "alAuxEffectSlot.h"
#include "alu.h"
#include "bool.h"
#include "ambdec.h"
#include "bformatdec.h"


extern inline void CalcXYZCoeffs(ALfloat x, ALfloat y, ALfloat z, ALfloat coeffs[MAX_AMBI_COEFFS]);


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

/* NOTE: These are scale factors as applied to Ambisonics content. Decoder
 * coefficients should be divided by these values to get proper N3D scalings.
 */
static const ALfloat UnitScale[MAX_AMBI_COEFFS] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};
static const ALfloat SN3D2N3DScale[MAX_AMBI_COEFFS] = {
    1.000000000f, /* ACN  0 (W), sqrt(1) */
    1.732050808f, /* ACN  1 (Y), sqrt(3) */
    1.732050808f, /* ACN  2 (Z), sqrt(3) */
    1.732050808f, /* ACN  3 (X), sqrt(3) */
    2.236067978f, /* ACN  4 (V), sqrt(5) */
    2.236067978f, /* ACN  5 (T), sqrt(5) */
    2.236067978f, /* ACN  6 (R), sqrt(5) */
    2.236067978f, /* ACN  7 (S), sqrt(5) */
    2.236067978f, /* ACN  8 (U), sqrt(5) */
    2.645751311f, /* ACN  9 (Q), sqrt(7) */
    2.645751311f, /* ACN 10 (O), sqrt(7) */
    2.645751311f, /* ACN 11 (M), sqrt(7) */
    2.645751311f, /* ACN 12 (K), sqrt(7) */
    2.645751311f, /* ACN 13 (L), sqrt(7) */
    2.645751311f, /* ACN 14 (N), sqrt(7) */
    2.645751311f, /* ACN 15 (P), sqrt(7) */
};
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


void CalcDirectionCoeffs(const ALfloat dir[3], ALfloat coeffs[MAX_AMBI_COEFFS])
{
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
}

void CalcAngleCoeffs(ALfloat angle, ALfloat elevation, ALfloat coeffs[MAX_AMBI_COEFFS])
{
    ALfloat dir[3] = {
        sinf(angle) * cosf(elevation),
        sinf(elevation),
        -cosf(angle) * cosf(elevation)
    };
    CalcDirectionCoeffs(dir, coeffs);
}


void ComputeAmbientGains(const ChannelConfig *chancoeffs, ALuint numchans, ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALuint i;

    for(i = 0;i < numchans;i++)
    {
        // The W coefficients are based on a mathematical average of the
        // output. The square root of the base average provides for a more
        // perceptual average volume, better suited to non-directional gains.
        gains[i] = sqrtf(chancoeffs[i][0]) * ingain;
    }
    for(;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
}

void ComputePanningGains(const ChannelConfig *chancoeffs, ALuint numchans, const ALfloat coeffs[MAX_AMBI_COEFFS], ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALuint i, j;

    for(i = 0;i < numchans;i++)
    {
        float gain = 0.0f;
        for(j = 0;j < MAX_AMBI_COEFFS;j++)
            gain += chancoeffs[i][j]*coeffs[j];
        gains[i] = gain * ingain;
    }
    for(;i < MAX_OUTPUT_CHANNELS;i++)
        gains[i] = 0.0f;
}

void ComputeFirstOrderGains(const ChannelConfig *chancoeffs, ALuint numchans, const ALfloat mtx[4], ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    ALuint i, j;

    for(i = 0;i < numchans;i++)
    {
        float gain = 0.0f;
        for(j = 0;j < 4;j++)
            gain += chancoeffs[i][j] * mtx[j];
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

        case InvalidChannel: break;
    }
    return "(unknown)";
}


DECL_CONST static const char *GetChannelLayoutName(enum DevFmtChannels chans)
{
    switch(chans)
    {
        case DevFmtMono: return "mono";
        case DevFmtStereo: return "stereo";
        case DevFmtQuad: return "quad";
        case DevFmtX51: return "surround51";
        case DevFmtX51Rear: return "surround51rear";
        case DevFmtX61: return "surround61";
        case DevFmtX71: return "surround71";
        case DevFmtBFormat3D:
            break;
    }
    return NULL;
}

typedef struct ChannelMap {
    enum Channel ChanName;
    ChannelConfig Config;
} ChannelMap;

static void SetChannelMap(const enum Channel *devchans, ChannelConfig *ambicoeffs,
                          const ChannelMap *chanmap, size_t count, ALuint *outcount,
                          ALboolean isfuma)
{
    size_t j, k;
    ALuint i;

    for(i = 0;i < MAX_OUTPUT_CHANNELS && devchans[i] != InvalidChannel;i++)
    {
        if(devchans[i] == LFE)
        {
            for(j = 0;j < MAX_AMBI_COEFFS;j++)
                ambicoeffs[i][j] = 0.0f;
            continue;
        }

        for(j = 0;j < count;j++)
        {
            if(devchans[i] != chanmap[j].ChanName)
                continue;

            if(isfuma)
            {
                /* Reformat FuMa -> ACN/N3D */
                for(k = 0;k < MAX_AMBI_COEFFS;++k)
                {
                    ALuint acn = FuMa2ACN[k];
                    ambicoeffs[i][acn] = chanmap[j].Config[k] / FuMa2N3DScale[acn];
                }
            }
            else
            {
                for(k = 0;k < MAX_AMBI_COEFFS;++k)
                    ambicoeffs[i][k] = chanmap[j].Config[k];
            }
            break;
        }
        if(j == count)
            ERR("Failed to match %s channel (%u) in channel map\n", GetLabelFromChannel(devchans[i]), i);
    }
    *outcount = i;
}

static bool MakeSpeakerMap(ALCdevice *device, const AmbDecConf *conf, ALuint speakermap[MAX_OUTPUT_CHANNELS])
{
    ALuint i;

    for(i = 0;i < conf->NumSpeakers;i++)
    {
        int c = -1;

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
        if(al_string_cmp_cstr(conf->Speakers[i].Name, "LF") == 0)
            c = GetChannelIdxByName(device->RealOut, FrontLeft);
        else if(al_string_cmp_cstr(conf->Speakers[i].Name, "RF") == 0)
            c = GetChannelIdxByName(device->RealOut, FrontRight);
        else if(al_string_cmp_cstr(conf->Speakers[i].Name, "CE") == 0)
            c = GetChannelIdxByName(device->RealOut, FrontCenter);
        else if(al_string_cmp_cstr(conf->Speakers[i].Name, "LS") == 0)
        {
            if(device->FmtChans == DevFmtX51Rear)
                c = GetChannelIdxByName(device->RealOut, BackLeft);
            else
                c = GetChannelIdxByName(device->RealOut, SideLeft);
        }
        else if(al_string_cmp_cstr(conf->Speakers[i].Name, "RS") == 0)
        {
            if(device->FmtChans == DevFmtX51Rear)
                c = GetChannelIdxByName(device->RealOut, BackRight);
            else
                c = GetChannelIdxByName(device->RealOut, SideRight);
        }
        else if(al_string_cmp_cstr(conf->Speakers[i].Name, "LB") == 0)
        {
            if(device->FmtChans == DevFmtX51)
                c = GetChannelIdxByName(device->RealOut, SideLeft);
            else
                c = GetChannelIdxByName(device->RealOut, BackLeft);
        }
        else if(al_string_cmp_cstr(conf->Speakers[i].Name, "RB") == 0)
        {
            if(device->FmtChans == DevFmtX51)
                c = GetChannelIdxByName(device->RealOut, SideRight);
            else
                c = GetChannelIdxByName(device->RealOut, BackRight);
        }
        else if(al_string_cmp_cstr(conf->Speakers[i].Name, "CB") == 0)
            c = GetChannelIdxByName(device->RealOut, BackCenter);
        else
        {
            ERR("AmbDec speaker label \"%s\" not recognized\n",
                al_string_get_cstr(conf->Speakers[i].Name));
            return false;
        }
        if(c == -1)
        {
            ERR("Failed to lookup AmbDec speaker label %s\n",
                al_string_get_cstr(conf->Speakers[i].Name));
            return false;
        }
        speakermap[i] = c;
    }

    return true;
}

static bool LoadChannelSetup(ALCdevice *device)
{
    ChannelMap chanmap[MAX_OUTPUT_CHANNELS];
    ALuint speakermap[MAX_OUTPUT_CHANNELS];
    const ALfloat *coeff_scale = UnitScale;
    const char *layout = NULL;
    ALfloat ambiscale = 1.0f;
    const char *fname;
    AmbDecConf conf;
    ALuint i, j;

    /* Don't use custom decoders with mono or stereo output (stereo is using
     * UHJ or pair-wise panning, thus ignores the custom coefficients anyway,
     * and mono would realistically only specify attenuation on the output).
     */
    if(device->FmtChans == DevFmtMono || device->FmtChans == DevFmtStereo)
        return false;

    layout = GetChannelLayoutName(device->FmtChans);
    if(!layout) return false;

    if(!ConfigValueStr(al_string_get_cstr(device->DeviceName), "decoder", layout, &fname))
        return false;

    ambdec_init(&conf);
    if(!ambdec_load(&conf, fname))
    {
        ERR("Failed to load layout file %s\n", fname);
        goto fail;
    }

    if(conf.FreqBands != 1)
        ERR("Basic renderer uses the high-frequency matrix as single-band (xover_freq = %.0fhz)\n",
            conf.XOverFreq);

    if(!MakeSpeakerMap(device, &conf, speakermap))
        goto fail;

    if(conf.ChanMask > 0x1ff)
        ambiscale = THIRD_ORDER_SCALE;
    else if(conf.ChanMask > 0xf)
        ambiscale = SECOND_ORDER_SCALE;
    else if(conf.ChanMask > 0x1)
        ambiscale = FIRST_ORDER_SCALE;
    else
        ambiscale = 0.0f;

    if(conf.CoeffScale == ADS_SN3D)
        coeff_scale = SN3D2N3DScale;
    else if(conf.CoeffScale == ADS_FuMa)
        coeff_scale = FuMa2N3DScale;

    for(i = 0;i < conf.NumSpeakers;i++)
    {
        ALuint chan = speakermap[i];
        ALfloat gain;
        ALuint k = 0;

        for(j = 0;j < MAX_AMBI_COEFFS;j++)
            chanmap[i].Config[j] = 0.0f;

        chanmap[i].ChanName = device->RealOut.ChannelName[chan];
        for(j = 0;j < MAX_AMBI_COEFFS;j++)
        {
            if(j == 0) gain = conf.HFOrderGain[0];
            else if(j == 1) gain = conf.HFOrderGain[1];
            else if(j == 4) gain = conf.HFOrderGain[2];
            else if(j == 9) gain = conf.HFOrderGain[3];
            if((conf.ChanMask&(1<<j)))
                chanmap[i].Config[j] = conf.HFMatrix[i][k++] / coeff_scale[j] * gain;
        }
    }

    SetChannelMap(device->Dry.ChannelName, device->Dry.AmbiCoeffs, chanmap, conf.NumSpeakers,
                  &device->Dry.NumChannels, AL_FALSE);

    memset(device->FOAOut.AmbiCoeffs, 0, sizeof(device->FOAOut.AmbiCoeffs));
    for(i = 0;i < device->Dry.NumChannels;i++)
    {
        device->FOAOut.AmbiCoeffs[i][0] = device->Dry.AmbiCoeffs[i][0];
        for(j = 1;j < 4;j++)
            device->FOAOut.AmbiCoeffs[i][j] = device->Dry.AmbiCoeffs[i][j] * ambiscale;
    }

    ambdec_deinit(&conf);
    return true;

fail:
    ambdec_deinit(&conf);
    return false;
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
        { FrontLeft,   { 0.353553f,  0.306184f,  0.306184f, 0.0f,  0.0f, 0.0f, 0.0f,  0.000000f,  0.117186f } },
        { FrontRight,  { 0.353553f,  0.306184f, -0.306184f, 0.0f,  0.0f, 0.0f, 0.0f,  0.000000f, -0.117186f } },
        { BackLeft,    { 0.353553f, -0.306184f,  0.306184f, 0.0f,  0.0f, 0.0f, 0.0f,  0.000000f, -0.117186f } },
        { BackRight,   { 0.353553f, -0.306184f, -0.306184f, 0.0f,  0.0f, 0.0f, 0.0f,  0.000000f,  0.117186f } },
    }, X51SideCfg[5] = {
        { FrontLeft,   { 0.208954f,  0.199518f,  0.223424f, 0.0f,  0.0f, 0.0f, 0.0f, -0.012543f,  0.144260f } },
        { FrontRight,  { 0.208950f,  0.199514f, -0.223425f, 0.0f,  0.0f, 0.0f, 0.0f, -0.012544f, -0.144258f } },
        { FrontCenter, { 0.109403f,  0.168250f, -0.000002f, 0.0f,  0.0f, 0.0f, 0.0f,  0.100431f, -0.000001f } },
        { SideLeft,    { 0.470934f, -0.346484f,  0.327504f, 0.0f,  0.0f, 0.0f, 0.0f, -0.022188f, -0.041113f } },
        { SideRight,   { 0.470936f, -0.346480f, -0.327507f, 0.0f,  0.0f, 0.0f, 0.0f, -0.022186f,  0.041114f } },
    }, X51RearCfg[5] = {
        { FrontLeft,   { 0.208954f,  0.199518f,  0.223424f, 0.0f,  0.0f, 0.0f, 0.0f, -0.012543f,  0.144260f } },
        { FrontRight,  { 0.208950f,  0.199514f, -0.223425f, 0.0f,  0.0f, 0.0f, 0.0f, -0.012544f, -0.144258f } },
        { FrontCenter, { 0.109403f,  0.168250f, -0.000002f, 0.0f,  0.0f, 0.0f, 0.0f,  0.100431f, -0.000001f } },
        { BackLeft,    { 0.470934f, -0.346484f,  0.327504f, 0.0f,  0.0f, 0.0f, 0.0f, -0.022188f, -0.041113f } },
        { BackRight,   { 0.470936f, -0.346480f, -0.327507f, 0.0f,  0.0f, 0.0f, 0.0f, -0.022186f,  0.041114f } },
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
    }, Cube8Cfg[8] = {
        { UpperFrontLeft,  { 0.176776695f,  0.072168784f,  0.072168784f,  0.072168784f } },
        { UpperFrontRight, { 0.176776695f,  0.072168784f, -0.072168784f,  0.072168784f } },
        { UpperBackLeft,   { 0.176776695f, -0.072168784f,  0.072168784f,  0.072168784f } },
        { UpperBackRight,  { 0.176776695f, -0.072168784f, -0.072168784f,  0.072168784f } },
        { LowerFrontLeft,  { 0.176776695f,  0.072168784f,  0.072168784f, -0.072168784f } },
        { LowerFrontRight, { 0.176776695f,  0.072168784f, -0.072168784f, -0.072168784f } },
        { LowerBackLeft,   { 0.176776695f, -0.072168784f,  0.072168784f, -0.072168784f } },
        { LowerBackRight,  { 0.176776695f, -0.072168784f, -0.072168784f, -0.072168784f } },
    }, BFormat2D[3] = {
        { Aux0, { 1.0f, 0.0f, 0.0f, 0.0f } },
        { Aux1, { 0.0f, 1.0f, 0.0f, 0.0f } },
        { Aux2, { 0.0f, 0.0f, 1.0f, 0.0f } },
    }, BFormat3D[4] = {
        { Aux0, { 1.0f, 0.0f, 0.0f, 0.0f } },
        { Aux1, { 0.0f, 1.0f, 0.0f, 0.0f } },
        { Aux2, { 0.0f, 0.0f, 1.0f, 0.0f } },
        { Aux3, { 0.0f, 0.0f, 0.0f, 1.0f } },
    };
    const ChannelMap *chanmap = NULL;
    ALfloat ambiscale;
    size_t count = 0;
    ALuint i, j;

    memset(device->Dry.AmbiCoeffs, 0, sizeof(device->Dry.AmbiCoeffs));
    device->Dry.NumChannels = 0;

    if(device->Hrtf)
    {
        static const struct {
            enum Channel Channel;
            ALfloat Angle;
            ALfloat Elevation;
        } CubeInfo[8] = {
            { UpperFrontLeft,  DEG2RAD( -45.0f), DEG2RAD( 45.0f) },
            { UpperFrontRight, DEG2RAD(  45.0f), DEG2RAD( 45.0f) },
            { UpperBackLeft,   DEG2RAD(-135.0f), DEG2RAD( 45.0f) },
            { UpperBackRight,  DEG2RAD( 135.0f), DEG2RAD( 45.0f) },
            { LowerFrontLeft,  DEG2RAD( -45.0f), DEG2RAD(-45.0f) },
            { LowerFrontRight, DEG2RAD(  45.0f), DEG2RAD(-45.0f) },
            { LowerBackLeft,   DEG2RAD(-135.0f), DEG2RAD(-45.0f) },
            { LowerBackRight,  DEG2RAD( 135.0f), DEG2RAD(-45.0f) },
        };

        count = COUNTOF(Cube8Cfg);
        chanmap = Cube8Cfg;

        for(i = 0;i < count;i++)
            device->Dry.ChannelName[i] = chanmap[i].ChanName;
        for(;i < MAX_OUTPUT_CHANNELS;i++)
            device->Dry.ChannelName[i] = InvalidChannel;
        SetChannelMap(device->Dry.ChannelName, device->Dry.AmbiCoeffs, chanmap, count,
                      &device->Dry.NumChannels, AL_TRUE);

        memcpy(device->FOAOut.AmbiCoeffs, device->Dry.AmbiCoeffs,
               sizeof(device->FOAOut.AmbiCoeffs));

        for(i = 0;i < device->Dry.NumChannels;i++)
        {
            int chan = GetChannelIdxByName(device->Dry, CubeInfo[i].Channel);
            GetLerpedHrtfCoeffs(device->Hrtf, CubeInfo[i].Elevation, CubeInfo[i].Angle, 1.0f, 1.0f,
                                device->Hrtf_Params[chan].Coeffs, device->Hrtf_Params[chan].Delay);
        }
        return;
    }
    if(device->Uhj_Encoder)
    {
        count = COUNTOF(BFormat2D);
        chanmap = BFormat2D;

        for(i = 0;i < count;i++)
            device->Dry.ChannelName[i] = chanmap[i].ChanName;
        for(;i < MAX_OUTPUT_CHANNELS;i++)
            device->Dry.ChannelName[i] = InvalidChannel;
        SetChannelMap(device->Dry.ChannelName, device->Dry.AmbiCoeffs, chanmap, count,
                      &device->Dry.NumChannels, AL_TRUE);

        memcpy(device->FOAOut.AmbiCoeffs, device->Dry.AmbiCoeffs,
               sizeof(device->FOAOut.AmbiCoeffs));

        return;
    }
    if(device->AmbiDecoder)
    {
        /* NOTE: This is ACN/N3D ordering and scaling, rather than FuMa. */
        static const ChannelMap Ambi3D[9] = {
            /* Zeroth order */
            { Aux0, { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            /* First order */
            { Aux1, { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            { Aux2, { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            { Aux3, { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            /* Second order */
            { Aux4, { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            { Aux5, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f } },
            { Aux6, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f } },
            { Aux7, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f } },
            { Aux8, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f } },
        }, Ambi2D[7] = {
            /* Zeroth order */
            { Aux0, { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            /* First order */
            { Aux1, { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            { Aux2, { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            /* Second order */
            { Aux3, { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            { Aux4, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            /* Third order */
            { Aux5, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            { Aux6, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f } },
        };
        const char *devname = al_string_get_cstr(device->DeviceName);
        ALuint speakermap[MAX_OUTPUT_CHANNELS];
        const char *fname = "";
        const char *layout;
        int decflags = 0;
        AmbDecConf conf;

        ambdec_init(&conf);

        /* Don't do HQ ambisonic decoding with mono or stereo output. Same
         * reasons as in LoadChannelSetup.
         */
        if(device->FmtChans == DevFmtMono || device->FmtChans == DevFmtStereo)
            goto ambi_fail;

        layout = GetChannelLayoutName(device->FmtChans);
        if(!layout) goto ambi_fail;

        if(!ConfigValueStr(devname, "decoder", layout, &fname))
            goto ambi_fail;

        if(GetConfigValueBool(devname, "decoder", "distance-comp", 1))
            decflags |= BFDF_DistanceComp;

        if(!ambdec_load(&conf, fname))
        {
            ERR("Failed to load %s\n", fname);
            goto ambi_fail;
        }

        if(conf.ChanMask > 0xffff)
        {
            ERR("Unsupported channel mask 0x%04x (max 0xffff)\n", conf.ChanMask);
            goto ambi_fail;
        }

        if(!MakeSpeakerMap(device, &conf, speakermap))
            goto ambi_fail;

        if((conf.ChanMask & ~0x831b))
        {
            if(conf.ChanMask > 0x1ff)
            {
                ERR("Third-order is unsupported for periphonic HQ decoding (mask 0x%04x)\n",
                    conf.ChanMask);
                goto ambi_fail;
            }
            count = (conf.ChanMask > 0xf) ? 9 : 4;
            chanmap = Ambi3D;
        }
        else
        {
            count = (conf.ChanMask > 0xf) ? (conf.ChanMask > 0x1ff) ? 7 : 5 : 3;
            chanmap = Ambi2D;
        }

        for(i = 0;i < count;i++)
            device->Dry.ChannelName[i] = chanmap[i].ChanName;
        for(;i < MAX_OUTPUT_CHANNELS;i++)
            device->Dry.ChannelName[i] = InvalidChannel;
        SetChannelMap(device->Dry.ChannelName, device->Dry.AmbiCoeffs, chanmap, count,
                      &device->Dry.NumChannels, AL_FALSE);

        TRACE("Enabling %s-band %s-order%s ambisonic decoder\n",
            (conf.FreqBands == 1) ? "single" : "dual",
            (conf.ChanMask > 0xf) ? (conf.ChanMask > 0x1ff) ? "third" : "second" : "first",
            (conf.ChanMask & ~0x831b) ? " periphonic" : ""
        );
        bformatdec_reset(device->AmbiDecoder, &conf, count, device->Frequency,
                         speakermap, decflags);
        ambdec_deinit(&conf);

        if(bformatdec_getOrder(device->AmbiDecoder) < 2)
            memcpy(device->FOAOut.AmbiCoeffs, device->Dry.AmbiCoeffs,
                   sizeof(device->FOAOut.AmbiCoeffs));
        else
        {
            memset(device->FOAOut.AmbiCoeffs, 0, sizeof(device->FOAOut.AmbiCoeffs));
            device->FOAOut.AmbiCoeffs[0][0] = 1.0f;
            device->FOAOut.AmbiCoeffs[1][1] = 1.0f;
            device->FOAOut.AmbiCoeffs[2][2] = 1.0f;
            device->FOAOut.AmbiCoeffs[3][3] = 1.0f;
        }

        return;

    ambi_fail:
        ambdec_deinit(&conf);
        bformatdec_free(device->AmbiDecoder);
        device->AmbiDecoder = NULL;
    }

    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
        device->Dry.ChannelName[i] = device->RealOut.ChannelName[i];

    if(LoadChannelSetup(device))
        return;

    ambiscale = 1.0f;
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
            ambiscale = SECOND_ORDER_SCALE;
            break;

        case DevFmtX51Rear:
            count = COUNTOF(X51RearCfg);
            chanmap = X51RearCfg;
            ambiscale = SECOND_ORDER_SCALE;
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
            ambiscale = FIRST_ORDER_SCALE;
            break;
    }

    SetChannelMap(device->Dry.ChannelName, device->Dry.AmbiCoeffs, chanmap, count,
                  &device->Dry.NumChannels, AL_TRUE);

    memset(device->FOAOut.AmbiCoeffs, 0, sizeof(device->FOAOut.AmbiCoeffs));
    for(i = 0;i < device->Dry.NumChannels;i++)
    {
        device->FOAOut.AmbiCoeffs[i][0] = device->Dry.AmbiCoeffs[i][0];
        for(j = 1;j < 4;j++)
            device->FOAOut.AmbiCoeffs[i][j] = device->Dry.AmbiCoeffs[i][j] * ambiscale;
    }
}

void aluInitEffectPanning(ALeffectslot *slot)
{
    static const ChannelMap FirstOrderN3D[4] = {
        { Aux0, { 1.0f, 0.0f, 0.0f, 0.0f } },
        { Aux1, { 0.0f, 1.0f, 0.0f, 0.0f } },
        { Aux2, { 0.0f, 0.0f, 1.0f, 0.0f } },
        { Aux3, { 0.0f, 0.0f, 0.0f, 1.0f } },
    };
    static const enum Channel AmbiChannels[MAX_OUTPUT_CHANNELS] = {
        Aux0, Aux1, Aux2, Aux3, InvalidChannel
    };

    memset(slot->AmbiCoeffs, 0, sizeof(slot->AmbiCoeffs));
    slot->NumChannels = 0;

    SetChannelMap(AmbiChannels, slot->AmbiCoeffs, FirstOrderN3D, COUNTOF(FirstOrderN3D),
                  &slot->NumChannels, AL_FALSE);
}
