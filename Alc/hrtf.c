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
#include "almalloc.h"


/* Current data set limits defined by the makehrtf utility. */
#define MIN_IR_SIZE                  (8)
#define MAX_IR_SIZE                  (128)
#define MOD_IR_SIZE                  (8)

#define MIN_EV_COUNT                 (5)
#define MAX_EV_COUNT                 (128)

#define MIN_AZ_COUNT                 (1)
#define MAX_AZ_COUNT                 (128)

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
void GetLerpedHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat spread, ALfloat gain, ALfloat (*coeffs)[2], ALuint *delays)
{
    ALuint evidx[2], lidx[4], ridx[4];
    ALfloat mu[3], blend[4];
    ALfloat dirfact;
    ALuint i;

    dirfact = 1.0f - (spread / F_TAU);

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


static struct Hrtf *LoadHrtf00(FILE *f, const_al_string filename)
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
        size_t total = sizeof(struct Hrtf);
        total += sizeof(azCount[0])*evCount;
        total += sizeof(evOffset[0])*evCount;
        total += sizeof(coeffs[0])*irSize*irCount;
        total += sizeof(delays[0])*irCount;
        total += al_string_length(filename)+1;

        Hrtf = al_calloc(16, total);
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
        Hrtf->azCount = ((ALubyte*)(Hrtf+1));
        Hrtf->evOffset = ((ALushort*)(Hrtf->azCount + evCount));
        Hrtf->coeffs = ((ALshort*)(Hrtf->evOffset + evCount));
        Hrtf->delays = ((ALubyte*)(Hrtf->coeffs + irSize*irCount));
        Hrtf->filename = ((char*)(Hrtf->delays + irCount));
        Hrtf->next = NULL;

        memcpy((void*)Hrtf->azCount, azCount, sizeof(azCount[0])*evCount);
        memcpy((void*)Hrtf->evOffset, evOffset, sizeof(evOffset[0])*evCount);
        memcpy((void*)Hrtf->coeffs, coeffs, sizeof(coeffs[0])*irSize*irCount);
        memcpy((void*)Hrtf->delays, delays, sizeof(delays[0])*irCount);
        memcpy((void*)Hrtf->filename, al_string_get_cstr(filename), al_string_length(filename)+1);
    }

    free(azCount);
    free(evOffset);
    free(coeffs);
    free(delays);
    return Hrtf;
}


static struct Hrtf *LoadHrtf01(FILE *f, const_al_string filename)
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
        size_t total = sizeof(struct Hrtf);
        total += sizeof(azCount[0])*evCount;
        total += sizeof(evOffset[0])*evCount;
        total += sizeof(coeffs[0])*irSize*irCount;
        total += sizeof(delays[0])*irCount;
        total += al_string_length(filename)+1;

        Hrtf = al_calloc(16, total);
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
        Hrtf->azCount = ((ALubyte*)(Hrtf+1));
        Hrtf->evOffset = ((ALushort*)(Hrtf->azCount + evCount));
        Hrtf->coeffs = ((ALshort*)(Hrtf->evOffset + evCount));
        Hrtf->delays = ((ALubyte*)(Hrtf->coeffs + irSize*irCount));
        Hrtf->filename = ((char*)(Hrtf->delays + irCount));
        Hrtf->next = NULL;

        memcpy((void*)Hrtf->azCount, azCount, sizeof(azCount[0])*evCount);
        memcpy((void*)Hrtf->evOffset, evOffset, sizeof(evOffset[0])*evCount);
        memcpy((void*)Hrtf->coeffs, coeffs, sizeof(coeffs[0])*irSize*irCount);
        memcpy((void*)Hrtf->delays, delays, sizeof(delays[0])*irCount);
        memcpy((void*)Hrtf->filename, al_string_get_cstr(filename), al_string_length(filename)+1);
    }

    free(azCount);
    free(evOffset);
    free(coeffs);
    free(delays);
    return Hrtf;
}


