/*
 * SOFA info utility for inspecting SOFA file metrics and determining HRTF
 * utility compatible layouts.
 *
 * Copyright (C) 2018-2019  Christopher Fitzgerald
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Or visit:  http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

#include <stdio.h>
#include <stdlib.h>

#include <cmath>
#include <vector>

#include <mysofa.h>

#include "win_main_utf8.h"

using uint = unsigned int;

// Per-field measurement info.
struct HrirFdT {
    float mDistance{0.0f};
    uint mEvCount{0u};
    uint mEvStart{0u};
    std::vector<uint> mAzCounts;
};

static const char *SofaErrorStr(int err)
{
    switch(err)
    {
        case MYSOFA_OK:
            return "OK";
        case MYSOFA_INVALID_FORMAT:
            return "Invalid format";
        case MYSOFA_UNSUPPORTED_FORMAT:
            return "Unsupported format";
        case MYSOFA_INTERNAL_ERROR:
            return "Internal error";
        case MYSOFA_NO_MEMORY:
            return "Out of memory";
        case MYSOFA_READ_ERROR:
            return "Read error";
    }

    return "Unknown";
}

static void PrintSofaAttributes(const char *prefix, struct MYSOFA_ATTRIBUTE *attribute)
{
    while(attribute)
    {
        fprintf(stdout, "%s.%s: %s\n", prefix, attribute->name, attribute->value);
        attribute = attribute->next;
    }
}

static void PrintSofaArray(const char *prefix, struct MYSOFA_ARRAY *array)
{
    PrintSofaAttributes(prefix, array->attributes);

    for(uint i{0u};i < array->elements;i++)
        fprintf(stdout, "%s[%u]: %.6f\n", prefix, i, array->values[i]);
}

/* Produces a sorted array of unique elements from a particular axis of the
 * triplets array.  The filters are used to focus on particular coordinates
 * of other axes as necessary.  The epsilons are used to constrain the
 * equality of unique elements.
 */
static uint GetUniquelySortedElems(const uint m, const float *triplets, const uint axis,
                                   const float *const (&filters)[3], const float (&epsilons)[3],
                                   float *elems)
{
    uint count{0u};
    for(uint i{0u};i < 3*m;i += 3)
    {
        float elem = triplets[i + axis];

        uint j;
        for(j = 0;j < 3;j++)
        {
            if(filters[j] && std::fabs(triplets[i + j] - *filters[j]) > epsilons[j])
                break;
        }
        if(j < 3)
            continue;

        for(j = 0;j < count;j++)
        {
            const float delta{elem - elems[j]};

            if(delta > epsilons[axis])
                continue;

            if(delta >= -epsilons[axis])
                break;

            for(uint k{count};k > j;k--)
                elems[k] = elems[k - 1];

            elems[j] = elem;
            count++;
            break;
        }

        if(j >= count)
            elems[count++] = elem;
    }

    return count;
}

/* Given a list of elements, this will produce the smallest step size that
 * can uniformly cover a fair portion of the list.  Ideally this will be over
 * half, but in degenerate cases this can fall to a minimum of 5 (the lower
 * limit on elevations necessary to build a layout).
 */
static float GetUniformStepSize(const float epsilon, const uint m, const float *elems)
{
    std::vector<float> steps(m, 0.0f);
    std::vector<uint> counts(m, 0u);
    uint count{0u};

    for(uint stride{1u};stride < m/2;stride++)
    {
        for(uint i{0u};i < m-stride;i++)
        {
            const float step{elems[i + stride] - elems[i]};

            uint j;
            for(j = 0;j < count;j++)
            {
                if(std::fabs(step - steps[j]) < epsilon)
                {
                    counts[j]++;
                    break;
                }
            }

            if(j >= count)
            {
                steps[j] = step;
                counts[j] = 1;
                count++;
            }
        }

        for(uint i{1u};i < count;i++)
        {
            if(counts[i] > counts[0])
            {
                steps[0] = steps[i];
                counts[0] = counts[i];
            }
        }

        count = 1;

        if(counts[0] > m/2)
            return steps[0];
    }

    if(counts[0] > 5)
        return steps[0];
    return 0.0f;
}

/* Attempts to produce a compatible layout.  Most data sets tend to be
 * uniform and have the same major axis as used by OpenAL Soft's HRTF model.
 * This will remove outliers and produce a maximally dense layout when
 * possible.  Those sets that contain purely random measurements or use
 * different major axes will fail.
 */
