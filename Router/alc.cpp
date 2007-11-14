/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2000 by authors.
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

#ifndef __MINGW32__
#define _CRT_SECURE_NO_DEPRECATE // get rid of sprintf security warnings on VS2005
#endif

#include <stdlib.h>
#include <memory.h>
#define AL_BUILD_LIBRARY
#include <al/alc.h>
#include <stdio.h>
#include <tchar.h>
#include <assert.h>

#include <stddef.h>
#include <windows.h>
#if defined(_MSC_VER)
#include <crtdbg.h>
#else
#define _malloc_dbg(s,x,f,l)     malloc(s)
#define _realloc_dbg(p,s,x,f,l)  realloc(p,s)
#endif
#include <objbase.h>
#ifndef __MINGW32__
#include <atlconv.h>
#else
#define T2A(x) x
#endif
#include <mmsystem.h>

#include "OpenAL32.h"


//*****************************************************************************
//*****************************************************************************
//
// Defines
//
//*****************************************************************************
//*****************************************************************************

typedef struct ALCextension_struct
{

    const char*		ename;

} ALCextension;

typedef struct
{
    const char*		ename;
    ALenum			value;

} ALCRouterEnum;

typedef struct ALCfunction_struct
{

    const char*		fname;
    ALvoid*			address;

} ALCfunction;



//*****************************************************************************
//*****************************************************************************
//
// Global Vars
//
//*****************************************************************************
//*****************************************************************************

ALlist* alContextList = 0;
ALCcontext* alCurrentContext = 0;

ALCdevice* g_CaptureDevice = NULL;

//*****************************************************************************
//*****************************************************************************
//
// Local Vars
//
//*****************************************************************************
//*****************************************************************************

//
// The values of the enums supported by OpenAL.
//
static ALCRouterEnum alcEnums[] =
{
    // Types
    {"ALC_INVALID",                     ALC_INVALID},
	{"ALC_FALSE",                       ALC_FALSE},
	{"ALC_TRUE",                        ALC_TRUE},

    // ALC Properties
    {"ALC_MAJOR_VERSION",               ALC_MAJOR_VERSION},
    {"ALC_MINOR_VERSION",               ALC_MINOR_VERSION},
    {"ALC_ATTRIBUTES_SIZE",             ALC_ATTRIBUTES_SIZE},
    {"ALC_ALL_ATTRIBUTES",              ALC_ALL_ATTRIBUTES},
    {"ALC_DEFAULT_DEVICE_SPECIFIER",    ALC_DEFAULT_DEVICE_SPECIFIER},
    {"ALC_DEVICE_SPECIFIER",            ALC_DEVICE_SPECIFIER},
    {"ALC_EXTENSIONS",                  ALC_EXTENSIONS},
    {"ALC_FREQUENCY",                   ALC_FREQUENCY},
    {"ALC_REFRESH",                     ALC_REFRESH},
    {"ALC_SYNC",                        ALC_SYNC},
	{"ALC_MONO_SOURCES",                ALC_MONO_SOURCES},
	{"ALC_STEREO_SOURCES",              ALC_STEREO_SOURCES},
	{"ALC_CAPTURE_DEVICE_SPECIFIER",    ALC_CAPTURE_DEVICE_SPECIFIER},
	{"ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER", ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER},
	{"ALC_CAPTURE_SAMPLES",             ALC_CAPTURE_SAMPLES},

	// New Enumeration extension
	{"ALC_DEFAULT_ALL_DEVICES_SPECIFIER",	ALC_DEFAULT_ALL_DEVICES_SPECIFIER},
	{"ALC_ALL_DEVICES_SPECIFIER",			ALC_ALL_DEVICES_SPECIFIER},

    // ALC Error Message
    {"ALC_NO_ERROR",                    ALC_NO_ERROR},
    {"ALC_INVALID_DEVICE",              ALC_INVALID_DEVICE},
    {"ALC_INVALID_CONTEXT",             ALC_INVALID_CONTEXT},
    {"ALC_INVALID_ENUM",                ALC_INVALID_ENUM},
    {"ALC_INVALID_VALUE",               ALC_INVALID_VALUE},
    {"ALC_OUT_OF_MEMORY",               ALC_OUT_OF_MEMORY},

    // Default
    {0,                                 (ALenum)0}
};

//
// Our function pointers.
//
static ALCfunction alcFunctions[] =
{
    {"alcCreateContext",                (ALvoid*)alcCreateContext},
	{"alcMakeContextCurrent",           (ALvoid*)alcMakeContextCurrent},
	{"alcProcessContext",               (ALvoid*)alcProcessContext},
	{"alcSuspendContext",               (ALvoid*)alcSuspendContext},
    {"alcDestroyContext",               (ALvoid*)alcDestroyContext},
	{"alcGetCurrentContext",            (ALvoid*)alcGetCurrentContext},
    {"alcGetContextsDevice",            (ALvoid*)alcGetContextsDevice},
    {"alcOpenDevice",                   (ALvoid*)alcOpenDevice},
	{"alcCloseDevice",                  (ALvoid*)alcCloseDevice},
	{"alcGetError",                     (ALvoid*)alcGetError},
	{"alcIsExtensionPresent",           (ALvoid*)alcIsExtensionPresent},
	{"alcGetProcAddress",               (ALvoid*)alcGetProcAddress},
	{"alcGetEnumValue",                 (ALvoid*)alcGetEnumValue},
    {"alcGetString",                    (ALvoid*)alcGetString},
    {"alcGetIntegerv",                  (ALvoid*)alcGetIntegerv},
	{"alcCaptureOpenDevice",            (ALvoid*)alcCaptureOpenDevice},
	{"alcCaptureCloseDevice",           (ALvoid*)alcCaptureCloseDevice},
	{"alcCaptureStart",                 (ALvoid*)alcCaptureStart},
	{"alcCaptureStop",                  (ALvoid*)alcCaptureStop},
	{"alcCaptureSamples",               (ALvoid*)alcCaptureSamples},
	{0,                                 (ALvoid*)0}
};

//
// Our extensions.
//
static ALCextension alcExtensions[] =
{
    "ALC_ENUMERATION_EXT",
	"ALC_ENUMERATE_ALL_EXT",
	"ALC_EXT_CAPTURE",
	0
};


// Error strings
static ALenum  LastError = ALC_NO_ERROR;
static const ALCchar alcNoError[] = "No Error";
static const ALCchar alcErrInvalidDevice[] = "Invalid Device";
static const ALCchar alcErrInvalidContext[] = "Invalid Context";
static const ALCchar alcErrInvalidEnum[] = "Invalid Enum";
static const ALCchar alcErrInvalidValue[] = "Invalid Value";

static ALint alcMajorVersion = 1;
static ALint alcMinorVersion = 1;

// Enumeration stuff
ALDEVICE *g_pDeviceList = NULL;				// ALC_ENUMERATION_EXT Device List
ALDEVICE *g_pCaptureDeviceList = NULL;		// ALC_ENUMERATION_EXT Capture Device List
ALDEVICE *g_pAllDevicesList = NULL;			// ALC_ENUMERATE_ALL_EXT Device List

ALchar *pszDefaultDeviceSpecifier = NULL;
ALchar *pszDeviceSpecifierList = NULL;
ALchar *pszDefaultCaptureDeviceSpecifier = NULL;
ALchar *pszCaptureDeviceSpecifierList = NULL;
ALchar *pszDefaultAllDevicesSpecifier = NULL;
ALchar *pszAllDevicesSpecifierList = NULL;
ALchar szEmptyString[] = "";

typedef BOOL (CALLBACK *LPDSENUMCALLBACKA)(LPGUID, LPCSTR, LPCSTR, LPVOID);
typedef HRESULT (WINAPI *LPDIRECTSOUNDENUMERATEA)(LPDSENUMCALLBACKA pDSEnumCallback, LPVOID pContext);
typedef HRESULT (WINAPI *LPDIRECTSOUNDCAPTUREENUMERATEA)(LPDSENUMCALLBACKA pDSEnumCallback, LPVOID pContext);

BOOL CALLBACK DSEnumCallback(LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext);
bool GetDefaultPlaybackDeviceName(char **pszName);
bool GetDefaultCaptureDeviceName(char **pszName);
bool FindDevice(ALDEVICE *pDeviceList, char *szDeviceName, bool bExactMatch, char **ppszDefaultName);
bool HasDLLAlreadyBeenUsed(ALDEVICE *pDeviceList, TCHAR *szDLLName);
//bool ValidCaptureDevice(const char *szCaptureDeviceName);

//*****************************************************************************
//*****************************************************************************
//
// Logging Options
//
//*****************************************************************************
//*****************************************************************************

// NOTE : LOG macro below requires a compiler newer than Visual Studio 6

//#define _LOGCALLS

#ifdef _LOGCALLS
 void OutputMessage(const char *szTest,...); 
 #define LOG(x, ...) OutputMessage(x, ##__VA_ARGS__)
 #define LOGFILENAME	"OpenALCalls.txt"
#endif

//*****************************************************************************
//*****************************************************************************
//
// Local Functions
//
//*****************************************************************************
//*****************************************************************************

//*****************************************************************************
// GetLoadedModuleDirectory
//*****************************************************************************
BOOL GetLoadedModuleDirectory(LPCTSTR moduleName,
                              LPTSTR  directoryContainingModule,
                              DWORD   directoryContainingModuleLength) {
    // Attempts to find the given module in the address space of this
    // process and return the directory containing the module. A NULL
    // moduleName means to look up the directory containing the
    // application rather than any given loaded module. There is no
    // trailing backslash ('\') on the returned path. If the named
    // module was found in the address space of this process, returns
    // TRUE, otherwise returns FALSE. directoryContainingModule may be
    // mutated regardless.
    HMODULE module = NULL;
    TCHAR fileDrive[MAX_PATH + 1];
    TCHAR fileDir[MAX_PATH + 1];
    TCHAR fileName[MAX_PATH + 1];
    TCHAR fileExt[MAX_PATH + 1];
    DWORD numChars;

    if (moduleName != NULL) {
        module = GetModuleHandle(moduleName);
        if (module == NULL)
            return FALSE;
    }

    numChars = GetModuleFileName(module,
                                 directoryContainingModule,
                                 directoryContainingModuleLength);
    if (numChars == 0)
        return FALSE;
    
    _splitpath(directoryContainingModule, fileDrive, fileDir, fileName, fileExt);
    _tcscpy(directoryContainingModule, fileDrive);
    _tcscat(directoryContainingModule, fileDir);
    return TRUE;
}




