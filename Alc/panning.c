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

extern inline void SetGains(const ALCdevice *device, ALfloat ingain, ALfloat gains[MaxChannels]);


static inline void Set0thOrder(ALfloat coeffs[16], ALfloat w)
{
    coeffs[0] = w;
}

static inline void Set1stOrder(ALfloat coeffs[16], ALfloat w, ALfloat x, ALfloat y, ALfloat z)
{
    coeffs[0] = w;
    coeffs[1] = x;
    coeffs[2] = y;
    coeffs[3] = z;
}

static inline void Set2ndOrder(ALfloat coeffs[16], ALfloat w, ALfloat x, ALfloat y, ALfloat z, ALfloat r, ALfloat s, ALfloat t, ALfloat u, ALfloat v)
{
    coeffs[0] = w;
    coeffs[1] = x;
    coeffs[2] = y;
    coeffs[3] = z;
    coeffs[4] = r;
    coeffs[5] = s;
    coeffs[6] = t;
    coeffs[7] = u;
    coeffs[8] = v;
}

static inline void Set3rdOrder(ALfloat coeffs[16], ALfloat w, ALfloat x, ALfloat y, ALfloat z, ALfloat r, ALfloat s, ALfloat t, ALfloat u, ALfloat v, ALfloat k, ALfloat l, ALfloat m, ALfloat n, ALfloat o, ALfloat p, ALfloat q)
{
    coeffs[0] = w;
    coeffs[1] = x;
    coeffs[2] = y;
    coeffs[3] = z;
    coeffs[4] = r;
    coeffs[5] = s;
    coeffs[6] = t;
    coeffs[7] = u;
    coeffs[8] = v;
    coeffs[9] = k;
    coeffs[10] = l;
    coeffs[11] = m;
    coeffs[12] = n;
    coeffs[13] = o;
    coeffs[14] = p;
    coeffs[15] = q;
}


