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

static void SetSpeakerArrangement(const char *name, ALCdevice *device)
{
    char *confkey, *next;
    char *layout_str;
    char *sep, *end;
    enum Channel val;
    const char *str;
    ALuint i;

    if(!ConfigValueStr(NULL, name, &str) && !ConfigValueStr(NULL, "layout", &str))
        return;

    layout_str = strdup(str);
    next = confkey = layout_str;
    while(next && *next)
    {
        confkey = next;
        next = strchr(confkey, ',');
        if(next)
        {
            *next = 0;
            do {
                next++;
            } while(isspace(*next) || *next == ',');
        }

        sep = strchr(confkey, '=');
        if(!sep || confkey == sep)
        {
            ERR("Malformed speaker key: %s\n", confkey);
            continue;
        }

        end = sep - 1;
        while(isspace(*end) && end != confkey)
            end--;
        *(++end) = 0;

        if(strcmp(confkey, "fl") == 0 || strcmp(confkey, "front-left") == 0)
            val = FrontLeft;
        else if(strcmp(confkey, "fr") == 0 || strcmp(confkey, "front-right") == 0)
            val = FrontRight;
        else if(strcmp(confkey, "fc") == 0 || strcmp(confkey, "front-center") == 0)
            val = FrontCenter;
        else if(strcmp(confkey, "bl") == 0 || strcmp(confkey, "back-left") == 0)
            val = BackLeft;
        else if(strcmp(confkey, "br") == 0 || strcmp(confkey, "back-right") == 0)
            val = BackRight;
        else if(strcmp(confkey, "bc") == 0 || strcmp(confkey, "back-center") == 0)
            val = BackCenter;
        else if(strcmp(confkey, "sl") == 0 || strcmp(confkey, "side-left") == 0)
            val = SideLeft;
        else if(strcmp(confkey, "sr") == 0 || strcmp(confkey, "side-right") == 0)
            val = SideRight;
        else
        {
            ERR("Unknown speaker for %s: \"%s\"\n", name, confkey);
            continue;
        }

        *(sep++) = 0;
        while(isspace(*sep))
            sep++;

        for(i = 0;i < device->NumSpeakers;i++)
        {
            if(device->Speaker[i].ChanName == val)
            {
                long angle = strtol(sep, NULL, 10);
                if(angle >= -180 && angle <= 180)
                    device->Speaker[i].Angle = DEG2RAD(angle);
                else
                    ERR("Invalid angle for speaker \"%s\": %ld\n", confkey, angle);
                break;
            }
        }
    }
    free(layout_str);
    layout_str = NULL;

    for(i = 0;i < device->NumSpeakers;i++)
    {
        ALuint min = i;
        ALuint i2;

        for(i2 = i+1;i2 < device->NumSpeakers;i2++)
        {
            if(device->Speaker[i2].Angle < device->Speaker[min].Angle)
                min = i2;
        }

        if(min != i)
        {
            ALfloat tmpf;
            enum Channel tmpc;

            tmpf = device->Speaker[i].Angle;
            device->Speaker[i].Angle = device->Speaker[min].Angle;
            device->Speaker[min].Angle = tmpf;

            tmpc = device->Speaker[i].ChanName;
            device->Speaker[i].ChanName = device->Speaker[min].ChanName;
            device->Speaker[min].ChanName = tmpc;
        }
    }
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

    /* Some easy special-cases first... */
    if(device->NumSpeakers <= 1 || hwidth >= F_PI)
    {
        /* Full coverage for all speakers. */
        for(i = 0;i < MaxChannels;i++)
            gains[i] = 0.0f;
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
        for(i = 0;i < MaxChannels;i++)
            gains[i] = 0.0f;
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


ALvoid aluInitPanning(ALCdevice *device)
{
    const char *layoutname = NULL;

    switch(device->FmtChans)
    {
        case DevFmtMono:
            device->NumSpeakers = 1;
            device->Speaker[0].ChanName = FrontCenter;
            device->Speaker[0].Angle = DEG2RAD(0.0f);
            layoutname = NULL;
            break;

        case DevFmtStereo:
            device->NumSpeakers = 2;
            device->Speaker[0].ChanName = FrontLeft;
            device->Speaker[1].ChanName = FrontRight;
            device->Speaker[0].Angle = DEG2RAD(-90.0f);
            device->Speaker[1].Angle = DEG2RAD( 90.0f);
            layoutname = "layout_stereo";
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
            layoutname = "layout_quad";
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
            layoutname = "layout_surround51";
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
            layoutname = "layout_side51";
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
            layoutname = "layout_surround61";
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
            layoutname = "layout_surround71";
            break;
    }
    if(layoutname && device->Type != Loopback)
        SetSpeakerArrangement(layoutname, device);
}