//*****************************************************************************
// AddDevice
//*****************************************************************************
void AddDevice(const char *pszDeviceName, TCHAR *pszHostDLLFilename, ALDEVICE **ppDeviceList)
{
	// Adds pszDeviceName nad pszHostDLLFilename to the given Device List *IF* pszDeviceName has
	// not already been added.
	ALDEVICE *pNewDevice, *pTempDevice;

	// Check if unique
	for (pTempDevice = *ppDeviceList; pTempDevice; pTempDevice = pTempDevice->pNextDevice)
	{
		if (strcmp(pTempDevice->pszDeviceName, pszDeviceName) == 0)
			break;
	}

	if (pTempDevice)
		return;

	pNewDevice = (ALDEVICE*)malloc(sizeof(ALDEVICE));
	if (pNewDevice)
	{
		pNewDevice->pszDeviceName = (char*)malloc((strlen(pszDeviceName)+1)*sizeof(char));
		if (pNewDevice->pszDeviceName)
			strcpy(pNewDevice->pszDeviceName, pszDeviceName);

		pNewDevice->pszHostDLLFilename = (TCHAR*)malloc((_tcslen(pszHostDLLFilename)+1)*sizeof(TCHAR));
		if (pNewDevice->pszHostDLLFilename)
			_tcscpy(pNewDevice->pszHostDLLFilename, pszHostDLLFilename);

		pNewDevice->pNextDevice = NULL;

		if (*ppDeviceList)
		{
			pTempDevice = *ppDeviceList;
			while (pTempDevice->pNextDevice)
				pTempDevice = pTempDevice->pNextDevice;
			pTempDevice->pNextDevice = pNewDevice;
		}
		else
		{
			*ppDeviceList = pNewDevice;
		}
	}
}




//*****************************************************************************
// BuildDeviceList
//*****************************************************************************
ALvoid BuildDeviceList()
{
	// This function will scan several directories (details below) looking for
	// OpenAL DLLs.  Each OpenAL DLL found will be opened and queried for it's
	// list of playback and capture devices.   All the information is stored
	// in various lists: -
	//
	// g_pDevicesList		:	List of Playback Devices
	// g_pCaptureDeviceList	:	List of Capture devices
	// g_pAllDevicesList	:	List of *all* possible Playback devices (ALC_ENUMERATE_ALL_EXT support)
	//
	// In addition this function allocates memory for the strings that will
	// be returned to the application in response to alcGetString queries.
	//
	// pszDefaultDeviceSpecifier		:	Default Playback Device
	// pszDeviceSpecifierList			:	List of Playback Devices
	// pszDefaultCaptureDeviceSpecifier	:	Default Capture Device
	// pszCaptureDeviceSpecifierList	:	List of Capture Devices
	// pszDefaultAllDevicesSpecifier	:	Default *all* Playback Device (ALC_ENUMERATE_ALL_EXT support)
	// pszAllDevicesSpecifierList		:	List of *all* Playback Devices (ALC_ENUMERATE_ALL_EXT support)
    WIN32_FIND_DATA findData;
    HANDLE searchHandle = INVALID_HANDLE_VALUE;
    TCHAR searchName[MAX_PATH + 1];
    BOOL found = FALSE;
    const ALCchar* specifier = 0;
    ALuint specifierSize = 0;
	ALCdevice *device;
	void *context;
	bool bUsedWrapper = false;
	ALDEVICE *pDevice = NULL;

	// Only build the list once ...
	if (((g_pDeviceList == NULL) && (waveOutGetNumDevs())) ||
		((g_pCaptureDeviceList == NULL) && (waveInGetNumDevs())))
	{
		//
		// Directory[0] is the directory containing OpenAL32.dll
		// Directory[1] is the current directory.
		// Directory[2] is the current app directory
		// Directory[3] is the system directory
		//
		TCHAR dir[4][MAX_PATH + 1] = { 0 };
        int numDirs = 0;
		int i;
		HINSTANCE dll = 0;
		ALCAPI_GET_STRING alcGetStringFxn = 0;
		ALCAPI_IS_EXTENSION_PRESENT alcIsExtensionPresentFxn = 0;
		ALCAPI_OPEN_DEVICE alcOpenDeviceFxn = 0;
		ALCAPI_CREATE_CONTEXT alcCreateContextFxn = 0;
		ALCAPI_MAKE_CONTEXT_CURRENT alcMakeContextCurrentFxn = 0;
		ALCAPI_DESTROY_CONTEXT alcDestroyContextFxn = 0;
		ALCAPI_CLOSE_DEVICE alcCloseDeviceFxn = 0;

		//
		// Construct our search paths
		//
		if (GetLoadedModuleDirectory("OpenAL32.dll", dir[0], MAX_PATH)) {
            ++numDirs;
        }

		GetCurrentDirectory(MAX_PATH, dir[1]);
		_tcscat(dir[1], _T("\\"));
		++numDirs;

		GetLoadedModuleDirectory(NULL, dir[2], MAX_PATH);
		++numDirs;

		GetSystemDirectory(dir[3], MAX_PATH);
		_tcscat(dir[3], _T("\\"));
		++numDirs;

		//
		// Begin searching for additional OpenAL implementations.
		//
		for(i = 0; i < numDirs; i++)
		{
			if ((i == 0) && (strcmp(dir[0], dir[3]) == 0))	// if searching router dir and router dir is sys dir, skip search
				continue;

			if ((i == 2) && (strcmp(dir[2], dir[1]) == 0))	// if searching app dir and app dir is current dir, skip search
				continue;

			if ((i == 3) && ((strcmp(dir[3], dir[2]) == 0) || (strcmp(dir[3], dir[1]) == 0)))	// if searching sys dir and sys dir is either current or app directory, skip search
				continue;

			_tcscpy(searchName, dir[i]);
			_tcscat(searchName, _T("*oal.dll"));
			searchHandle = FindFirstFile(searchName, &findData);
			if(searchHandle != INVALID_HANDLE_VALUE)
			{
				while(TRUE)
				{
					//
					// if this is an OpenAL32.dll, skip it -- it's probably a router and shouldn't be enumerated regardless
					//
					_tcscpy(searchName, dir[i]);
					_tcscat(searchName, findData.cFileName);
					TCHAR cmpName[MAX_PATH];
					_tcscpy(cmpName, searchName);
					_tcsupr(cmpName);
					if (_tcsstr(cmpName, _T("OPENAL32.DLL")) == 0)
					{
						boolean skipSearch = false;

						// don't search the same DLL twice
						TCHAR *szDLLName = _tcsrchr(searchName, _T('\\'));
						if (szDLLName)
							szDLLName++;	// Skip over the '\'
						else
							szDLLName = searchName;

						skipSearch = HasDLLAlreadyBeenUsed(g_pDeviceList, szDLLName);
						if (!skipSearch)
							skipSearch = HasDLLAlreadyBeenUsed(g_pCaptureDeviceList, szDLLName);
						if (!skipSearch)
							skipSearch = HasDLLAlreadyBeenUsed(g_pAllDevicesList, szDLLName);

						if (skipSearch == false) {
							dll = LoadLibrary(searchName);
							if(dll)
							{
								alcOpenDeviceFxn = (ALCAPI_OPEN_DEVICE)GetProcAddress(dll, "alcOpenDevice");
								alcCreateContextFxn = (ALCAPI_CREATE_CONTEXT)GetProcAddress(dll, "alcCreateContext");
								alcMakeContextCurrentFxn = (ALCAPI_MAKE_CONTEXT_CURRENT)GetProcAddress(dll, "alcMakeContextCurrent");
								alcGetStringFxn = (ALCAPI_GET_STRING)GetProcAddress(dll, "alcGetString");
								alcDestroyContextFxn = (ALCAPI_DESTROY_CONTEXT)GetProcAddress(dll, "alcDestroyContext");
								alcCloseDeviceFxn = (ALCAPI_CLOSE_DEVICE)GetProcAddress(dll, "alcCloseDevice");
								alcIsExtensionPresentFxn = (ALCAPI_IS_EXTENSION_PRESENT)GetProcAddress(dll, "alcIsExtensionPresent");

								if ((alcOpenDeviceFxn != 0) &&
									(alcCreateContextFxn != 0) &&
									(alcMakeContextCurrentFxn != 0) &&
									(alcGetStringFxn != 0) &&
									(alcDestroyContextFxn != 0) &&
									(alcCloseDeviceFxn != 0) &&
									(alcIsExtensionPresentFxn != 0)) {

									bool bAddToAllDevicesList = false;

									if (alcIsExtensionPresentFxn(NULL, "ALC_ENUMERATE_ALL_EXT")) {
										// this DLL can enumerate *all* devices -- so add complete list of devices
										specifier = alcGetStringFxn(0, ALC_ALL_DEVICES_SPECIFIER);
										if ((specifier) && strlen(specifier))
										{
											do {
												AddDevice(specifier, searchName, &g_pAllDevicesList);
												specifier += strlen((char *)specifier) + 1;
											} while (strlen((char *)specifier) > 0);
										}
									} else {
										bAddToAllDevicesList = true;
									}

									if (alcIsExtensionPresentFxn(NULL, "ALC_ENUMERATION_EXT")) {
										// this DLL can enumerate devices -- so add complete list of devices
										specifier = alcGetStringFxn(0, ALC_DEVICE_SPECIFIER);
										if ((specifier) && strlen(specifier))
										{
											do {
												AddDevice(specifier, searchName, &g_pDeviceList);
												if (bAddToAllDevicesList)
													AddDevice(specifier, searchName, &g_pAllDevicesList);
												specifier += strlen((char *)specifier) + 1;
											} while (strlen((char *)specifier) > 0);
										}
									} else {
										// no enumeration ability, -- so just add default device to the list
										device = alcOpenDeviceFxn(NULL);
										if (device != NULL) {
											context = alcCreateContextFxn(device, NULL);
											alcMakeContextCurrentFxn((ALCcontext *)context);
											if (context != NULL) {
												specifier = alcGetStringFxn(device, ALC_DEVICE_SPECIFIER);
												if ((specifier) && strlen(specifier))
												{
													AddDevice(specifier, searchName, &g_pDeviceList);
													if (bAddToAllDevicesList)
														AddDevice(specifier, searchName, &g_pAllDevicesList);
												}
												alcMakeContextCurrentFxn((ALCcontext *)NULL);
												alcDestroyContextFxn((ALCcontext *)context);
												alcCloseDeviceFxn(device);
											}
										}
									}

									// add to capture device list
									if (_tcsstr(cmpName, _T("CT_OAL.DLL")) == 0) {
										// Skip native AL component (will contain same Capture List as the wrap_oal component)
										if (alcIsExtensionPresentFxn(NULL, "ALC_EXT_CAPTURE")) {
											// this DLL supports capture -- so add complete list of capture devices
											specifier = alcGetStringFxn(0, ALC_CAPTURE_DEVICE_SPECIFIER);
											if ((specifier) && strlen(specifier))
											{
												do {
													AddDevice(specifier, searchName, &g_pCaptureDeviceList);
													specifier += strlen((char *)specifier) + 1;
												} while (strlen((char *)specifier) > 0);
											}
										}
									}
								}

								FreeLibrary(dll);
								dll = 0;
							}
						}
					}

					if(!FindNextFile(searchHandle, &findData))
					{
						if(GetLastError() == ERROR_NO_MORE_FILES)
						{
							break;
						}
					}
				}

				FindClose(searchHandle);
				searchHandle = INVALID_HANDLE_VALUE;
			}
		}

		// We now have a list of all the Device Names and their associated DLLs.
		// Put the names in the appropriate strings
		ALuint uiLength;
		ALchar *pszTemp;
		char *pszDefaultName = NULL;
		bool bFound = false;

		if (g_pDeviceList)
		{
			uiLength = 0;
			for (pDevice = g_pDeviceList; pDevice; pDevice = pDevice->pNextDevice)
				uiLength += (strlen(pDevice->pszDeviceName) + 1);

			pszDeviceSpecifierList = (ALchar*)malloc((uiLength + 1) * sizeof(ALchar));
			if (pszTemp = pszDeviceSpecifierList)
			{
				memset(pszDeviceSpecifierList, 0, (uiLength + 1) * sizeof(ALchar));
				for (pDevice = g_pDeviceList; pDevice; pDevice = pDevice->pNextDevice)
				{
					strcpy(pszTemp, pDevice->pszDeviceName);
					pszTemp += (strlen(pDevice->pszDeviceName) + 1);
				}
			}

			// Determine what the Default Device should be
			if (GetDefaultPlaybackDeviceName(&pszDefaultName))
			{
				bFound = false;

				// Search for an exact match first
				bFound = FindDevice(g_pDeviceList, pszDefaultName, true, &pszDefaultDeviceSpecifier);

				// If we haven't found a match ... search for a partial match if name contains 'X-Fi'
				if ((!bFound) && (strstr(pszDefaultName, "X-Fi")))
					bFound = FindDevice(g_pDeviceList, "X-Fi", false, &pszDefaultDeviceSpecifier);
				
				// If we haven't found a match ... search for a partial match if name contains 'Audigy'
				if ((!bFound) && (strstr(pszDefaultName, "Audigy")))
					bFound = FindDevice(g_pDeviceList, "Audigy", false, &pszDefaultDeviceSpecifier);

				// If we haven't found a match ... search for a partial match with 'Generic Hardware'
				if (!bFound)
					bFound = FindDevice(g_pDeviceList, "Generic Hardware", false, &pszDefaultDeviceSpecifier);

				// If we haven't found a match ... search for a partial match with 'Generic Software'
				if (!bFound)
					bFound = FindDevice(g_pDeviceList, "Generic Software", false, &pszDefaultDeviceSpecifier);

				// If we STILL haven't found a match ... pick the 1st device!
				if (!bFound)
				{
					pszDefaultDeviceSpecifier = (char*)malloc((strlen(g_pDeviceList->pszDeviceName) + 1) * sizeof(char));
					if (pszDefaultDeviceSpecifier)
						strcpy(pszDefaultDeviceSpecifier, g_pDeviceList->pszDeviceName);
				}
				
				free(pszDefaultName);
				pszDefaultName = NULL;
			}
		}

		if (g_pCaptureDeviceList)
		{
			uiLength = 0;
			for (pDevice = g_pCaptureDeviceList; pDevice; pDevice = pDevice->pNextDevice)
				uiLength += (strlen(pDevice->pszDeviceName) + 1);

			pszCaptureDeviceSpecifierList = (ALchar*)malloc((uiLength + 1) * sizeof(ALchar));
			if (pszTemp = pszCaptureDeviceSpecifierList)
			{
				memset(pszCaptureDeviceSpecifierList, 0, (uiLength + 1) * sizeof(ALchar));
				for (pDevice = g_pCaptureDeviceList; pDevice; pDevice = pDevice->pNextDevice)
				{
					strcpy(pszTemp, pDevice->pszDeviceName);
					pszTemp += (strlen(pDevice->pszDeviceName) + 1);
				}
			}

			if (GetDefaultCaptureDeviceName(&pszDefaultName))
			{
				bFound = false;

				// Search for an exact match first
				bFound = FindDevice(g_pCaptureDeviceList, pszDefaultName, true, &pszDefaultCaptureDeviceSpecifier);

				// If we haven't found a match, truncate the default name to 32 characters (MMSYSTEM limitation)
				if ((!bFound) && (strlen(pszDefaultName) > 31))
				{
					pszDefaultName[31] = '\0';
					bFound = FindDevice(g_pCaptureDeviceList, pszDefaultName, true, &pszDefaultCaptureDeviceSpecifier);
				}

				// If we haven't found a match ... pick the 1st device!
				if (!bFound)
				{
					pszDefaultCaptureDeviceSpecifier = (char*)malloc((strlen(g_pCaptureDeviceList->pszDeviceName) + 1) * sizeof(char));
					if (pszDefaultCaptureDeviceSpecifier)
						strcpy(pszDefaultCaptureDeviceSpecifier, g_pCaptureDeviceList->pszDeviceName);
				}

				free(pszDefaultName);
				pszDefaultName = NULL;
			}
		}

		if (g_pAllDevicesList)
		{
			uiLength = 0;
			for (pDevice = g_pAllDevicesList; pDevice; pDevice = pDevice->pNextDevice)
				uiLength += (strlen(pDevice->pszDeviceName) + 1);

			pszAllDevicesSpecifierList = (ALchar*)malloc((uiLength + 1) * sizeof(ALchar));
			if (pszTemp = pszAllDevicesSpecifierList)
			{
				memset(pszAllDevicesSpecifierList, 0, (uiLength + 1) * sizeof(ALchar));
				for (pDevice = g_pAllDevicesList; pDevice; pDevice = pDevice->pNextDevice)
				{
					strcpy(pszTemp, pDevice->pszDeviceName);
					pszTemp += (strlen(pDevice->pszDeviceName) + 1);
				}
			}

			// Determine what the Default Device should be
			if (GetDefaultPlaybackDeviceName(&pszDefaultName))
			{
				bFound = false;

				// If the (regular) default Playback device exists in this list ... use that
				bFound = FindDevice(g_pAllDevicesList, pszDefaultDeviceSpecifier, true, &pszDefaultAllDevicesSpecifier);

				// If we haven't found a match ... pick a partial match with the Default Device Name
				if (!bFound)
					bFound = FindDevice(g_pAllDevicesList, pszDefaultName, false, &pszDefaultAllDevicesSpecifier);

				// If we STILL haven't found a match ... pick the 1st device!
				if (!bFound)
				{
					pszDefaultAllDevicesSpecifier = (char*)malloc((strlen(g_pAllDevicesList->pszDeviceName) + 1) * sizeof(char));
					if (pszDefaultAllDevicesSpecifier)
						strcpy(pszDefaultAllDevicesSpecifier, g_pAllDevicesList->pszDeviceName);
				}
				
				free(pszDefaultName);
				pszDefaultName = NULL;
			}
		}
	}

    return;
}




