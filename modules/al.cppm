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

/* The AL module provides core functionality of the AL API, without any non-
 * standard extensions.
 *
 * There are some limitations with the AL module. Stuff like AL_API and
 * AL_APIENTRY can't be used by code importing it since macros can't be
 * exported from modules, and there's no way to make aliases for these
 * properties that can be exported. Luckily AL_API isn't typically needed by
 * user code since it's used to indicate functions as being imported from the
 * library, which is only relevant to the declarations made in the module
 * itself.
 *
 * AL_APIENTRY is similarly typically only needed for specifying the calling
 * convention for functions and function pointers declared in the module.
 * However, some extensions use callbacks that need user code to define
 * functions with the same calling convention. Currently this is set to use the
 * platform's default calling convention (that is, it's defined to nothing),
 * except on Windows where it's defined to __cdecl. Interestingly, capture-less
 * lambdas seem to generate conversion operators that match function pointers
 * of any calling convention, but short of that, the user will be responsible
 * for ensuring callbacks use the cdecl calling convention on Windows and the
 * default for other OSs.
 *
 * Additionally, enums are declared as global inline constexpr ints. This
 * should generally be fine, as long as user code doesn't try to use them in
 * the preprocessor which will no longer recognize or expand them to integer
 * literals. Being global ints also defines them as actual objects stored in
 * memory, lvalues whose addresses can be taken, instead of as integer literals
 * or prvalues, which may have subtle implications. An unnamed enum would be
 * better here, since the enumerators associate a value with a name and don't
 * become referenceable objects in memory, except that gives the name a new
 * type (e.g. typeid(AL_NO_ERROR) != typeid(int)) which could create problems
 * for type deduction.
 *
 * Note that defining AL_LIBTYPE_STATIC, AL_DISABLE_NOEXCEPT, and/or
 * AL_NO_PROTOTYPES does still influence the function and function pointer type
 * declarations, but only when compiling the module. The user-defined macros
 * have no effect when importing the module.
 */


module;

#ifndef AL_API
 #if defined(AL_LIBTYPE_STATIC)
  #define AL_API
 #elif defined(_WIN32)
  #define AL_API __declspec(dllimport)
 #else
  #define AL_API extern
 #endif
#endif

#ifdef _WIN32
 #define AL_APIENTRY __cdecl
#else
 #define AL_APIENTRY
#endif

#ifndef AL_DISABLE_NOEXCEPT
 #define AL_API_NOEXCEPT noexcept
#else
 #define AL_API_NOEXCEPT
#endif

export module openal.al;