void ComputeAngleGains(const ALCdevice *device, ALfloat angle, ALfloat hwidth, ALfloat ingain, ALfloat gains[MaxChannels])
{
    ALfloat tmpgains[MaxChannels] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    enum Channel Speaker2Chan[MaxChannels];
    ALfloat SpeakerAngle[MaxChannels];
    ALfloat langle, rangle;
    ALfloat a;
    ALuint i;

    for(i = 0;i < device->NumSpeakers;i++)
        Speaker2Chan[i] = device->Speaker[i].ChanName;
    for(i = 0;i < device->NumSpeakers;i++)
        SpeakerAngle[i] = device->Speaker[i].Angle;

    for(i = 0;i < MaxChannels;i++)
        gains[i] = 0.0f;

    /* Some easy special-cases first... */
    if(device->NumSpeakers <= 1 || hwidth >= F_PI)
    {
        /* Full coverage for all speakers. */
        for(i = 0;i < device->NumSpeakers;i++)
        {
            enum Channel chan = Speaker2Chan[i];
            gains[chan] = ingain;
        }
        return;
    }
    if(hwidth <= 0.0f)
    {
        /* Infinitely small sound point. */
        for(i = 0;i < device->NumSpeakers-1;i++)
        {
            if(angle >= SpeakerAngle[i] && angle < SpeakerAngle[i+1])
            {
                /* Sound is between speakers i and i+1 */
                a =             (angle-SpeakerAngle[i]) /
                    (SpeakerAngle[i+1]-SpeakerAngle[i]);
                gains[Speaker2Chan[i]]   = sqrtf(1.0f-a) * ingain;
                gains[Speaker2Chan[i+1]] = sqrtf(     a) * ingain;
                return;
            }
        }
        /* Sound is between last and first speakers */
        if(angle < SpeakerAngle[0])
            angle += F_2PI;
        a =                   (angle-SpeakerAngle[i]) /
            (F_2PI + SpeakerAngle[0]-SpeakerAngle[i]);
        gains[Speaker2Chan[i]] = sqrtf(1.0f-a) * ingain;
        gains[Speaker2Chan[0]] = sqrtf(     a) * ingain;
        return;
    }

    if(fabsf(angle)+hwidth > F_PI)
    {
        /* The coverage area would go outside of -pi...+pi. Instead, rotate the
         * speaker angles so it would be as if angle=0, and keep them wrapped
         * within -pi...+pi. */
        if(angle > 0.0f)
        {
            ALuint done;
            ALuint i = 0;
            while(i < device->NumSpeakers && device->Speaker[i].Angle-angle < -F_PI)
                i++;
            for(done = 0;i < device->NumSpeakers;done++)
            {
                SpeakerAngle[done] = device->Speaker[i].Angle-angle;
                Speaker2Chan[done] = device->Speaker[i].ChanName;
                i++;
            }
            for(i = 0;done < device->NumSpeakers;i++)
            {
                SpeakerAngle[done] = device->Speaker[i].Angle-angle + F_2PI;
                Speaker2Chan[done] = device->Speaker[i].ChanName;
                done++;
            }
        }
        else
        {
            /* NOTE: '< device->NumChan' on the iterators is correct here since
             * we need to handle index 0. Because the iterators are unsigned,
             * they'll underflow and wrap to become 0xFFFFFFFF, which will
             * break as expected. */
            ALuint done;
            ALuint i = device->NumSpeakers-1;
            while(i < device->NumSpeakers && device->Speaker[i].Angle-angle > F_PI)
                i--;
            for(done = device->NumSpeakers-1;i < device->NumSpeakers;done--)
            {
                SpeakerAngle[done] = device->Speaker[i].Angle-angle;
                Speaker2Chan[done] = device->Speaker[i].ChanName;
                i--;
            }
            for(i = device->NumSpeakers-1;done < device->NumSpeakers;i--)
            {
                SpeakerAngle[done] = device->Speaker[i].Angle-angle - F_2PI;
                Speaker2Chan[done] = device->Speaker[i].ChanName;
                done--;
            }
        }
        angle = 0.0f;
    }
    langle = angle - hwidth;
    rangle = angle + hwidth;

    /* First speaker */
    i = 0;
    do {
        ALuint last = device->NumSpeakers-1;
        enum Channel chan = Speaker2Chan[i];

        if(SpeakerAngle[i] >= langle && SpeakerAngle[i] <= rangle)
        {
            tmpgains[chan] = 1.0f;
            continue;
        }

        if(SpeakerAngle[i] < langle && SpeakerAngle[i+1] > langle)
        {
            a =            (langle-SpeakerAngle[i]) /
                (SpeakerAngle[i+1]-SpeakerAngle[i]);
            tmpgains[chan] = lerp(tmpgains[chan], 1.0f, 1.0f-a);
        }
        if(SpeakerAngle[i] > rangle)
        {
            a =          (F_2PI + rangle-SpeakerAngle[last]) /
                (F_2PI + SpeakerAngle[i]-SpeakerAngle[last]);
            tmpgains[chan] = lerp(tmpgains[chan], 1.0f, a);
        }
        else if(SpeakerAngle[last] < rangle)
        {
            a =                  (rangle-SpeakerAngle[last]) /
                (F_2PI + SpeakerAngle[i]-SpeakerAngle[last]);
            tmpgains[chan] = lerp(tmpgains[chan], 1.0f, a);
        }
    } while(0);

    for(i = 1;i < device->NumSpeakers-1;i++)
    {
        enum Channel chan = Speaker2Chan[i];
        if(SpeakerAngle[i] >= langle && SpeakerAngle[i] <= rangle)
        {
            tmpgains[chan] = 1.0f;
            continue;
        }

        if(SpeakerAngle[i] < langle && SpeakerAngle[i+1] > langle)
        {
            a =            (langle-SpeakerAngle[i]) /
                (SpeakerAngle[i+1]-SpeakerAngle[i]);
            tmpgains[chan] = lerp(tmpgains[chan], 1.0f, 1.0f-a);
        }
        if(SpeakerAngle[i] > rangle && SpeakerAngle[i-1] < rangle)
        {
            a =          (rangle-SpeakerAngle[i-1]) /
                (SpeakerAngle[i]-SpeakerAngle[i-1]);
            tmpgains[chan] = lerp(tmpgains[chan], 1.0f, a);
        }
    }

    /* Last speaker */
    i = device->NumSpeakers-1;
    do {
        enum Channel chan = Speaker2Chan[i];
        if(SpeakerAngle[i] >= langle && SpeakerAngle[i] <= rangle)
        {
            tmpgains[Speaker2Chan[i]] = 1.0f;
            continue;
        }
        if(SpeakerAngle[i] > rangle && SpeakerAngle[i-1] < rangle)
        {
            a =          (rangle-SpeakerAngle[i-1]) /
                (SpeakerAngle[i]-SpeakerAngle[i-1]);
            tmpgains[chan] = lerp(tmpgains[chan], 1.0f, a);
        }
        if(SpeakerAngle[i] < langle)
        {
            a =                  (langle-SpeakerAngle[i]) /
                (F_2PI + SpeakerAngle[0]-SpeakerAngle[i]);
            tmpgains[chan] = lerp(tmpgains[chan], 1.0f, 1.0f-a);
        }
        else if(SpeakerAngle[0] > langle)
        {
            a =          (F_2PI + langle-SpeakerAngle[i]) /
                (F_2PI + SpeakerAngle[0]-SpeakerAngle[i]);
            tmpgains[chan] = lerp(tmpgains[chan], 1.0f, 1.0f-a);
        }
    } while(0);

    for(i = 0;i < device->NumSpeakers;i++)
    {
        enum Channel chan = device->Speaker[i].ChanName;
        gains[chan] = sqrtf(tmpgains[chan]) * ingain;
    }
}

