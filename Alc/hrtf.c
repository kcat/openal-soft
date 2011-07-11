/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by Chris Robinson
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
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alSource.h"

#define HRIR_COUNT 828

static const ALuint sampleRate = 44100;
static const ALubyte evCount = 19;
static const ALushort evOffset[19] = { 0, 1, 13, 37, 73, 118, 174, 234, 306, 378, 450, 522, 594, 654, 710, 755, 791, 815, 827 };
static const ALubyte azCount[19] = { 1, 12, 24, 36, 45, 56, 60, 72, 72, 72, 72, 72, 60, 56, 45, 36, 24, 12, 1 };

static struct HRTF {
    ALshort coeffs[HRIR_COUNT][HRIR_LENGTH];
    ALubyte delays[HRIR_COUNT];
} Hrtf = {
#include "hrtf_tables.inc"
};

// Calculate the elevation indices given the polar elevation in radians.
// This will return two indices between 0 and (evCount - 1) and an
// interpolation factor between 0.0 and 1.0.
static void CalcEvIndices(ALfloat ev, ALuint *evidx, ALfloat *evmu)
{
    ev = (M_PI/2.0f + ev) * (evCount-1) / M_PI;
    evidx[0] = (ALuint)ev;
    evidx[1] = __min(evidx[0] + 1, evCount - 1);
    *evmu = ev - evidx[0];
}

// Calculate the azimuth indices given the polar azimuth in radians.  This
// will return two indices between 0 and (azCount [ei] - 1) and an
// interpolation factor between 0.0 and 1.0.
static void CalcAzIndices(ALuint evidx, ALfloat az, ALuint *azidx, ALfloat *azmu)
{
    az = (M_PI*2.0f + az) * azCount[evidx] / (M_PI*2.0f);
    azidx[0] = (ALuint)az % azCount[evidx];
    azidx[1] = (azidx[0] + 1) % azCount[evidx];
    *azmu = az - (ALuint)az;
}

// Calculates static HRIR coefficients and delays for the given polar
// elevation and azimuth in radians.  Linear interpolation is used to
// increase the apparent resolution of the HRIR dataset.  The coefficients
// are also normalized and attenuated by the specified gain.
void GetLerpedHrtfCoeffs(ALfloat elevation, ALfloat azimuth, ALfloat gain, ALfloat (*coeffs)[2], ALuint *delays)
{
    ALuint evidx[2], azidx[2];
    ALfloat mu[3];
    ALuint lidx[4], ridx[4];
    ALuint i;

    // Claculate elevation indices and interpolation factor.
    CalcEvIndices(elevation, evidx, &mu[2]);

    // Calculate azimuth indices and interpolation factor for the first
    // elevation.
    CalcAzIndices(evidx[0], azimuth, azidx, &mu[0]);

    // Calculate the first set of linear HRIR indices for left and right
    // channels.
    lidx[0] = evOffset[evidx[0]] + azidx[0];
    lidx[1] = evOffset[evidx[0]] + azidx[1];
    ridx[0] = evOffset[evidx[0]] + ((azCount[evidx[0]]-azidx[0]) % azCount[evidx[0]]);
    ridx[1] = evOffset[evidx[0]] + ((azCount[evidx[0]]-azidx[1]) % azCount[evidx[0]]);

    // Calculate azimuth indices and interpolation factor for the second
    // elevation.
    CalcAzIndices (evidx[1], azimuth, azidx, &mu[1]);

    // Calculate the second set of linear HRIR indices for left and right
    // channels.
    lidx[2] = evOffset[evidx[1]] + azidx[0];
    lidx[3] = evOffset[evidx[1]] + azidx[1];
    ridx[2] = evOffset[evidx[1]] + ((azCount[evidx[1]]-azidx[0]) % azCount[evidx[1]]);
    ridx[3] = evOffset[evidx[1]] + ((azCount[evidx[1]]-azidx[1]) % azCount[evidx[1]]);

    // Calculate the normalized and attenuated HRIR coefficients using linear
    // interpolation when there is enough gain to warrant it.  Zero the
    // coefficients if gain is too low.
    if(gain > 0.0001f)
    {
        ALdouble scale = gain * (1.0/32767.0);
        for(i = 0;i < HRIR_LENGTH;i++)
        {
            coeffs[i][0] = lerp(lerp(Hrtf.coeffs[lidx[0]][i], Hrtf.coeffs[lidx[1]][i], mu[0]),
                                lerp(Hrtf.coeffs[lidx[2]][i], Hrtf.coeffs[lidx[3]][i], mu[1]),
                                mu[2]) * scale;
            coeffs[i][1] = lerp(lerp(Hrtf.coeffs[ridx[0]][i], Hrtf.coeffs[ridx[1]][i], mu[0]),
                                lerp(Hrtf.coeffs[ridx[2]][i], Hrtf.coeffs[ridx[3]][i], mu[1]),
                                mu[2]) * scale;
        }
    }
    else
    {
        for(i = 0;i < HRIR_LENGTH;i++)
        {
            coeffs[i][0] = 0.0f;
            coeffs[i][1] = 0.0f;
        }
    }

    // Calculate the HRIR delays using linear interpolation.
    delays[0] = (ALuint)(lerp(lerp(Hrtf.delays[lidx[0]], Hrtf.delays[lidx[1]], mu[0]),
                              lerp(Hrtf.delays[lidx[2]], Hrtf.delays[lidx[3]], mu[1]),
                              mu[2]) + 0.5f);
    delays[1] = (ALuint)(lerp(lerp(Hrtf.delays[ridx[0]], Hrtf.delays[ridx[1]], mu[0]),
                              lerp(Hrtf.delays[ridx[2]], Hrtf.delays[ridx[3]], mu[1]),
                              mu[2]) + 0.5f);
}

ALCboolean IsHrtfCompatible(ALCdevice *device)
{
    if(device->FmtChans == DevFmtStereo && device->Frequency == sampleRate)
        return ALC_TRUE;
    return ALC_FALSE;
}

void InitHrtf(void)
{
    const char *str;
    FILE *f = NULL;

    str = GetConfigValue(NULL, "hrtf_tables", "");
    if(str[0] != '\0')
    {
        f = fopen(str, "rb");
        if(f == NULL)
            ERROR("Could not open %s\n", str);
    }
    if(f != NULL)
    {
        const ALubyte maxDelay = SRC_HISTORY_LENGTH;
        ALboolean failed = AL_FALSE;
        struct HRTF newdata;
        size_t i, j;

        for(i = 0;i < HRIR_COUNT;i++)
        {
            for(j = 0;j < HRIR_LENGTH;j++)
            {
                ALshort val;
                val  = fgetc(f);
                val |= fgetc(f)<<8;
                newdata.coeffs[i][j] = val;
            }
        }
        for(i = 0;i < HRIR_COUNT;i++)
        {
            ALubyte val;
            val = fgetc(f);
            newdata.delays[i] = val;
            if(val > maxDelay)
            {
                ERROR("Invalid delay at idx %zu: %u (max: %u), in %s\n", i, val, maxDelay, str);
                failed = AL_TRUE;
            }
        }
        if(feof(f))
        {
            ERROR("Premature end of data while reading %s\n", str);
            failed = AL_TRUE;
        }

        fclose(f);
        f = NULL;

        if(!failed)
            Hrtf = newdata;
    }
}