static void AddFileEntry(vector_HrtfEntry *list, al_string *filename)
{
    HrtfEntry entry = { AL_STRING_INIT_STATIC(), NULL };
    struct Hrtf *hrtf = NULL;
    const HrtfEntry *iter;
    const char *name;
    const char *ext;
    ALchar magic[8];
    FILE *f;
    int i;

    name = strrchr(al_string_get_cstr(*filename), '/');
    if(!name) name = strrchr(al_string_get_cstr(*filename), '\\');
    if(!name) name = al_string_get_cstr(*filename);
    else ++name;

    entry.hrtf = LoadedHrtfs;
    while(entry.hrtf)
    {
        if(al_string_cmp_cstr(*filename, entry.hrtf->filename) == 0)
        {
            TRACE("Skipping duplicate file entry %s\n", al_string_get_cstr(*filename));
            goto done;
        }
        entry.hrtf = entry.hrtf->next;
    }

    TRACE("Loading %s...\n", al_string_get_cstr(*filename));
    f = al_fopen(al_string_get_cstr(*filename), "rb");
    if(f == NULL)
    {
        ERR("Could not open %s\n", al_string_get_cstr(*filename));
        goto done;
    }

    if(fread(magic, 1, sizeof(magic), f) != sizeof(magic))
        ERR("Failed to read header from %s\n", al_string_get_cstr(*filename));
    else
    {
        if(memcmp(magic, magicMarker00, sizeof(magicMarker00)) == 0)
        {
            TRACE("Detected data set format v0\n");
            hrtf = LoadHrtf00(f, *filename);
        }
        else if(memcmp(magic, magicMarker01, sizeof(magicMarker01)) == 0)
        {
            TRACE("Detected data set format v1\n");
            hrtf = LoadHrtf01(f, *filename);
        }
        else
            ERR("Invalid header in %s: \"%.8s\"\n", al_string_get_cstr(*filename), magic);
    }
    fclose(f);

    if(!hrtf)
    {
        ERR("Failed to load %s\n", al_string_get_cstr(*filename));
        goto done;
    }

    hrtf->next = LoadedHrtfs;
    LoadedHrtfs = hrtf;
    TRACE("Loaded HRTF support for format: %s %uhz\n",
            DevFmtChannelsString(DevFmtStereo), hrtf->sampleRate);
    entry.hrtf = hrtf;

    /* TODO: Get a human-readable name from the HRTF data (possibly coming in a
     * format update). */
    ext = strrchr(name, '.');

    i = 0;
    do {
        if(!ext)
            al_string_copy_cstr(&entry.name, name);
        else
            al_string_copy_range(&entry.name, name, ext);
        if(i != 0)
        {
            char str[64];
            snprintf(str, sizeof(str), " #%d", i+1);
            al_string_append_cstr(&entry.name, str);
        }
        ++i;

#define MATCH_NAME(i)  (al_string_cmp(entry.name, (i)->name) == 0)
        VECTOR_FIND_IF(iter, const HrtfEntry, *list, MATCH_NAME);
#undef MATCH_NAME
    } while(iter != VECTOR_END(*list));

    TRACE("Adding entry \"%s\" from file \"%s\"\n", al_string_get_cstr(entry.name),
          al_string_get_cstr(*filename));
    VECTOR_PUSH_BACK(*list, entry);

done:
    al_string_deinit(filename);
}

vector_HrtfEntry EnumerateHrtf(const_al_string devname)
{
    vector_HrtfEntry list = VECTOR_INIT_STATIC();
    const char *defaulthrtf = "";
    const char *pathlist = "";
    bool usedefaults = true;

    if(ConfigValueStr(al_string_get_cstr(devname), NULL, "hrtf-paths", &pathlist))
    {
        while(pathlist && *pathlist)
        {
            const char *next, *end;

            while(isspace(*pathlist) || *pathlist == ',')
                pathlist++;
            if(*pathlist == '\0')
                continue;

            next = strchr(pathlist, ',');
            if(next)
                end = next++;
            else
            {
                end = pathlist + strlen(pathlist);
                usedefaults = false;
            }

            while(end != pathlist && isspace(*(end-1)))
                --end;
            if(end != pathlist)
            {
                al_string pname = AL_STRING_INIT_STATIC();
                vector_al_string flist;

                al_string_append_range(&pname, pathlist, end);

                flist = SearchDataFiles(".mhr", al_string_get_cstr(pname));
                VECTOR_FOR_EACH_PARAMS(al_string, flist, AddFileEntry, &list);
                VECTOR_DEINIT(flist);

                al_string_deinit(&pname);
            }

            pathlist = next;
        }
    }
    else if(ConfigValueExists(al_string_get_cstr(devname), NULL, "hrtf_tables"))
        ERR("The hrtf_tables option is deprecated, please use hrtf-paths instead.\n");

    if(usedefaults)
    {
        vector_al_string flist = SearchDataFiles(".mhr", "openal/hrtf");
        VECTOR_FOR_EACH_PARAMS(al_string, flist, AddFileEntry, &list);
        VECTOR_DEINIT(flist);
    }

    if(VECTOR_SIZE(list) > 1 && ConfigValueStr(al_string_get_cstr(devname), NULL, "default-hrtf", &defaulthrtf))
    {
        const HrtfEntry *iter;
        /* Find the preferred HRTF and move it to the front of the list. */
#define FIND_ENTRY(i)  (al_string_cmp_cstr((i)->name, defaulthrtf) == 0)
        VECTOR_FIND_IF(iter, const HrtfEntry, list, FIND_ENTRY);
        if(iter != VECTOR_END(list) && iter != VECTOR_BEGIN(list))
        {
            HrtfEntry entry = *iter;
            memmove(&VECTOR_ELEM(list,1), &VECTOR_ELEM(list,0),
                    (iter-VECTOR_BEGIN(list))*sizeof(HrtfEntry));
            VECTOR_ELEM(list,0) = entry;
        }
        else
            WARN("Failed to find default HRTF \"%s\"\n", defaulthrtf);
#undef FIND_ENTRY
    }

    return list;
}

void FreeHrtfList(vector_HrtfEntry *list)
{
#define CLEAR_ENTRY(i) do {           \
    al_string_deinit(&(i)->name);     \
} while(0)
    VECTOR_FOR_EACH(HrtfEntry, *list, CLEAR_ENTRY);
    VECTOR_DEINIT(*list);
#undef CLEAR_ENTRY
}


void FreeHrtfs(void)
{
    struct Hrtf *Hrtf = LoadedHrtfs;
    LoadedHrtfs = NULL;

    while(Hrtf != NULL)
    {
        struct Hrtf *next = Hrtf->next;
        al_free(Hrtf);
        Hrtf = next;
    }
}
