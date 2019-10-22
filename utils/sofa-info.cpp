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

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <mysofa.h>

#include "win_main_utf8.h"


using uint = unsigned int;
using double3 = std::array<double,3>;

struct MySofaDeleter {
    void operator()(MYSOFA_HRTF *sofa) { mysofa_free(sofa); }
};
using MySofaHrtfPtr = std::unique_ptr<MYSOFA_HRTF,MySofaDeleter>;

// Per-field measurement info.
struct HrirFdT {
    double mDistance{0.0};
    uint mEvCount{0u};
    uint mEvStart{0u};
    std::vector<uint> mAzCounts;
};

static const char *SofaErrorStr(int err)
{
    switch(err)
    {
    case MYSOFA_OK: return "OK";
    case MYSOFA_INVALID_FORMAT: return "Invalid format";
    case MYSOFA_UNSUPPORTED_FORMAT: return "Unsupported format";
    case MYSOFA_INTERNAL_ERROR: return "Internal error";
    case MYSOFA_NO_MEMORY: return "Out of memory";
    case MYSOFA_READ_ERROR: return "Read error";
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
static uint GetUniquelySortedElems(const uint m, const double3 *aers, const uint axis,
    const double *const (&filters)[3], const double (&epsilons)[3], double *elems)
{
    uint count{0u};
    for(uint i{0u};i < m;++i)
    {
        const double elem{aers[i][axis]};

        uint j;
        for(j = 0;j < 3;j++)
        {
            if(filters[j] && std::fabs(aers[i][j] - *filters[j]) > epsilons[j])
                break;
        }
        if(j < 3)
            continue;

        for(j = 0;j < count;j++)
        {
            const double delta{elem - elems[j]};

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
static double GetUniformStepSize(const double epsilon, const uint m, const double *elems)
{
    auto steps = std::vector<double>(m, 0.0);
    auto counts = std::vector<uint>(m, 0u);
    uint count{0u};

    for(uint stride{1u};stride < m/2;stride++)
    {
        for(uint i{0u};i < m-stride;i++)
        {
            const double step{elems[i + stride] - elems[i]};

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
            break;
    }

    if(counts[0] > 255)
    {
        uint i{2u};
        while(counts[0]/i > 255 && (counts[0]%i) != 0)
            ++i;
        counts[0] /= i;
        steps[0] *= i;
    }
    if(counts[0] > 5)
        return steps[0];
    return 0.0;
}

/* Attempts to produce a compatible layout.  Most data sets tend to be
 * uniform and have the same major axis as used by OpenAL Soft's HRTF model.
 * This will remove outliers and produce a maximally dense layout when
 * possible.  Those sets that contain purely random measurements or use
 * different major axes will fail.
 */
static void PrintCompatibleLayout(const uint m, const float *xyzs)
{
    auto aers = std::vector<double3>(m, double3{});
    auto elems = std::vector<double>(m, {});

    fprintf(stdout, "\n");

    for(uint i{0u};i < m;++i)
    {
        float aer[3]{xyzs[i*3], xyzs[i*3 + 1], xyzs[i*3 + 2]};
        mysofa_c2s(&aer[0]);
        aers[i][0] = aer[0];
        aers[i][1] = aer[1];
        aers[i][2] = aer[2];
    }

    uint fdCount{GetUniquelySortedElems(m, aers.data(), 2, { nullptr, nullptr, nullptr },
        { 0.1, 0.1, 0.001 }, elems.data())};
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
        const double dist{fds[fi].mDistance};
        uint evCount{GetUniquelySortedElems(m, aers.data(), 1, { nullptr, nullptr, &dist },
            { 0.1, 0.1, 0.001 }, elems.data())};

        if(evCount > (m / 3))
        {
            fprintf(stdout, "Incompatible layout (innumerable elevations).\n");
            return;
        }

        double step{GetUniformStepSize(0.1, evCount, elems.data())};
        if(step <= 0.0)
        {
            fprintf(stdout, "Incompatible layout (non-uniform elevations).\n");
            return;
        }

        uint evStart{0u};
        for(uint ei{0u};ei < evCount;ei++)
        {
            double ev{90.0 + elems[ei]};
            double eif{std::round(ev / step)};
            const uint ev_start{static_cast<uint>(eif)};

            if(std::fabs(eif - static_cast<double>(ev_start)) < (0.1/step))
            {
                evStart = ev_start;
                break;
            }
        }

        evCount = static_cast<uint>(std::round(180.0 / step)) + 1;
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
            double ev{-90.0 + static_cast<double>(ei)*180.0/static_cast<double>(evCount - 1)};
            uint azCount{GetUniquelySortedElems(m, aers.data(), 0, { nullptr, &ev, &dist },
                { 0.1, 0.1, 0.001 }, elems.data())};

            if(azCount > (m / 3))
            {
                fprintf(stdout, "Incompatible layout (innumerable azimuths).\n");
                return;
            }

            if(ei > 0 && ei < (evCount - 1))
            {
                step = GetUniformStepSize(0.1, azCount, elems.data());
                if(step <= 0.0)
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
    int err;
    MySofaHrtfPtr sofa{mysofa_load(filename, &err)};
    if(!sofa)
    {
        fprintf(stdout, "Error: Could not load source file '%s'.\n", filename);
        return;
    }

    /* NOTE: Some valid SOFA files are failing this check. */
    err = mysofa_check(sofa.get());
    if(err != MYSOFA_OK)
        fprintf(stdout, "Warning: Supposedly malformed source file '%s' (%s).\n", filename,
            SofaErrorStr(err));

    mysofa_tocartesian(sofa.get());

    PrintSofaAttributes("Info", sofa->attributes);

    fprintf(stdout, "Measurements: %u\n", sofa->M);
    fprintf(stdout, "Receivers: %u\n", sofa->R);
    fprintf(stdout, "Emitters: %u\n", sofa->E);
    fprintf(stdout, "Samples: %u\n", sofa->N);

    PrintSofaArray("SampleRate", &sofa->DataSamplingRate);
    PrintSofaArray("DataDelay", &sofa->DataDelay);

    PrintCompatibleLayout(sofa->M, sofa->SourcePosition.values);
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