//*****************************************************************************
// HasDLLAlreadyBeenUsed
//*****************************************************************************
bool HasDLLAlreadyBeenUsed(ALDEVICE *pDeviceList, TCHAR *szDLLName)
{
	// Checks if an OpenAL DLL has already been enumerated
	ALDEVICE *pDevice = NULL;
	TCHAR *szHostDLLName;
	bool bReturn = false;

	for (pDevice = pDeviceList; pDevice; pDevice = pDevice->pNextDevice)
	{
		szHostDLLName = _tcsrchr(pDevice->pszHostDLLFilename, _T('\\'));
		if (szHostDLLName)
			szHostDLLName++;	// Skip over the '\'
		else
			szHostDLLName = pDevice->pszHostDLLFilename;

		if (_tcscmp(szHostDLLName, szDLLName) == 0)
		{
			bReturn = true;
			break;
		}
	}

	return bReturn;
}




//*****************************************************************************
// ValidCaptureDevice
//*****************************************************************************
/*
bool ValidCaptureDevice(const char *szCaptureDeviceName)
{
	// Microsoft changed the behaviour of Input devices on Windows Vista such that *each* input
	// on each soundcard is reported as a separate device.   Unfortunately, even though you can
	// enumerate each input there are restrictions on what devices can be opened (e.g. you can only
	// open the soundcard's default input).   There is no API call to change the default input, so
	// there is little point enumerating input devices that cannot be used, so we filter them out here.
	WAVEFORMATEX wfex = { WAVE_FORMAT_PCM, 1, 22050, 44100, 2, 16, 0 };	// 16bit Mono 22050Hz
	WAVEINCAPS WaveInCaps;
	HWAVEIN	hWaveIn;
	bool bValid = false;

	// Find the device ID from the device name
	long lNumCaptureDevs = waveInGetNumDevs();
	long lDeviceID = -1;
	for (long lLoop = 0; lLoop < lNumCaptureDevs; lLoop++)
	{
		if (waveInGetDevCaps(lLoop, &WaveInCaps, sizeof(WAVEINCAPS)) == MMSYSERR_NOERROR)
		{
			if (!strcmp(szCaptureDeviceName, WaveInCaps.szPname))
			{
				lDeviceID = lLoop;
				break;
			}
		}
	}

	if (lDeviceID != -1)
	{
		if (waveInOpen(&hWaveIn, lDeviceID, &wfex, NULL, NULL, WAVE_MAPPED) == MMSYSERR_NOERROR)
		{
			waveInClose(hWaveIn);
			bValid = true;
		}
	}

	return bValid;
}
*/



