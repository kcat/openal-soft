/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
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

#include <stdlib.h>

#include "alMain.h"
#include "alThunk.h"


static ALboolean  *ThunkArray;
static ALuint      ThunkArraySize;

static CRITICAL_SECTION ThunkLock;

void ThunkInit(void)
{
    InitializeCriticalSection(&ThunkLock);
    ThunkArraySize = 1;
    ThunkArray = calloc(1, ThunkArraySize * sizeof(*ThunkArray));
}

void ThunkExit(void)
{
    free(ThunkArray);
    ThunkArray = NULL;
    ThunkArraySize = 0;
    DeleteCriticalSection(&ThunkLock);
}

ALenum NewThunkEntry(ALuint *index)
{
    ALuint i;

    EnterCriticalSection(&ThunkLock);

    for(i = 0;i < ThunkArraySize;i++)
    {
        if(ThunkArray[i] == AL_FALSE)
            break;
    }

    if(i == ThunkArraySize)
    {
        ALboolean *NewList;

        NewList = realloc(ThunkArray, ThunkArraySize*2 * sizeof(*ThunkArray));
        if(!NewList)
        {
            LeaveCriticalSection(&ThunkLock);
            ERR("Realloc failed to increase to %u enties!\n", ThunkArraySize*2);
            return AL_OUT_OF_MEMORY;
        }
        memset(&NewList[ThunkArraySize], 0, ThunkArraySize*sizeof(*ThunkArray));
        ThunkArraySize *= 2;
        ThunkArray = NewList;
    }

    ThunkArray[i] = AL_TRUE;
    *index = i+1;

    LeaveCriticalSection(&ThunkLock);

    return AL_NO_ERROR;
}

void FreeThunkEntry(ALuint index)
{
    EnterCriticalSection(&ThunkLock);

    if(index > 0 && index <= ThunkArraySize)
        ThunkArray[index-1] = AL_FALSE;

    LeaveCriticalSection(&ThunkLock);
}
