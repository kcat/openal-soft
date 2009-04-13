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

#ifndef ALC_EXT_EFX
#define AL_FILTER_TYPE                                     0x8001
#define AL_EFFECT_TYPE                                     0x8001
#define AL_FILTER_NULL                                     0x0000
#define AL_FILTER_LOWPASS                                  0x0001
#define AL_FILTER_HIGHPASS                                 0x0002
#define AL_FILTER_BANDPASS                                 0x0003
#define AL_EFFECT_NULL                                     0x0000
#define AL_EFFECT_EAXREVERB                                0x8000
#define AL_EFFECT_REVERB                                   0x0001
#define AL_EFFECT_CHORUS                                   0x0002
#define AL_EFFECT_DISTORTION                               0x0003
#define AL_EFFECT_ECHO                                     0x0004
#define AL_EFFECT_FLANGER                                  0x0005
#define AL_EFFECT_FREQUENCY_SHIFTER                        0x0006
#define AL_EFFECT_VOCAL_MORPHER                            0x0007
#define AL_EFFECT_PITCH_SHIFTER                            0x0008
#define AL_EFFECT_RING_MODULATOR                           0x0009
#define AL_EFFECT_AUTOWAH                                  0x000A
#define AL_EFFECT_COMPRESSOR                               0x000B
#define AL_EFFECT_EQUALIZER                                0x000C
#define ALC_EFX_MAJOR_VERSION                              0x20001
#define ALC_EFX_MINOR_VERSION                              0x20002
#define ALC_MAX_AUXILIARY_SENDS                            0x20003
#endif
ALvoid (AL_APIENTRY *p_alGenFilters)(ALsizei,ALuint*);
ALvoid (AL_APIENTRY *p_alDeleteFilters)(ALsizei,ALuint*);
ALvoid (AL_APIENTRY *p_alFilteri)(ALuint,ALenum,ALint);
ALvoid (AL_APIENTRY *p_alGenEffects)(ALsizei,ALuint*);
ALvoid (AL_APIENTRY *p_alDeleteEffects)(ALsizei,ALuint*);
ALvoid (AL_APIENTRY *p_alEffecti)(ALuint,ALenum,ALint);

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
    alcGetIntegerv(device, ALC_MINOR_VERSION, 1, &minor);
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

static void printEFXInfo(void)
{
    ALCint major, minor, sends;
    ALCdevice *device;
    ALuint obj;
    int i;
    const struct {
        ALenum type;
        const char *name;
    } effects[] = {
        { AL_EFFECT_EAXREVERB,         "EAX Reverb"        },
        { AL_EFFECT_REVERB,            "Reverb"            },
        { AL_EFFECT_CHORUS,            "Chorus"            },
        { AL_EFFECT_DISTORTION,        "Distortion"        },
        { AL_EFFECT_ECHO,              "Echo"              },
        { AL_EFFECT_FLANGER,           "Flanger"           },
        { AL_EFFECT_FREQUENCY_SHIFTER, "Frequency Shifter" },
        { AL_EFFECT_VOCAL_MORPHER,     "Vocal Morpher"     },
        { AL_EFFECT_PITCH_SHIFTER,     "Pitch Shifter"     },
        { AL_EFFECT_RING_MODULATOR,    "Ring Modulator"    },
        { AL_EFFECT_AUTOWAH,           "Autowah"           },
        { AL_EFFECT_COMPRESSOR,        "Compressor"        },
        { AL_EFFECT_EQUALIZER,         "Equalizer"         },
        { AL_EFFECT_NULL, NULL }
    };
    const struct {
        ALenum type;
        const char *name;
    } filters[] = {
        { AL_FILTER_LOWPASS,  "Low-pass"  },
        { AL_FILTER_HIGHPASS, "High-pass" },
        { AL_FILTER_BANDPASS, "Band-pass" },
        { AL_FILTER_NULL, NULL }
    };

    device = alcGetContextsDevice(alcGetCurrentContext());

    if(alcIsExtensionPresent(device, (const ALCchar*)"ALC_EXT_EFX") == AL_FALSE)
    {
        printf("EFX not available\n");
        return;
    }

    alcGetIntegerv(device, ALC_EFX_MAJOR_VERSION, 1, &major);
    alcGetIntegerv(device, ALC_EFX_MINOR_VERSION, 1, &minor);
    checkForErrors();
    printf("EFX version: %d.%d\n", (int)major, (int)minor);

    alcGetIntegerv(device, ALC_MAX_AUXILIARY_SENDS, 1, &sends);
    checkForErrors();
    printf("Max auxiliary sends: %d\n", (int)sends);

    p_alGenFilters = alGetProcAddress("alGenFilters");
    p_alDeleteFilters = alGetProcAddress("alDeleteFilters");
    p_alFilteri = alGetProcAddress("alFilteri");
    p_alGenEffects = alGetProcAddress("alGenEffects");
    p_alDeleteEffects = alGetProcAddress("alDeleteEffects");
    p_alEffecti = alGetProcAddress("alEffecti");
    checkForErrors();
    if(!p_alGenEffects || !p_alDeleteEffects || !p_alEffecti ||
       !p_alGenFilters || !p_alDeleteFilters || !p_alFilteri)
    {
        printf("Missing EFX functions!\n");
        return;
    }

    p_alGenFilters(1, &obj);
    checkForErrors();
    printf("Available filters:\n");
    for(i = 0;filters[i].type != AL_FILTER_NULL;i++)
    {
        p_alFilteri(obj, AL_FILTER_TYPE, filters[i].type);
        if(alGetError() == AL_NO_ERROR)
            printf("    %s\n", filters[i].name);
    }
    p_alDeleteFilters(1, &obj);
    checkForErrors();

    p_alGenEffects(1, &obj);
    checkForErrors();
    printf("Available effects:\n");
    for(i = 0;effects[i].type != AL_EFFECT_NULL;i++)
    {
        p_alEffecti(obj, AL_EFFECT_TYPE, effects[i].type);
        if(alGetError() == AL_NO_ERROR)
            printf("    %s\n", effects[i].name);
    }
    p_alDeleteEffects(1, &obj);
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
    printEFXInfo();
    checkForErrors();

    alcMakeContextCurrent(NULL);
    alcDestroyContext(context);
    alcCloseDevice(device);

    return EXIT_SUCCESS;
}