//*****************************************************************************
// GetDefaultPlaybackDeviceName
//*****************************************************************************
bool GetDefaultPlaybackDeviceName(char **pszName)
{
	// Try to use DirectSound to get the name of the 'Preferred Audio Device / Endpoint"
	// If that fails use MMSYSTEM (name will be limited to 32 characters in length)
	TCHAR szPath[_MAX_PATH];
	HINSTANCE hDSoundDLL;

	if (!pszName)
		return false;

	*pszName = NULL;

	// Load dsound.dll from the System Directory and use the DirectSoundEnumerateA function to
	// get the list of playback devices
	if (GetSystemDirectory(szPath, _MAX_PATH))
	{
		_tcscat(szPath, "\\dsound.dll");
		hDSoundDLL = LoadLibrary(szPath);
		if (hDSoundDLL)
		{
			LPDIRECTSOUNDENUMERATEA pfnDirectSoundEnumerateA = (LPDIRECTSOUNDENUMERATEA)GetProcAddress(hDSoundDLL, "DirectSoundEnumerateA");
			if (pfnDirectSoundEnumerateA)
				pfnDirectSoundEnumerateA(&DSEnumCallback, pszName);
			FreeLibrary(hDSoundDLL);
		}
	}

	// Falling back to MMSYSTEM
	if (*pszName == NULL)
	{
		UINT uDeviceID=0;
		DWORD dwFlags=1;
		WAVEOUTCAPS outputInfo;

		#if !defined(_WIN64)
		#ifdef __GNUC__
		  __asm__ ("pusha;");
        #else
		__asm pusha; // workaround for register destruction caused by these wavOutMessage calls (weird but true)
		#endif
		#endif // !defined(_WIN64)
		waveOutMessage((HWAVEOUT)(UINT_PTR)WAVE_MAPPER,0x2000+0x0015,(LPARAM)&uDeviceID,(WPARAM)&dwFlags);
		waveOutGetDevCaps(uDeviceID,&outputInfo,sizeof(outputInfo));
		#if !defined(_WIN64)
		#ifdef __GNUC__
		  __asm__ ("popa;");
        #else
		__asm popa;
		#endif
		#endif // !defined(_WIN64)

		*pszName = (char*)malloc((strlen(outputInfo.szPname) + 1) * sizeof(char));
		if (*pszName)
			strcpy(*pszName, outputInfo.szPname);
	}

	return (*pszName) ? true : false;
}




//*****************************************************************************
// GetDefaultCaptureDeviceName
//*****************************************************************************
bool GetDefaultCaptureDeviceName(char **pszName)
{
	// Try to use DirectSound to get the name of the 'Preferred Audio Device / Endpoint" for recording.
	// If that fails use MMSYSTEM (name will be limited to 32 characters in length)
	TCHAR szPath[_MAX_PATH];
	HINSTANCE hDSoundDLL;

	if (!pszName)
		return false;

	*pszName = NULL;

	// Load dsound.dll from the System Directory and use the DirectSoundCaptureEnumerateA function to
	// get the list of capture devices
	if (GetSystemDirectory(szPath, _MAX_PATH))
	{
		_tcscat(szPath, "\\dsound.dll");
		hDSoundDLL = LoadLibrary(szPath);
		if (hDSoundDLL)
		{
			LPDIRECTSOUNDCAPTUREENUMERATEA pfnDirectSoundCaptureEnumerateA = (LPDIRECTSOUNDCAPTUREENUMERATEA)GetProcAddress(hDSoundDLL, "DirectSoundCaptureEnumerateA");
			if (pfnDirectSoundCaptureEnumerateA)
				pfnDirectSoundCaptureEnumerateA(&DSEnumCallback, pszName);
			FreeLibrary(hDSoundDLL);
		}
	}

	// Falling back to MMSYSTEM
	if (*pszName == NULL)
	{
		UINT uDeviceID=0;
		DWORD dwFlags=1;
		WAVEINCAPS inputInfo;

		#if !defined(_WIN64)
		#ifdef __GNUC__
		  __asm__ ("pusha;");
        #else
		__asm pusha; // workaround for register destruction caused by these wavOutMessage calls (weird but true)
		#endif
		#endif // !defined(_WIN64)
		waveInMessage((HWAVEIN)(UINT_PTR)WAVE_MAPPER,0x2000+0x0015,(LPARAM)&uDeviceID,(WPARAM)&dwFlags);
		waveInGetDevCaps(uDeviceID, &inputInfo, sizeof(inputInfo));
		#if !defined(_WIN64)
		#ifdef __GNUC__
		  __asm__ ("popa;");
        #else
		__asm popa;
		#endif
		#endif // !defined(_WIN64)

		*pszName = (char*)malloc((strlen(inputInfo.szPname) + 1) * sizeof(char));
		if (*pszName)
			strcpy(*pszName, inputInfo.szPname);
	}

	return (*pszName) ? true : false;
}




//*****************************************************************************
// DSEnumCallback
//*****************************************************************************
BOOL CALLBACK DSEnumCallback(LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext)
{
	// DirectSound Enumeration callback will be called for each device found.
	// The first device returned with a non-NULL GUID is the 'preferred device'

	// Skip over the device without a GUID
	if (lpGuid)
	{
		char **pszName = (char**)lpContext;
		*pszName = (char*)malloc((strlen(lpcstrDescription)+1) * sizeof(char));
		if (*pszName)
		{
			strcpy(*pszName, lpcstrDescription);
			return FALSE;
		}
	}

	return TRUE;
}



//*****************************************************************************
// FindDevice
//*****************************************************************************
bool FindDevice(ALDEVICE *pDeviceList, char *szDeviceName, bool bExactMatch, char **ppszDefaultName)
{
	// Search through pDeviceList for szDeviceName using an exact match if bExactMatch is true, or using
	// a sub-string search otherwise.
	// If found, allocate memory for *ppszDefaultName and copy the device name over
	ALDEVICE *pDevice = NULL;
	bool bFound = false;

	if (!pDeviceList || !szDeviceName || !ppszDefaultName)
		return false;

	for (pDevice = pDeviceList; pDevice; pDevice = pDevice->pNextDevice)
	{
		if (bExactMatch)
			bFound = (strcmp(pDevice->pszDeviceName, szDeviceName) == 0) ? true : false;
		else
			bFound = (strstr(pDevice->pszDeviceName, szDeviceName)) ? true : false;

		if (bFound)
		{
			*ppszDefaultName = (char*)malloc((strlen(pDevice->pszDeviceName) + 1) * sizeof(char));
			if (*ppszDefaultName)
			{
				strcpy(*ppszDefaultName, pDevice->pszDeviceName);
				break;
			}
		}
	}

	return *ppszDefaultName ? true : false;
}




//*****************************************************************************
// LoadDevicesDLL
//*****************************************************************************
HINSTANCE LoadDevicesDLL(ALDEVICE *pDeviceList, const ALchar *szDeviceName)
{
	// Search pDeviceList for szDeviceName, and when found load the OpenAL DLL
	// that contains that Device name.
	HINSTANCE hDLL = NULL;
	ALDEVICE *pDevice;

	for (pDevice = pDeviceList; pDevice; pDevice = pDevice->pNextDevice)
	{
		if (strcmp(pDevice->pszDeviceName, szDeviceName) == 0)
		{
			hDLL = LoadLibrary(pDevice->pszHostDLLFilename);
			break;
		}
	}

	return hDLL;
}




//*****************************************************************************
// FillOutAlcFunctions
//*****************************************************************************
ALboolean FillOutAlcFunctions(ALCdevice* device)
{
    ALboolean alcFxns = FALSE;
    ALCAPI_FXN_TABLE* alcApi = &device->AlcApi;

	memset(alcApi, 0, sizeof(ALCAPI_FXN_TABLE));

    //
    // Get the OpenAL 1.0 Entry points.
    //
	alcApi->alcCreateContext      = (ALCAPI_CREATE_CONTEXT)GetProcAddress(device->Dll, "alcCreateContext");
    alcApi->alcMakeContextCurrent = (ALCAPI_MAKE_CONTEXT_CURRENT)GetProcAddress(device->Dll, "alcMakeContextCurrent");
    alcApi->alcProcessContext     = (ALCAPI_PROCESS_CONTEXT)GetProcAddress(device->Dll, "alcProcessContext");
	alcApi->alcSuspendContext     = (ALCAPI_SUSPEND_CONTEXT)GetProcAddress(device->Dll, "alcSuspendContext");
    alcApi->alcDestroyContext     = (ALCAPI_DESTROY_CONTEXT)GetProcAddress(device->Dll, "alcDestroyContext");
	alcApi->alcGetCurrentContext  = (ALCAPI_GET_CURRENT_CONTEXT)GetProcAddress(device->Dll, "alcGetCurrentContext");
    alcApi->alcGetContextsDevice  = (ALCAPI_GET_CONTEXTS_DEVICE)GetProcAddress(device->Dll, "alcGetContextsDevice");

	alcApi->alcOpenDevice         = (ALCAPI_OPEN_DEVICE)GetProcAddress(device->Dll, "alcOpenDevice");
    alcApi->alcCloseDevice        = (ALCAPI_CLOSE_DEVICE)GetProcAddress(device->Dll, "alcCloseDevice");

	alcApi->alcGetError           = (ALCAPI_GET_ERROR)GetProcAddress(device->Dll, "alcGetError");

	alcApi->alcIsExtensionPresent = (ALCAPI_IS_EXTENSION_PRESENT)GetProcAddress(device->Dll, "alcIsExtensionPresent");
    alcApi->alcGetProcAddress     = (ALCAPI_GET_PROC_ADDRESS)GetProcAddress(device->Dll, "alcGetProcAddress");
	alcApi->alcGetEnumValue       = (ALCAPI_GET_ENUM_VALUE)GetProcAddress(device->Dll, "alcGetEnumValue");

    alcApi->alcGetString          = (ALCAPI_GET_STRING)GetProcAddress(device->Dll, "alcGetString");
    alcApi->alcGetIntegerv        = (ALCAPI_GET_INTEGERV)GetProcAddress(device->Dll, "alcGetIntegerv");

	//
	// Get the OpenAL 1.1 Entry points.
	//
    alcApi->alcCaptureOpenDevice = (ALCAPI_CAPTURE_OPEN_DEVICE)GetProcAddress(device->Dll, "alcCaptureOpenDevice");
    alcApi->alcCaptureCloseDevice = (ALCAPI_CAPTURE_CLOSE_DEVICE)GetProcAddress(device->Dll, "alcCaptureCloseDevice");
    alcApi->alcCaptureStart = (ALCAPI_CAPTURE_START)GetProcAddress(device->Dll, "alcCaptureStart");
    alcApi->alcCaptureStop = (ALCAPI_CAPTURE_STOP)GetProcAddress(device->Dll, "alcCaptureStop");
    alcApi->alcCaptureSamples = (ALCAPI_CAPTURE_SAMPLES)GetProcAddress(device->Dll, "alcCaptureSamples");

	// handle legacy issue with old Creative DLLs which may not have alcGetProcAddress, alcIsExtensionPresent, alcGetEnumValue
	if (alcApi->alcGetProcAddress == NULL) {
		alcApi->alcGetProcAddress = (ALCAPI_GET_PROC_ADDRESS)alcGetProcAddress;
	}
	if (alcApi->alcIsExtensionPresent == NULL) {
		alcApi->alcIsExtensionPresent = (ALCAPI_IS_EXTENSION_PRESENT)alcIsExtensionPresent;
	}
	if (alcApi->alcGetEnumValue == NULL) {
		alcApi->alcGetEnumValue = (ALCAPI_GET_ENUM_VALUE)alcGetEnumValue;
	}


    alcFxns = (alcApi->alcCreateContext      &&
               alcApi->alcMakeContextCurrent &&
               alcApi->alcProcessContext     &&
			   alcApi->alcSuspendContext     &&
               alcApi->alcDestroyContext     &&
			   alcApi->alcGetCurrentContext  &&
               alcApi->alcGetContextsDevice  &&
			   alcApi->alcOpenDevice         &&
               alcApi->alcCloseDevice        &&
			   alcApi->alcGetError           &&
			   alcApi->alcIsExtensionPresent &&
               alcApi->alcGetProcAddress     &&
			   alcApi->alcGetEnumValue       &&
			   alcApi->alcGetString          &&
               alcApi->alcGetIntegerv);

    return alcFxns;
}




