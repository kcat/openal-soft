/*
 * openal-info: Display information about ALC and AL.
 *
 * Idea based on glxinfo for OpenGL.
 * Initial OpenAL version by Erik Hofman <erik@ehofman.com>.
 * Further hacked by Sven Panne <sven.panne@aedion.de>.
 * More work (clean up) by Chris Robinson <chris.kcat@gmail.com>.
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"


static const int indentation = 4;
static const int maxmimumWidth = 79;

static void printChar(int c, int *width)
{
    putchar(c);
    *width = ((c == '\n') ? 0 : ((*width) + 1));
}

static void indent(int *width)
{
    int i;
    for(i = 0; i < indentation; i++)
        printChar(' ', width);
}

static void printExtensions(const char *header, char separator, const char *extensions)
{
    int width = 0, start = 0, end = 0;

    printf("%s:\n", header);
    if(extensions == NULL || extensions[0] == '\0')
        return;

    indent(&width);
    while (1)
    {
        if(extensions[end] == separator || extensions[end] == '\0')
        {
            if(width + end - start + 2 > maxmimumWidth)
            {
                printChar('\n', &width);
                indent(&width);
            }
            while(start < end)
            {
                printChar(extensions[start], &width);
                start++;
            }
            if(extensions[end] == '\0')
                break;
            start++;
            end++;
            if(extensions[end] == '\0')
                break;
            printChar(',', &width);
            printChar(' ', &width);
        }
        end++;
    }
    printChar('\n', &width);
}

static void die(const char *kind, const char *description)
{
    fprintf(stderr, "%s error %s occured\n", kind, description);
    exit(EXIT_FAILURE);
}

static void checkForErrors(void)
{
    {
        ALCdevice *device = alcGetContextsDevice(alcGetCurrentContext());
        ALCenum error = alcGetError(device);
        if(error != ALC_NO_ERROR)
            die("ALC", (const char*)alcGetString(device, error));
    }
    {
        ALenum error = alGetError();
        if(error != AL_NO_ERROR)
            die("AL", (const char*)alGetString(error));
    }
}

static void printDevices(ALCenum which, const char *kind)
{
    const char *s = alcGetString(NULL, which);
    checkForErrors();

    printf("Available %sdevices:\n", kind);
    while(*s != '\0')
    {
        printf("    %s\n", s);
        while(*s++ != '\0')
            ;
    }
}

static void printALCInfo (void)
{
    ALCint major, minor;
    ALCdevice *device;

    if(alcIsExtensionPresent(NULL, (const ALCchar*)"ALC_ENUMERATION_EXT") == AL_TRUE)
    {
        if(alcIsExtensionPresent(NULL, (const ALCchar*)"ALC_ENUMERATE_ALL_EXT") == AL_TRUE)
            printDevices(ALC_ALL_DEVICES_SPECIFIER, "playback ");
        else
            printDevices(ALC_DEVICE_SPECIFIER, "playback ");
        printDevices(ALC_CAPTURE_DEVICE_SPECIFIER, "capture ");
    }
    else
        printf("No device enumeration available\n");

    device = alcGetContextsDevice(alcGetCurrentContext());
    checkForErrors();

    printf("Default device: %s\n",
           alcGetString(device, ALC_DEFAULT_DEVICE_SPECIFIER));

    printf("Default capture device: %s\n",
           alcGetString(device, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER));

    alcGetIntegerv(device, ALC_MAJOR_VERSION, 1, &major);
    alcGetIntegerv(device, ALC_MAJOR_VERSION, 1, &minor);
    checkForErrors();
    printf("ALC version: %d.%d\n", (int)major, (int)minor);

    printExtensions("ALC extensions", ' ',
                    alcGetString(device, ALC_EXTENSIONS));
    checkForErrors();
}

static void printALInfo(void)
{
    printf("OpenAL vendor string: %s\n", alGetString(AL_VENDOR));
    printf("OpenAL renderer string: %s\n", alGetString(AL_RENDERER));
    printf("OpenAL version string: %s\n", alGetString(AL_VERSION));
    printExtensions("OpenAL extensions", ' ', alGetString(AL_EXTENSIONS));
    checkForErrors();
}

int main()
{
    ALCdevice *device = alcOpenDevice(NULL);
    ALCcontext *context = alcCreateContext(device, NULL);
    alcMakeContextCurrent(context);
    checkForErrors();

    printALCInfo();
    printALInfo();
    checkForErrors();

    alcMakeContextCurrent(NULL);
    alcDestroyContext(context);
    alcCloseDevice(device);

    return EXIT_SUCCESS;
}
