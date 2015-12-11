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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <ctype.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alSource.h"
#include "alu.h"
#include "hrtf.h"

#include "compat.h"


/* Current data set limits defined by the makehrtf utility. */
#define MIN_IR_SIZE                  (8)
#define MAX_IR_SIZE                  (128)
#define MOD_IR_SIZE                  (8)

#define MIN_EV_COUNT                 (5)
#define MAX_EV_COUNT                 (128)

#define MIN_AZ_COUNT                 (1)
#define MAX_AZ_COUNT                 (128)

struct Hrtf {
    ALuint sampleRate;
    ALuint irSize;
    ALubyte evCount;

    const ALubyte *azCount;
    const ALushort *evOffset;
    const ALshort *coeffs;
    const ALubyte *delays;

    al_string filename;
    struct Hrtf *next;
};

static const ALchar magicMarker00[8] = "MinPHR00";
static const ALchar magicMarker01[8] = "MinPHR01";

/* First value for pass-through coefficients (remaining are 0), used for omni-
 * directional sounds. */
static const ALfloat PassthruCoeff = 32767.0f * 0.707106781187f/*sqrt(0.5)*/;

static struct Hrtf *LoadedHrtfs = NULL;

/* Calculate the elevation indices given the polar elevation in radians.
 * This will return two indices between 0 and (evcount - 1) and an
 * interpolation factor between 0.0 and 1.0.
 */
static void CalcEvIndices(ALuint evcount, ALfloat ev, ALuint *evidx, ALfloat *evmu)
{
    ev = (F_PI_2 + ev) * (evcount-1) / F_PI;
    evidx[0] = fastf2u(ev);
    evidx[1] = minu(evidx[0] + 1, evcount-1);
    *evmu = ev - evidx[0];
}

/* Calculate the azimuth indices given the polar azimuth in radians.  This
 * will return two indices between 0 and (azcount - 1) and an interpolation
 * factor between 0.0 and 1.0.
 */
static void CalcAzIndices(ALuint azcount, ALfloat az, ALuint *azidx, ALfloat *azmu)
{
    az = (F_TAU + az) * azcount / F_TAU;
    azidx[0] = fastf2u(az) % azcount;
    azidx[1] = (azidx[0] + 1) % azcount;
    *azmu = az - floorf(az);
}

/* Calculates static HRIR coefficients and delays for the given polar
 * elevation and azimuth in radians.  Linear interpolation is used to
 * increase the apparent resolution of the HRIR data set.  The coefficients
 * are also normalized and attenuated by the specified gain.
 */
void GetLerpedHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat dirfact, ALfloat gain, ALfloat (*coeffs)[2], ALuint *delays)
{
    ALuint evidx[2], lidx[4], ridx[4];
    ALfloat mu[3], blend[4];
    ALuint i;

    /* Claculate elevation indices and interpolation factor. */
    CalcEvIndices(Hrtf->evCount, elevation, evidx, &mu[2]);

    for(i = 0;i < 2;i++)
    {
        ALuint azcount = Hrtf->azCount[evidx[i]];
        ALuint evoffset = Hrtf->evOffset[evidx[i]];
        ALuint azidx[2];

        /* Calculate azimuth indices and interpolation factor for this elevation. */
        CalcAzIndices(azcount, azimuth, azidx, &mu[i]);

        /* Calculate a set of linear HRIR indices for left and right channels. */
        lidx[i*2 + 0] = evoffset + azidx[0];
        lidx[i*2 + 1] = evoffset + azidx[1];
        ridx[i*2 + 0] = evoffset + ((azcount-azidx[0]) % azcount);
        ridx[i*2 + 1] = evoffset + ((azcount-azidx[1]) % azcount);
    }

    /* Calculate 4 blending weights for 2D bilinear interpolation. */
    blend[0] = (1.0f-mu[0]) * (1.0f-mu[2]);
    blend[1] = (     mu[0]) * (1.0f-mu[2]);
    blend[2] = (1.0f-mu[1]) * (     mu[2]);
    blend[3] = (     mu[1]) * (     mu[2]);

    /* Calculate the HRIR delays using linear interpolation. */
    delays[0] = fastf2u((Hrtf->delays[lidx[0]]*blend[0] + Hrtf->delays[lidx[1]]*blend[1] +
                         Hrtf->delays[lidx[2]]*blend[2] + Hrtf->delays[lidx[3]]*blend[3]) *
                        dirfact + 0.5f) << HRTFDELAY_BITS;
    delays[1] = fastf2u((Hrtf->delays[ridx[0]]*blend[0] + Hrtf->delays[ridx[1]]*blend[1] +
                         Hrtf->delays[ridx[2]]*blend[2] + Hrtf->delays[ridx[3]]*blend[3]) *
                        dirfact + 0.5f) << HRTFDELAY_BITS;

    /* Calculate the sample offsets for the HRIR indices. */
    lidx[0] *= Hrtf->irSize;
    lidx[1] *= Hrtf->irSize;
    lidx[2] *= Hrtf->irSize;
    lidx[3] *= Hrtf->irSize;
    ridx[0] *= Hrtf->irSize;
    ridx[1] *= Hrtf->irSize;
    ridx[2] *= Hrtf->irSize;
    ridx[3] *= Hrtf->irSize;

    /* Calculate the normalized and attenuated HRIR coefficients using linear
     * interpolation when there is enough gain to warrant it.  Zero the
     * coefficients if gain is too low.
     */
    if(gain > 0.0001f)
    {
        ALfloat c;

        i = 0;
        c = (Hrtf->coeffs[lidx[0]+i]*blend[0] + Hrtf->coeffs[lidx[1]+i]*blend[1] +
             Hrtf->coeffs[lidx[2]+i]*blend[2] + Hrtf->coeffs[lidx[3]+i]*blend[3]);
        coeffs[i][0] = lerp(PassthruCoeff, c, dirfact) * gain * (1.0f/32767.0f);
        c = (Hrtf->coeffs[ridx[0]+i]*blend[0] + Hrtf->coeffs[ridx[1]+i]*blend[1] +
             Hrtf->coeffs[ridx[2]+i]*blend[2] + Hrtf->coeffs[ridx[3]+i]*blend[3]);
        coeffs[i][1] = lerp(PassthruCoeff, c, dirfact) * gain * (1.0f/32767.0f);

        for(i = 1;i < Hrtf->irSize;i++)
        {
            c = (Hrtf->coeffs[lidx[0]+i]*blend[0] + Hrtf->coeffs[lidx[1]+i]*blend[1] +
                 Hrtf->coeffs[lidx[2]+i]*blend[2] + Hrtf->coeffs[lidx[3]+i]*blend[3]);
            coeffs[i][0] = lerp(0.0f, c, dirfact) * gain * (1.0f/32767.0f);
            c = (Hrtf->coeffs[ridx[0]+i]*blend[0] + Hrtf->coeffs[ridx[1]+i]*blend[1] +
                 Hrtf->coeffs[ridx[2]+i]*blend[2] + Hrtf->coeffs[ridx[3]+i]*blend[3]);
            coeffs[i][1] = lerp(0.0f, c, dirfact) * gain * (1.0f/32767.0f);
        }
    }
    else
    {
        for(i = 0;i < Hrtf->irSize;i++)
        {
            coeffs[i][0] = 0.0f;
            coeffs[i][1] = 0.0f;
        }
    }
}

/* Calculates the moving HRIR target coefficients, target delays, and
 * stepping values for the given polar elevation and azimuth in radians.
 * Linear interpolation is used to increase the apparent resolution of the
 * HRIR data set.  The coefficients are also normalized and attenuated by the
 * specified gain.  Stepping resolution and count is determined using the
 * given delta factor between 0.0 and 1.0.
 */
