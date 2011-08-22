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


static ALboolean  *g_ThunkArray;
static ALuint      g_ThunkArraySize;

static CRITICAL_SECTION g_ThunkLock;

void ThunkInit(void)
{
    InitializeCriticalSection(&g_ThunkLock);
    g_ThunkArraySize = 1;
    g_ThunkArray = calloc(1, g_ThunkArraySize * sizeof(*g_ThunkArray));
}

void ThunkExit(void)
{
    free(g_ThunkArray);
    g_ThunkArray = NULL;
    g_ThunkArraySize = 0;
    DeleteCriticalSection(&g_ThunkLock);
}

ALenum NewThunkEntry(ALuint *index)
{
    ALuint i;

    EnterCriticalSection(&g_ThunkLock);

    for(i = 0;i < g_ThunkArraySize;i++)
    {
        if(g_ThunkArray[i] == AL_FALSE)
            break;
    }

    if(i == g_ThunkArraySize)
    {
        ALboolean *NewList;

        NewList = realloc(g_ThunkArray, g_ThunkArraySize*2 * sizeof(*g_ThunkArray));
        if(!NewList)
        {
            LeaveCriticalSection(&g_ThunkLock);
            ERR("Realloc failed to increase to %u enties!\n", g_ThunkArraySize*2);
            return AL_OUT_OF_MEMORY;
        }
        memset(&NewList[g_ThunkArraySize], 0, g_ThunkArraySize*sizeof(*g_ThunkArray));
        g_ThunkArraySize *= 2;
        g_ThunkArray = NewList;
    }

    g_ThunkArray[i] = AL_TRUE;
    *index = i+1;

    LeaveCriticalSection(&g_ThunkLock);

    return AL_NO_ERROR;
}

void FreeThunkEntry(ALuint index)
{
    EnterCriticalSection(&g_ThunkLock);

    if(index > 0 && index <= g_ThunkArraySize)
        g_ThunkArray[index-1] = AL_FALSE;

    LeaveCriticalSection(&g_ThunkLock);
}
