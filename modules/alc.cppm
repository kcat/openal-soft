/* This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

/* This file is auto-generated! Please do not edit it manually.
 * Instead, modify the API in al.xml and regenerate using genheaders.py.
 *
 * Last regenerated: 2025-10-05 15:14:05.617838+00:00
 */

module;

#ifndef ALC_API
 #if defined(AL_LIBTYPE_STATIC)
  #define ALC_API
 #elif defined(_WIN32)
  #define ALC_API __declspec(dllimport)
 #else
  #define ALC_API extern
 #endif
#endif

#ifdef _WIN32
 #define ALC_APIENTRY __cdecl
#else
 #define ALC_APIENTRY
#endif

#ifndef AL_DISABLE_NOEXCEPT
 #define ALC_API_NOEXCEPT noexcept
#else
 #define ALC_API_NOEXCEPT
#endif

#define ENUMDCL inline constexpr auto

export module openal.alc;

export extern "C" {
/*** ALC_VERSION_1_0 ***/
/* Deprecated macros. */
/** Deprecated enum. */
ENUMDCL ALC_INVALID [[deprecated("Use 0 instead")]]  = 0;

/** Opaque device handle */
using ALCdevice = struct ALCdevice;

/** Opaque context handle */
using ALCcontext = struct ALCcontext;

/** 8-bit boolean */
using ALCboolean = char;

/** character */
using ALCchar = char;

/** signed 8-bit integer */
using ALCbyte = signed char;

/** unsigned 8-bit integer */
using ALCubyte = unsigned char;

/** signed 16-bit integer */
using ALCshort = short;

/** unsigned 16-bit integer */
using ALCushort = unsigned short;

/** signed 32-bit integer */
using ALCint = int;

/** unsigned 32-bit integer */
using ALCuint = unsigned int;

/** non-negative 32-bit integer size */
using ALCsizei = int;

/** 32-bit enumeration value */
using ALCenum = int;

/** 32-bit IEEE-754 floating-point */
using ALCfloat = float;

/** 64-bit IEEE-754 floating-point */
using ALCdouble = double;

/** void type (for opaque pointers only) */
using ALCvoid = void;

/** Boolean False. */
ENUMDCL ALC_FALSE =                              0;

/** Boolean True. */
ENUMDCL ALC_TRUE =                               1;

/** Context attribute: <int> Hz. */
ENUMDCL ALC_FREQUENCY =                          0x1007;

/** Context attribute: <int> Hz. */
ENUMDCL ALC_REFRESH =                            0x1008;

/** Context attribute: AL_TRUE or AL_FALSE synchronous context? */
ENUMDCL ALC_SYNC =                               0x1009;

/** No error. */
ENUMDCL ALC_NO_ERROR =                           0;

/** Invalid device handle. */
ENUMDCL ALC_INVALID_DEVICE =                     0xA001;

/** Invalid context handle. */
ENUMDCL ALC_INVALID_CONTEXT =                    0xA002;

/** Invalid enumeration passed to an ALC call. */
ENUMDCL ALC_INVALID_ENUM =                       0xA003;

/** Invalid value passed to an ALC call. */
ENUMDCL ALC_INVALID_VALUE =                      0xA004;

/** Out of memory. */
ENUMDCL ALC_OUT_OF_MEMORY =                      0xA005;

/** Runtime ALC major version. */
ENUMDCL ALC_MAJOR_VERSION =                      0x1000;

/** Runtime ALC minor version. */
ENUMDCL ALC_MINOR_VERSION =                      0x1001;

/** Context attribute list size. */
ENUMDCL ALC_ATTRIBUTES_SIZE =                    0x1002;

/** Context attribute list properties. */
ENUMDCL ALC_ALL_ATTRIBUTES =                     0x1003;

/** String for the default device specifier. */
ENUMDCL ALC_DEFAULT_DEVICE_SPECIFIER =           0x1004;

/**
 * Device specifier string.
 *
 * If device handle is NULL, it is instead a null-character separated list of
 * strings of known device specifiers (list ends with an empty string).
 */
ENUMDCL ALC_DEVICE_SPECIFIER =                   0x1005;

/** String for space-separated list of ALC extensions. */
ENUMDCL ALC_EXTENSIONS =                         0x1006;

#ifndef ALC_NO_PROTOTYPES
/* Context management. */
/** Create and attach a context to the given device. */
ALC_API auto ALC_APIENTRY alcCreateContext(ALCdevice *device, const ALCint *attrlist) ALC_API_NOEXCEPT -> ALCcontext*;

/**
 * Makes the given context the active process-wide context. Passing NULL clears
 * the active context.
 */
ALC_API auto ALC_APIENTRY alcMakeContextCurrent(ALCcontext *context) ALC_API_NOEXCEPT -> ALCboolean;

/** Resumes processing updates for the given context. */
ALC_API void ALC_APIENTRY alcProcessContext(ALCcontext *context) ALC_API_NOEXCEPT;

/** Suspends updates for the given context. */
ALC_API void ALC_APIENTRY alcSuspendContext(ALCcontext *context) ALC_API_NOEXCEPT;

/** Remove a context from its device and destroys it. */
ALC_API void ALC_APIENTRY alcDestroyContext(ALCcontext *context) ALC_API_NOEXCEPT;

/** Returns the currently active context. */
ALC_API auto ALC_APIENTRY alcGetCurrentContext(void) ALC_API_NOEXCEPT -> ALCcontext*;

/** Returns the device that a particular context is attached to. */
ALC_API auto ALC_APIENTRY alcGetContextsDevice(ALCcontext *context) ALC_API_NOEXCEPT -> ALCdevice*;

/* Device management. */
/** Opens the named playback device. */
ALC_API auto ALC_APIENTRY alcOpenDevice(const ALCchar *devicename) ALC_API_NOEXCEPT -> ALCdevice*;

/** Closes the given playback device. */
ALC_API auto ALC_APIENTRY alcCloseDevice(ALCdevice *device) ALC_API_NOEXCEPT -> ALCboolean;

/* Error support. */
/** Obtain the most recent Device error. */
ALC_API auto ALC_APIENTRY alcGetError(ALCdevice *device) ALC_API_NOEXCEPT -> ALCenum;

/* Extension support. */
/**
 * Query for the presence of an extension on the device. Pass a NULL device to
 * query a device-inspecific extension.
 */
ALC_API auto ALC_APIENTRY alcIsExtensionPresent(ALCdevice *device, const ALCchar *extname) ALC_API_NOEXCEPT -> ALCboolean;

/**
 * Retrieve the address of a function. Given a non-NULL device, the returned
 * function may be device-specific.
 */
ALC_API auto ALC_APIENTRY alcGetProcAddress(ALCdevice *device, const ALCchar *funcname) ALC_API_NOEXCEPT -> ALCvoid*;

/**
 * Retrieve the value of an enum. Given a non-NULL device, the returned value
 * may be device-specific.
 */
ALC_API auto ALC_APIENTRY alcGetEnumValue(ALCdevice *device, const ALCchar *enumname) ALC_API_NOEXCEPT -> ALCenum;

/* Query functions. */
/** Returns information about the device, and error strings. */
ALC_API auto ALC_APIENTRY alcGetString(ALCdevice *device, ALCenum param) ALC_API_NOEXCEPT -> const ALCchar*;

/** Returns information about the device and the version of OpenAL. */
ALC_API void ALC_APIENTRY alcGetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values) ALC_API_NOEXCEPT;