ALuint GetMovingHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat dirfact, ALfloat gain, ALfloat delta, ALint counter, ALfloat (*coeffs)[2], ALuint *delays, ALfloat (*coeffStep)[2], ALint *delayStep)
{
    ALuint evidx[2], lidx[4], ridx[4];
    ALfloat mu[3], blend[4];
    ALfloat left, right;
    ALfloat steps;
    ALuint i;

    /* Claculate elevation indices and interpolation factor. */
    CalcEvIndices(Hrtf->evCount, elevation, evidx, &mu[2]);

    for(i = 0;i < 2;i++)
    {
        ALuint azcount = Hrtf->azCount[evidx[i]];
        ALuint evoffset = Hrtf->evOffset[evidx[i]];
        ALuint azidx[2];

        /* Calculate azimuth indices and interpolation factor for this elevation. */
        CalcAzIndices(azcount, azimuth, azidx, &mu[i]);

        /* Calculate a set of linear HRIR indices for left and right channels. */
        lidx[i*2 + 0] = evoffset + azidx[0];
        lidx[i*2 + 1] = evoffset + azidx[1];
        ridx[i*2 + 0] = evoffset + ((azcount-azidx[0]) % azcount);
        ridx[i*2 + 1] = evoffset + ((azcount-azidx[1]) % azcount);
    }

    // Calculate the stepping parameters.
    steps = maxf(floorf(delta*Hrtf->sampleRate + 0.5f), 1.0f);
    delta = 1.0f / steps;

    /* Calculate 4 blending weights for 2D bilinear interpolation. */
    blend[0] = (1.0f-mu[0]) * (1.0f-mu[2]);
    blend[1] = (     mu[0]) * (1.0f-mu[2]);
    blend[2] = (1.0f-mu[1]) * (     mu[2]);
    blend[3] = (     mu[1]) * (     mu[2]);

    /* Calculate the HRIR delays using linear interpolation.  Then calculate
     * the delay stepping values using the target and previous running
     * delays.
     */
    left = (ALfloat)(delays[0] - (delayStep[0] * counter));
    right = (ALfloat)(delays[1] - (delayStep[1] * counter));

    delays[0] = fastf2u((Hrtf->delays[lidx[0]]*blend[0] + Hrtf->delays[lidx[1]]*blend[1] +
                         Hrtf->delays[lidx[2]]*blend[2] + Hrtf->delays[lidx[3]]*blend[3]) *
                        dirfact + 0.5f) << HRTFDELAY_BITS;
    delays[1] = fastf2u((Hrtf->delays[ridx[0]]*blend[0] + Hrtf->delays[ridx[1]]*blend[1] +
                         Hrtf->delays[ridx[2]]*blend[2] + Hrtf->delays[ridx[3]]*blend[3]) *
                        dirfact + 0.5f) << HRTFDELAY_BITS;

    delayStep[0] = fastf2i(delta * (delays[0] - left));
    delayStep[1] = fastf2i(delta * (delays[1] - right));

    /* Calculate the sample offsets for the HRIR indices. */
    lidx[0] *= Hrtf->irSize;
    lidx[1] *= Hrtf->irSize;
    lidx[2] *= Hrtf->irSize;
    lidx[3] *= Hrtf->irSize;
    ridx[0] *= Hrtf->irSize;
    ridx[1] *= Hrtf->irSize;
    ridx[2] *= Hrtf->irSize;
    ridx[3] *= Hrtf->irSize;

    /* Calculate the normalized and attenuated target HRIR coefficients using
     * linear interpolation when there is enough gain to warrant it.  Zero
     * the target coefficients if gain is too low.  Then calculate the
     * coefficient stepping values using the target and previous running
     * coefficients.
     */
    if(gain > 0.0001f)
    {
        ALfloat c;

        i = 0;
        left = coeffs[i][0] - (coeffStep[i][0] * counter);
        right = coeffs[i][1] - (coeffStep[i][1] * counter);

        c = (Hrtf->coeffs[lidx[0]+i]*blend[0] + Hrtf->coeffs[lidx[1]+i]*blend[1] +
             Hrtf->coeffs[lidx[2]+i]*blend[2] + Hrtf->coeffs[lidx[3]+i]*blend[3]);
        coeffs[i][0] = lerp(PassthruCoeff, c, dirfact) * gain * (1.0f/32767.0f);
        c = (Hrtf->coeffs[ridx[0]+i]*blend[0] + Hrtf->coeffs[ridx[1]+i]*blend[1] +
             Hrtf->coeffs[ridx[2]+i]*blend[2] + Hrtf->coeffs[ridx[3]+i]*blend[3]);
        coeffs[i][1] = lerp(PassthruCoeff, c, dirfact) * gain * (1.0f/32767.0f);

        coeffStep[i][0] = delta * (coeffs[i][0] - left);
        coeffStep[i][1] = delta * (coeffs[i][1] - right);

        for(i = 1;i < Hrtf->irSize;i++)
        {
            left = coeffs[i][0] - (coeffStep[i][0] * counter);
            right = coeffs[i][1] - (coeffStep[i][1] * counter);

            c = (Hrtf->coeffs[lidx[0]+i]*blend[0] + Hrtf->coeffs[lidx[1]+i]*blend[1] +
                 Hrtf->coeffs[lidx[2]+i]*blend[2] + Hrtf->coeffs[lidx[3]+i]*blend[3]);
            coeffs[i][0] = lerp(0.0f, c, dirfact) * gain * (1.0f/32767.0f);
            c = (Hrtf->coeffs[ridx[0]+i]*blend[0] + Hrtf->coeffs[ridx[1]+i]*blend[1] +
                 Hrtf->coeffs[ridx[2]+i]*blend[2] + Hrtf->coeffs[ridx[3]+i]*blend[3]);
            coeffs[i][1] = lerp(0.0f, c, dirfact) * gain * (1.0f/32767.0f);

            coeffStep[i][0] = delta * (coeffs[i][0] - left);
            coeffStep[i][1] = delta * (coeffs[i][1] - right);
        }
    }
    else
    {
        for(i = 0;i < Hrtf->irSize;i++)
        {
            left = coeffs[i][0] - (coeffStep[i][0] * counter);
            right = coeffs[i][1] - (coeffStep[i][1] * counter);

            coeffs[i][0] = 0.0f;
            coeffs[i][1] = 0.0f;

            coeffStep[i][0] = delta * -left;
            coeffStep[i][1] = delta * -right;
        }
    }

    /* The stepping count is the number of samples necessary for the HRIR to
     * complete its transition.  The mixer will only apply stepping for this
     * many samples.
     */
    return fastf2u(steps);
}


