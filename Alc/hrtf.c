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


typedef struct {
    ALsizei num_angles;
    ALsizei max_angle;
    ALshort coeffs[][2][HRTF_LENGTH];
} HrtfFilterCoeffs;

#include "hrtf_tables.inc"

static void get_angle_coeffs(const HrtfFilterCoeffs *elev, ALfloat angle, const ALshort **left, const ALshort **right)
{
    if(angle < 0)
    {
        int idx = ((angle > -elev->max_angle) ?
                   (int)(angle*(elev->num_angles-1)/-elev->max_angle + 0.5) :
                   (elev->num_angles-1));
        *left  = elev->coeffs[idx][1];
        *right = elev->coeffs[idx][0];
    }
    else
    {
        int idx = ((angle < elev->max_angle) ?
                   (int)(angle*(elev->num_angles-1)/elev->max_angle + 0.5) :
                   (elev->num_angles-1));
        *left  = elev->coeffs[idx][0];
        *right = elev->coeffs[idx][1];
    }
}

void GetHrtfCoeffs(ALfloat elevation, ALfloat angle, const ALshort **left, const ALshort **right)
{
    int idx;

    if(elevation > 90.f) elevation = 90.f - (elevation - 90.f);
    else if(elevation < -90.f) elevation = -90.f - (elevation - -90.f);

    idx = (int)(elevation/10.0 + 0.5);
    if(idx >= 9) return get_angle_coeffs(&Elev90, angle, left, right);
    if(idx >= 8) return get_angle_coeffs(&Elev80, angle, left, right);
    if(idx >= 7) return get_angle_coeffs(&Elev70, angle, left, right);
    if(idx >= 6) return get_angle_coeffs(&Elev60, angle, left, right);
    if(idx >= 5) return get_angle_coeffs(&Elev50, angle, left, right);
    if(idx >= 4) return get_angle_coeffs(&Elev40, angle, left, right);
    if(idx >= 3) return get_angle_coeffs(&Elev30, angle, left, right);
    if(idx >= 2) return get_angle_coeffs(&Elev20, angle, left, right);
    if(idx >= 1) return get_angle_coeffs(&Elev10, angle, left, right);
    if(idx >= 0) return get_angle_coeffs(&Elev0, angle, left, right);
    if(idx >= -1) return get_angle_coeffs(&Elev10n, angle, left, right);
    if(idx >= -2) return get_angle_coeffs(&Elev20n, angle, left, right);
    if(idx >= -3) return get_angle_coeffs(&Elev30n, angle, left, right);
    return get_angle_coeffs(&Elev40n, angle, left, right);
}