#endif /* ALC_NO_PROTOTYPES */

/* Pointer-to-function types, useful for storing dynamically loaded AL entry
 * points.
 */
using LPALCCREATECONTEXT = auto (ALC_APIENTRY*)(ALCdevice *device, const ALCint *attrlist) ALC_API_NOEXCEPT -> ALCcontext*;
using LPALCMAKECONTEXTCURRENT = auto (ALC_APIENTRY*)(ALCcontext *context) ALC_API_NOEXCEPT -> ALCboolean;
using LPALCPROCESSCONTEXT = void (ALC_APIENTRY*)(ALCcontext *context) ALC_API_NOEXCEPT;
using LPALCSUSPENDCONTEXT = void (ALC_APIENTRY*)(ALCcontext *context) ALC_API_NOEXCEPT;
using LPALCDESTROYCONTEXT = void (ALC_APIENTRY*)(ALCcontext *context) ALC_API_NOEXCEPT;
using LPALCGETCURRENTCONTEXT = auto (ALC_APIENTRY*)(void) ALC_API_NOEXCEPT -> ALCcontext*;
using LPALCGETCONTEXTSDEVICE = auto (ALC_APIENTRY*)(ALCcontext *context) ALC_API_NOEXCEPT -> ALCdevice*;

using LPALCOPENDEVICE = auto (ALC_APIENTRY*)(const ALCchar *devicename) ALC_API_NOEXCEPT -> ALCdevice*;
using LPALCCLOSEDEVICE = auto (ALC_APIENTRY*)(ALCdevice *device) ALC_API_NOEXCEPT -> ALCboolean;

