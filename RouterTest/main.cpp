#include <al.h>
#include <alc.h>
#include <stdio.h>
#include <conio.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>

int main(int argc, char* argv[])
{
	const ALchar *szNames = NULL;
	long lErrorCount = 0;

	///////////////////////////////////////////////////////////////////
	// TEST : Enumerate the playback devices
	//
	printf("--------------------------------------\n");
	printf("TESTING ALC_ENUMERATION_EXT EXTENSION\n\n");
	if (alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT") == AL_TRUE)
	{
		printf("ALC_ENUMERATION_EXT Device List:-\n\n");

		szNames = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
		if (strlen(szNames) == 0)
			printf("NO DEVICES FOUND\n");
		else
		{
			while (szNames && *szNames)
			{
				printf("%s ", szNames);
				// Try opening each device
				ALCdevice *pDevice = alcOpenDevice(szNames);
				if (pDevice)
				{
					printf("- Opened Successfully\n");
					alcCloseDevice(pDevice);
				}
				else
				{
					printf("- FAILED to open\n");
					lErrorCount++;
				}
				szNames += (strlen(szNames) + 1);
			}
		}
	}
	else
	{
		printf("!!!ERROR!!! : ALC_ENUMERATION_EXT NOT FOUND!\n");
		lErrorCount++;
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Get Default Playback Device
	//
	printf("--------------------------------------\n");
	printf("TESTING GET DEFAULT PLAYBACK DEVICE\n\n");
	szNames = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
	if (szNames && strlen(szNames))
	{
		printf("\nDEFAULT DEVICE is %s\n", szNames);
	}
	else
	{
		if (waveOutGetNumDevs())
		{
			printf("\n!!!ERROR!!! DEFAULT DEVICE NOT FOUND!\n");
			lErrorCount++;
		}
		else
		{
			printf("\nDEFAULT DEVICE NOT FOUND!\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Enumerate all the capture devices
	//
	printf("--------------------------------------\n");
	printf("TESTING CAPTURE ENUMERATION EXTENSION\n\n");
	if (alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT") == AL_TRUE)
	{
		printf("ALC_ENUMERATION_EXT Capture Device List:-\n\n");

		szNames = alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER);
		if (strlen(szNames) == 0)
			printf("NO DEVICES FOUND\n");
		else
		{
			while (szNames && *szNames)
			{
				printf("%s ", szNames);
				// Try opening each device
				ALCdevice *pDevice = alcCaptureOpenDevice(szNames, 11025, AL_FORMAT_STEREO16, 8192);
				if (pDevice)
				{
					printf("- Opened Successfully\n");
					alcCaptureCloseDevice(pDevice);
				}
				else
				{
					printf("- FAILED to open\n");
					lErrorCount++;
				}
				szNames += (strlen(szNames) + 1);
			}
		}
	}
	else
	{
		printf("!!!ERROR!!! : ALC_ENUMERATION_EXT NOT FOUND!\n");
		lErrorCount++;
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Get Default Capture Device
	//
	printf("--------------------------------------\n");
	printf("TESTING DEFAULT CAPTURE DEVICE\n\n");
	szNames = alcGetString(NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
	if (szNames && strlen(szNames))
	{
		printf("\nDEFAULT CAPTURE DEVICE IS %s\n", szNames);
	}
	else
	{
		if (waveInGetNumDevs())
		{
			printf("\n!!!ERROR!!! DEFAULT CAPTURE DEVICE NOT FOUND!\n");
			lErrorCount++;
		}
		else
		{
			printf("\nDEFAULT CAPTURE DEVICE NOT FOUND!\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Enumerate *all* the playback devices
	//
	printf("--------------------------------------\n");
	printf("TESTING PLAYBACK ENUMERATE ALL EXTENSION\n\n");
	if (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT") == AL_TRUE)
	{
		printf("ALC_ENUMERATE_ALL_EXT DEVICE LIST:-\n\n");

		szNames = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
		if (strlen(szNames) == 0)
			printf("NO DEVICES FOUND\n");
		else
		{
			while (szNames && *szNames)
			{
				printf("%s ", szNames);

				// Try opening each device
				ALCdevice *pDevice = alcOpenDevice(szNames);
				if (pDevice)
				{
					printf("- Opened Successfully\n");
					alcCloseDevice(pDevice);
				}
				else
				{
					printf("- FAILED to open\n");
					lErrorCount++;
				}

				szNames += (strlen(szNames) + 1);
			}
		}
	}
	else
	{
		printf("!!!ERROR!!! : ALC_ENUMERATE_ALL_EXT NOT FOUND!\n");
		lErrorCount++;
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Get Default *All* Playback Device
	//
	printf("--------------------------------------\n");
	printf("TESTING DEFAULT ALL PLAYBACK DEVICE\n\n");
	szNames = alcGetString(NULL, ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
	if (szNames && strlen(szNames))
	{
		printf("\nDEFAULT ALL DEVICES IS %s\n", szNames);
	}
	else
	{
		if (waveOutGetNumDevs())
		{
			printf("\n!!!ERROR!!! DEFAULT ALL DEVICE NOT FOUND!\n");
			lErrorCount++;
		}
		else
		{
			printf("\nDEFAULT ALL DEVICES NOT FOUND!\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open 'Generic Hardware' device
	//
	printf("--------------------------------------\n");
	printf("TESTING 'Generic Hardware' DEVICE\n\n");
	ALCdevice *pDevice = alcOpenDevice("Generic Hardware");
	if (pDevice)
	{
		printf("OPENED 'Generic Hardware' DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_DEVICE_SPECIFIER));
		alcCloseDevice(pDevice);
	}
	else
	{
		if (waveOutGetNumDevs())
		{
			printf("!!!ERROR!!! : FAILED TO OPEN 'Generic Hardware' DEVICE\n");
			lErrorCount++;
		}
		else
		{
			printf("FAILED TO OPEN 'Generic Hardware' DEVICE\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open 'Generic Software' device
	//
	printf("--------------------------------------\n");
	printf("TESTING 'Generic Software' DEVICE\n\n");
	pDevice = alcOpenDevice("Generic Software");
	if (pDevice)
	{
		printf("OPENED 'Generic Software' DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_DEVICE_SPECIFIER));
		alcCloseDevice(pDevice);
	}
	else
	{
		if (waveOutGetNumDevs())
		{
			printf("!!!ERROR!!! : FAILED TO OPEN 'Generic Software' DEVICE\n");
			lErrorCount++;
		}
		else
		{
			printf("FAILED TO OPEN 'Generic Software' DEVICE\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////////
	// TEST : Open legacy 'DirectSound3D' device
	//
	printf("--------------------------------------\n");
	printf("TESTING LEGACY 'DirectSound3D' DEVICE\n\n");
	pDevice = alcOpenDevice("DirectSound3D");
	if (pDevice)
	{
		printf("OPENED 'DirectSound3D' DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_DEVICE_SPECIFIER));
		alcCloseDevice(pDevice);
	}
	else
	{
		if (waveOutGetNumDevs())
		{
			printf("!!!ERROR!!! : FAILED TO OPEN 'DirectSound3D' DEVICE\n");
			lErrorCount++;
		}
		else
		{
			printf("FAILED TO OPEN 'DirectSound3D' DEVICE\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open legacy 'DirectSound' device
	//
	printf("--------------------------------------\n");
	printf("TESTING LEGACY 'DirectSound' DEVICE\n\n");
	pDevice = alcOpenDevice("DirectSound");
	if (pDevice)
	{
		printf("OPENED 'DirectSound' DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_DEVICE_SPECIFIER));
		alcCloseDevice(pDevice);
	}
	else
	{
		if (waveOutGetNumDevs())
		{
			printf("!!!ERROR!!! : FAILED TO OPEN 'DirectSound' DEVICE\n");
			lErrorCount++;
		}
		else
		{
			printf("FAILED TO OPEN 'DirectSound' DEVICE\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open legacy 'MMSYSTEM' device
	//
	printf("--------------------------------------\n");
	printf("TESTING LEGACY 'MMSYSTEM' DEVICE\n\n");
	pDevice = alcOpenDevice("MMSYSTEM");
	if (pDevice)
	{
		printf("OPENED 'MMSYSTEM' DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_DEVICE_SPECIFIER));
		alcCloseDevice(pDevice);
	}
	else
	{
		if (waveOutGetNumDevs())
		{
			printf("!!!ERROR!!! : FAILED TO OPEN 'MMSYSTEM' DEVICE\n");
			lErrorCount++;
		}
		else
		{
			printf("FAILED TO OPEN 'MMSYSTEM' DEVICE\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open NULL device
	//
	printf("--------------------------------------\n");
	printf("TESTING NULL DEVICE\n\n");
	pDevice = alcOpenDevice(NULL);
	if (pDevice)
	{
		printf("OPENED NULL DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_DEVICE_SPECIFIER));
		alcCloseDevice(pDevice);
	}
	else
	{
		if (waveOutGetNumDevs())
		{
			printf("!!!ERROR!!! : FAILED TO OPEN NULL DEVICE\n");
			lErrorCount++;
		}
		else
		{
			printf("FAILED TO OPEN NULL DEVICE\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open "" device
	//
	printf("--------------------------------------\n");
	printf("TESTING EMPTY DEVICE\n\n");
	pDevice = alcOpenDevice("");
	if (pDevice)
	{
		printf("OPENED \"\" DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_DEVICE_SPECIFIER));
		alcCloseDevice(pDevice);
	}
	else
	{
		if (waveOutGetNumDevs())
		{
			printf("!!!ERROR!!! : FAILED TO OPEN EMPTY DEVICE\n");
			lErrorCount++;
		}
		else
		{
			printf("FAILED TO OPEN EMPTY DEVICE\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open "A Random Name" device
	//
	printf("--------------------------------------\n");
	printf("TESTING 'A Random Name' DEVICE\n\n");
	pDevice = alcOpenDevice("A Random Name");
	if (pDevice)
	{
		printf("!!!ERROR!!! : OPENED 'A Random Name' DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_DEVICE_SPECIFIER));
		lErrorCount++;
		alcCloseDevice(pDevice);
	}
	else
	{
		printf("FAILED TO OPEN 'A Random Name' DEVICE\n");
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open NULL Capture device
	//
	printf("--------------------------------------\n");
	printf("TESTING NULL CAPTURE DEVICE\n\n");
	pDevice = alcCaptureOpenDevice(NULL, 22500, AL_FORMAT_MONO16, 4096);
	if (pDevice)
	{
		printf("OPENED NULL CAPTURE DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_CAPTURE_DEVICE_SPECIFIER));
		alcCaptureCloseDevice(pDevice);
	}
	else
	{
		if (waveInGetNumDevs())
		{
			printf("!!!ERROR!!! : FAILED TO OPEN NULL CAPTURE DEVICE\n");
			lErrorCount++;
		}
		else
		{
			printf("FAILED TO OPEN NULL CAPTURE DEVICE\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open "" capture device
	//
	printf("--------------------------------------\n");
	printf("TESTING EMPTY CAPTURE DEVICE\n\n");
	pDevice = alcCaptureOpenDevice("", 22500, AL_FORMAT_MONO16, 4096);
	if (pDevice)
	{
		printf("OPENED \"\" CAPTURE DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_CAPTURE_DEVICE_SPECIFIER));
		alcCaptureCloseDevice(pDevice);
	}
	else
	{
		if (waveInGetNumDevs())
		{
			printf("!!!ERROR!!! : FAILED TO OPEN EMPTY CAPTURE DEVICE\n");
			lErrorCount++;
		}
		else
		{
			printf("FAILED TO OPEN EMPTY CAPTURE DEVICE\n");
		}
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////


	///////////////////////////////////////////////////////////////////
	// TEST : Open "A Random Name" capture device
	//
	printf("--------------------------------------\n");
	printf("TESTING 'A Random Name' CAPTURE DEVICE\n\n");
	pDevice = alcCaptureOpenDevice("A Random Name", 22500, AL_FORMAT_MONO16, 4096);
	if (pDevice)
	{
		printf("!!!ERROR!!! : OPENED 'A Random Name' CAPTURE DEVICE ... GOT %s\n", alcGetString(pDevice, ALC_CAPTURE_DEVICE_SPECIFIER));
		lErrorCount++;
		alcCaptureCloseDevice(pDevice);
	}
	else
	{
		printf("FAILED TO OPEN 'A Random Name' CAPTURE DEVICE\n");
	}
	printf("--------------------------------------\n\n");
	///////////////////////////////////////////////////////////////////

	printf("\nFOUND %d ERRORS\n", lErrorCount);

	printf("\nPress a key to quit\n");
	char ch = _getch();

	return 0;
}


/*
ALboolean InitOpenAL(ALCdevice **ppDevice, ALCcontext **ppContext)
{
	ALchar		szDeviceNames[10][1024];
	ALchar		*szNames;
	ALint		lNumDevices;
	ALint		lLoop;
	ALint		lLength;
	ALbyte		ch;
	ALboolean	bInit;

	bInit = AL_FALSE;
	memset(szDeviceNames, 0, sizeof(ALchar) * 10 * 1024);
	lNumDevices = 0;

	

	if (!szNames)
	{
		szNames = (char *)alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
		if (szNames && *szNames && (strlen(szNames) < 1024))
			strcpy(szDeviceNames[lNumDevices++], szNames);
	}

	for (lLoop = 0; lLoop < lNumDevices; lLoop++)
		printf("Press %d for '%s' Device\n", lLoop+1, szDeviceNames[lLoop]);
	printf("Press Q to quit\n");

	do
	{
		ch = _getch();
		if (ch == 'q' || ch == 'Q')
			exit(0);
		else if ((ch >= '1') && (ch < ('1' + lNumDevices)))
			break;
	} while (1);

	*ppDevice = alcOpenDevice(szDeviceNames[ch - '1']);
	if (*ppDevice)
	{
		*ppContext = alcCreateContext(*ppDevice,NULL);
		if (*ppContext)
		{
			alcGetError(*ppDevice);
			alcMakeContextCurrent(*ppContext);
			if (alcGetError(*ppDevice) == ALC_NO_ERROR)
				bInit = AL_TRUE;
		}
		else
		{
			alcCloseDevice(*ppDevice);
		}
	}

	return bInit;
}


ALboolean InitOpenALEx(ALCdevice **ppDevice, ALCcontext **ppContext)
{
	ALchar			szDeviceNames[32][1024];
	const ALchar	*szNames;
	const ALchar	*szDefaultName = NULL;
	const ALchar	*szDefaultAllName = NULL;
	ALint			lNumDevices = 0;
	ALint			lLoop;
	ALint			lLength;
	ALbyte			ch;
	ALboolean		bInit;

	bInit = AL_FALSE;
	memset(szDeviceNames, 0, sizeof(ALchar) * 32 * 1024);

	// Get legacy enumeration list and Default Device
	if (alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT") == AL_TRUE)
	{
		szNames = alcGetString(NULL, ALC_DEVICE_SPECIFIER);

		while (szNames && *szNames)
		{
			if ((lLength = strlen(szNames)) < 1024)
				strcpy(szDeviceNames[lNumDevices++], szNames);
			szNames += lLength + 1;
		}

		szDefaultName = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
	}

	// Get new uber enumeration list and Default Device
	if (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT") == AL_TRUE)
	{
		szNames = alcGetString(NULL, alcGetEnumValue(NULL, "ALC_ALL_DEVICES_SPECIFIER"));

		while (szNames && *szNames)
		{
			if ((lLength = strlen(szNames)) < 1024)
				strcpy(szDeviceNames[lNumDevices++], szNames);
			szNames += lLength + 1;
		}

		szDefaultAllName = alcGetString(NULL, alcGetEnumValue(NULL, "ALC_DEFAULT_ALL_DEVICES_SPECIFIER"));
	}


	for (lLoop = 0; lLoop < lNumDevices; lLoop++)
		printf("Press %d for '%s' Device %s\n", lLoop+1, szDeviceNames[lLoop],
		(strcmp(szDeviceNames[lLoop], szDefaultName?szDefaultName:"") == 0) ? "*" : (strcmp(szDeviceNames[lLoop], szDefaultAllName?szDefaultAllName:"") == 0) ? "**" : "");
	printf("Press Q to quit\n");

	do
	{
		ch = _getch();
		if (ch == 'q' || ch == 'Q')
			exit(0);
		else if ((ch >= '1') && (ch < ('1' + lNumDevices)))
			break;
	} while (1);

	*ppDevice = alcOpenDevice(szDeviceNames[ch - '1']);
	if (*ppDevice)
	{
		*ppContext = alcCreateContext(*ppDevice,NULL);
		if (*ppContext)
		{
			alcGetError(*ppDevice);
			alcMakeContextCurrent(*ppContext);
			if (alcGetError(*ppDevice) == ALC_NO_ERROR)
				bInit = AL_TRUE;
		}
		else
		{
			alcCloseDevice(*ppDevice);
		}
	}

	return bInit;
}
*/