/* Calculates HRTF coefficients for B-Format channels (only up to first-order).
 * Note that these will decode a B-Format output mix, which uses FuMa ordering
 * and scaling, not N3D!
 */
void GetBFormatHrtfCoeffs(const struct Hrtf *Hrtf, const ALuint num_chans, ALfloat (**coeffs_list)[2], ALuint **delay_list)
{
    ALuint elev_idx, azi_idx;
    ALfloat scale;
    ALuint i, c;

    assert(num_chans <= 4);

    for(c = 0;c < num_chans;c++)
    {
        ALfloat (*coeffs)[2] = coeffs_list[c];
        ALuint *delay = delay_list[c];

        for(i = 0;i < Hrtf->irSize;i++)
        {
            coeffs[i][0] = 0.0f;
            coeffs[i][1] = 0.0f;
        }
        delay[0] = 0;
        delay[1] = 0;
    }

    /* NOTE: HRTF coefficients are generated by combining all the HRIRs in the
     * dataset, with each entry scaled according to how much it contributes to
     * the given B-Format channel based on its direction (including negative
     * contributions!).
     */
    scale = 0.0f;
    for(elev_idx = 0;elev_idx < Hrtf->evCount;elev_idx++)
    {
        ALfloat elev = (ALfloat)elev_idx/(ALfloat)(Hrtf->evCount-1)*F_PI - F_PI_2;
        ALuint evoffset = Hrtf->evOffset[elev_idx];
        ALuint azcount = Hrtf->azCount[elev_idx];

        scale += (ALfloat)azcount;

        for(azi_idx = 0;azi_idx < azcount;azi_idx++)
        {
            ALuint lidx, ridx;
            ALfloat ambi_coeffs[4];
            ALfloat az, gain;
            ALfloat x, y, z;

            lidx = evoffset + azi_idx;
            ridx = evoffset + ((azcount-azi_idx) % azcount);

            az = (ALfloat)azi_idx / (ALfloat)azcount * F_TAU;
            if(az > F_PI) az -= F_TAU;

            x = cosf(-az) * cosf(elev);
            y = sinf(-az) * cosf(elev);
            z = sinf(elev);

            ambi_coeffs[0] = 1.414213562f;
            ambi_coeffs[1] = x;
            ambi_coeffs[2] = y;
            ambi_coeffs[3] = z;

            for(c = 0;c < num_chans;c++)
            {
                ALfloat (*coeffs)[2] = coeffs_list[c];
                ALuint *delay = delay_list[c];

                /* NOTE: Always include the total delay average since the
                 * channels need to have matching delays. */
                delay[0] += Hrtf->delays[lidx];
                delay[1] += Hrtf->delays[ridx];

                gain = ambi_coeffs[c];
                if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
                    continue;

                for(i = 0;i < Hrtf->irSize;i++)
                {
                    coeffs[i][0] += Hrtf->coeffs[lidx*Hrtf->irSize + i]*(1.0f/32767.0f) * gain;
                    coeffs[i][1] += Hrtf->coeffs[ridx*Hrtf->irSize + i]*(1.0f/32767.0f) * gain;
                }
            }
        }
    }

    scale = 1.0f/scale;

    for(c = 0;c < num_chans;c++)
    {
        ALfloat (*coeffs)[2] = coeffs_list[c];
        ALuint *delay = delay_list[c];

        for(i = 0;i < Hrtf->irSize;i++)
        {
            coeffs[i][0] *= scale;
            coeffs[i][1] *= scale;
        }
        delay[0] = minu((ALuint)((ALfloat)delay[0] * scale), HRTF_HISTORY_LENGTH-1);
        delay[0] <<= HRTFDELAY_BITS;
        delay[1] = minu((ALuint)((ALfloat)delay[1] * scale), HRTF_HISTORY_LENGTH-1);
        delay[1] <<= HRTFDELAY_BITS;
    }
}