//*****************************************************************************
// FillOutAlFunctions
//*****************************************************************************
ALboolean FillOutAlFunctions(ALCcontext* context)
{
    ALboolean  alFxns = FALSE;
    ALAPI_FXN_TABLE*   alApi = &context->AlApi;

	memset(alApi, 0, sizeof(ALAPI_FXN_TABLE));

    //
    // Get the OpenAL 1.0 & 1.1 Entry points.
    //
    alApi->alEnable               = (ALAPI_ENABLE)GetProcAddress(context->Device->Dll, "alEnable");
    alApi->alDisable              = (ALAPI_DISABLE)GetProcAddress(context->Device->Dll, "alDisable");
    alApi->alIsEnabled            = (ALAPI_IS_ENABLED)GetProcAddress(context->Device->Dll, "alIsEnabled");

	alApi->alGetString            = (ALAPI_GET_STRING)GetProcAddress(context->Device->Dll, "alGetString");
	alApi->alGetBooleanv          = (ALAPI_GET_BOOLEANV)GetProcAddress(context->Device->Dll, "alGetBooleanv");
    alApi->alGetIntegerv          = (ALAPI_GET_INTEGERV)GetProcAddress(context->Device->Dll, "alGetIntegerv");
    alApi->alGetFloatv            = (ALAPI_GET_FLOATV)GetProcAddress(context->Device->Dll, "alGetFloatv");
    alApi->alGetDoublev           = (ALAPI_GET_DOUBLEV)GetProcAddress(context->Device->Dll, "alGetDoublev");
    alApi->alGetBoolean           = (ALAPI_GET_BOOLEAN)GetProcAddress(context->Device->Dll, "alGetBoolean");
    alApi->alGetInteger           = (ALAPI_GET_INTEGER)GetProcAddress(context->Device->Dll, "alGetInteger");
    alApi->alGetFloat             = (ALAPI_GET_FLOAT)GetProcAddress(context->Device->Dll, "alGetFloat");
    alApi->alGetDouble            = (ALAPI_GET_DOUBLE)GetProcAddress(context->Device->Dll, "alGetDouble");
	alApi->alGetError             = (ALAPI_GET_ERROR)GetProcAddress(context->Device->Dll, "alGetError");
    alApi->alIsExtensionPresent   = (ALAPI_IS_EXTENSION_PRESENT)GetProcAddress(context->Device->Dll, "alIsExtensionPresent");
	alApi->alGetProcAddress       = (ALAPI_GET_PROC_ADDRESS)GetProcAddress(context->Device->Dll, "alGetProcAddress");
    alApi->alGetEnumValue         = (ALAPI_GET_ENUM_VALUE)GetProcAddress(context->Device->Dll, "alGetEnumValue");

	alApi->alListenerf            = (ALAPI_LISTENERF)GetProcAddress(context->Device->Dll, "alListenerf");
	alApi->alListener3f           = (ALAPI_LISTENER3F)GetProcAddress(context->Device->Dll, "alListener3f");
	alApi->alListenerfv           = (ALAPI_LISTENERFV)GetProcAddress(context->Device->Dll, "alListenerfv");
    alApi->alListeneri            = (ALAPI_LISTENERI)GetProcAddress(context->Device->Dll, "alListeneri");
	alApi->alListener3i           = (ALAPI_LISTENER3I)GetProcAddress(context->Device->Dll, "alListener3i");
	alApi->alListeneriv           = (ALAPI_LISTENERIV)GetProcAddress(context->Device->Dll, "alListeneriv");
    alApi->alGetListenerf         = (ALAPI_GET_LISTENERF)GetProcAddress(context->Device->Dll, "alGetListenerf");
	alApi->alGetListener3f        = (ALAPI_GET_LISTENER3F)GetProcAddress(context->Device->Dll, "alGetListener3f");
	alApi->alGetListenerfv        = (ALAPI_GET_LISTENERFV)GetProcAddress(context->Device->Dll, "alGetListenerfv");
	alApi->alGetListeneri         = (ALAPI_GET_LISTENERI)GetProcAddress(context->Device->Dll, "alGetListeneri");
	alApi->alGetListener3i        = (ALAPI_GET_LISTENER3I)GetProcAddress(context->Device->Dll, "alGetListener3i");
	alApi->alGetListeneriv        = (ALAPI_GET_LISTENERIV)GetProcAddress(context->Device->Dll, "alGetListeneriv");

    alApi->alGenSources           = (ALAPI_GEN_SOURCES)GetProcAddress(context->Device->Dll, "alGenSources");
    alApi->alDeleteSources        = (ALAPI_DELETE_SOURCES)GetProcAddress(context->Device->Dll, "alDeleteSources");
    alApi->alIsSource             = (ALAPI_IS_SOURCE)GetProcAddress(context->Device->Dll, "alIsSource");
	alApi->alSourcef              = (ALAPI_SOURCEF)GetProcAddress(context->Device->Dll, "alSourcef");
    alApi->alSource3f             = (ALAPI_SOURCE3F)GetProcAddress(context->Device->Dll, "alSource3f");
    alApi->alSourcefv             = (ALAPI_SOURCEFV)GetProcAddress(context->Device->Dll, "alSourcefv");
    alApi->alSourcei              = (ALAPI_SOURCEI)GetProcAddress(context->Device->Dll, "alSourcei");
    alApi->alSource3i             = (ALAPI_SOURCE3I)GetProcAddress(context->Device->Dll, "alSource3i");
    alApi->alSourceiv             = (ALAPI_SOURCEIV)GetProcAddress(context->Device->Dll, "alSourceiv");
    alApi->alGetSourcef           = (ALAPI_GET_SOURCEF)GetProcAddress(context->Device->Dll, "alGetSourcef");
    alApi->alGetSource3f          = (ALAPI_GET_SOURCE3F)GetProcAddress(context->Device->Dll, "alGetSource3f");
    alApi->alGetSourcefv          = (ALAPI_GET_SOURCEFV)GetProcAddress(context->Device->Dll, "alGetSourcefv");
	alApi->alGetSourcei           = (ALAPI_GET_SOURCEI)GetProcAddress(context->Device->Dll, "alGetSourcei");
    alApi->alGetSource3i          = (ALAPI_GET_SOURCE3I)GetProcAddress(context->Device->Dll, "alGetSource3i");
    alApi->alGetSourceiv          = (ALAPI_GET_SOURCEIV)GetProcAddress(context->Device->Dll, "alGetSourceiv");
    alApi->alSourcePlayv          = (ALAPI_SOURCE_PLAYV)GetProcAddress(context->Device->Dll, "alSourcePlayv");
    alApi->alSourceStopv          = (ALAPI_SOURCE_STOPV)GetProcAddress(context->Device->Dll, "alSourceStopv");
    alApi->alSourceRewindv        = (ALAPI_SOURCE_REWINDV)GetProcAddress(context->Device->Dll, "alSourceRewindv");
	alApi->alSourcePausev         = (ALAPI_SOURCE_PAUSEV)GetProcAddress(context->Device->Dll, "alSourcePausev");
    alApi->alSourcePlay           = (ALAPI_SOURCE_PLAY)GetProcAddress(context->Device->Dll, "alSourcePlay");
    alApi->alSourceStop           = (ALAPI_SOURCE_STOP)GetProcAddress(context->Device->Dll, "alSourceStop");
    alApi->alSourceRewind         = (ALAPI_SOURCE_STOP)GetProcAddress(context->Device->Dll, "alSourceRewind");
	alApi->alSourcePause          = (ALAPI_SOURCE_PAUSE)GetProcAddress(context->Device->Dll, "alSourcePause");

	alApi->alSourceQueueBuffers   = (ALAPI_SOURCE_QUEUE_BUFFERS)GetProcAddress(context->Device->Dll, "alSourceQueueBuffers");
    alApi->alSourceUnqueueBuffers = (ALAPI_SOURCE_UNQUEUE_BUFFERS)GetProcAddress(context->Device->Dll, "alSourceUnqueueBuffers");

    alApi->alGenBuffers           = (ALAPI_GEN_BUFFERS)GetProcAddress(context->Device->Dll, "alGenBuffers");
    alApi->alDeleteBuffers        = (ALAPI_DELETE_BUFFERS)GetProcAddress(context->Device->Dll, "alDeleteBuffers");
    alApi->alIsBuffer             = (ALAPI_IS_BUFFER)GetProcAddress(context->Device->Dll, "alIsBuffer");
    alApi->alBufferData           = (ALAPI_BUFFER_DATA)GetProcAddress(context->Device->Dll, "alBufferData");
	alApi->alBufferf              = (ALAPI_BUFFERF)GetProcAddress(context->Device->Dll, "alBufferf");
    alApi->alBuffer3f             = (ALAPI_BUFFER3F)GetProcAddress(context->Device->Dll, "alBuffer3f");
    alApi->alBufferfv             = (ALAPI_BUFFERFV)GetProcAddress(context->Device->Dll, "alBufferfv");
    alApi->alBufferi              = (ALAPI_BUFFERI)GetProcAddress(context->Device->Dll, "alBufferi");
    alApi->alBuffer3i             = (ALAPI_BUFFER3I)GetProcAddress(context->Device->Dll, "alBuffer3i");
    alApi->alBufferiv             = (ALAPI_BUFFERIV)GetProcAddress(context->Device->Dll, "alBufferiv");
	alApi->alGetBufferf           = (ALAPI_GET_BUFFERF)GetProcAddress(context->Device->Dll, "alGetBufferf");
    alApi->alGetBuffer3f          = (ALAPI_GET_BUFFER3F)GetProcAddress(context->Device->Dll, "alGetBuffer3f");
    alApi->alGetBufferfv          = (ALAPI_GET_BUFFERFV)GetProcAddress(context->Device->Dll, "alGetBufferfv");
    alApi->alGetBufferi           = (ALAPI_GET_BUFFERI)GetProcAddress(context->Device->Dll, "alGetBufferi");
    alApi->alGetBuffer3i          = (ALAPI_GET_BUFFER3I)GetProcAddress(context->Device->Dll, "alGetBuffer3i");
    alApi->alGetBufferiv          = (ALAPI_GET_BUFFERIV)GetProcAddress(context->Device->Dll, "alGetBufferiv");

	alApi->alDopplerFactor        = (ALAPI_DOPPLER_FACTOR)GetProcAddress(context->Device->Dll, "alDopplerFactor");
    alApi->alDopplerVelocity      = (ALAPI_DOPPLER_VELOCITY)GetProcAddress(context->Device->Dll, "alDopplerVelocity");
	alApi->alSpeedOfSound         = (ALAPI_SPEED_OF_SOUND)GetProcAddress(context->Device->Dll, "alSpeedOfSound");
    alApi->alDistanceModel        = (ALAPI_DISTANCE_MODEL)GetProcAddress(context->Device->Dll, "alDistanceModel");
    
    alFxns = (alApi->alEnable               &&
              alApi->alDisable              &&
              alApi->alIsEnabled            &&

              alApi->alGetString            &&
              alApi->alGetBooleanv          &&
              alApi->alGetIntegerv          &&
              alApi->alGetFloatv            &&
              alApi->alGetDoublev           &&
			  alApi->alGetBoolean           &&
              alApi->alGetInteger           &&
              alApi->alGetFloat             &&
              alApi->alGetDouble            &&
              
              alApi->alGetError             &&

              alApi->alIsExtensionPresent   &&
              alApi->alGetProcAddress       &&
              alApi->alGetEnumValue         &&

			  alApi->alListenerf            &&
              alApi->alListener3f           &&
              alApi->alListenerfv           &&
              alApi->alListeneri            &&
              alApi->alGetListenerf         &&
              alApi->alGetListener3f        &&
              alApi->alGetListenerfv        &&
			  alApi->alGetListeneri         &&
              
              alApi->alGenSources           &&
              alApi->alDeleteSources        &&
              alApi->alIsSource             &&
              alApi->alSourcef              &&
              alApi->alSource3f             &&
              alApi->alSourcefv             &&
			  alApi->alSourcei              &&
              alApi->alGetSourcef           &&
              alApi->alGetSource3f          &&
              alApi->alGetSourcefv          &&
              alApi->alGetSourcei           &&
              alApi->alSourcePlayv          &&
              alApi->alSourceStopv          &&
              alApi->alSourceRewindv        &&
			  alApi->alSourcePausev         &&
              alApi->alSourcePlay           &&
              alApi->alSourceStop           &&
              alApi->alSourceRewind         &&
			  alApi->alSourcePause          &&

			  alApi->alSourceQueueBuffers   &&
              alApi->alSourceUnqueueBuffers &&              

              alApi->alGenBuffers           &&
              alApi->alDeleteBuffers        &&
              alApi->alIsBuffer             &&
              alApi->alBufferData           &&
              alApi->alGetBufferf           &&
			  alApi->alGetBufferi           &&

			  alApi->alDopplerFactor        &&
              alApi->alDopplerVelocity      &&
              alApi->alDistanceModel);

    return alFxns;
}




