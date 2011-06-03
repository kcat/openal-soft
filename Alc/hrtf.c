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

static const ALubyte evCount = 19;
static const ALushort evOffset[19] = { 0, 1, 13, 37, 73, 118, 174, 234, 306, 378, 450, 522, 594, 654, 710, 755, 791, 815, 827 };
static const ALubyte azCount[19] = { 1, 12, 24, 36, 45, 56, 60, 72, 72, 72, 72, 72, 60, 56, 45, 36, 24, 12, 1 };

static struct HRTF {
    ALshort coeffs[HRIR_COUNT][HRIR_LENGTH];
    ALubyte delays[HRIR_COUNT];
} Hrtf = {
#include "hrtf_tables.inc"
};

static ALuint CalcEvIndex(ALdouble ev)
{
    ev = (M_PI/2.0 + ev) * (evCount-1) / M_PI;
    return (ALuint)(ev+0.5);
}

static ALuint CalcAzIndex(ALint evidx, ALdouble az)
{
    az = (M_PI*2.0 + az) * azCount[evidx] / (M_PI*2.0);
    return (ALuint)(az+0.5) % azCount[evidx];
}

void GetHrtfCoeffs(ALfloat elevation, ALfloat angle, const ALshort **left, const ALshort **right, ALuint *ldelay, ALuint *rdelay)
{
    ALuint lidx, ridx;
    ALuint evidx, azidx;

    evidx = CalcEvIndex(elevation);
    azidx = CalcAzIndex(evidx, angle);

    lidx = evOffset[evidx] + azidx;
    ridx = evOffset[evidx] + ((azCount[evidx]-azidx) % azCount[evidx]);

    *ldelay = Hrtf.delays[lidx];
    *rdelay = Hrtf.delays[ridx];

    *left  = Hrtf.coeffs[lidx];
    *right = Hrtf.coeffs[ridx];
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
            AL_PRINT("Could not open %s\n", str);
    }
    if(f != NULL)
    {
        const ALubyte maxDelay = SRC_HISTORY_LENGTH - HRIR_LENGTH;
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
                AL_PRINT("Invalid delay at idx %d: %u (max: %u), in %s\n", i, val, maxDelay, str);
                failed = AL_TRUE;
            }
        }
        if(feof(f))
        {
            AL_PRINT("Premature end of data while reading %s\n", str);
            failed = AL_TRUE;
        }

        fclose(f);
        f = NULL;

        if(!failed)
            Hrtf = newdata;
    }
}