static struct Hrtf *LoadHrtf00(FILE *f)
{
    const ALubyte maxDelay = HRTF_HISTORY_LENGTH-1;
    struct Hrtf *Hrtf = NULL;
    ALboolean failed = AL_FALSE;
    ALuint rate = 0, irCount = 0;
    ALushort irSize = 0;
    ALubyte evCount = 0;
    ALubyte *azCount = NULL;
    ALushort *evOffset = NULL;
    ALshort *coeffs = NULL;
    ALubyte *delays = NULL;
    ALuint i, j;

    rate  = fgetc(f);
    rate |= fgetc(f)<<8;
    rate |= fgetc(f)<<16;
    rate |= fgetc(f)<<24;

    irCount  = fgetc(f);
    irCount |= fgetc(f)<<8;

    irSize  = fgetc(f);
    irSize |= fgetc(f)<<8;

    evCount = fgetc(f);

    if(irSize < MIN_IR_SIZE || irSize > MAX_IR_SIZE || (irSize%MOD_IR_SIZE))
    {
        ERR("Unsupported HRIR size: irSize=%d (%d to %d by %d)\n",
            irSize, MIN_IR_SIZE, MAX_IR_SIZE, MOD_IR_SIZE);
        failed = AL_TRUE;
    }
    if(evCount < MIN_EV_COUNT || evCount > MAX_EV_COUNT)
    {
        ERR("Unsupported elevation count: evCount=%d (%d to %d)\n",
            evCount, MIN_EV_COUNT, MAX_EV_COUNT);
        failed = AL_TRUE;
    }

    if(failed)
        return NULL;

    azCount = malloc(sizeof(azCount[0])*evCount);
    evOffset = malloc(sizeof(evOffset[0])*evCount);
    if(azCount == NULL || evOffset == NULL)
    {
        ERR("Out of memory.\n");
        failed = AL_TRUE;
    }