//*****************************************************************************
//*****************************************************************************
//
// ALC API Entry Points
//
//*****************************************************************************ALC_
//*****************************************************************************

//*****************************************************************************
// alcCloseDevice
//*****************************************************************************
//
ALCAPI ALCboolean ALCAPIENTRY alcCloseDevice(ALCdevice* device)
{
#ifdef _LOGCALLS
	LOG("alcCloseDevice device %p\n", device);
#endif
    if(!device)
    {
        return ALC_FALSE;
    }

	if (device == g_CaptureDevice)
		return g_CaptureDevice->AlcApi.alcCloseDevice(g_CaptureDevice->CaptureDevice);

    //
    // Check if its linked to a context.
    //
    if(device->InUse)
    {
        ALCcontext* context = 0;
        ALlistEntry* entry = 0;

        //
        // Not all of the contexts using the device have been destroyed.
        //
        assert(0);

        //
        // Loop through the context list and free and contexts linked to the device.
        // Go back to the beginning each time in case some one changed the context
        // list iterator.
        //
        alListAcquireLock(alContextList);
        entry = alListIteratorReset(alContextList);
        while(entry)
        {
            context = (ALCcontext*)alListGetData(entry);
            if(context->Device == device)
            {
                alListReleaseLock(alContextList);
                alcDestroyContext((ALCcontext *)context);
                alListAcquireLock(alContextList);
                entry = alListIteratorReset(alContextList);
            }

            else
            {
                entry = alListIteratorNext(alContextList);
            }
        }

        alListReleaseLock(alContextList);
        assert(!device->InUse);
    }

    device->AlcApi.alcCloseDevice(device->DllDevice);
    FreeLibrary(device->Dll);
    free(device);

	return ALC_TRUE;
}




//*****************************************************************************
// alcCreateContext
//*****************************************************************************
ALCAPI ALCcontext* ALCAPIENTRY alcCreateContext(ALCdevice* device, const ALint* attrList)
{
#ifdef _LOGCALLS
	LOG("alcCreateContext device %p ", device);
	if (attrList)
	{
		unsigned long ulIndex = 0;
		while ((ulIndex < 16) && (attrList[ulIndex]))
		{
			switch(attrList[ulIndex])
			{
			case ALC_FREQUENCY:
				LOG("ALC_FREQUENCY %d ", attrList[ulIndex + 1]);
				break;

			case ALC_REFRESH:
				LOG("ALC_REFRESH %d ", attrList[ulIndex + 1]);
				break;

			case ALC_SYNC:
				LOG("ALC_SYNC %d ", attrList[ulIndex + 1]);
				break;

			case ALC_MONO_SOURCES:
				LOG("ALC_MONO_SOURCES %d ", attrList[ulIndex + 1]);
				break;

			case ALC_STEREO_SOURCES:
				LOG("ALC_STEREO_SOURCES %d ", attrList[ulIndex + 1]);
				break;

			case 0x20003/*ALC_MAX_AUXILIARY_SENDS*/:
				LOG("ALC_MAX_AUXILIARY_SENDS %d", attrList[ulIndex + 1]);
				break;
			}
			ulIndex += 2;
		}
	}
	LOG("\n");
#endif

	ALCcontext* context = 0;

    if(!device)
    {
        LastError = ALC_INVALID_DEVICE;
        return 0;
    }

	if (device == g_CaptureDevice)
		return g_CaptureDevice->AlcApi.alcCreateContext(g_CaptureDevice->CaptureDevice, attrList);

    //
    // Allocate the context.
    //
    context = (ALCcontext*)malloc(sizeof(ALCcontext));
    if(!context)
    {
        return 0;
    }

    memset(context, 0, sizeof(ALCcontext));
    context->Device = device;
    context->Suspended = FALSE;
    context->LastError = ALC_NO_ERROR;
    InitializeCriticalSection(&context->Lock);

    //
    // We don't fill out the AL functions in case they are context specific.
    //

    context->DllContext = device->AlcApi.alcCreateContext(device->DllDevice, attrList);
    if(!context->DllContext)
    {
        DeleteCriticalSection(&context->Lock);
        free(context);
        context = 0;
        return 0;
    }

    device->InUse++;

    //
    // Add it to the context list.
    //
    alListInitializeEntry(&context->ListEntry, context);
    alListAcquireLock(alContextList);
    alListAddEntry(alContextList, &context->ListEntry);
    alListReleaseLock(alContextList);
    return context;
}




//*****************************************************************************
// alcDestroyContext
//*****************************************************************************
ALCAPI ALvoid ALCAPIENTRY alcDestroyContext(ALCcontext* context)
{
#ifdef _LOGCALLS
	LOG("alcDestroyContext context %p\n", context);
#endif
    ALCcontext* listData = 0;

    if(!context)
    {
        return;
    }

    //
    // Remove the entry from the context list.
    //
    alListAcquireLock(alContextList);
    listData = (ALCcontext*)alListRemoveEntry(alContextList, &context->ListEntry);
    if(!listData)
    {
        alListReleaseLock(alContextList);
        return;
    }

    if(context == alCurrentContext)
    {
        alCurrentContext = 0;
    }

    EnterCriticalSection(&context->Lock);
    alListReleaseLock(alContextList);

    context->Device->InUse--;

    // Clean up the context.
    if(context->DllContext)
    {
        context->Device->AlcApi.alcDestroyContext(context->DllContext);
    }

    LeaveCriticalSection(&context->Lock);
    DeleteCriticalSection(&context->Lock);
    free(context);
}




//*****************************************************************************
// alcGetContextsDevice
//*****************************************************************************
ALCAPI ALCdevice* ALCAPIENTRY alcGetContextsDevice(ALCcontext* context)
{
#ifdef _LOGCALLS
	LOG("alcGetContextsDevice context %p\n", context);
#endif
    ALCdevice* ALCdevice = 0;

    alListAcquireLock(alContextList);
    if(alListMatchData(alContextList, context))
    {
        ALCdevice = context->Device;
    }

    alListReleaseLock(alContextList);

    return ALCdevice;
}




//*****************************************************************************
// alcGetCurrentContext
//*****************************************************************************
ALCAPI ALCcontext* ALCAPIENTRY alcGetCurrentContext(ALvoid)
{
#ifdef _LOGCALLS
	LOG("alcGetCurrentContext\n");
#endif
    return (ALCcontext *)alCurrentContext;
}




//*****************************************************************************
// alcGetEnumValue
//*****************************************************************************
ALCAPI ALenum ALCAPIENTRY alcGetEnumValue(ALCdevice* device, const ALCchar* ename)
{
#ifdef _LOGCALLS
	LOG("alcGetEnumValue device %p enum name '%s'\n", device, ename ? ename : "<NULL>");
#endif
    //
    // Always return the router version of the ALC enum if it exists.
    //
    ALsizei i = 0;
    while(alcEnums[i].ename && strcmp((char*)alcEnums[i].ename, (char*)ename))
    {
        i++;
    }

    if(alcEnums[i].ename)
    {
        return alcEnums[i].value;
    }

    if(device)
    {
		if (device == g_CaptureDevice)
			return g_CaptureDevice->AlcApi.alcGetEnumValue(g_CaptureDevice->CaptureDevice, ename);

        return device->AlcApi.alcGetEnumValue(device->DllDevice, ename);
    }

    LastError = ALC_INVALID_ENUM;
    return 0;
}




//*****************************************************************************
// alcGetError
//*****************************************************************************
ALCAPI ALenum ALCAPIENTRY alcGetError(ALCdevice* device)
{
#ifdef _LOGCALLS
	LOG("alcGetError device %p\n", device);
#endif
    ALenum errorCode = ALC_NO_ERROR;

    // Try to get a valid device.
    if(!device)
    {
		if (g_CaptureDevice == device)
			return 
        errorCode = LastError;
        LastError = ALC_NO_ERROR;
        return errorCode;
    }

    //
    // Check if its a 3rd party device.
    //
	if (device == g_CaptureDevice)
		errorCode = g_CaptureDevice->AlcApi.alcGetError(g_CaptureDevice->CaptureDevice);
	else
		errorCode = device->AlcApi.alcGetError(device->DllDevice);

    return errorCode;
}