static void PrintCompatibleLayout(const uint m, const float *xyzs)
{
    std::vector<float> aers(3*m, 0.0f);
    std::vector<float> elems(m, 0.0f);

    fprintf(stdout, "\n");

    for(uint i{0u};i < 3*m;i += 3)
    {
        aers[i] = xyzs[i];
        aers[i + 1] = xyzs[i + 1];
        aers[i + 2] = xyzs[i + 2];
        mysofa_c2s(&aers[i]);
    }

    uint fdCount{GetUniquelySortedElems(m, aers.data(), 2, { nullptr, nullptr, nullptr }, { 0.1f, 0.1f, 0.001f }, elems.data())};
    if(fdCount > (m / 3))
    {
        fprintf(stdout, "Incompatible layout (inumerable radii).\n");
        return;
    }

    std::vector<HrirFdT> fds(fdCount);
    for(uint fi{0u};fi < fdCount;fi++)
        fds[fi].mDistance = elems[fi];

    for(uint fi{0u};fi < fdCount;fi++)
    {
        float dist{fds[fi].mDistance};
        uint evCount{GetUniquelySortedElems(m, aers.data(), 1, { nullptr, nullptr, &dist }, { 0.1f, 0.1f, 0.001f }, elems.data())};

        if(evCount > (m / 3))
        {
            fprintf(stdout, "Incompatible layout (innumerable elevations).\n");
            return;
        }

        float step{GetUniformStepSize(0.1f, evCount, elems.data())};
        if(step <= 0.0f)
        {
            fprintf(stdout, "Incompatible layout (non-uniform elevations).\n");
            return;
        }

        uint evStart{0u};
        for(uint ei{0u};ei < evCount;ei++)
        {
            float ev{90.0f + elems[ei]};
            float eif{std::round(ev / step)};

            if(std::fabs(eif - static_cast<uint>(eif)) < (0.1f / step))
            {
                evStart = static_cast<uint>(eif);
                break;
            }
        }

        evCount = static_cast<uint>(std::round(180.0f / step)) + 1;
        if(evCount < 5)
        {
            fprintf(stdout, "Incompatible layout (too few uniform elevations).\n");
            return;
        }

        fds[fi].mEvCount = evCount;
        fds[fi].mEvStart = evStart;
        fds[fi].mAzCounts.resize(evCount);
        auto &azCounts = fds[fi].mAzCounts;

        for(uint ei{evStart};ei < evCount;ei++)
        {
            float ev{-90.0f + ei * 180.0f / (evCount - 1)};
            uint azCount{GetUniquelySortedElems(m, aers.data(), 0, { nullptr, &ev, &dist }, { 0.1f, 0.1f, 0.001f }, elems.data())};

            if(azCount > (m / 3))
            {
                fprintf(stdout, "Incompatible layout (innumerable azimuths).\n");
                return;
            }

            if(ei > 0 && ei < (evCount - 1))
            {
                step = GetUniformStepSize(0.1f, azCount, elems.data());
                if(step <= 0.0f)
                {
                    fprintf(stdout, "Incompatible layout (non-uniform azimuths).\n");
                    return;
                }

                azCounts[ei] = static_cast<uint>(std::round(360.0f / step));
            }
            else if(azCount != 1)
            {
                fprintf(stdout, "Incompatible layout (non-singular poles).\n");
                return;
            }
            else
            {
                azCounts[ei] = 1;
            }
        }

        for(uint ei{0u};ei < evStart;ei++)
            azCounts[ei] = azCounts[evCount - ei - 1];
    }

    fprintf(stdout, "Compatible Layout:\n\ndistance = %.3f", fds[0].mDistance);

    for(uint fi{1u};fi < fdCount;fi++)
        fprintf(stdout, ", %.3f", fds[fi].mDistance);

    fprintf(stdout, "\nazimuths = ");
    for(uint fi{0u};fi < fdCount;fi++)
    {
        for(uint ei{0u};ei < fds[fi].mEvCount;ei++)
            fprintf(stdout, "%d%s", fds[fi].mAzCounts[ei],
                (ei < (fds[fi].mEvCount - 1)) ? ", " :
                (fi < (fdCount - 1)) ? ";\n           " : "\n");
    }
}

// Load and inspect the given SOFA file.
static void SofaInfo(const char *filename)
{
    struct MYSOFA_EASY sofa;

    sofa.lookup = nullptr;
    sofa.neighborhood = nullptr;

    int err;
    sofa.hrtf = mysofa_load(filename, &err);

    if(!sofa.hrtf)
    {
        mysofa_close(&sofa);
        fprintf(stdout, "Error: Could not load source file '%s'.\n", filename);
        return;
    }

    err = mysofa_check(sofa.hrtf);
    if(err != MYSOFA_OK)
/* NOTE: Some valid SOFA files are failing this check.
    {
        mysofa_close(&sofa);
        fprintf(stdout, "Error: Malformed source file '%s' (%s).\n", filename, SofaErrorStr(err));

        return;
    }
*/
        fprintf(stdout, "Warning: Supposedly malformed source file '%s' (%s).\n", filename, SofaErrorStr(err));

    mysofa_tocartesian(sofa.hrtf);

    PrintSofaAttributes("Info", sofa.hrtf->attributes);

    fprintf(stdout, "Measurements: %u\n", sofa.hrtf->M);
    fprintf(stdout, "Receivers: %u\n", sofa.hrtf->R);
    fprintf(stdout, "Emitters: %u\n", sofa.hrtf->E);
    fprintf(stdout, "Samples: %u\n", sofa.hrtf->N);

    PrintSofaArray("SampleRate", &sofa.hrtf->DataSamplingRate);
    PrintSofaArray("DataDelay", &sofa.hrtf->DataDelay);

    PrintCompatibleLayout(sofa.hrtf->M, sofa.hrtf->SourcePosition.values);

    mysofa_free(sofa.hrtf);
}

int main(int argc, char *argv[])
{
    GET_UNICODE_ARGS(&argc, &argv);

    if(argc != 2)
    {
        fprintf(stdout, "Usage: %s <sofa-file>\n", argv[0]);
        return 0;
    }

    SofaInfo(argv[1]);

    return 0;
}