    if(!failed)
    {
        evOffset[0]  = fgetc(f);
        evOffset[0] |= fgetc(f)<<8;
        for(i = 1;i < evCount;i++)
        {
            evOffset[i]  = fgetc(f);
            evOffset[i] |= fgetc(f)<<8;
            if(evOffset[i] <= evOffset[i-1])
            {
                ERR("Invalid evOffset: evOffset[%d]=%d (last=%d)\n",
                    i, evOffset[i], evOffset[i-1]);
                failed = AL_TRUE;
            }

            azCount[i-1] = evOffset[i] - evOffset[i-1];
            if(azCount[i-1] < MIN_AZ_COUNT || azCount[i-1] > MAX_AZ_COUNT)
            {
                ERR("Unsupported azimuth count: azCount[%d]=%d (%d to %d)\n",
                    i-1, azCount[i-1], MIN_AZ_COUNT, MAX_AZ_COUNT);
                failed = AL_TRUE;
            }
        }
        if(irCount <= evOffset[i-1])
        {
            ERR("Invalid evOffset: evOffset[%d]=%d (irCount=%d)\n",
                i-1, evOffset[i-1], irCount);
            failed = AL_TRUE;
        }

        azCount[i-1] = irCount - evOffset[i-1];
        if(azCount[i-1] < MIN_AZ_COUNT || azCount[i-1] > MAX_AZ_COUNT)
        {
            ERR("Unsupported azimuth count: azCount[%d]=%d (%d to %d)\n",
                i-1, azCount[i-1], MIN_AZ_COUNT, MAX_AZ_COUNT);
            failed = AL_TRUE;
        }
    }

    if(!failed)
    {
        coeffs = malloc(sizeof(coeffs[0])*irSize*irCount);
        delays = malloc(sizeof(delays[0])*irCount);
        if(coeffs == NULL || delays == NULL)
        {
            ERR("Out of memory.\n");
            failed = AL_TRUE;
        }
    }

    if(!failed)
    {
        for(i = 0;i < irCount*irSize;i+=irSize)
        {
            for(j = 0;j < irSize;j++)
            {
                ALshort coeff;
                coeff  = fgetc(f);
                coeff |= fgetc(f)<<8;
                coeffs[i+j] = coeff;
            }
        }
        for(i = 0;i < irCount;i++)
        {
            delays[i] = fgetc(f);
            if(delays[i] > maxDelay)
            {
                ERR("Invalid delays[%d]: %d (%d)\n", i, delays[i], maxDelay);
                failed = AL_TRUE;
            }
        }

        if(feof(f))
        {
            ERR("Premature end of data\n");
            failed = AL_TRUE;
        }
    }

    if(!failed)
    {
        Hrtf = malloc(sizeof(struct Hrtf));
        if(Hrtf == NULL)
        {
            ERR("Out of memory.\n");
            failed = AL_TRUE;
        }
    }

    if(!failed)
    {
        Hrtf->sampleRate = rate;
        Hrtf->irSize = irSize;
        Hrtf->evCount = evCount;
        Hrtf->azCount = azCount;
        Hrtf->evOffset = evOffset;
        Hrtf->coeffs = coeffs;
        Hrtf->delays = delays;
        AL_STRING_INIT(Hrtf->filename);
        Hrtf->next = NULL;
        return Hrtf;
    }

    free(azCount);
    free(evOffset);
    free(coeffs);
    free(delays);
    return NULL;
}


static struct Hrtf *LoadHrtf01(FILE *f)
{
    const ALubyte maxDelay = HRTF_HISTORY_LENGTH-1;
    struct Hrtf *Hrtf = NULL;
    ALboolean failed = AL_FALSE;
    ALuint rate = 0, irCount = 0;
    ALubyte irSize = 0, evCount = 0;
    ALubyte *azCount = NULL;
    ALushort *evOffset = NULL;
    ALshort *coeffs = NULL;
    ALubyte *delays = NULL;
    ALuint i, j;

    rate  = fgetc(f);
    rate |= fgetc(f)<<8;
    rate |= fgetc(f)<<16;
    rate |= fgetc(f)<<24;

    irSize = fgetc(f);

    evCount = fgetc(f);

    if(irSize < MIN_IR_SIZE || irSize > MAX_IR_SIZE || (irSize%MOD_IR_SIZE))
    {
        ERR("Unsupported HRIR size: irSize=%d (%d to %d by %d)\n",
            irSize, MIN_IR_SIZE, MAX_IR_SIZE, MOD_IR_SIZE);
        failed = AL_TRUE;
    }
    if(evCount < MIN_EV_COUNT || evCount > MAX_EV_COUNT)
    {
        ERR("Unsupported elevation count: evCount=%d (%d to %d)\n",
            evCount, MIN_EV_COUNT, MAX_EV_COUNT);
        failed = AL_TRUE;
    }

    if(failed)
        return NULL;

