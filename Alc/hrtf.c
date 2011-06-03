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
} Hrtf;

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
        f = fopen(str, "rb");
    if(f != NULL)
    {
        const ALubyte maxDelay = SRC_HISTORY_LENGTH - HRIR_LENGTH;
        struct HRTF newdata;
        size_t i, j;
        union {
            ALfloat f;
            ALubyte ub[4];
        } val;

        for(i = 0;i < HRIR_COUNT;i++)
        {
            for(j = 0;j < HRIR_LENGTH;j++)
            {
                val.ub[0] = fgetc(f);
                val.ub[1] = fgetc(f);
                val.ub[2] = fgetc(f);
                val.ub[3] = fgetc(f);
                if(val.f > 1.0f) newdata.coeffs[i][j] = 32767;
                else if(val.f < -1.0f) newdata.coeffs[i][j] = -32768;
                else newdata.coeffs[i][j] = (ALshort)(val.f*32767.0f);
            }
        }
        val.ub[0] = fgetc(f);
        val.ub[1] = fgetc(f);
        val.ub[2] = fgetc(f);
        val.ub[3] = fgetc(f);
        /* skip maxHrtd */
        for(i = 0;i < HRIR_COUNT;i++)
        {
            val.ub[0] = fgetc(f);
            val.ub[1] = fgetc(f);
            val.ub[2] = fgetc(f);
            val.ub[3] = fgetc(f);
            val.f *= 44100.0f;
            if(val.f >= maxDelay) newdata.delays[i] = maxDelay;
            else newdata.delays[i] = (ALubyte)val.f;
        }
        if(!feof(f))
            Hrtf = newdata;

        fclose(f);
        f = NULL;
    }
}
