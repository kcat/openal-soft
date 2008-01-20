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

#include "alMain.h"
#include "alBuffer.h"
#include "alFilter.h"
#include "alEffect.h"
#include "alAuxEffectSlot.h"
#include "alThunk.h"

CRITICAL_SECTION _alMutex;

#ifdef _WIN32
BOOL APIENTRY DllMain(HANDLE hModule,DWORD ul_reason_for_call,LPVOID lpReserved)
{
    (void)lpReserved;

    // Perform actions based on the reason for calling.
    switch(ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            break;

        case DLL_PROCESS_DETACH:
            ReleaseALC();
            ReleaseALBuffers();
            ReleaseALEffects();
            ReleaseALFilters();
            FreeALConfig();
            ALTHUNK_EXIT();
            DeleteCriticalSection(&_alMutex);
            break;
    }
    return TRUE;
}
#else
#ifdef HAVE_GCC_DESTRUCTOR
static void my_deinit() __attribute__((destructor));
static void my_deinit()
{
    static ALenum once = AL_FALSE;
    if(once) return;
    once = AL_TRUE;

    ReleaseALC();
    ReleaseALBuffers();
    ReleaseALEffects();
    ReleaseALFilters();
    FreeALConfig();
    ALTHUNK_EXIT();
    DeleteCriticalSection(&_alMutex);
}
#endif
#endif