    azCount = malloc(sizeof(azCount[0])*evCount);
    evOffset = malloc(sizeof(evOffset[0])*evCount);
    if(azCount == NULL || evOffset == NULL)
    {
        ERR("Out of memory.\n");
        failed = AL_TRUE;
    }

    if(!failed)
    {
        for(i = 0;i < evCount;i++)
        {
            azCount[i] = fgetc(f);
            if(azCount[i] < MIN_AZ_COUNT || azCount[i] > MAX_AZ_COUNT)
            {
                ERR("Unsupported azimuth count: azCount[%d]=%d (%d to %d)\n",
                    i, azCount[i], MIN_AZ_COUNT, MAX_AZ_COUNT);
                failed = AL_TRUE;
            }
        }
    }

    if(!failed)
    {
        evOffset[0] = 0;
        irCount = azCount[0];
        for(i = 1;i < evCount;i++)
        {
            evOffset[i] = evOffset[i-1] + azCount[i-1];
            irCount += azCount[i];
        }

        coeffs = malloc(sizeof(coeffs[0])*irSize*irCount);
        delays = malloc(sizeof(delays[0])*irCount);
        if(coeffs == NULL || delays == NULL)
        {
            ERR("Out of memory.\n");
            failed = AL_TRUE;
        }
    }

    if(!failed)
    {
        for(i = 0;i < irCount*irSize;i+=irSize)
        {
            for(j = 0;j < irSize;j++)
            {
                ALshort coeff;
                coeff  = fgetc(f);
                coeff |= fgetc(f)<<8;
                coeffs[i+j] = coeff;
            }
        }
        for(i = 0;i < irCount;i++)
        {
            delays[i] = fgetc(f);
            if(delays[i] > maxDelay)
            {
                ERR("Invalid delays[%d]: %d (%d)\n", i, delays[i], maxDelay);
                failed = AL_TRUE;
            }
        }

        if(feof(f))
        {
            ERR("Premature end of data\n");
            failed = AL_TRUE;
        }
    }

    if(!failed)
    {
        Hrtf = malloc(sizeof(struct Hrtf));
        if(Hrtf == NULL)
        {
            ERR("Out of memory.\n");
            failed = AL_TRUE;
        }
    }

    if(!failed)
    {
        Hrtf->sampleRate = rate;
        Hrtf->irSize = irSize;
        Hrtf->evCount = evCount;
        Hrtf->azCount = azCount;
        Hrtf->evOffset = evOffset;
        Hrtf->coeffs = coeffs;
        Hrtf->delays = delays;
        AL_STRING_INIT(Hrtf->filename);
        Hrtf->next = NULL;
        return Hrtf;
    }

    free(azCount);
    free(evOffset);
    free(coeffs);
    free(delays);
    return NULL;
}