//*****************************************************************************
// alcGetIntegerv
//*****************************************************************************
ALCAPI ALvoid ALCAPIENTRY alcGetIntegerv(ALCdevice* device, ALenum param, ALsizei size, ALint* data)
{
#ifdef _LOGCALLS
	LOG("alcGetIntegerv device %p enum ", device);
	switch (param)
	{
	case ALC_ATTRIBUTES_SIZE:
		LOG("ALC_ATTRIBUTES_SIZE\n");
		break;
	case ALC_ALL_ATTRIBUTES:
		LOG("ALC_ALL_ATTRIBUTES\n");
		break;
	case ALC_MAJOR_VERSION:
		LOG("ALC_MAJOR_VERSION\n");
		break;
	case ALC_MINOR_VERSION:
		LOG("ALC_MINOR_VERSION\n");
		break;
	case ALC_CAPTURE_SAMPLES:
		LOG("ALC_CAPTURE_SAMPLES\n");
		break;
	case ALC_FREQUENCY:
		LOG("ALC_FREQUENCY\n");
		break;
	case ALC_REFRESH:
		LOG("ALC_REFRESH\n");
		break;
	case ALC_SYNC:
		LOG("ALC_SYNC\n");
		break;
	case ALC_MONO_SOURCES:
		LOG("ALC_MONO_SOURCES\n");
		break;
	case ALC_STEREO_SOURCES:
		LOG("ALC_STEREO_SOURCES\n");
		break;
	case 0x20003: // ALC_MAX_AUXILIARY_SENDS
		LOG("ALC_MAX_AUXILIARY_SENDS\n");
		break;
	case 0x20001: // ALC_EFX_MAJOR_VERSION
		LOG("ALC_EFX_MAJOR_VERSION\n");
		break;
	case 0x20002: // ALC_EFX_MINOR_VERSION
		LOG("ALC_EFX_MINOR_VERSION\n");
		break;
	default:
		LOG("<Unknown>\n");
		break;
	}
#endif

	if(device)
    {
		if (device == g_CaptureDevice)
		{
			g_CaptureDevice->AlcApi.alcGetIntegerv(g_CaptureDevice->CaptureDevice, param, size, data);
			return;
		}

        device->AlcApi.alcGetIntegerv(device->DllDevice, param, size, data);
        return;
    }

    switch(param)
    {
        case ALC_MAJOR_VERSION:
        {
            if((size < sizeof(ALint)) || (data == 0))
            {
                LastError = ALC_INVALID;
                return;
            }

            *data = alcMajorVersion;
        }
        break;

        case ALC_MINOR_VERSION:
        {
            if((size < sizeof(ALint)) || (data == 0))
            {
                LastError = ALC_INVALID;
                return;
            }

            *data = alcMinorVersion;
        }
        break;

        default:
        {
            device->LastError = ALC_INVALID_ENUM;
        }
        break;
    }
}




//*****************************************************************************
// alcGetProcAddress
//*****************************************************************************
ALCAPI ALvoid* ALCAPIENTRY alcGetProcAddress(ALCdevice* device, const ALCchar* fname)
{
#ifdef _LOGCALLS
	LOG("alcGetProcAddress device %p function name '%s'\n", device, fname ? fname : "<NULL>");
#endif

    //
    // Always return the router version of the ALC function if it exists.
    //
    ALsizei i = 0;
    while(alcFunctions[i].fname && strcmp((char*)alcFunctions[i].fname, (char*)fname))
    {
        i++;
    }

    if(alcFunctions[i].fname)
    {
        return alcFunctions[i].address;
    }

    if(device)
    {
		if (device == g_CaptureDevice)
			return g_CaptureDevice->AlcApi.alcGetProcAddress(g_CaptureDevice->CaptureDevice, fname);

        return device->AlcApi.alcGetProcAddress(device->DllDevice, fname);
    }

    LastError = ALC_INVALID_ENUM;
    return 0;
}




//*****************************************************************************
// alcIsExtensionPresent
//*****************************************************************************
ALCAPI ALboolean ALCAPIENTRY alcIsExtensionPresent(ALCdevice* device, const ALCchar* ename)
{
#ifdef _LOGCALLS
	LOG("alcIsExtensionPresent device %p extension name '%s'\n", device, ename ? ename : "<NULL>");
#endif
    //
    // Check if its a router supported extension first as its a good idea to have
    // ALC calls go through the router if possible.
    //
    ALsizei i = 0;
    while(alcExtensions[i].ename && _stricmp((char*)alcExtensions[i].ename, (char*)ename))
    {
        i++;
    }

    if(alcExtensions[i].ename)
    {
        return ALC_TRUE;
    }

    //
    // Check the device passed in to see if the extension is supported.
    //
    if(device)
    {
		if (device == g_CaptureDevice)
			return g_CaptureDevice->AlcApi.alcIsExtensionPresent(g_CaptureDevice->CaptureDevice, ename);

        return device->AlcApi.alcIsExtensionPresent(device->DllDevice, ename);
    }

    LastError = ALC_INVALID_ENUM;
    return ALC_FALSE;
}




//*****************************************************************************
// alcMakeContextCurrent
//*****************************************************************************
ALCAPI ALboolean ALCAPIENTRY alcMakeContextCurrent(ALCcontext* context)
{
#ifdef _LOGCALLS
	LOG("alcMakeContextCurrent context %p\n", context);
#endif
    ALboolean contextSwitched = AL_TRUE;

    //
    // Context must be a valid context or 0
    //
    alListAcquireLock(alContextList);
    if(!alListMatchData(alContextList, context) && context != 0)
    {
        alListReleaseLock(alContextList);
        return ALC_FALSE;
    }

    //
    // Try the new context.
    //
    if(context)
    {
        contextSwitched = context->Device->AlcApi.alcMakeContextCurrent(context->DllContext);

        //
        // If this is the first time the context has been made the current context, fill in the context
        // function pointers.
        //
        if(contextSwitched && !context->AlApi.alGetProcAddress)
        {
            //
            // Don't fill out the functions here in case they are context specific pointers in the device.
            //
            if(!FillOutAlFunctions(context))
            {
                LastError = ALC_INVALID_CONTEXT;
                contextSwitched = AL_FALSE;

                //
                // Something went wrong, restore the old context.
                //
                if(alCurrentContext)
                {
                    alCurrentContext->Device->AlcApi.alcMakeContextCurrent(alCurrentContext->DllContext);
                }

                else
                {
                    alCurrentContext->Device->AlcApi.alcMakeContextCurrent(0);
                }
            }
        }
	} else {
		if ((alCurrentContext) && (alCurrentContext->Device) && (alCurrentContext->Device->AlcApi.alcMakeContextCurrent)) {
			contextSwitched = alCurrentContext->Device->AlcApi.alcMakeContextCurrent(0);
		}
    }

    //
    // Set the context states if the switch was successful.
    //
    if(contextSwitched)
    {
        alCurrentContext = context;
    }

    alListReleaseLock(alContextList);
    return contextSwitched;
}




//*****************************************************************************
// alcOpenDevice
//*****************************************************************************
ALCAPI ALCdevice* ALCAPIENTRY alcOpenDevice(const ALCchar* deviceName)
{
#ifdef _LOGCALLS
	LOG("alcOpenDevice device name '%s'\n", deviceName ? deviceName : "<NULL>");
#endif
    HINSTANCE dll = 0;
    ALCdevice* device = 0;
	const ALchar *pszDeviceName = NULL;

	BuildDeviceList();

	if (g_pDeviceList)
	{
		if ((!deviceName) || (strlen(deviceName)==0) || (strcmp(deviceName, "DirectSound3D")==0))
			pszDeviceName = pszDefaultDeviceSpecifier;
		else
			pszDeviceName = deviceName;

		// Search for device in Playback Device List
		dll = LoadDevicesDLL(g_pDeviceList, pszDeviceName);

		if (!dll)
		{
			// If NOT found, and the requested name is one of these ...
			//		"Generic Hardware"	(no longer available on Windows Vista)
			//		"DirectSound"		(legacy name for OpenAL Software mixer device)
			//		"MMSYSTEM"			(legacy name for OpenAL Software mixer using MMSYSTEM instead of DirectSound)
			// try to open the "Generic Software" device instead
			if ((strcmp(pszDeviceName, "Generic Hardware") == 0) ||
				(strcmp(pszDeviceName, "DirectSound") == 0) ||
				(strcmp(pszDeviceName, "MMSYSTEM") == 0))
			{
				dll = LoadDevicesDLL(g_pDeviceList, "Generic Software");
			}
		}

		if (!dll)
			dll = LoadDevicesDLL(g_pAllDevicesList, pszDeviceName);

		if (dll)
		{
			device = (ALCdevice*)malloc(sizeof(ALCdevice));
			if (device)
			{
				memset(device, 0, sizeof(ALCdevice));
				device->LastError = ALC_NO_ERROR;
				device->InUse = 0;
				device->Dll = dll;
				if (FillOutAlcFunctions(device))
					device->DllDevice = device->AlcApi.alcOpenDevice(pszDeviceName);

				if (!device->DllDevice)
				{
					FreeLibrary(dll);
				    free(device);
				    device = 0;
				}
			}
		}
	}

	if (!device)
		LastError = ALC_INVALID_DEVICE;

	return device;
}




//*****************************************************************************
// alcProcessContext
//*****************************************************************************
ALCAPI ALvoid ALCAPIENTRY alcProcessContext(ALCcontext* context)
{
#ifdef _LOGCALLS
	LOG("alcProcessContext context %p\n", context);
#endif
    alListAcquireLock(alContextList);
    if(!context && !alCurrentContext)
    {
        alListReleaseLock(alContextList);
        return;
    }

    if(!context)
    {
        context = alCurrentContext;
    }

    EnterCriticalSection(&context->Lock);
    alListReleaseLock(alContextList);

    if(context->DllContext)
    {
        context->Device->AlcApi.alcProcessContext(context->DllContext);
    }

    context->Suspended = FALSE;

    LeaveCriticalSection(&context->Lock);
    return;
}




//*****************************************************************************
// alcSuspendContext
//*****************************************************************************
ALCAPI ALCvoid ALCAPIENTRY alcSuspendContext(ALCcontext* context)
{
#ifdef _LOGCALLS
	LOG("alcSuspendContext context %p\n", context);
#endif
    alListAcquireLock(alContextList);
    if(!context && !alCurrentContext)
    {
        alListReleaseLock(alContextList);
        return;
    }

    if(!context)
    {
        context = (ALCcontext *)alCurrentContext;
    }

    EnterCriticalSection(&context->Lock);
    alListReleaseLock(alContextList);

    context->Suspended = TRUE;

    if(context->DllContext)
    {
        context->Device->AlcApi.alcSuspendContext(context->DllContext);
    }

    LeaveCriticalSection(&context->Lock);
    return;
}