void ComputeDirectionalGains(const ALCdevice *device, const ALfloat dir[3], ALfloat ingain, ALfloat gains[MaxChannels])
{
    ALfloat coeffs[MAX_AMBI_COEFFS];
    ALuint i, j;

    /* Convert from OpenAL coords to Ambisonics. */
    coeffs[0] = 0.7071f; /* sqrt(1.0 / 2.0) */
    coeffs[1] = -dir[2]; /* X */
    coeffs[2] = -dir[0]; /* Y */
    coeffs[3] =  dir[1]; /* Z */
    coeffs[4] = 0.5f * (3.0f*dir[1]*dir[1] - 1.0f); /* 0.5 * (3*Z*Z - 1) */
    coeffs[5] = 2.0f *  dir[1] * -dir[2]; /* 2*Z*X */
    coeffs[6] = 2.0f * -dir[0] *  dir[1]; /* 2*Y*Z */
    coeffs[7] = dir[2]*dir[2] - dir[0]*dir[0]; /* X*X - Y*Y */
    coeffs[8] = 2.0f * -dir[2] * -dir[0]; /* 2*X*Y */
    coeffs[9] = 0.5f *  dir[1] * (5.0f*dir[1]*dir[1] - 3.0f); /* 0.5 * Z * (5*Z*Z - 3) */
    coeffs[10] = 0.7262f * -dir[2] * (5.0f*dir[1]*dir[1] - 1.0f); /* sqrt(135.0 / 256.0) * X * (5*Z*Z - 1) */
    coeffs[11] = 0.7262f * -dir[0] * (5.0f*dir[1]*dir[1] - 1.0f); /* sqrt(135.0 / 256.0) * Y * (5*Z*Z - 1) */
    coeffs[12] = 2.5981f *  dir[1] * (dir[2]*dir[2] - dir[0]*dir[0]); /* sqrt(27.0 / 4.0) * Z * (X*X - Y*Y) */
    coeffs[13] = 5.1962f * -dir[2] * -dir[0] * dir[1]; /* sqrt(27) * X * Y * Z */
    coeffs[14] = -dir[2] * (dir[2]*dir[2] - 3.0f*dir[0]*dir[0]); /* X * (X*X - 3*Y*Y) */
    coeffs[15] = -dir[0] * (3.0f*dir[2]*dir[2] - dir[0]*dir[0]); /* Y * (3*X*X - Y*Y) */

    for(i = 0;i < MaxChannels;i++)
        gains[i] = 0.0f;
    for(i = 0;i < device->NumSpeakers;i++)
    {
        enum Channel chan = device->Speaker[i].ChanName;
        for(j = 0;j < MAX_AMBI_COEFFS;j++)
            gains[chan] += device->Speaker[i].Coeff[j]*coeffs[j];
        gains[chan] = maxf(gains[chan], 0.0f) * ingain;
    }
}