static void AddFileEntry(vector_HrtfEntry *list, al_string *filename)
{
    HrtfEntry entry = { AL_STRING_INIT_STATIC(), *filename, NULL };
    HrtfEntry *iter;
    const char *name;
    int i;

    name = strrchr(al_string_get_cstr(entry.filename), '/');
    if(!name) name = strrchr(al_string_get_cstr(entry.filename), '\\');
    if(!name) name = al_string_get_cstr(entry.filename);
    else ++name;

    entry.hrtf = LoadedHrtfs;
    while(entry.hrtf)
    {
        if(al_string_cmp(entry.filename, entry.hrtf->filename) == 0)
            break;
        entry.hrtf = entry.hrtf->next;
    }

    if(!entry.hrtf)
    {
        struct Hrtf *hrtf = NULL;
        ALchar magic[8];
        FILE *f;

        TRACE("Loading %s...\n", al_string_get_cstr(entry.filename));
        f = al_fopen(al_string_get_cstr(entry.filename), "rb");
        if(f == NULL)
        {
            ERR("Could not open %s\n", al_string_get_cstr(entry.filename));
            goto error;
        }

        if(fread(magic, 1, sizeof(magic), f) != sizeof(magic))
            ERR("Failed to read header from %s\n", al_string_get_cstr(entry.filename));
        else
        {
            if(memcmp(magic, magicMarker00, sizeof(magicMarker00)) == 0)
            {
                TRACE("Detected data set format v0\n");
                hrtf = LoadHrtf00(f);
            }
            else if(memcmp(magic, magicMarker01, sizeof(magicMarker01)) == 0)
            {
                TRACE("Detected data set format v1\n");
                hrtf = LoadHrtf01(f);
            }
            else
                ERR("Invalid header in %s: \"%.8s\"\n", al_string_get_cstr(entry.filename), magic);
        }
        fclose(f);

        if(!hrtf)
        {
            ERR("Failed to load %s\n", al_string_get_cstr(entry.filename));
            goto error;
        }

        al_string_copy(&hrtf->filename, entry.filename);
        hrtf->next = LoadedHrtfs;
        LoadedHrtfs = hrtf;
        TRACE("Loaded HRTF support for format: %s %uhz\n",
                DevFmtChannelsString(DevFmtStereo), hrtf->sampleRate);
        entry.hrtf = hrtf;
    }

    /* TODO: Get a human-readable name from the HRTF data (possibly coming in a
     * format update). */

    i = 0;
    do {
        al_string_copy_cstr(&entry.name, name);
        if(i != 0)
        {
            char str[64];
            snprintf(str, sizeof(str), " #%d", i+1);
            al_string_append_cstr(&entry.name, str);
        }
        ++i;

#define MATCH_NAME(i)  (al_string_cmp(entry.name, (i)->name) == 0)
        VECTOR_FIND_IF(iter, HrtfEntry, *list, MATCH_NAME);
#undef MATCH_NAME
    } while(iter != VECTOR_ITER_END(*list));

    TRACE("Adding entry \"%s\" from file \"%s\"\n", al_string_get_cstr(entry.name),
          al_string_get_cstr(entry.filename));
    VECTOR_PUSH_BACK(*list, entry);
    return;

error:
    al_string_deinit(&entry.filename);
}

vector_HrtfEntry EnumerateHrtf(const_al_string devname)
{
    vector_HrtfEntry list = VECTOR_INIT_STATIC();
    const char *fnamelist = "%s.mhr";

    ConfigValueStr(al_string_get_cstr(devname), NULL, "hrtf_tables", &fnamelist);
    while(fnamelist && *fnamelist)
    {
        while(isspace(*fnamelist) || *fnamelist == ',')
            fnamelist++;
        if(*fnamelist != '\0')
        {
            const char *next, *end;

            next = strchr(fnamelist, ',');
            if(!next)
                end = fnamelist + strlen(fnamelist);
            else
                end = next++;

            while(end != fnamelist && isspace(*(end-1)))
                --end;
            if(end != fnamelist)
            {
                al_string fname = AL_STRING_INIT_STATIC();
                vector_al_string flist;

                al_string_append_range(&fname, fnamelist, end);

                flist = SearchDataFiles(al_string_get_cstr(fname), "openal/hrtf");
                VECTOR_FOR_EACH_PARAMS(al_string, flist, AddFileEntry, &list);
                VECTOR_DEINIT(flist);

                al_string_deinit(&fname);
            }

            fnamelist = next;
        }
    }

    return list;
}

void FreeHrtfList(vector_HrtfEntry *list)
{
#define CLEAR_ENTRY(i) do {           \
    al_string_deinit(&(i)->name);     \
    al_string_deinit(&(i)->filename); \
} while(0)
    VECTOR_FOR_EACH(HrtfEntry, *list, CLEAR_ENTRY);
    VECTOR_DEINIT(*list);
#undef CLEAR_ENTRY
}


ALuint GetHrtfSampleRate(const struct Hrtf *Hrtf)
{
    return Hrtf->sampleRate;
}

ALuint GetHrtfIrSize(const struct Hrtf *Hrtf)
{
    return Hrtf->irSize;
}


void FreeHrtfs(void)
{
    struct Hrtf *Hrtf = NULL;

    while((Hrtf=LoadedHrtfs) != NULL)
    {
        LoadedHrtfs = Hrtf->next;
        free((void*)Hrtf->azCount);
        free((void*)Hrtf->evOffset);
        free((void*)Hrtf->coeffs);
        free((void*)Hrtf->delays);
        al_string_deinit(&Hrtf->filename);
        free(Hrtf);
    }
}