//*****************************************************************************
// alcGetString
//*****************************************************************************
ALCAPI const ALCchar* ALCAPIENTRY alcGetString(ALCdevice* device, ALenum param)
{
#ifdef _LOGCALLS
	LOG("alcGetString device %p enum ", device);
	switch (param)
	{
	case ALC_NO_ERROR:
		LOG("ALC_NO_ERROR\n");
		break;
    case ALC_INVALID_ENUM:
	    LOG("ALC_INVALID_ENUM\n");
		break;
    case ALC_INVALID_VALUE:
		LOG("ALC_INVALID_VALUE\n");
	    break;
    case ALC_INVALID_DEVICE:
		LOG("ALC_INVALID_DEVICE\n");
	    break;
    case ALC_INVALID_CONTEXT:
		LOG("ALC_INVALID_CONTEXT\n");
	    break;
    case ALC_DEFAULT_DEVICE_SPECIFIER:
		LOG("ALC_DEFAULT_DEVICE_SPECIFIER\n");
		break;
    case ALC_DEVICE_SPECIFIER:
		LOG("ALC_DEVICE_SPECIFIER\n");
		break;
	case ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER:
		LOG("ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER\n");
		break;
	case ALC_CAPTURE_DEVICE_SPECIFIER:
		LOG("ALC_CAPTURE_DEVICE_SPECIFIER\n");
		break;
	case ALC_ALL_DEVICES_SPECIFIER:
		LOG("ALC_ALL_DEVICES_SPECIFIER\n");
		break;
	case ALC_DEFAULT_ALL_DEVICES_SPECIFIER:
		LOG("ALC_DEFAULT_ALL_DEVICES_SPECIFIER\n");
		break;
	case ALC_EXTENSIONS:
		LOG("ALC_EXTENSIONS\n");
		break;
	default:
		LOG("<Unknown>\n");
		break;
	}
#endif

    const ALCchar* value = 0;

	if ((param != ALC_DEFAULT_DEVICE_SPECIFIER) && (param != ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER)) {
		if(device)
		{
			if (device == g_CaptureDevice)
				return g_CaptureDevice->AlcApi.alcGetString(g_CaptureDevice->CaptureDevice, param);

			return device->AlcApi.alcGetString(device->DllDevice, param);
		}
	}

    switch(param)
    {
        case ALC_NO_ERROR:
        {
            value = alcNoError;
        }
        break;

        case ALC_INVALID_ENUM:
        {
            value = alcErrInvalidEnum;
        }
        break;

        case ALC_INVALID_VALUE:
        {
            value = alcErrInvalidValue;
        }
        break;

        case ALC_INVALID_DEVICE:
        {
            value = alcErrInvalidDevice;
        }
        break;

        case ALC_INVALID_CONTEXT:
        {
            value = alcErrInvalidContext;
        }
        break;

        case ALC_DEFAULT_DEVICE_SPECIFIER:
			BuildDeviceList();
			if (pszDefaultDeviceSpecifier)
				value = pszDefaultDeviceSpecifier;
			else
				value = szEmptyString;
			break;

        case ALC_DEVICE_SPECIFIER:
			BuildDeviceList();
			if (pszDeviceSpecifierList)
				value = pszDeviceSpecifierList;
			else
				value = szEmptyString;
			break;

		case ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER:
			BuildDeviceList();
			if (pszDefaultCaptureDeviceSpecifier)
				value = pszDefaultCaptureDeviceSpecifier;
			else
				value = szEmptyString;
			break;

		case ALC_CAPTURE_DEVICE_SPECIFIER:
			BuildDeviceList();
			if (pszCaptureDeviceSpecifierList)
				value = pszCaptureDeviceSpecifierList;
			else
				value = szEmptyString;
			break;

		case ALC_ALL_DEVICES_SPECIFIER:
			BuildDeviceList();
			if (pszAllDevicesSpecifierList)
				value = pszAllDevicesSpecifierList;
			else
				value = szEmptyString;
			break;

		case ALC_DEFAULT_ALL_DEVICES_SPECIFIER:
			BuildDeviceList();
			if (pszDefaultAllDevicesSpecifier)
				value = pszDefaultAllDevicesSpecifier;
			else
				value = szEmptyString;
			break;

        default:
            LastError = ALC_INVALID_ENUM;
            break;
    }

    return value;
}




//*****************************************************************************
// alcCaptureOpenDevice
//*****************************************************************************
ALCAPI ALCdevice * ALCAPIENTRY alcCaptureOpenDevice(const ALCchar *deviceName, ALCuint frequency, ALCenum format, ALCsizei buffersize)
{
#ifdef _LOGCALLS
	LOG("alcCaptureOpenDevice device name '%s' frequency %d format %d buffersize %d\n", deviceName ? deviceName : "<NULL>", frequency, format, buffersize);
#endif
	const ALchar *pszDeviceName = NULL;

	BuildDeviceList();

	if (!g_pCaptureDeviceList)
		return NULL;

	if (!g_CaptureDevice) {
		g_CaptureDevice = (ALCdevice*)malloc(sizeof(ALCdevice));

		if (g_CaptureDevice)
		{
			// clear
			memset(g_CaptureDevice, 0, sizeof(ALCdevice));

			// make sure we have a device name
			if ((!deviceName) || (strlen(deviceName) == 0))
				pszDeviceName = pszDefaultCaptureDeviceSpecifier;
			else
				pszDeviceName = deviceName;

			g_CaptureDevice->Dll = LoadDevicesDLL(g_pCaptureDeviceList, pszDeviceName);

			if (g_CaptureDevice->Dll) {
				if(FillOutAlcFunctions(g_CaptureDevice)) {
					if (g_CaptureDevice->AlcApi.alcCaptureOpenDevice) {
						g_CaptureDevice->CaptureDevice = g_CaptureDevice->AlcApi.alcCaptureOpenDevice(pszDeviceName, frequency, format, buffersize);
						g_CaptureDevice->LastError = ALC_NO_ERROR;
						g_CaptureDevice->InUse = 0;
					} else {
						g_CaptureDevice->LastError = ALC_INVALID_DEVICE;
					}
				}
			}
		}
	} else {
		// already open
		g_CaptureDevice->LastError = ALC_INVALID_VALUE;
	}

	if (g_CaptureDevice != NULL) {
		if (g_CaptureDevice->CaptureDevice) {
			return g_CaptureDevice;
		} else {
			free(g_CaptureDevice);
			g_CaptureDevice = NULL;
			return NULL;
		}
	} else {
		return NULL;
	}
}




//*****************************************************************************
// alcCaptureCloseDevice
//*****************************************************************************
ALCAPI ALCboolean ALCAPIENTRY alcCaptureCloseDevice(ALCdevice *device)
{
#ifdef _LOGCALLS
	LOG("alcCaptureCloseDevice device %p\n", device);
#endif
	ALCboolean bReturn = ALC_FALSE;

	if (device == g_CaptureDevice)
	{
		if (g_CaptureDevice != NULL) {
			if (g_CaptureDevice->AlcApi.alcCaptureCloseDevice) {
				bReturn = g_CaptureDevice->AlcApi.alcCaptureCloseDevice(g_CaptureDevice->CaptureDevice);
				delete g_CaptureDevice;
				g_CaptureDevice = NULL;
			} else {
				g_CaptureDevice->LastError = ALC_INVALID_DEVICE;
			}
		}
	}

    return bReturn;
}




//*****************************************************************************
// alcCaptureStart
//*****************************************************************************
ALCAPI ALCvoid ALCAPIENTRY alcCaptureStart(ALCdevice *device)
{
#ifdef _LOGCALLS
	LOG("alcCaptureStart device %p\n", device);
#endif
	if (device == g_CaptureDevice)
	{
		if (g_CaptureDevice != NULL) {
			if (g_CaptureDevice->AlcApi.alcCaptureStart) {
				g_CaptureDevice->AlcApi.alcCaptureStart(g_CaptureDevice->CaptureDevice);
			} else {
				g_CaptureDevice->LastError = ALC_INVALID_DEVICE;
			}
		}
	}

    return;
}




//*****************************************************************************
// alcCaptureStop
//*****************************************************************************
ALCAPI ALCvoid ALCAPIENTRY alcCaptureStop(ALCdevice *device)
{
#ifdef _LOGCALLS
	LOG("alcCaptureStop device %p\n", device);
#endif
	if (device == g_CaptureDevice)
	{
		if (g_CaptureDevice != NULL) {
			if (g_CaptureDevice->AlcApi.alcCaptureStop) {
				g_CaptureDevice->AlcApi.alcCaptureStop(g_CaptureDevice->CaptureDevice);
			} else {
				g_CaptureDevice->LastError = ALC_INVALID_DEVICE;
			}
		}
	}

    return;
}




//*****************************************************************************
// alcCaptureSamples
//*****************************************************************************
ALCAPI ALCvoid ALCAPIENTRY alcCaptureSamples(ALCdevice *device, ALCvoid *buffer, ALCsizei samples)
{
#ifdef _LOGCALLS
	LOG("alcCaptureSamples device %p buffer %p samples %d\n", device, buffer, samples);
#endif
	if (device == g_CaptureDevice)
	{
		if (g_CaptureDevice != NULL) {
			if (g_CaptureDevice->AlcApi.alcCaptureSamples) {
				g_CaptureDevice->AlcApi.alcCaptureSamples(g_CaptureDevice->CaptureDevice, buffer, samples);
			} else {
				g_CaptureDevice->LastError = ALC_INVALID_DEVICE;
			}
		}
	}

    return;
}

#ifdef _LOGCALLS
void OutputMessage(const char *szDebug,...)
{
	static FILE *pFile = NULL;
	SYSTEMTIME sysTime;
	va_list args;

	va_start(args, szDebug);

	if (!pFile)
	{
		pFile = fopen(LOGFILENAME, "w");
		GetLocalTime(&sysTime);
		fprintf(pFile, "OpenAL Router\n\nLog Time : %d/%d/%d at %d:%s%d:%s%d\n\n", sysTime.wDay, sysTime.wMonth, sysTime.wYear,
			sysTime.wHour, (sysTime.wMinute < 10) ? "0" : "", sysTime.wMinute, (sysTime.wSecond < 10) ? "0" : "", sysTime.wSecond);
	}

	vfprintf(pFile, szDebug, args);
	fflush(pFile);

	va_end(args);
}
#endif