export extern "C" {

/** 8-bit boolean */
using ALboolean = char;

/** character */
using ALchar = char;

/** signed 8-bit integer */
using ALbyte = signed char;

/** unsigned 8-bit integer */
using ALubyte = unsigned char;

/** signed 16-bit integer */
using ALshort = short;

/** unsigned 16-bit integer */
using ALushort = unsigned short;

/** signed 32-bit integer */
using ALint = int;

/** unsigned 32-bit integer */
using ALuint = unsigned int;

/** non-negative 32-bit integer size */
using ALsizei = int;

/** 32-bit enumeration value */
using ALenum = int;

/** 32-bit IEEE-754 floating-point */
using ALfloat = float;

/** 64-bit IEEE-754 floating-point */
using ALdouble = double;

/** void type (opaque pointers only) */
using ALvoid = void;


/* Enumeration values begin at column 50. Do not use tabs. */
#define ENUMDCL inline constexpr auto

/** No distance model or no buffer */
ENUMDCL AL_NONE =                                0;

/** Boolean False. */
ENUMDCL AL_FALSE =                               0;

/** Boolean True. */
ENUMDCL AL_TRUE =                                1;


/**
 * Relative source.
 * Type:    ALboolean
 * Range:   [AL_FALSE, AL_TRUE]
 * Default: AL_FALSE
 *
 * Specifies if the source uses relative coordinates.
 */
ENUMDCL AL_SOURCE_RELATIVE =                     0x202;


/**
 * Inner cone angle, in degrees.
 * Type:    ALint, ALfloat
 * Range:   [0 - 360]
 * Default: 360
 *
 * The angle covered by the inner cone, the area within which the source will
 * not be attenuated by direction.
 */
ENUMDCL AL_CONE_INNER_ANGLE =                    0x1001;

/**
 * Outer cone angle, in degrees.
 * Range:   [0 - 360]
 * Default: 360
 *
 * The angle covered by the outer cone, the area outside of which the source
 * will be fully attenuated by direction.
 */
ENUMDCL AL_CONE_OUTER_ANGLE =                    0x1002;

/**
 * Source pitch.
 * Type:    ALfloat
 * Range:   [0.5 - 2.0]
 * Default: 1.0
 *
 * A multiplier for the sample rate of the source's buffer.
 */
ENUMDCL AL_PITCH =                               0x1003;

/**
 * Source or listener position.
 * Type:    ALfloat[3], ALint[3]
 * Default: {0, 0, 0}
 *
 * The source or listener location in three dimensional space.
 *
 * OpenAL uses a right handed coordinate system, like OpenGL, where with a
 * default view, X points right (thumb), Y points up (index finger), and Z
 * points towards the viewer/camera (middle finger).
 *
 * To change from or to a left handed coordinate system, negate the Z
 * component.
 */
ENUMDCL AL_POSITION =                            0x1004;

/**
 * Source direction.
 * Type:    ALfloat[3], ALint[3]
 * Default: {0, 0, 0}
 *
 * Specifies the current direction in local space. A zero-length vector
 * specifies an omni-directional source (cone is ignored).
 *
 * To change from or to a left handed coordinate system, negate the Z
 * component.
 */
ENUMDCL AL_DIRECTION =                           0x1005;

/**
 * Source or listener velocity.
 * Type:    ALfloat[3], ALint[3]
 * Default: {0, 0, 0}
 *
 * Specifies the current velocity in units per second, relative to the
 * position.
 *
 * To change from or to a left handed coordinate system, negate the Z
 * component.
 */
ENUMDCL AL_VELOCITY =                            0x1006;

/**
 * Source looping.
 * Type:    ALboolean
 * Range:   [AL_FALSE, AL_TRUE]
 * Default: AL_FALSE
 *
 * Specifies whether source playback loops.
 */
ENUMDCL AL_LOOPING =                             0x1007;

/**
 * Source buffer.
 * Type:    ALuint
 * Range:   any valid Buffer ID
 * Default: AL_NONE
 *
 * Specifies the buffer to provide sound samples for a source.
 */
ENUMDCL AL_BUFFER =                              0x1009;

/**
 * Source or listener gain.
 * Type:  ALfloat
 * Range: [0.0 - ]
 *
 * For sources, an initial linear gain value (before attenuation is applied).
 * For the listener, an output linear gain adjustment.
 *
 * A value of 1.0 means unattenuated. Each division by 2 equals an attenuation
 * of about -6dB. Each multiplication by 2 equals an amplification of about
 * +6dB.
 */
ENUMDCL AL_GAIN =                                0x100A;

/**
 * Minimum source gain.
 * Type:  ALfloat
 * Range: [0.0 - 1.0]
 *
 * The minimum gain allowed for a source, after distance and cone attenuation
 * are applied (if applicable).
 */
ENUMDCL AL_MIN_GAIN =                            0x100D;

/**
 * Maximum source gain.
 * Type:  ALfloat
 * Range: [0.0 - 1.0]
 *
 * The maximum gain allowed for a source, after distance and cone attenuation
 * are applied (if applicable).
 */
ENUMDCL AL_MAX_GAIN =                            0x100E;

/**
 * Listener orientation.
 * Type:    ALfloat[6]
 * Default: {0.0, 0.0, -1.0, 0.0, 1.0, 0.0}
 *
 * Effectively two three dimensional vectors. The first vector is the front (or
 * "at") and the second is the top (or "up"). Both vectors are relative to the
 * listener position.
 *
 * To change from or to a left handed coordinate system, negate the Z component
 * of both vectors.
 */
ENUMDCL AL_ORIENTATION =                         0x100F;

/**
 * Source state (query only).
 * Type:  ALenum
 * Range: [AL_INITIAL, AL_PLAYING, AL_PAUSED, AL_STOPPED]
 */
ENUMDCL AL_SOURCE_STATE =                        0x1010;

/* Source state values. */
ENUMDCL AL_INITIAL =                             0x1011;
ENUMDCL AL_PLAYING =                             0x1012;
ENUMDCL AL_PAUSED =                              0x1013;
ENUMDCL AL_STOPPED =                             0x1014;

/**
 * Source Buffer Queue size (query only).
 * Type: ALint
 *
 * The number of buffers queued using alSourceQueueBuffers, minus the buffers
 * removed with alSourceUnqueueBuffers.
 */
ENUMDCL AL_BUFFERS_QUEUED =                      0x1015;

/**
 * Source Buffer Queue processed count (query only).
 * Type: ALint
 *
 * The number of queued buffers that have been fully processed, and can be
 * removed with alSourceUnqueueBuffers.
 *
 * Looping sources will never fully process buffers because they will be set to
 * play again for when the source loops.
 */
ENUMDCL AL_BUFFERS_PROCESSED =                   0x1016;

/**
 * Source reference distance.
 * Type:    ALfloat
 * Range:   [0.0 - ]
 * Default: 1.0
 *
 * The distance in units that no distance attenuation occurs.
 *
 * At 0.0, no distance attenuation occurs with non-linear attenuation models.
 */
ENUMDCL AL_REFERENCE_DISTANCE =                  0x1020;

/**
 * Source rolloff factor.
 * Type:    ALfloat
 * Range:   [0.0 - ]
 * Default: 1.0
 *
 * Multiplier to exaggerate or diminish distance attenuation.
 *
 * At 0.0, no distance attenuation ever occurs.
 */
ENUMDCL AL_ROLLOFF_FACTOR =                      0x1021;

/**
 * Outer cone gain.
 * Type:    ALfloat
 * Range:   [0.0 - 1.0]
 * Default: 0.0
 *
 * The gain attenuation applied when the listener is outside of the source's
 * outer cone angle.
 */
ENUMDCL AL_CONE_OUTER_GAIN =                     0x1022;

/**
 * Source maximum distance.
 * Type:    ALfloat
 * Range:   [0.0 - ]
 * Default: FLT_MAX
 *
 * The distance above which the source is not attenuated any further with a
 * clamped distance model, or where attenuation reaches 0.0 gain for linear
 * distance models with a default rolloff factor.
 */
ENUMDCL AL_MAX_DISTANCE =                        0x1023;

/** Source buffer offset, in seconds */
ENUMDCL AL_SEC_OFFSET =                          0x1024;
/** Source buffer offset, in sample frames */
ENUMDCL AL_SAMPLE_OFFSET =                       0x1025;
/** Source buffer offset, in bytes */
ENUMDCL AL_BYTE_OFFSET =                         0x1026;

/**
 * Source type (query only).
 * Type:  ALenum
 * Range: [AL_STATIC, AL_STREAMING, AL_UNDETERMINED]
 *
 * A Source is Static if a Buffer has been attached using AL_BUFFER.
 *
 * A Source is Streaming if one or more Buffers have been attached using
 * alSourceQueueBuffers.
 *
 * A Source is Undetermined when it has the NULL buffer attached using
 * AL_BUFFER.
 */
ENUMDCL AL_SOURCE_TYPE =                         0x1027;

/* Source type values. */
ENUMDCL AL_STATIC =                              0x1028;
ENUMDCL AL_STREAMING =                           0x1029;
ENUMDCL AL_UNDETERMINED =                        0x1030;

/** Unsigned 8-bit mono buffer format. */
ENUMDCL AL_FORMAT_MONO8 =                        0x1100;
/** Signed 16-bit mono buffer format. */
ENUMDCL AL_FORMAT_MONO16 =                       0x1101;
/** Unsigned 8-bit stereo buffer format. */
ENUMDCL AL_FORMAT_STEREO8 =                      0x1102;
/** Signed 16-bit stereo buffer format. */
ENUMDCL AL_FORMAT_STEREO16 =                     0x1103;

/** Buffer frequency/sample rate (query only). */
ENUMDCL AL_FREQUENCY =                           0x2001;
/** Buffer bits per sample (query only). */
ENUMDCL AL_BITS =                                0x2002;
/** Buffer channel count (query only). */
ENUMDCL AL_CHANNELS =                            0x2003;
/** Buffer data size in bytes (query only). */
ENUMDCL AL_SIZE =                                0x2004;


/** No error. */
ENUMDCL AL_NO_ERROR =                            0;

/** Invalid name (ID) passed to an AL call. */
ENUMDCL AL_INVALID_NAME =                        0xA001;

/** Invalid enumeration passed to AL call. */
ENUMDCL AL_INVALID_ENUM =                        0xA002;

/** Invalid value passed to AL call. */
ENUMDCL AL_INVALID_VALUE =                       0xA003;

/** Illegal AL call. */
ENUMDCL AL_INVALID_OPERATION =                   0xA004;

/** Not enough memory to execute the AL call. */
ENUMDCL AL_OUT_OF_MEMORY =                       0xA005;


/** Context string: Vendor name. */
ENUMDCL AL_VENDOR =                              0xB001;
/** Context string: Version. */
ENUMDCL AL_VERSION =                             0xB002;
/** Context string: Renderer name. */
ENUMDCL AL_RENDERER =                            0xB003;
/** Context string: Space-separated extension list. */
ENUMDCL AL_EXTENSIONS =                          0xB004;

/**
 * Doppler scale.
 * Type:    ALfloat
 * Range:   [0.0 - ]
 * Default: 1.0
 *
 * Scale for source and listener velocities.
 */
ENUMDCL AL_DOPPLER_FACTOR =                      0xC000;

/**
 * Doppler velocity (deprecated).
 *
 * A multiplier applied to the Speed of Sound.
 */
ENUMDCL AL_DOPPLER_VELOCITY =                    0xC001;

/**
 * Speed of Sound, in units per second.
 * Type:    ALfloat
 * Range:   [0.0001 - ]
 * Default: 343.3
 *
 * The speed at which sound waves are assumed to travel, in units per second,
 * when calculating the doppler effect from source and listener velocities.
 */
ENUMDCL AL_SPEED_OF_SOUND =                      0xC003;

/**
 * Distance attenuation model.
 * Type:    ALenum
 * Range:   [AL_NONE, AL_INVERSE_DISTANCE, AL_INVERSE_DISTANCE_CLAMPED,
 *           AL_LINEAR_DISTANCE, AL_LINEAR_DISTANCE_CLAMPED,
 *           AL_EXPONENT_DISTANCE, AL_EXPONENT_DISTANCE_CLAMPED]
 * Default: AL_INVERSE_DISTANCE_CLAMPED
 *
 * The model by which sources attenuate with distance.
 *
 * None     - No distance attenuation.
 * Inverse  - Doubling the distance halves the source gain.
 * Linear   - Linear gain scaling between the reference and max distances.
 * Exponent - Exponential gain dropoff.
 *
 * Clamped variations work like the non-clamped counterparts, except the
 * distance calculated is clamped between the reference and max distances.
 */
ENUMDCL AL_DISTANCE_MODEL =                      0xD000;

/* Distance model values. */
ENUMDCL AL_INVERSE_DISTANCE =                    0xD001;
ENUMDCL AL_INVERSE_DISTANCE_CLAMPED =            0xD002;
ENUMDCL AL_LINEAR_DISTANCE =                     0xD003;
ENUMDCL AL_LINEAR_DISTANCE_CLAMPED =             0xD004;
ENUMDCL AL_EXPONENT_DISTANCE =                   0xD005;
ENUMDCL AL_EXPONENT_DISTANCE_CLAMPED =           0xD006;

/* Deprecated enums. */
ENUMDCL AL_INVALID [[deprecated]] =              -1;
ENUMDCL AL_ILLEGAL_ENUM [[deprecated]] =         AL_INVALID_ENUM;
ENUMDCL AL_ILLEGAL_COMMAND [[deprecated]] =      AL_INVALID_OPERATION;
#undef ENUMDCL


#ifndef AL_NO_PROTOTYPES
/* Renderer State management. */
AL_API void AL_APIENTRY alEnable(ALenum capability) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alDisable(ALenum capability) AL_API_NOEXCEPT;
AL_API auto AL_APIENTRY alIsEnabled(ALenum capability) AL_API_NOEXCEPT -> ALboolean;

/* Context state setting. */
AL_API void AL_APIENTRY alDopplerFactor(ALfloat value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alDopplerVelocity(ALfloat value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSpeedOfSound(ALfloat value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alDistanceModel(ALenum distanceModel) AL_API_NOEXCEPT;

/* Context state retrieval. */
AL_API auto AL_APIENTRY alGetString(ALenum param) AL_API_NOEXCEPT -> const ALchar*;
AL_API void AL_APIENTRY alGetBooleanv(ALenum param, ALboolean *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetIntegerv(ALenum param, ALint *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetFloatv(ALenum param, ALfloat *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetDoublev(ALenum param, ALdouble *values) AL_API_NOEXCEPT;
AL_API auto AL_APIENTRY alGetBoolean(ALenum param) AL_API_NOEXCEPT -> ALboolean;
AL_API auto AL_APIENTRY alGetInteger(ALenum param) AL_API_NOEXCEPT -> ALint;
AL_API auto AL_APIENTRY alGetFloat(ALenum param) AL_API_NOEXCEPT -> ALfloat;
AL_API auto AL_APIENTRY alGetDouble(ALenum param) AL_API_NOEXCEPT -> ALdouble;

/**
 * Obtain the first error generated in the AL context since the last call to
 * this function.
 */
AL_API auto AL_APIENTRY alGetError() AL_API_NOEXCEPT -> ALenum;

/** Query for the presence of an extension on the AL context. */
AL_API auto AL_APIENTRY alIsExtensionPresent(const ALchar *extname) AL_API_NOEXCEPT -> ALboolean;
/**
 * Retrieve the address of a function. The returned function may be context-
 * specific.
 */
AL_API auto AL_APIENTRY alGetProcAddress(const ALchar *fname) AL_API_NOEXCEPT -> void*;
/**
 * Retrieve the value of an enum. The returned value may be context-specific.
 */
AL_API auto AL_APIENTRY alGetEnumValue(const ALchar *ename) AL_API_NOEXCEPT -> ALenum;


/* Set listener parameters. */
AL_API void AL_APIENTRY alListenerf(ALenum param, ALfloat value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alListener3f(ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alListenerfv(ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alListeneri(ALenum param, ALint value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alListener3i(ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alListeneriv(ALenum param, const ALint *values) AL_API_NOEXCEPT;

/* Get listener parameters. */
AL_API void AL_APIENTRY alGetListenerf(ALenum param, ALfloat *value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetListener3f(ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetListenerfv(ALenum param, ALfloat *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetListeneri(ALenum param, ALint *value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetListener3i(ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetListeneriv(ALenum param, ALint *values) AL_API_NOEXCEPT;


/** Create source objects. */
AL_API void AL_APIENTRY alGenSources(ALsizei n, ALuint *sources) AL_API_NOEXCEPT;
/** Delete source objects. */
AL_API void AL_APIENTRY alDeleteSources(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
/** Verify an ID is for a valid source. */
AL_API auto AL_APIENTRY alIsSource(ALuint source) AL_API_NOEXCEPT -> ALboolean;

/* Set source parameters. */
AL_API void AL_APIENTRY alSourcef(ALuint source, ALenum param, ALfloat value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSource3f(ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSourcefv(ALuint source, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSourcei(ALuint source, ALenum param, ALint value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSource3i(ALuint source, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSourceiv(ALuint source, ALenum param, const ALint *values) AL_API_NOEXCEPT;

/* Get source parameters. */
AL_API void AL_APIENTRY alGetSourcef(ALuint source, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSource3f(ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSourcefv(ALuint source, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSourcei(ALuint source,  ALenum param, ALint *value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSource3i(ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSourceiv(ALuint source,  ALenum param, ALint *values) AL_API_NOEXCEPT;


/** Play, restart, or resume a source, setting its state to AL_PLAYING. */
AL_API void AL_APIENTRY alSourcePlay(ALuint source) AL_API_NOEXCEPT;
/** Stop a source, setting its state to AL_STOPPED if playing or paused. */
AL_API void AL_APIENTRY alSourceStop(ALuint source) AL_API_NOEXCEPT;
/** Rewind a source, setting its state to AL_INITIAL. */
AL_API void AL_APIENTRY alSourceRewind(ALuint source) AL_API_NOEXCEPT;
/** Pause a source, setting its state to AL_PAUSED if playing. */
AL_API void AL_APIENTRY alSourcePause(ALuint source) AL_API_NOEXCEPT;

/** Play, restart, or resume a list of sources atomically. */
AL_API void AL_APIENTRY alSourcePlayv(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
/** Stop a list of sources atomically. */
AL_API void AL_APIENTRY alSourceStopv(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
/** Rewind a list of sources atomically. */
AL_API void AL_APIENTRY alSourceRewindv(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
/** Pause a list of sources atomically. */
AL_API void AL_APIENTRY alSourcePausev(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;

/** Queue buffers onto a source */
AL_API void AL_APIENTRY alSourceQueueBuffers(ALuint source, ALsizei nb, const ALuint *buffers) AL_API_NOEXCEPT;
/** Unqueue processed buffers from a source */
AL_API void AL_APIENTRY alSourceUnqueueBuffers(ALuint source, ALsizei nb, ALuint *buffers) AL_API_NOEXCEPT;


/** Create buffer objects */
AL_API void AL_APIENTRY alGenBuffers(ALsizei n, ALuint *buffers) AL_API_NOEXCEPT;
/** Delete buffer objects */
AL_API void AL_APIENTRY alDeleteBuffers(ALsizei n, const ALuint *buffers) AL_API_NOEXCEPT;
/** Verify an ID is a valid buffer (including the NULL buffer) */
AL_API auto AL_APIENTRY alIsBuffer(ALuint buffer) AL_API_NOEXCEPT -> ALboolean;

/**
 * Copies data into the buffer, interpreting it using the specified format and
 * samplerate.
 */
AL_API void AL_APIENTRY alBufferData(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei samplerate) AL_API_NOEXCEPT;

/* Set buffer parameters. */
AL_API void AL_APIENTRY alBufferf(ALuint buffer, ALenum param, ALfloat value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alBuffer3f(ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alBufferfv(ALuint buffer, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alBufferi(ALuint buffer, ALenum param, ALint value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alBuffer3i(ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alBufferiv(ALuint buffer, ALenum param, const ALint *values) AL_API_NOEXCEPT;

/* Get buffer parameters. */
AL_API void AL_APIENTRY alGetBufferf(ALuint buffer, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetBuffer3f(ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetBufferfv(ALuint buffer, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetBufferi(ALuint buffer, ALenum param, ALint *value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetBuffer3i(ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetBufferiv(ALuint buffer, ALenum param, ALint *values) AL_API_NOEXCEPT;
#endif /* AL_NO_PROTOTYPES */

/* Pointer-to-function types, useful for storing dynamically loaded AL entry
 * points.
 */
using LPALENABLE =    void (AL_APIENTRY*)(ALenum capability) AL_API_NOEXCEPT;
using LPALDISABLE =   void (AL_APIENTRY*)(ALenum capability) AL_API_NOEXCEPT;
using LPALISENABLED = auto (AL_APIENTRY*)(ALenum capability) AL_API_NOEXCEPT -> ALboolean;
using LPALGETSTRING =   auto (AL_APIENTRY*)(ALenum param) AL_API_NOEXCEPT -> const ALchar*;
using LPALGETBOOLEANV = void (AL_APIENTRY*)(ALenum param, ALboolean *values) AL_API_NOEXCEPT;
using LPALGETINTEGERV = void (AL_APIENTRY*)(ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALGETFLOATV =   void (AL_APIENTRY*)(ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGETDOUBLEV =  void (AL_APIENTRY*)(ALenum param, ALdouble *values) AL_API_NOEXCEPT;
using LPALGETBOOLEAN =  auto (AL_APIENTRY*)(ALenum param) AL_API_NOEXCEPT -> ALboolean;
using LPALGETINTEGER =  auto (AL_APIENTRY*)(ALenum param) AL_API_NOEXCEPT -> ALint;
using LPALGETFLOAT =    auto (AL_APIENTRY*)(ALenum param) AL_API_NOEXCEPT -> ALfloat;
using LPALGETDOUBLE =   auto (AL_APIENTRY*)(ALenum param) AL_API_NOEXCEPT -> ALdouble;
using LPALGETERROR = auto (AL_APIENTRY*)() AL_API_NOEXCEPT -> ALenum;
using LPALISEXTENSIONPRESENT = auto (AL_APIENTRY*)(const ALchar *extname) AL_API_NOEXCEPT -> ALboolean;
using LPALGETPROCADDRESS =     auto (AL_APIENTRY*)(const ALchar *fname) AL_API_NOEXCEPT -> void*;
using LPALGETENUMVALUE =       auto (AL_APIENTRY*)(const ALchar *ename) AL_API_NOEXCEPT -> ALenum;
using LPALLISTENERF =     void (AL_APIENTRY*)(ALenum param, ALfloat value) AL_API_NOEXCEPT;
using LPALLISTENER3F =    void (AL_APIENTRY*)(ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
using LPALLISTENERFV =    void (AL_APIENTRY*)(ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
using LPALLISTENERI =     void (AL_APIENTRY*)(ALenum param, ALint value) AL_API_NOEXCEPT;
using LPALLISTENER3I =    void (AL_APIENTRY*)(ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
using LPALLISTENERIV =    void (AL_APIENTRY*)(ALenum param, const ALint *values) AL_API_NOEXCEPT;
using LPALGETLISTENERF =  void (AL_APIENTRY*)(ALenum param, ALfloat *value) AL_API_NOEXCEPT;
using LPALGETLISTENER3F = void (AL_APIENTRY*)(ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
using LPALGETLISTENERFV = void (AL_APIENTRY*)(ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGETLISTENERI =  void (AL_APIENTRY*)(ALenum param, ALint *value) AL_API_NOEXCEPT;
using LPALGETLISTENER3I = void (AL_APIENTRY*)(ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
using LPALGETLISTENERIV = void (AL_APIENTRY*)(ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALGENSOURCES =           void (AL_APIENTRY*)(ALsizei n, ALuint *sources) AL_API_NOEXCEPT;
using LPALDELETESOURCES =        void (AL_APIENTRY*)(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALISSOURCE =             auto (AL_APIENTRY*)(ALuint source) AL_API_NOEXCEPT -> ALboolean;
using LPALSOURCEF =              void (AL_APIENTRY*)(ALuint source, ALenum param, ALfloat value) AL_API_NOEXCEPT;
using LPALSOURCE3F =             void (AL_APIENTRY*)(ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
using LPALSOURCEFV =             void (AL_APIENTRY*)(ALuint source, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
using LPALSOURCEI =              void (AL_APIENTRY*)(ALuint source, ALenum param, ALint value) AL_API_NOEXCEPT;
using LPALSOURCE3I =             void (AL_APIENTRY*)(ALuint source, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
using LPALSOURCEIV =             void (AL_APIENTRY*)(ALuint source, ALenum param, const ALint *values) AL_API_NOEXCEPT;
using LPALGETSOURCEF =           void (AL_APIENTRY*)(ALuint source, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
using LPALGETSOURCE3F =          void (AL_APIENTRY*)(ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
using LPALGETSOURCEFV =          void (AL_APIENTRY*)(ALuint source, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGETSOURCEI =           void (AL_APIENTRY*)(ALuint source, ALenum param, ALint *value) AL_API_NOEXCEPT;
using LPALGETSOURCE3I =          void (AL_APIENTRY*)(ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
using LPALGETSOURCEIV =          void (AL_APIENTRY*)(ALuint source, ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALSOURCEPLAYV =          void (AL_APIENTRY*)(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALSOURCESTOPV =          void (AL_APIENTRY*)(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALSOURCEREWINDV =        void (AL_APIENTRY*)(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALSOURCEPAUSEV =         void (AL_APIENTRY*)(ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALSOURCEPLAY =           void (AL_APIENTRY*)(ALuint source) AL_API_NOEXCEPT;
using LPALSOURCESTOP =           void (AL_APIENTRY*)(ALuint source) AL_API_NOEXCEPT;
using LPALSOURCEREWIND =         void (AL_APIENTRY*)(ALuint source) AL_API_NOEXCEPT;
using LPALSOURCEPAUSE =          void (AL_APIENTRY*)(ALuint source) AL_API_NOEXCEPT;
using LPALSOURCEQUEUEBUFFERS =   void (AL_APIENTRY*)(ALuint source, ALsizei nb, const ALuint *buffers) AL_API_NOEXCEPT;
using LPALSOURCEUNQUEUEBUFFERS = void (AL_APIENTRY*)(ALuint source, ALsizei nb, ALuint *buffers) AL_API_NOEXCEPT;
using LPALGENBUFFERS =    void (AL_APIENTRY*)(ALsizei n, ALuint *buffers) AL_API_NOEXCEPT;
using LPALDELETEBUFFERS = void (AL_APIENTRY*)(ALsizei n, const ALuint *buffers) AL_API_NOEXCEPT;
using LPALISBUFFER =      auto (AL_APIENTRY*)(ALuint buffer) AL_API_NOEXCEPT -> ALboolean;
using LPALBUFFERDATA =    void (AL_APIENTRY*)(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei samplerate) AL_API_NOEXCEPT;
using LPALBUFFERF =       void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALfloat value) AL_API_NOEXCEPT;
using LPALBUFFER3F =      void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
using LPALBUFFERFV =      void (AL_APIENTRY*)(ALuint buffer, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
using LPALBUFFERI =       void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALint value) AL_API_NOEXCEPT;
using LPALBUFFER3I =      void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
using LPALBUFFERIV =      void (AL_APIENTRY*)(ALuint buffer, ALenum param, const ALint *values) AL_API_NOEXCEPT;
using LPALGETBUFFERF =    void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
using LPALGETBUFFER3F =   void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
using LPALGETBUFFERFV =   void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGETBUFFERI =    void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALint *value) AL_API_NOEXCEPT;
using LPALGETBUFFER3I =   void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
using LPALGETBUFFERIV =   void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALDOPPLERFACTOR =   void (AL_APIENTRY*)(ALfloat value) AL_API_NOEXCEPT;
using LPALDOPPLERVELOCITY = void (AL_APIENTRY*)(ALfloat value) AL_API_NOEXCEPT;
using LPALSPEEDOFSOUND =    void (AL_APIENTRY*)(ALfloat value) AL_API_NOEXCEPT;
using LPALDISTANCEMODEL =   void (AL_APIENTRY*)(ALenum distanceModel) AL_API_NOEXCEPT;

} /* extern "C" */