using LPALCGETERROR = auto (ALC_APIENTRY*)(ALCdevice *device) ALC_API_NOEXCEPT -> ALCenum;

using LPALCISEXTENSIONPRESENT = auto (ALC_APIENTRY*)(ALCdevice *device, const ALCchar *extname) ALC_API_NOEXCEPT -> ALCboolean;
using LPALCGETPROCADDRESS = auto (ALC_APIENTRY*)(ALCdevice *device, const ALCchar *funcname) ALC_API_NOEXCEPT -> ALCvoid*;
using LPALCGETENUMVALUE = auto (ALC_APIENTRY*)(ALCdevice *device, const ALCchar *enumname) ALC_API_NOEXCEPT -> ALCenum;

using LPALCGETSTRING = auto (ALC_APIENTRY*)(ALCdevice *device, ALCenum param) ALC_API_NOEXCEPT -> const ALCchar*;
using LPALCGETINTEGERV = void (ALC_APIENTRY*)(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values) ALC_API_NOEXCEPT;

/*** ALC_VERSION_1_1 ***/
/** Context attribute: <int> requested Mono (3D) Sources. */
ENUMDCL ALC_MONO_SOURCES =                       0x1010;

/** Context attribute: <int> requested Stereo Sources. */
ENUMDCL ALC_STEREO_SOURCES =                     0x1011;

/**
 * Capture specifier string.
 *
 * If device handle is NULL, it is instead a null-character separated list of
 * strings of known device specifiers (list ends with an empty string).
 */
ENUMDCL ALC_CAPTURE_DEVICE_SPECIFIER =           0x310;

/** String for the default capture device specifier. */
ENUMDCL ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER =   0x311;

/** Number of sample frames available for capture. */
ENUMDCL ALC_CAPTURE_SAMPLES =                    0x312;

/** String for the default extended device specifier. */
ENUMDCL ALC_DEFAULT_ALL_DEVICES_SPECIFIER =      0x1012;

/**
 * Device's extended specifier string.
 *
 * If device handle is NULL, it is instead a null-character separated list of
 * strings of known extended device specifiers (list ends with an empty string).
 */
ENUMDCL ALC_ALL_DEVICES_SPECIFIER =              0x1013;

#ifndef ALC_NO_PROTOTYPES
/**
 * Opens the named capture device with the given frequency, format, and buffer
 * size.
 */
ALC_API auto ALC_APIENTRY alcCaptureOpenDevice(const ALCchar *devicename, ALCuint frequency, ALCenum format, ALCsizei buffersize) ALC_API_NOEXCEPT -> ALCdevice*;

/** Closes the given capture device. */
ALC_API auto ALC_APIENTRY alcCaptureCloseDevice(ALCdevice *device) ALC_API_NOEXCEPT -> ALCboolean;

/** Starts capturing samples into the device buffer. */
ALC_API void ALC_APIENTRY alcCaptureStart(ALCdevice *device) ALC_API_NOEXCEPT;

/** Stops capturing samples. Samples in the device buffer remain available. */
ALC_API void ALC_APIENTRY alcCaptureStop(ALCdevice *device) ALC_API_NOEXCEPT;

/** Reads samples from the device buffer. */
ALC_API void ALC_APIENTRY alcCaptureSamples(ALCdevice *device, ALCvoid *buffer, ALCsizei samples) ALC_API_NOEXCEPT;

#endif /* ALC_NO_PROTOTYPES */

/* Pointer-to-function types, useful for storing dynamically loaded AL entry
 * points.
 */
using LPALCCAPTUREOPENDEVICE = auto (ALC_APIENTRY*)(const ALCchar *devicename, ALCuint frequency, ALCenum format, ALCsizei buffersize) ALC_API_NOEXCEPT -> ALCdevice*;
using LPALCCAPTURECLOSEDEVICE = auto (ALC_APIENTRY*)(ALCdevice *device) ALC_API_NOEXCEPT -> ALCboolean;
using LPALCCAPTURESTART = void (ALC_APIENTRY*)(ALCdevice *device) ALC_API_NOEXCEPT;
using LPALCCAPTURESTOP = void (ALC_APIENTRY*)(ALCdevice *device) ALC_API_NOEXCEPT;
using LPALCCAPTURESAMPLES = void (ALC_APIENTRY*)(ALCdevice *device, ALCvoid *buffer, ALCsizei samples) ALC_API_NOEXCEPT;


} /* extern "C" */