ALvoid aluInitPanning(ALCdevice *device)
{
    memset(device->Speaker, 0, sizeof(device->Speaker));
    device->NumSpeakers = 0;

    switch(device->FmtChans)
    {
        case DevFmtMono:
            device->NumSpeakers = 1;
            device->Speaker[0].ChanName = FrontCenter;
            device->Speaker[0].Angle = DEG2RAD(0.0f);
            Set0thOrder(device->Speaker[0].Coeff, 1.4142f);
            break;

        case DevFmtStereo:
            device->NumSpeakers = 2;
            device->Speaker[0].ChanName = FrontLeft;
            device->Speaker[1].ChanName = FrontRight;
            device->Speaker[0].Angle = DEG2RAD(-90.0f);
            device->Speaker[1].Angle = DEG2RAD( 90.0f);
            Set1stOrder(device->Speaker[0].Coeff, 0.7071f, -0.5f, 0.0f, 0.0f);
            Set1stOrder(device->Speaker[1].Coeff, 0.7071f,  0.5f, 0.0f, 0.0f);
            break;

        case DevFmtQuad:
            device->NumSpeakers = 4;
            device->Speaker[0].ChanName = BackLeft;
            device->Speaker[1].ChanName = FrontLeft;
            device->Speaker[2].ChanName = FrontRight;
            device->Speaker[3].ChanName = BackRight;
            device->Speaker[0].Angle = DEG2RAD(-135.0f);
            device->Speaker[1].Angle = DEG2RAD( -45.0f);
            device->Speaker[2].Angle = DEG2RAD(  45.0f);
            device->Speaker[3].Angle = DEG2RAD( 135.0f);
            Set2ndOrder(device->Speaker[0].Coeff, 0.353543f, -0.306192f,  0.306181f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.117193f);
            Set2ndOrder(device->Speaker[1].Coeff, 0.353558f,  0.306181f,  0.306192f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.117183f);
            Set2ndOrder(device->Speaker[2].Coeff, 0.353543f,  0.306181f, -0.306192f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.117193f);
            Set2ndOrder(device->Speaker[3].Coeff, 0.353558f, -0.306192f, -0.306181f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.117183f);
            break;

        case DevFmtX51:
            device->NumSpeakers = 5;
            device->Speaker[0].ChanName = BackLeft;
            device->Speaker[1].ChanName = FrontLeft;
            device->Speaker[2].ChanName = FrontCenter;
            device->Speaker[3].ChanName = FrontRight;
            device->Speaker[4].ChanName = BackRight;
            device->Speaker[0].Angle = DEG2RAD(-110.0f);
            device->Speaker[1].Angle = DEG2RAD( -30.0f);
            device->Speaker[2].Angle = DEG2RAD(   0.0f);
            device->Speaker[3].Angle = DEG2RAD(  30.0f);
            device->Speaker[4].Angle = DEG2RAD( 110.0f);
            Set3rdOrder(device->Speaker[0].Coeff, 0.470934f, -0.369630f,  0.349383f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031379f, -0.058143f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007116f, -0.043968f);
            Set3rdOrder(device->Speaker[1].Coeff, 0.208954f,  0.212846f,  0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017738f,  0.204014f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051023f,  0.047490f);
            Set3rdOrder(device->Speaker[2].Coeff, 0.109403f,  0.179490f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f, -0.000001f);
            Set3rdOrder(device->Speaker[3].Coeff, 0.208950f,  0.212842f, -0.238350f, 0.0f, 0.0f, 0.0f, 0.0f, -0.017740f, -0.204011f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.051022f, -0.047489f);
            Set3rdOrder(device->Speaker[4].Coeff, 0.470936f, -0.369626f, -0.349386f, 0.0f, 0.0f, 0.0f, 0.0f, -0.031375f,  0.058144f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.007119f,  0.043968f);
            break;

        case DevFmtX51Side:
            device->NumSpeakers = 5;
            device->Speaker[0].ChanName = SideLeft;
            device->Speaker[1].ChanName = FrontLeft;
            device->Speaker[2].ChanName = FrontCenter;
            device->Speaker[3].ChanName = FrontRight;
            device->Speaker[4].ChanName = SideRight;
            device->Speaker[0].Angle = DEG2RAD(-90.0f);
            device->Speaker[1].Angle = DEG2RAD(-30.0f);
            device->Speaker[2].Angle = DEG2RAD(  0.0f);
            device->Speaker[3].Angle = DEG2RAD( 30.0f);
            device->Speaker[4].Angle = DEG2RAD( 90.0f);
            Set3rdOrder(device->Speaker[0].Coeff, 0.289151f, -0.081301f,  0.401292f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f, -0.071420f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f, -0.032897f);
            Set3rdOrder(device->Speaker[1].Coeff, 0.167065f,  0.200583f,  0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f,  0.068910f);
            Set3rdOrder(device->Speaker[2].Coeff, 0.109403f,  0.179490f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f, -0.000001f);
            Set3rdOrder(device->Speaker[3].Coeff, 0.167058f,  0.200580f, -0.172701f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029846f, -0.186405f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f, -0.068904f);
            Set3rdOrder(device->Speaker[4].Coeff, 0.289157f, -0.081298f, -0.401295f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f,  0.071419f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f,  0.032897f);
            break;

        case DevFmtX61:
            device->NumSpeakers = 6;
            device->Speaker[0].ChanName = SideLeft;
            device->Speaker[1].ChanName = FrontLeft;
            device->Speaker[2].ChanName = FrontCenter;
            device->Speaker[3].ChanName = FrontRight;
            device->Speaker[4].ChanName = SideRight;
            device->Speaker[5].ChanName = BackCenter;
            device->Speaker[0].Angle = DEG2RAD(-90.0f);
            device->Speaker[1].Angle = DEG2RAD(-30.0f);
            device->Speaker[2].Angle = DEG2RAD(  0.0f);
            device->Speaker[3].Angle = DEG2RAD( 30.0f);
            device->Speaker[4].Angle = DEG2RAD( 90.0f);
            device->Speaker[5].Angle = DEG2RAD(180.0f);
            Set3rdOrder(device->Speaker[0].Coeff, 0.289151f, -0.081301f,  0.401292f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f, -0.071420f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f, -0.032897f);
            Set3rdOrder(device->Speaker[1].Coeff, 0.167065f,  0.200583f,  0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f,  0.068910f);
            Set3rdOrder(device->Speaker[2].Coeff, 0.109403f,  0.179490f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f, -0.000001f);
            Set3rdOrder(device->Speaker[3].Coeff, 0.167058f,  0.200580f, -0.172701f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029846f, -0.186405f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f, -0.068904f);
            Set3rdOrder(device->Speaker[4].Coeff, 0.289157f, -0.081298f, -0.401295f, 0.0f, 0.0f, 0.0f, 0.0f, -0.188208f,  0.071419f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.010099f,  0.032897f);
            Set3rdOrder(device->Speaker[5].Coeff, 0.353556f, -0.461940f, -0.000006f, 0.0f, 0.0f, 0.0f, 0.0f,  0.165723f, -0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.000000f,  0.000005f);
            break;

        case DevFmtX71:
            device->NumSpeakers = 7;
            device->Speaker[0].ChanName = BackLeft;
            device->Speaker[1].ChanName = SideLeft;
            device->Speaker[2].ChanName = FrontLeft;
            device->Speaker[3].ChanName = FrontCenter;
            device->Speaker[4].ChanName = FrontRight;
            device->Speaker[5].ChanName = SideRight;
            device->Speaker[6].ChanName = BackRight;
            device->Speaker[0].Angle = DEG2RAD(-150.0f);
            device->Speaker[1].Angle = DEG2RAD( -90.0f);
            device->Speaker[2].Angle = DEG2RAD( -30.0f);
            device->Speaker[3].Angle = DEG2RAD(   0.0f);
            device->Speaker[4].Angle = DEG2RAD(  30.0f);
            device->Speaker[5].Angle = DEG2RAD(  90.0f);
            device->Speaker[6].Angle = DEG2RAD( 150.0f);
            Set3rdOrder(device->Speaker[0].Coeff, 0.224752f, -0.295009f,  0.170325f, 0.0f, 0.0f, 0.0f, 0.0f,  0.105349f, -0.182473f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.000000f,  0.065799f);
            Set3rdOrder(device->Speaker[1].Coeff, 0.224739f,  0.000002f,  0.340644f, 0.0f, 0.0f, 0.0f, 0.0f, -0.210697f,  0.000002f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.000000f, -0.065795f);
            Set3rdOrder(device->Speaker[2].Coeff, 0.167065f,  0.200583f,  0.172695f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029855f,  0.186407f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f,  0.068910f);
            Set3rdOrder(device->Speaker[3].Coeff, 0.109403f,  0.179490f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f,  0.142031f, -0.000002f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  0.072024f, -0.000001f);
            Set3rdOrder(device->Speaker[4].Coeff, 0.167058f,  0.200580f, -0.172701f, 0.0f, 0.0f, 0.0f, 0.0f,  0.029846f, -0.186405f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.039241f, -0.068904f);
            Set3rdOrder(device->Speaker[5].Coeff, 0.224754f,  0.000004f, -0.340647f, 0.0f, 0.0f, 0.0f, 0.0f, -0.210697f, -0.000004f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.000000f,  0.065796f);
            Set3rdOrder(device->Speaker[6].Coeff, 0.224739f, -0.295005f, -0.170331f, 0.0f, 0.0f, 0.0f, 0.0f,  0.105342f,  0.182470f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.000000f, -0.065792f);
            break;
    }
}
