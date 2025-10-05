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

/* The ALEXT module provides the interfaces for known extensions to the ALC and
 * AL APIs.
 *
 * Note that because macros aren't exported from modules, it is not possible to
 * test the existence of an extension interface with a preprocessor check (e.g.
 * #ifdef AL_EXT_MCFORMATS will fail). However, as long as this file isn't
 * downgraded, no interfaces should be removed so what's here will remain here
 * as it updates.
 */

module;

#include <array>
#include <cstddef>
#include <cstdint>

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

#ifndef AL_API
 #define AL_API ALC_API
#endif
#define AL_APIENTRY ALC_APIENTRY
#define AL_API_NOEXCEPT ALC_API_NOEXCEPT

export module openal.ext;

export import openal.efx;

import openal.std;

extern "C" struct _GUID; /* NOLINT(*-reserved-identifier) */

export extern "C" {

using ALCint64SOFT = std::int64_t;
using ALCuint64SOFT = std::uint64_t;

using ALint64SOFT = std::int64_t;
using ALuint64SOFT = std::uint64_t;


/* Enumeration values begin at column 50. Do not use tabs. */
#define ENUMDCL inline constexpr auto

/*** AL_LOKI_IMA_ADPCM_format ***/
ENUMDCL AL_FORMAT_IMA_ADPCM_MONO16_EXT =         0x10000;
ENUMDCL AL_FORMAT_IMA_ADPCM_STEREO16_EXT =       0x10001;

/*** AL_LOKI_WAVE_format ***/
ENUMDCL AL_FORMAT_WAVE_EXT =                     0x10002;

/*** AL_EXT_vorbis ***/
ENUMDCL AL_FORMAT_VORBIS_EXT =                   0x10003;

/*** AL_LOKI_quadriphonic ***/
ENUMDCL AL_FORMAT_QUAD8_LOKI =                   0x10004;
ENUMDCL AL_FORMAT_QUAD16_LOKI =                  0x10005;

/*** AL_EXT_float32 ***/
ENUMDCL AL_FORMAT_MONO_FLOAT32 =                 0x10010;
ENUMDCL AL_FORMAT_STEREO_FLOAT32 =               0x10011;

/*** AL_EXT_double ***/
ENUMDCL AL_FORMAT_MONO_DOUBLE_EXT =              0x10012;
ENUMDCL AL_FORMAT_STEREO_DOUBLE_EXT =            0x10013;

/*** AL_EXT_MULAW ***/
ENUMDCL AL_FORMAT_MONO_MULAW_EXT =               0x10014;
ENUMDCL AL_FORMAT_STEREO_MULAW_EXT =             0x10015;

/*** AL_EXT_ALAW ***/
ENUMDCL AL_FORMAT_MONO_ALAW_EXT =                0x10016;
ENUMDCL AL_FORMAT_STEREO_ALAW_EXT =              0x10017;

/*** ALC_LOKI_audio_channel ***/
ENUMDCL ALC_CHAN_MAIN_LOKI =                     0x500001;
ENUMDCL ALC_CHAN_PCM_LOKI =                      0x500002;
ENUMDCL ALC_CHAN_CD_LOKI =                       0x500003;

/*** AL_EXT_MCFORMATS ***/
/* Provides support for surround sound buffer formats with 8, 16, and 32-bit
 * samples.
 *
 * QUAD8: Unsigned 8-bit, Quadraphonic (Front Left, Front Right, Rear Left,
 *        Rear Right).
 * QUAD16: Signed 16-bit, Quadraphonic.
 * QUAD32: 32-bit float, Quadraphonic.
 * REAR8: Unsigned 8-bit, Rear Stereo (Rear Left, Rear Right).
 * REAR16: Signed 16-bit, Rear Stereo.
 * REAR32: 32-bit float, Rear Stereo.
 * 51CHN8: Unsigned 8-bit, 5.1 Surround (Front Left, Front Right, Front Center,
 *         LFE, Side Left, Side Right). Note that some audio systems may label
 *         5.1's Side channels as Rear or Surround; they are equivalent for the
 *         purposes of this extension.
 * 51CHN16: Signed 16-bit, 5.1 Surround.
 * 51CHN32: 32-bit float, 5.1 Surround.
 * 61CHN8: Unsigned 8-bit, 6.1 Surround (Front Left, Front Right, Front Center,
 *         LFE, Rear Center, Side Left, Side Right).
 * 61CHN16: Signed 16-bit, 6.1 Surround.
 * 61CHN32: 32-bit float, 6.1 Surround.
 * 71CHN8: Unsigned 8-bit, 7.1 Surround (Front Left, Front Right, Front Center,
 *         LFE, Rear Left, Rear Right, Side Left, Side Right).
 * 71CHN16: Signed 16-bit, 7.1 Surround.
 * 71CHN32: 32-bit float, 7.1 Surround.
 */
ENUMDCL AL_FORMAT_QUAD8 =                        0x1204;
ENUMDCL AL_FORMAT_QUAD16 =                       0x1205;
ENUMDCL AL_FORMAT_QUAD32 =                       0x1206;
ENUMDCL AL_FORMAT_REAR8 =                        0x1207;
ENUMDCL AL_FORMAT_REAR16 =                       0x1208;
ENUMDCL AL_FORMAT_REAR32 =                       0x1209;
ENUMDCL AL_FORMAT_51CHN8 =                       0x120A;
ENUMDCL AL_FORMAT_51CHN16 =                      0x120B;
ENUMDCL AL_FORMAT_51CHN32 =                      0x120C;
ENUMDCL AL_FORMAT_61CHN8 =                       0x120D;
ENUMDCL AL_FORMAT_61CHN16 =                      0x120E;
ENUMDCL AL_FORMAT_61CHN32 =                      0x120F;
ENUMDCL AL_FORMAT_71CHN8 =                       0x1210;
ENUMDCL AL_FORMAT_71CHN16 =                      0x1211;
ENUMDCL AL_FORMAT_71CHN32 =                      0x1212;

/*** AL_EXT_MULAW_MCFORMATS ***/
ENUMDCL AL_FORMAT_MONO_MULAW =                   0x10014;
ENUMDCL AL_FORMAT_STEREO_MULAW =                 0x10015;
ENUMDCL AL_FORMAT_QUAD_MULAW =                   0x10021;
ENUMDCL AL_FORMAT_REAR_MULAW =                   0x10022;
ENUMDCL AL_FORMAT_51CHN_MULAW =                  0x10023;
ENUMDCL AL_FORMAT_61CHN_MULAW =                  0x10024;
ENUMDCL AL_FORMAT_71CHN_MULAW =                  0x10025;

/*** AL_EXT_IMA4 ***/
ENUMDCL AL_FORMAT_MONO_IMA4 =                    0x1300;
ENUMDCL AL_FORMAT_STEREO_IMA4 =                  0x1301;

/*** AL_EXT_STATIC_BUFFER ***/
using PFNALBUFFERDATASTATICPROC = void (AL_APIENTRY*)(const ALuint,ALenum,ALvoid*,ALsizei,ALsizei) AL_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
void AL_APIENTRY alBufferDataStatic(const ALuint buffer, ALenum format, ALvoid *data, ALsizei size, ALsizei freq) AL_API_NOEXCEPT;
#endif

/*** ALC_EXT_disconnect ***/
ENUMDCL ALC_CONNECTED =                          0x313;

/*** ALC_EXT_thread_local_context ***/
using PFNALCSETTHREADCONTEXTPROC = auto (ALC_APIENTRY*)(ALCcontext *context) ALC_API_NOEXCEPT -> ALCboolean;
using PFNALCGETTHREADCONTEXTPROC = auto (ALC_APIENTRY*)() ALC_API_NOEXCEPT -> ALCcontext*;
#ifdef AL_ALEXT_PROTOTYPES
ALC_API auto ALC_APIENTRY alcSetThreadContext(ALCcontext *context) ALC_API_NOEXCEPT -> ALCboolean;
ALC_API auto ALC_APIENTRY alcGetThreadContext() ALC_API_NOEXCEPT -> ALCcontext*;
#endif

/*** AL_EXT_source_distance_model ***/
ENUMDCL AL_SOURCE_DISTANCE_MODEL =               0x200;

/*** AL_SOFT_buffer_sub_data ***/
ENUMDCL AL_BYTE_RW_OFFSETS_SOFT =                0x1031;
ENUMDCL AL_SAMPLE_RW_OFFSETS_SOFT =              0x1032;
using PFNALBUFFERSUBDATASOFTPROC = void (AL_APIENTRY*)(ALuint,ALenum,const ALvoid*,ALsizei,ALsizei) AL_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alBufferSubDataSOFT(ALuint buffer,ALenum format,const ALvoid *data,ALsizei offset,ALsizei length) AL_API_NOEXCEPT;
#endif

/*** AL_SOFT_loop_points ***/
ENUMDCL AL_LOOP_POINTS_SOFT =                    0x2015;

/*** AL_EXT_FOLDBACK ***/
inline constexpr auto AL_EXT_FOLDBACK_NAME =     std::to_array<const ALCchar>("AL_EXT_FOLDBACK");
ENUMDCL AL_FOLDBACK_EVENT_BLOCK =                0x4112;
ENUMDCL AL_FOLDBACK_EVENT_START =                0x4111;
ENUMDCL AL_FOLDBACK_EVENT_STOP =                 0x4113;
ENUMDCL AL_FOLDBACK_MODE_MONO =                  0x4101;
ENUMDCL AL_FOLDBACK_MODE_STEREO =                0x4102;
using LPALFOLDBACKCALLBACK =     void (AL_APIENTRY*)(ALenum,ALsizei) AL_API_NOEXCEPT;
using LPALREQUESTFOLDBACKSTART = void (AL_APIENTRY*)(ALenum,ALsizei,ALsizei,ALfloat*,LPALFOLDBACKCALLBACK) AL_API_NOEXCEPT;
using LPALREQUESTFOLDBACKSTOP =  void (AL_APIENTRY*)() AL_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alRequestFoldbackStart(ALenum mode,ALsizei count,ALsizei length,ALfloat *mem,LPALFOLDBACKCALLBACK callback) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alRequestFoldbackStop() AL_API_NOEXCEPT;
#endif

/*** ALC_EXT_DEDICATED ***/
ENUMDCL AL_DEDICATED_GAIN =                      0x0001;
ENUMDCL AL_EFFECT_DEDICATED_DIALOGUE =           0x9001;
ENUMDCL AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT = 0x9000;

/*** AL_SOFT_buffer_samples ***/
/* Channel configurations */
ENUMDCL AL_MONO_SOFT =                           0x1500;
ENUMDCL AL_STEREO_SOFT =                         0x1501;
ENUMDCL AL_REAR_SOFT =                           0x1502;
ENUMDCL AL_QUAD_SOFT =                           0x1503;
ENUMDCL AL_5POINT1_SOFT =                        0x1504;
ENUMDCL AL_6POINT1_SOFT =                        0x1505;
ENUMDCL AL_7POINT1_SOFT =                        0x1506;

/* Sample types */
ENUMDCL AL_BYTE_SOFT =                           0x1400;
ENUMDCL AL_UNSIGNED_BYTE_SOFT =                  0x1401;
ENUMDCL AL_SHORT_SOFT =                          0x1402;
ENUMDCL AL_UNSIGNED_SHORT_SOFT =                 0x1403;
ENUMDCL AL_INT_SOFT =                            0x1404;
ENUMDCL AL_UNSIGNED_INT_SOFT =                   0x1405;
ENUMDCL AL_FLOAT_SOFT =                          0x1406;
ENUMDCL AL_DOUBLE_SOFT =                         0x1407;
ENUMDCL AL_BYTE3_SOFT =                          0x1408;
ENUMDCL AL_UNSIGNED_BYTE3_SOFT =                 0x1409;

/* Storage formats */
ENUMDCL AL_MONO8_SOFT =                          0x1100;
ENUMDCL AL_MONO16_SOFT =                         0x1101;
ENUMDCL AL_MONO32F_SOFT =                        0x10010;
ENUMDCL AL_STEREO8_SOFT =                        0x1102;
ENUMDCL AL_STEREO16_SOFT =                       0x1103;
ENUMDCL AL_STEREO32F_SOFT =                      0x10011;
ENUMDCL AL_QUAD8_SOFT =                          0x1204;
ENUMDCL AL_QUAD16_SOFT =                         0x1205;
ENUMDCL AL_QUAD32F_SOFT =                        0x1206;
ENUMDCL AL_REAR8_SOFT =                          0x1207;
ENUMDCL AL_REAR16_SOFT =                         0x1208;
ENUMDCL AL_REAR32F_SOFT =                        0x1209;
ENUMDCL AL_5POINT1_8_SOFT =                      0x120A;
ENUMDCL AL_5POINT1_16_SOFT =                     0x120B;
ENUMDCL AL_5POINT1_32F_SOFT =                    0x120C;
ENUMDCL AL_6POINT1_8_SOFT =                      0x120D;
ENUMDCL AL_6POINT1_16_SOFT =                     0x120E;
ENUMDCL AL_6POINT1_32F_SOFT =                    0x120F;
ENUMDCL AL_7POINT1_8_SOFT =                      0x1210;
ENUMDCL AL_7POINT1_16_SOFT =                     0x1211;
ENUMDCL AL_7POINT1_32F_SOFT =                    0x1212;

/* Buffer attributes */
ENUMDCL AL_INTERNAL_FORMAT_SOFT =                0x2008;
ENUMDCL AL_BYTE_LENGTH_SOFT =                    0x2009;
ENUMDCL AL_SAMPLE_LENGTH_SOFT =                  0x200A;
ENUMDCL AL_SEC_LENGTH_SOFT =                     0x200B;

using LPALBUFFERSAMPLESSOFT =           void (AL_APIENTRY*)(ALuint,ALuint,ALenum,ALsizei,ALenum,ALenum,const ALvoid*) AL_API_NOEXCEPT;
using LPALBUFFERSUBSAMPLESSOFT =        void (AL_APIENTRY*)(ALuint,ALsizei,ALsizei,ALenum,ALenum,const ALvoid*) AL_API_NOEXCEPT;
using LPALGETBUFFERSAMPLESSOFT =        void (AL_APIENTRY*)(ALuint,ALsizei,ALsizei,ALenum,ALenum,ALvoid*) AL_API_NOEXCEPT;
using LPALISBUFFERFORMATSUPPORTEDSOFT = auto (AL_APIENTRY*)(ALenum) AL_API_NOEXCEPT -> ALboolean;
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alBufferSamplesSOFT(ALuint buffer, ALuint samplerate, ALenum internalformat, ALsizei samples, ALenum channels, ALenum type, const ALvoid *data) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alBufferSubSamplesSOFT(ALuint buffer, ALsizei offset, ALsizei samples, ALenum channels, ALenum type, const ALvoid *data) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetBufferSamplesSOFT(ALuint buffer, ALsizei offset, ALsizei samples, ALenum channels, ALenum type, ALvoid *data) AL_API_NOEXCEPT;
AL_API auto AL_APIENTRY alIsBufferFormatSupportedSOFT(ALenum format) AL_API_NOEXCEPT -> ALboolean;
#endif

/*** AL_SOFT_direct_channels ***/
ENUMDCL AL_DIRECT_CHANNELS_SOFT =                0x1033;

/*** ALC_SOFT_loopback ***/
ENUMDCL ALC_FORMAT_CHANNELS_SOFT =               0x1990;
ENUMDCL ALC_FORMAT_TYPE_SOFT =                   0x1991;

/* Sample types */
ENUMDCL ALC_BYTE_SOFT =                          0x1400;
ENUMDCL ALC_UNSIGNED_BYTE_SOFT =                 0x1401;
ENUMDCL ALC_SHORT_SOFT =                         0x1402;
ENUMDCL ALC_UNSIGNED_SHORT_SOFT =                0x1403;
ENUMDCL ALC_INT_SOFT =                           0x1404;
ENUMDCL ALC_UNSIGNED_INT_SOFT =                  0x1405;
ENUMDCL ALC_FLOAT_SOFT =                         0x1406;

/* Channel configurations */
ENUMDCL ALC_MONO_SOFT =                          0x1500;
ENUMDCL ALC_STEREO_SOFT =                        0x1501;
ENUMDCL ALC_QUAD_SOFT =                          0x1503;
ENUMDCL ALC_5POINT1_SOFT =                       0x1504;
ENUMDCL ALC_6POINT1_SOFT =                       0x1505;
ENUMDCL ALC_7POINT1_SOFT =                       0x1506;

using LPALCLOOPBACKOPENDEVICESOFT =      auto (ALC_APIENTRY*)(const ALCchar*) ALC_API_NOEXCEPT -> ALCdevice*;
using LPALCISRENDERFORMATSUPPORTEDSOFT = auto (ALC_APIENTRY*)(ALCdevice*,ALCsizei,ALCenum,ALCenum) ALC_API_NOEXCEPT -> ALCboolean;
using LPALCRENDERSAMPLESSOFT =           void (ALC_APIENTRY*)(ALCdevice*,ALCvoid*,ALCsizei) ALC_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
ALC_API auto ALC_APIENTRY alcLoopbackOpenDeviceSOFT(const ALCchar *deviceName) AL_API_NOEXCEPT -> ALCdevice*;
ALC_API auto ALC_APIENTRY alcIsRenderFormatSupportedSOFT(ALCdevice *device, ALCsizei freq, ALCenum channels, ALCenum type) AL_API_NOEXCEPT -> ALCboolean;
ALC_API void ALC_APIENTRY alcRenderSamplesSOFT(ALCdevice *device, ALCvoid *buffer, ALCsizei samples) AL_API_NOEXCEPT;
#endif

/*** AL_EXT_STEREO_ANGLES ***/
ENUMDCL AL_STEREO_ANGLES =                       0x1030;

/*** AL_EXT_SOURCE_RADIUS ***/
ENUMDCL AL_SOURCE_RADIUS =                       0x1031;

/*** AL_SOFT_source_latency ***/
ENUMDCL AL_SAMPLE_OFFSET_LATENCY_SOFT =          0x1200;
ENUMDCL AL_SEC_OFFSET_LATENCY_SOFT =             0x1201;
using LPALSOURCEDSOFT =       void (AL_APIENTRY*)(ALuint,ALenum,ALdouble) AL_API_NOEXCEPT;
using LPALSOURCE3DSOFT =      void (AL_APIENTRY*)(ALuint,ALenum,ALdouble,ALdouble,ALdouble) AL_API_NOEXCEPT;
using LPALSOURCEDVSOFT =      void (AL_APIENTRY*)(ALuint,ALenum,const ALdouble*) AL_API_NOEXCEPT;
using LPALGETSOURCEDSOFT =    void (AL_APIENTRY*)(ALuint,ALenum,ALdouble*) AL_API_NOEXCEPT;
using LPALGETSOURCE3DSOFT =   void (AL_APIENTRY*)(ALuint,ALenum,ALdouble*,ALdouble*,ALdouble*) AL_API_NOEXCEPT;
using LPALGETSOURCEDVSOFT =   void (AL_APIENTRY*)(ALuint,ALenum,ALdouble*) AL_API_NOEXCEPT;
using LPALSOURCEI64SOFT =     void (AL_APIENTRY*)(ALuint,ALenum,ALint64SOFT) AL_API_NOEXCEPT;
using LPALSOURCE3I64SOFT =    void (AL_APIENTRY*)(ALuint,ALenum,ALint64SOFT,ALint64SOFT,ALint64SOFT) AL_API_NOEXCEPT;
using LPALSOURCEI64VSOFT =    void (AL_APIENTRY*)(ALuint,ALenum,const ALint64SOFT*) AL_API_NOEXCEPT;
using LPALGETSOURCEI64SOFT =  void (AL_APIENTRY*)(ALuint,ALenum,ALint64SOFT*) AL_API_NOEXCEPT;
using LPALGETSOURCE3I64SOFT = void (AL_APIENTRY*)(ALuint,ALenum,ALint64SOFT*,ALint64SOFT*,ALint64SOFT*) AL_API_NOEXCEPT;
using LPALGETSOURCEI64VSOFT = void (AL_APIENTRY*)(ALuint,ALenum,ALint64SOFT*) AL_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alSourcedSOFT(ALuint source, ALenum param, ALdouble value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSource3dSOFT(ALuint source, ALenum param, ALdouble value1, ALdouble value2, ALdouble value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSourcedvSOFT(ALuint source, ALenum param, const ALdouble *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSourcedSOFT(ALuint source, ALenum param, ALdouble *value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSource3dSOFT(ALuint source, ALenum param, ALdouble *value1, ALdouble *value2, ALdouble *value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSourcedvSOFT(ALuint source, ALenum param, ALdouble *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSourcei64SOFT(ALuint source, ALenum param, ALint64SOFT value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSource3i64SOFT(ALuint source, ALenum param, ALint64SOFT value1, ALint64SOFT value2, ALint64SOFT value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alSourcei64vSOFT(ALuint source, ALenum param, const ALint64SOFT *values) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSourcei64SOFT(ALuint source, ALenum param, ALint64SOFT *value) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSource3i64SOFT(ALuint source, ALenum param, ALint64SOFT *value1, ALint64SOFT *value2, ALint64SOFT *value3) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetSourcei64vSOFT(ALuint source, ALenum param, ALint64SOFT *values) AL_API_NOEXCEPT;
#endif

/*** ALC_EXT_DEFAULT_FILTER_ORDER ***/
ENUMDCL ALC_DEFAULT_FILTER_ORDER =               0x1100;

/*** AL_SOFT_deferred_updates ***/
ENUMDCL AL_DEFERRED_UPDATES_SOFT =               0xC002;
using LPALDEFERUPDATESSOFT =   void (AL_APIENTRY*)() AL_API_NOEXCEPT;
using LPALPROCESSUPDATESSOFT = void (AL_APIENTRY*)() AL_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alDeferUpdatesSOFT() AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alProcessUpdatesSOFT() AL_API_NOEXCEPT;
#endif

/*** AL_SOFT_block_alignment ***/
ENUMDCL AL_UNPACK_BLOCK_ALIGNMENT_SOFT =         0x200C;
ENUMDCL AL_PACK_BLOCK_ALIGNMENT_SOFT =           0x200D;

/*** AL_SOFT_MSADPCM ***/
ENUMDCL AL_FORMAT_MONO_MSADPCM_SOFT =            0x1302;
ENUMDCL AL_FORMAT_STEREO_MSADPCM_SOFT =          0x1303;

/*** AL_SOFT_source_length ***/
/*ENUMDCL AL_BYTE_LENGTH_SOFT =                    0x2009;*/
/*ENUMDCL AL_SAMPLE_LENGTH_SOFT =                  0x200A;*/
/*ENUMDCL AL_SEC_LENGTH_SOFT =                     0x200B;*/

/*** AL_SOFT_buffer_length_query ***/
/*ENUMDCL AL_BYTE_LENGTH_SOFT =                    0x2009;*/
/*ENUMDCL AL_SAMPLE_LENGTH_SOFT =                  0x200A;*/
/*ENUMDCL AL_SEC_LENGTH_SOFT =                     0x200B;*/

/*** ALC_SOFT_pause_device ***/
using LPALCDEVICEPAUSESOFT =  void (ALC_APIENTRY*)(ALCdevice *device) ALC_API_NOEXCEPT;
using LPALCDEVICERESUMESOFT = void (ALC_APIENTRY*)(ALCdevice *device) ALC_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
ALC_API void ALC_APIENTRY alcDevicePauseSOFT(ALCdevice *device) ALC_API_NOEXCEPT;
ALC_API void ALC_APIENTRY alcDeviceResumeSOFT(ALCdevice *device) ALC_API_NOEXCEPT;
#endif

/*** AL_EXT_BFORMAT ***/
/* Provides support for B-Format ambisonic buffers (first-order, FuMa scaling
 * and layout).
 *
 * BFORMAT2D_8: Unsigned 8-bit, 3-channel non-periphonic (WXY).
 * BFORMAT2D_16: Signed 16-bit, 3-channel non-periphonic (WXY).
 * BFORMAT2D_FLOAT32: 32-bit float, 3-channel non-periphonic (WXY).
 * BFORMAT3D_8: Unsigned 8-bit, 4-channel periphonic (WXYZ).
 * BFORMAT3D_16: Signed 16-bit, 4-channel periphonic (WXYZ).
 * BFORMAT3D_FLOAT32: 32-bit float, 4-channel periphonic (WXYZ).
 */
ENUMDCL AL_FORMAT_BFORMAT2D_8 =                  0x20021;
ENUMDCL AL_FORMAT_BFORMAT2D_16 =                 0x20022;
ENUMDCL AL_FORMAT_BFORMAT2D_FLOAT32 =            0x20023;
ENUMDCL AL_FORMAT_BFORMAT3D_8 =                  0x20031;
ENUMDCL AL_FORMAT_BFORMAT3D_16 =                 0x20032;
ENUMDCL AL_FORMAT_BFORMAT3D_FLOAT32 =            0x20033;

/*** AL_EXT_MULAW_BFORMAT ***/
ENUMDCL AL_FORMAT_BFORMAT2D_MULAW =              0x10031;
ENUMDCL AL_FORMAT_BFORMAT3D_MULAW =              0x10032;

/*** ALC_SOFT_HRTF ***/
ENUMDCL ALC_HRTF_SOFT =                          0x1992;
ENUMDCL ALC_DONT_CARE_SOFT =                     0x0002;
ENUMDCL ALC_HRTF_STATUS_SOFT =                   0x1993;
ENUMDCL ALC_HRTF_DISABLED_SOFT =                 0x0000;
ENUMDCL ALC_HRTF_ENABLED_SOFT =                  0x0001;
ENUMDCL ALC_HRTF_DENIED_SOFT =                   0x0002;
ENUMDCL ALC_HRTF_REQUIRED_SOFT =                 0x0003;
ENUMDCL ALC_HRTF_HEADPHONES_DETECTED_SOFT =      0x0004;
ENUMDCL ALC_HRTF_UNSUPPORTED_FORMAT_SOFT =       0x0005;
ENUMDCL ALC_NUM_HRTF_SPECIFIERS_SOFT =           0x1994;
ENUMDCL ALC_HRTF_SPECIFIER_SOFT =                0x1995;
ENUMDCL ALC_HRTF_ID_SOFT =                       0x1996;
using LPALCGETSTRINGISOFT =  auto (ALC_APIENTRY*)(ALCdevice *device, ALCenum paramName, ALCsizei index) ALC_API_NOEXCEPT -> const ALCchar*;
using LPALCRESETDEVICESOFT = auto (ALC_APIENTRY*)(ALCdevice *device, const ALCint *attribs) ALC_API_NOEXCEPT -> ALCboolean;
#ifdef AL_ALEXT_PROTOTYPES
ALC_API auto ALC_APIENTRY alcGetStringiSOFT(ALCdevice *device, ALCenum paramName, ALCsizei index) ALC_API_NOEXCEPT -> const ALCchar*;
ALC_API auto ALC_APIENTRY alcResetDeviceSOFT(ALCdevice *device, const ALCint *attribs) ALC_API_NOEXCEPT -> ALCboolean;
#endif

/*** AL_SOFT_gain_clamp_ex ***/
ENUMDCL AL_GAIN_LIMIT_SOFT =                     0x200E;

/*** AL_SOFT_source_resampler ***/
ENUMDCL AL_NUM_RESAMPLERS_SOFT =                 0x1210;
ENUMDCL AL_DEFAULT_RESAMPLER_SOFT =              0x1211;
ENUMDCL AL_SOURCE_RESAMPLER_SOFT =               0x1212;
ENUMDCL AL_RESAMPLER_NAME_SOFT =                 0x1213;
using LPALGETSTRINGISOFT = auto (AL_APIENTRY*)(ALenum pname, ALsizei index) AL_API_NOEXCEPT -> const ALchar*;
#ifdef AL_ALEXT_PROTOTYPES
AL_API auto AL_APIENTRY alGetStringiSOFT(ALenum pname, ALsizei index) AL_API_NOEXCEPT -> const ALchar*;
#endif

/*** AL_SOFT_source_spatialize ***/
ENUMDCL AL_SOURCE_SPATIALIZE_SOFT =              0x1214;
ENUMDCL AL_AUTO_SOFT =                           0x0002;

/*** ALC_SOFT_output_limiter ***/
ENUMDCL ALC_OUTPUT_LIMITER_SOFT =                0x199A;

/*** ALC_SOFT_device_clock ***/
ENUMDCL ALC_DEVICE_CLOCK_SOFT =                  0x1600;
ENUMDCL ALC_DEVICE_LATENCY_SOFT =                0x1601;
ENUMDCL ALC_DEVICE_CLOCK_LATENCY_SOFT =          0x1602;
ENUMDCL AL_SAMPLE_OFFSET_CLOCK_SOFT =            0x1202;
ENUMDCL AL_SEC_OFFSET_CLOCK_SOFT =               0x1203;
using LPALCGETINTEGER64VSOFT = void (ALC_APIENTRY*)(ALCdevice *device, ALCenum pname, ALsizei size, ALCint64SOFT *values) ALC_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
ALC_API void ALC_APIENTRY alcGetInteger64vSOFT(ALCdevice *device, ALCenum pname, ALsizei size, ALCint64SOFT *values) ALC_API_NOEXCEPT;
#endif

/*** AL_SOFT_direct_channels_remix ***/
ENUMDCL AL_DROP_UNMATCHED_SOFT =                 0x0001;
ENUMDCL AL_REMIX_UNMATCHED_SOFT =                0x0002;

/*** AL_SOFT_bformat_ex ***/
ENUMDCL AL_AMBISONIC_LAYOUT_SOFT =               0x1997;
ENUMDCL AL_AMBISONIC_SCALING_SOFT =              0x1998;

/* Ambisonic layouts */
ENUMDCL AL_FUMA_SOFT =                           0x0000;
ENUMDCL AL_ACN_SOFT =                            0x0001;

/* Ambisonic scalings (normalization) */
/*ENUMDCL AL_FUMA_SOFT =                           0x0000;*/
ENUMDCL AL_SN3D_SOFT =                           0x0001;
ENUMDCL AL_N3D_SOFT =                            0x0002;

/*** ALC_SOFT_loopback_bformat ***/
ENUMDCL ALC_AMBISONIC_LAYOUT_SOFT =              0x1997;
ENUMDCL ALC_AMBISONIC_SCALING_SOFT =             0x1998;
ENUMDCL ALC_AMBISONIC_ORDER_SOFT =               0x1999;
ENUMDCL ALC_MAX_AMBISONIC_ORDER_SOFT =           0x199B;

ENUMDCL ALC_BFORMAT3D_SOFT =                     0x1507;

/* Ambisonic layouts */
ENUMDCL ALC_FUMA_SOFT =                          0x0000;
ENUMDCL ALC_ACN_SOFT =                           0x0001;

/* Ambisonic scalings (normalization) */
/*ENUMDCL ALC_FUMA_SOFT =                          0x0000;*/
ENUMDCL ALC_SN3D_SOFT =                          0x0001;
ENUMDCL ALC_N3D_SOFT =                           0x0002;

/*** AL_SOFT_effect_target ***/
ENUMDCL AL_EFFECTSLOT_TARGET_SOFT =              0x199C;

/*** AL_SOFT_events ***/
ENUMDCL AL_EVENT_CALLBACK_FUNCTION_SOFT =        0x19A2;
ENUMDCL AL_EVENT_CALLBACK_USER_PARAM_SOFT =      0x19A3;
ENUMDCL AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT =    0x19A4;
ENUMDCL AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT = 0x19A5;
ENUMDCL AL_EVENT_TYPE_DISCONNECTED_SOFT =        0x19A6;
using ALEVENTPROCSOFT = void (AL_APIENTRY*)(ALenum eventType, ALuint object, ALuint param,
    ALsizei length, const ALchar *message, void *userParam) AL_API_NOEXCEPT;
using LPALEVENTCONTROLSOFT =  void (AL_APIENTRY*)(ALsizei count, const ALenum *types, ALboolean enable) AL_API_NOEXCEPT;
using LPALEVENTCALLBACKSOFT = void (AL_APIENTRY*)(ALEVENTPROCSOFT callback, void *userParam) AL_API_NOEXCEPT;
using LPALGETPOINTERSOFT =    auto (AL_APIENTRY*)(ALenum pname) AL_API_NOEXCEPT -> void*;
using LPALGETPOINTERVSOFT =   void (AL_APIENTRY*)(ALenum pname, void **values) AL_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alEventControlSOFT(ALsizei count, const ALenum *types, ALboolean enable) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alEventCallbackSOFT(ALEVENTPROCSOFT callback, void *userParam) AL_API_NOEXCEPT;
AL_API auto AL_APIENTRY alGetPointerSOFT(ALenum pname) AL_API_NOEXCEPT -> void*;
AL_API void AL_APIENTRY alGetPointervSOFT(ALenum pname, void **values) AL_API_NOEXCEPT;
#endif

/*** ALC_SOFT_reopen_device ***/
using LPALCREOPENDEVICESOFT = auto (ALC_APIENTRY*)(ALCdevice *device, const ALCchar *deviceName, const ALCint *attribs) ALC_API_NOEXCEPT -> ALCboolean;
#ifdef AL_ALEXT_PROTOTYPES
auto ALC_APIENTRY alcReopenDeviceSOFT(ALCdevice *device, const ALCchar *deviceName, const ALCint *attribs) ALC_API_NOEXCEPT -> ALCboolean;
#endif

/*** AL_SOFT_callback_buffer ***/
ENUMDCL AL_BUFFER_CALLBACK_FUNCTION_SOFT =       0x19A0;
ENUMDCL AL_BUFFER_CALLBACK_USER_PARAM_SOFT =     0x19A1;
using ALBUFFERCALLBACKTYPESOFT = auto (AL_APIENTRY*)(ALvoid *userptr, ALvoid *sampledata,
    ALsizei numbytes) AL_API_NOEXCEPT -> ALsizei;
using LPALBUFFERCALLBACKSOFT = void (AL_APIENTRY*)(ALuint buffer, ALenum format, ALsizei freq, ALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr) AL_API_NOEXCEPT;
using LPALGETBUFFERPTRSOFT =   void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALvoid **value) AL_API_NOEXCEPT;
using LPALGETBUFFER3PTRSOFT =  void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALvoid **value1, ALvoid **value2, ALvoid **value3) AL_API_NOEXCEPT;
using LPALGETBUFFERPTRVSOFT =  void (AL_APIENTRY*)(ALuint buffer, ALenum param, ALvoid **values) AL_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alBufferCallbackSOFT(ALuint buffer, ALenum format, ALsizei freq, ALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetBufferPtrSOFT(ALuint buffer, ALenum param, ALvoid **ptr) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetBuffer3PtrSOFT(ALuint buffer, ALenum param, ALvoid **ptr0, ALvoid **ptr1, ALvoid **ptr2) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetBufferPtrvSOFT(ALuint buffer, ALenum param, ALvoid **ptr) AL_API_NOEXCEPT;
#endif

/*** AL_SOFT_UHJ ***/
ENUMDCL AL_FORMAT_UHJ2CHN8_SOFT =                0x19A2;
ENUMDCL AL_FORMAT_UHJ2CHN16_SOFT =               0x19A3;
ENUMDCL AL_FORMAT_UHJ2CHN_FLOAT32_SOFT =         0x19A4;
ENUMDCL AL_FORMAT_UHJ3CHN8_SOFT =                0x19A5;
ENUMDCL AL_FORMAT_UHJ3CHN16_SOFT =               0x19A6;
ENUMDCL AL_FORMAT_UHJ3CHN_FLOAT32_SOFT =         0x19A7;
ENUMDCL AL_FORMAT_UHJ4CHN8_SOFT =                0x19A8;
ENUMDCL AL_FORMAT_UHJ4CHN16_SOFT =               0x19A9;
ENUMDCL AL_FORMAT_UHJ4CHN_FLOAT32_SOFT =         0x19AA;

ENUMDCL AL_STEREO_MODE_SOFT =                    0x19B0;
ENUMDCL AL_NORMAL_SOFT =                         0x0000;
ENUMDCL AL_SUPER_STEREO_SOFT =                   0x0001;
ENUMDCL AL_SUPER_STEREO_WIDTH_SOFT =             0x19B1;

/*** AL_SOFT_UHJ_ex ***/
ENUMDCL AL_FORMAT_UHJ2CHN_MULAW_SOFT =           0x19B3;
ENUMDCL AL_FORMAT_UHJ2CHN_ALAW_SOFT =            0x19B4;
ENUMDCL AL_FORMAT_UHJ2CHN_IMA4_SOFT =            0x19B5;
ENUMDCL AL_FORMAT_UHJ2CHN_MSADPCM_SOFT =         0x19B6;
ENUMDCL AL_FORMAT_UHJ3CHN_MULAW_SOFT =           0x19B7;
ENUMDCL AL_FORMAT_UHJ3CHN_ALAW_SOFT =            0x19B8;
ENUMDCL AL_FORMAT_UHJ4CHN_MULAW_SOFT =           0x19B9;
ENUMDCL AL_FORMAT_UHJ4CHN_ALAW_SOFT =            0x19BA;

/*** ALC_SOFT_output_mode ***/
ENUMDCL ALC_OUTPUT_MODE_SOFT =                   0x19AC;
ENUMDCL ALC_ANY_SOFT =                           0x19AD;
/*ENUMDCL ALC_MONO_SOFT =                          0x1500;*/
/*ENUMDCL ALC_STEREO_SOFT =                        0x1501;*/
ENUMDCL ALC_STEREO_BASIC_SOFT =                  0x19AE;
ENUMDCL ALC_STEREO_UHJ_SOFT =                    0x19AF;
ENUMDCL ALC_STEREO_HRTF_SOFT =                   0x19B2;
/*ENUMDCL ALC_QUAD_SOFT =                          0x1503;*/
ENUMDCL ALC_SURROUND_5_1_SOFT =                  0x1504;
ENUMDCL ALC_SURROUND_6_1_SOFT =                  0x1505;
ENUMDCL ALC_SURROUND_7_1_SOFT =                  0x1506;

/*** AL_SOFT_source_start_delay ***/
using LPALSOURCEPLAYATTIMESOFT =  void (AL_APIENTRY*)(ALuint source, ALint64SOFT start_time) AL_API_NOEXCEPT;
using LPALSOURCEPLAYATTIMEVSOFT = void (AL_APIENTRY*)(ALsizei n, const ALuint *sources, ALint64SOFT start_time) AL_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
void AL_APIENTRY alSourcePlayAtTimeSOFT(ALuint source, ALint64SOFT start_time) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePlayAtTimevSOFT(ALsizei n, const ALuint *sources, ALint64SOFT start_time) AL_API_NOEXCEPT;
#endif

/*** ALC_EXT_debug / AL_EXT_debug ***/
ENUMDCL ALC_CONTEXT_FLAGS_EXT =                  0x19CF;
ENUMDCL ALC_CONTEXT_DEBUG_BIT_EXT =              0x0001;

ENUMDCL AL_DONT_CARE_EXT =                       0x0002;
ENUMDCL AL_DEBUG_OUTPUT_EXT =                    0x19B2;
ENUMDCL AL_DEBUG_CALLBACK_FUNCTION_EXT =         0x19B3;
ENUMDCL AL_DEBUG_CALLBACK_USER_PARAM_EXT =       0x19B4;
ENUMDCL AL_DEBUG_SOURCE_API_EXT =                0x19B5;
ENUMDCL AL_DEBUG_SOURCE_AUDIO_SYSTEM_EXT =       0x19B6;
ENUMDCL AL_DEBUG_SOURCE_THIRD_PARTY_EXT =        0x19B7;
ENUMDCL AL_DEBUG_SOURCE_APPLICATION_EXT =        0x19B8;
ENUMDCL AL_DEBUG_SOURCE_OTHER_EXT =              0x19B9;
ENUMDCL AL_DEBUG_TYPE_ERROR_EXT =                0x19BA;
ENUMDCL AL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_EXT =  0x19BB;
ENUMDCL AL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_EXT =   0x19BC;
ENUMDCL AL_DEBUG_TYPE_PORTABILITY_EXT =          0x19BD;
ENUMDCL AL_DEBUG_TYPE_PERFORMANCE_EXT =          0x19BE;
ENUMDCL AL_DEBUG_TYPE_MARKER_EXT =               0x19BF;
ENUMDCL AL_DEBUG_TYPE_PUSH_GROUP_EXT =           0x19C0;
ENUMDCL AL_DEBUG_TYPE_POP_GROUP_EXT =            0x19C1;
ENUMDCL AL_DEBUG_TYPE_OTHER_EXT =                0x19C2;
ENUMDCL AL_DEBUG_SEVERITY_HIGH_EXT =             0x19C3;
ENUMDCL AL_DEBUG_SEVERITY_MEDIUM_EXT =           0x19C4;
ENUMDCL AL_DEBUG_SEVERITY_LOW_EXT =              0x19C5;
ENUMDCL AL_DEBUG_SEVERITY_NOTIFICATION_EXT =     0x19C6;
ENUMDCL AL_DEBUG_LOGGED_MESSAGES_EXT =           0x19C7;
ENUMDCL AL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_EXT = 0x19C8;
ENUMDCL AL_MAX_DEBUG_MESSAGE_LENGTH_EXT =        0x19C9;
ENUMDCL AL_MAX_DEBUG_LOGGED_MESSAGES_EXT =       0x19CA;
ENUMDCL AL_MAX_DEBUG_GROUP_STACK_DEPTH_EXT =     0x19CB;
ENUMDCL AL_MAX_LABEL_LENGTH_EXT =                0x19CC;
ENUMDCL AL_STACK_OVERFLOW_EXT =                  0x19CD;
ENUMDCL AL_STACK_UNDERFLOW_EXT =                 0x19CE;
ENUMDCL AL_CONTEXT_FLAGS_EXT =                   0x19CF;
ENUMDCL AL_BUFFER_EXT =                          AL_BUFFER;
ENUMDCL AL_SOURCE_EXT =                          0x19D0;
ENUMDCL AL_FILTER_EXT =                          0x19D1;
ENUMDCL AL_EFFECT_EXT =                          0x19D2;
ENUMDCL AL_AUXILIARY_EFFECT_SLOT_EXT =           0x19D3;

using ALDEBUGPROCEXT = void (AL_APIENTRY*)(ALenum source, ALenum type, ALuint id, ALenum severity,
    ALsizei length, const ALchar *message, void *userParam) AL_API_NOEXCEPT;
using LPALDEBUGMESSAGECALLBACKEXT = void (AL_APIENTRY*)(ALDEBUGPROCEXT callback, void *userParam) AL_API_NOEXCEPT;
using LPALDEBUGMESSAGEINSERTEXT =   void (AL_APIENTRY*)(ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) AL_API_NOEXCEPT;
using LPALDEBUGMESSAGECONTROLEXT =  void (AL_APIENTRY*)(ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) AL_API_NOEXCEPT;
using LPALPUSHDEBUGGROUPEXT =       void (AL_APIENTRY*)(ALenum source, ALuint id, ALsizei length, const ALchar *message) AL_API_NOEXCEPT;
using LPALPOPDEBUGGROUPEXT =        void (AL_APIENTRY*)() AL_API_NOEXCEPT;
using LPALGETDEBUGMESSAGELOGEXT =   auto (AL_APIENTRY*)(ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf) AL_API_NOEXCEPT -> ALuint;
using LPALOBJECTLABELEXT =          void (AL_APIENTRY*)(ALenum identifier, ALuint name, ALsizei length, const ALchar *label) AL_API_NOEXCEPT;
using LPALGETOBJECTLABELEXT =       void (AL_APIENTRY*)(ALenum identifier, ALuint name, ALsizei bufSize, ALsizei *length, ALchar *label) AL_API_NOEXCEPT;
using LPALGETPOINTEREXT =           auto (AL_APIENTRY*)(ALenum pname) AL_API_NOEXCEPT -> void*;
using LPALGETPOINTERVEXT =          void (AL_APIENTRY*)(ALenum pname, void **values) AL_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
void AL_APIENTRY alDebugMessageCallbackEXT(ALDEBUGPROCEXT callback, void *userParam) AL_API_NOEXCEPT;
void AL_APIENTRY alDebugMessageInsertEXT(ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) AL_API_NOEXCEPT;
void AL_APIENTRY alDebugMessageControlEXT(ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) AL_API_NOEXCEPT;
void AL_APIENTRY alPushDebugGroupEXT(ALenum source, ALuint id, ALsizei length, const ALchar *message) AL_API_NOEXCEPT;
void AL_APIENTRY alPopDebugGroupEXT() AL_API_NOEXCEPT;
auto AL_APIENTRY alGetDebugMessageLogEXT(ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf) AL_API_NOEXCEPT -> ALuint;
void AL_APIENTRY alObjectLabelEXT(ALenum identifier, ALuint name, ALsizei length, const ALchar *label) AL_API_NOEXCEPT;
void AL_APIENTRY alGetObjectLabelEXT(ALenum identifier, ALuint name, ALsizei bufSize, ALsizei *length, ALchar *label) AL_API_NOEXCEPT;
auto AL_APIENTRY alGetPointerEXT(ALenum pname) AL_API_NOEXCEPT -> void*;
void AL_APIENTRY alGetPointervEXT(ALenum pname, void **values) AL_API_NOEXCEPT;
#endif

/*** ALC_SOFT_system_events ***/
ENUMDCL ALC_PLAYBACK_DEVICE_SOFT =               0x19D4;
ENUMDCL ALC_CAPTURE_DEVICE_SOFT =                0x19D5;
ENUMDCL ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT = 0x19D6;
ENUMDCL ALC_EVENT_TYPE_DEVICE_ADDED_SOFT =       0x19D7;
ENUMDCL ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT =     0x19D8;
ENUMDCL ALC_EVENT_SUPPORTED_SOFT =               0x19D9;
ENUMDCL ALC_EVENT_NOT_SUPPORTED_SOFT =           0x19DA;
using ALCEVENTPROCTYPESOFT = void (ALC_APIENTRY*)(ALCenum eventType, ALCenum deviceType,
    ALCdevice *device, ALCsizei length, const ALCchar *message, void *userParam) ALC_API_NOEXCEPT;
using LPALCEVENTISSUPPORTEDSOFT = auto (ALC_APIENTRY*)(ALCenum eventType, ALCenum deviceType) ALC_API_NOEXCEPT -> ALCenum;
using LPALCEVENTCONTROLSOFT =     auto (ALC_APIENTRY*)(ALCsizei count, const ALCenum *events, ALCboolean enable) ALC_API_NOEXCEPT -> ALCboolean;
using LPALCEVENTCALLBACKSOFT =    void (ALC_APIENTRY*)(ALCEVENTPROCTYPESOFT callback, void *userParam) ALC_API_NOEXCEPT;
#ifdef AL_ALEXT_PROTOTYPES
auto ALC_APIENTRY alcEventIsSupportedSOFT(ALCenum eventType, ALCenum deviceType) ALC_API_NOEXCEPT -> ALCenum;
auto ALC_APIENTRY alcEventControlSOFT(ALCsizei count, const ALCenum *events, ALCboolean enable) ALC_API_NOEXCEPT -> ALCboolean;
void ALC_APIENTRY alcEventCallbackSOFT(ALCEVENTPROCTYPESOFT callback, void *userParam) ALC_API_NOEXCEPT;
#endif

/*** ALC_EXT_direct_context / AL_EXT_direct_context ***/
using LPALCGETPROCADDRESS2 = auto (ALC_APIENTRY*)(ALCdevice *device, const ALCchar *funcname) AL_API_NOEXCEPT -> ALCvoid*;

using LPALENABLEDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT;
using LPALDISABLEDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT;
using LPALISENABLEDDIRECT = auto (AL_APIENTRY*)(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT -> ALboolean;
using LPALDOPPLERFACTORDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALfloat value) AL_API_NOEXCEPT;
using LPALSPEEDOFSOUNDDIRECT =  void (AL_APIENTRY*)(ALCcontext *context, ALfloat value) AL_API_NOEXCEPT;
using LPALDISTANCEMODELDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALenum distanceModel) AL_API_NOEXCEPT;
using LPALGETSTRINGDIRECT =   auto (AL_APIENTRY*)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> const ALchar*;
using LPALGETBOOLEANVDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALboolean *values) AL_API_NOEXCEPT;
using LPALGETINTEGERVDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALGETFLOATVDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGETDOUBLEVDIRECT =  void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALdouble *values) AL_API_NOEXCEPT;
using LPALGETBOOLEANDIRECT =  auto (AL_APIENTRY*)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> ALboolean;
using LPALGETINTEGERDIRECT =  auto (AL_APIENTRY*)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> ALint;
using LPALGETFLOATDIRECT =    auto (AL_APIENTRY*)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> ALfloat;
using LPALGETDOUBLEDIRECT =   auto (AL_APIENTRY*)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> ALdouble;
using LPALGETERRORDIRECT =    auto (AL_APIENTRY*)(ALCcontext *context) AL_API_NOEXCEPT -> ALenum;
using LPALISEXTENSIONPRESENTDIRECT = auto (AL_APIENTRY*)(ALCcontext *context, const ALchar *extname) AL_API_NOEXCEPT -> ALboolean;
using LPALGETPROCADDRESSDIRECT =     auto (AL_APIENTRY*)(ALCcontext *context, const ALchar *fname) AL_API_NOEXCEPT -> void*;
using LPALGETENUMVALUEDIRECT =       auto (AL_APIENTRY*)(ALCcontext *context, const ALchar *ename) AL_API_NOEXCEPT -> ALenum;
using LPALLISTENERFDIRECT =     void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALfloat value) AL_API_NOEXCEPT;
using LPALLISTENER3FDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
using LPALLISTENERFVDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
using LPALLISTENERIDIRECT =     void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALint value) AL_API_NOEXCEPT;
using LPALLISTENER3IDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
using LPALLISTENERIVDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALenum param, const ALint *values) AL_API_NOEXCEPT;
using LPALGETLISTENERFDIRECT =  void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
using LPALGETLISTENER3FDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
using LPALGETLISTENERFVDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGETLISTENERIDIRECT =  void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALint *value) AL_API_NOEXCEPT;
using LPALGETLISTENER3IDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
using LPALGETLISTENERIVDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALGENSOURCESDIRECT =           void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, ALuint *sources) AL_API_NOEXCEPT;
using LPALDELETESOURCESDIRECT =        void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALISSOURCEDIRECT =             auto (AL_APIENTRY*)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT -> ALboolean;
using LPALSOURCEFDIRECT =              void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALfloat value) AL_API_NOEXCEPT;
using LPALSOURCE3FDIRECT =             void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
using LPALSOURCEFVDIRECT =             void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
using LPALSOURCEIDIRECT =              void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALint value) AL_API_NOEXCEPT;
using LPALSOURCE3IDIRECT =             void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
using LPALSOURCEIVDIRECT =             void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, const ALint *values) AL_API_NOEXCEPT;
using LPALGETSOURCEFDIRECT =           void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
using LPALGETSOURCE3FDIRECT =          void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
using LPALGETSOURCEFVDIRECT =          void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGETSOURCEIDIRECT =           void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALint *value) AL_API_NOEXCEPT;
using LPALGETSOURCE3IDIRECT =          void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
using LPALGETSOURCEIVDIRECT =          void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALSOURCEPLAYDIRECT =           void (AL_APIENTRY*)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
using LPALSOURCESTOPDIRECT =           void (AL_APIENTRY*)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
using LPALSOURCEREWINDDIRECT =         void (AL_APIENTRY*)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
using LPALSOURCEPAUSEDIRECT =          void (AL_APIENTRY*)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
using LPALSOURCEPLAYVDIRECT =          void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALSOURCESTOPVDIRECT =          void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALSOURCEREWINDVDIRECT =        void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALSOURCEPAUSEVDIRECT =         void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
using LPALSOURCEQUEUEBUFFERSDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALsizei nb, const ALuint *buffers) AL_API_NOEXCEPT;
using LPALSOURCEUNQUEUEBUFFERSDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALsizei nb, ALuint *buffers) AL_API_NOEXCEPT;
using LPALGENBUFFERSDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, ALuint *buffers) AL_API_NOEXCEPT;
using LPALDELETEBUFFERSDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *buffers) AL_API_NOEXCEPT;
using LPALISBUFFERDIRECT =      auto (AL_APIENTRY*)(ALCcontext *context, ALuint buffer) AL_API_NOEXCEPT -> ALboolean;
using LPALBUFFERDATADIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei samplerate) AL_API_NOEXCEPT;
using LPALBUFFERFDIRECT =       void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value) AL_API_NOEXCEPT;
using LPALBUFFER3FDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
using LPALBUFFERFVDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
using LPALBUFFERIDIRECT =       void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALint value) AL_API_NOEXCEPT;
using LPALBUFFER3IDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
using LPALBUFFERIVDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, const ALint *values) AL_API_NOEXCEPT;
using LPALGETBUFFERFDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
using LPALGETBUFFER3FDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
using LPALGETBUFFERFVDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGETBUFFERIDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALint *value) AL_API_NOEXCEPT;
using LPALGETBUFFER3IDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
using LPALGETBUFFERIVDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALint *values) AL_API_NOEXCEPT;
/* ALC_EXT_EFX */
using LPALGENEFFECTSDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, ALuint *effects) AL_API_NOEXCEPT;
using LPALDELETEEFFECTSDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *effects) AL_API_NOEXCEPT;
using LPALISEFFECTDIRECT =      auto (AL_APIENTRY*)(ALCcontext *context, ALuint effect) AL_API_NOEXCEPT -> ALboolean;
using LPALEFFECTIDIRECT =       void (AL_APIENTRY*)(ALCcontext *context, ALuint effect, ALenum param, ALint value) AL_API_NOEXCEPT;
using LPALEFFECTIVDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint effect, ALenum param, const ALint *values) AL_API_NOEXCEPT;
using LPALEFFECTFDIRECT =       void (AL_APIENTRY*)(ALCcontext *context, ALuint effect, ALenum param, ALfloat value) AL_API_NOEXCEPT;
using LPALEFFECTFVDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint effect, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
using LPALGETEFFECTIDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALuint effect, ALenum param, ALint *value) AL_API_NOEXCEPT;
using LPALGETEFFECTIVDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint effect, ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALGETEFFECTFDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALuint effect, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
using LPALGETEFFECTFVDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint effect, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGENFILTERSDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, ALuint *filters) AL_API_NOEXCEPT;
using LPALDELETEFILTERSDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *filters) AL_API_NOEXCEPT;
using LPALISFILTERDIRECT =      auto (AL_APIENTRY*)(ALCcontext *context, ALuint filter) AL_API_NOEXCEPT -> ALboolean;
using LPALFILTERIDIRECT =       void (AL_APIENTRY*)(ALCcontext *context, ALuint filter, ALenum param, ALint value) AL_API_NOEXCEPT;
using LPALFILTERIVDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint filter, ALenum param, const ALint *values) AL_API_NOEXCEPT;
using LPALFILTERFDIRECT =       void (AL_APIENTRY*)(ALCcontext *context, ALuint filter, ALenum param, ALfloat value) AL_API_NOEXCEPT;
using LPALFILTERFVDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint filter, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
using LPALGETFILTERIDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALuint filter, ALenum param, ALint *value) AL_API_NOEXCEPT;
using LPALGETFILTERIVDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint filter, ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALGETFILTERFDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALuint filter, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
using LPALGETFILTERFVDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint filter, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
using LPALGENAUXILIARYEFFECTSLOTSDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, ALuint *effectslots) AL_API_NOEXCEPT;
using LPALDELETEAUXILIARYEFFECTSLOTSDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *effectslots) AL_API_NOEXCEPT;
using LPALISAUXILIARYEFFECTSLOTDIRECT =      auto (AL_APIENTRY*)(ALCcontext *context, ALuint effectslot) AL_API_NOEXCEPT -> ALboolean;
using LPALAUXILIARYEFFECTSLOTIDIRECT =       void (AL_APIENTRY*)(ALCcontext *context, ALuint effectslot, ALenum param, ALint value) AL_API_NOEXCEPT;
using LPALAUXILIARYEFFECTSLOTIVDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint effectslot, ALenum param, const ALint *values) AL_API_NOEXCEPT;
using LPALAUXILIARYEFFECTSLOTFDIRECT =       void (AL_APIENTRY*)(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat value) AL_API_NOEXCEPT;
using LPALAUXILIARYEFFECTSLOTFVDIRECT =      void (AL_APIENTRY*)(ALCcontext *context, ALuint effectslot, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
using LPALGETAUXILIARYEFFECTSLOTIDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALuint effectslot, ALenum param, ALint *value) AL_API_NOEXCEPT;
using LPALGETAUXILIARYEFFECTSLOTIVDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint effectslot, ALenum param, ALint *values) AL_API_NOEXCEPT;
using LPALGETAUXILIARYEFFECTSLOTFDIRECT =    void (AL_APIENTRY*)(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
using LPALGETAUXILIARYEFFECTSLOTFVDIRECT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
/* AL_EXT_BUFFER_DATA_STATIC */
using LPALBUFFERDATASTATICDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum format, ALvoid *data, ALsizei size, ALsizei freq) AL_API_NOEXCEPT;
/* AL_EXT_debug */
using LPALDEBUGMESSAGECALLBACKDIRECTEXT = void (AL_APIENTRY*)(ALCcontext *context, ALDEBUGPROCEXT callback, void *userParam) AL_API_NOEXCEPT;
using LPALDEBUGMESSAGEINSERTDIRECTEXT =   void (AL_APIENTRY*)(ALCcontext *context, ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) AL_API_NOEXCEPT;
using LPALDEBUGMESSAGECONTROLDIRECTEXT =  void (AL_APIENTRY*)(ALCcontext *context, ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) AL_API_NOEXCEPT;
using LPALPUSHDEBUGGROUPDIRECTEXT =       void (AL_APIENTRY*)(ALCcontext *context, ALenum source, ALuint id, ALsizei length, const ALchar *message) AL_API_NOEXCEPT;
using LPALPOPDEBUGGROUPDIRECTEXT =        void (AL_APIENTRY*)(ALCcontext *context) AL_API_NOEXCEPT;
using LPALGETDEBUGMESSAGELOGDIRECTEXT =   auto (AL_APIENTRY*)(ALCcontext *context, ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf) AL_API_NOEXCEPT -> ALuint;
using LPALOBJECTLABELDIRECTEXT =          void (AL_APIENTRY*)(ALCcontext *context, ALenum identifier, ALuint name, ALsizei length, const ALchar *label) AL_API_NOEXCEPT;
using LPALGETOBJECTLABELDIRECTEXT =       void (AL_APIENTRY*)(ALCcontext *context, ALenum identifier, ALuint name, ALsizei bufSize, ALsizei *length, ALchar *label) AL_API_NOEXCEPT;
using LPALGETPOINTERDIRECTEXT =           auto (AL_APIENTRY*)(ALCcontext *context, ALenum pname) AL_API_NOEXCEPT -> void*;
using LPALGETPOINTERVDIRECTEXT =          void (AL_APIENTRY*)(ALCcontext *context, ALenum pname, void **values) AL_API_NOEXCEPT;
/* AL_EXT_FOLDBACK */
using LPALREQUESTFOLDBACKSTARTDIRECT = void (AL_APIENTRY*)(ALCcontext *context, ALenum mode, ALsizei count, ALsizei length, ALfloat *mem, LPALFOLDBACKCALLBACK callback) AL_API_NOEXCEPT;
using LPALREQUESTFOLDBACKSTOPDIRECT =  void (AL_APIENTRY*)(ALCcontext *context) AL_API_NOEXCEPT;
/* AL_SOFT_buffer_sub_data */
using LPALBUFFERSUBDATADIRECTSOFT = void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length) AL_API_NOEXCEPT;
/* AL_SOFT_source_latency */
using LPALSOURCEDDIRECTSOFT =       void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALdouble) AL_API_NOEXCEPT;
using LPALSOURCE3DDIRECTSOFT =      void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALdouble,ALdouble,ALdouble) AL_API_NOEXCEPT;
using LPALSOURCEDVDIRECTSOFT =      void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,const ALdouble*) AL_API_NOEXCEPT;
using LPALGETSOURCEDDIRECTSOFT =    void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALdouble*) AL_API_NOEXCEPT;
using LPALGETSOURCE3DDIRECTSOFT =   void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALdouble*,ALdouble*,ALdouble*) AL_API_NOEXCEPT;
using LPALGETSOURCEDVDIRECTSOFT =   void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALdouble*) AL_API_NOEXCEPT;
using LPALSOURCEI64DIRECTSOFT =     void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALint64SOFT) AL_API_NOEXCEPT;
using LPALSOURCE3I64DIRECTSOFT =    void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALint64SOFT,ALint64SOFT,ALint64SOFT) AL_API_NOEXCEPT;
using LPALSOURCEI64VDIRECTSOFT =    void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,const ALint64SOFT*) AL_API_NOEXCEPT;
using LPALGETSOURCEI64DIRECTSOFT =  void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALint64SOFT*) AL_API_NOEXCEPT;
using LPALGETSOURCE3I64DIRECTSOFT = void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALint64SOFT*,ALint64SOFT*,ALint64SOFT*) AL_API_NOEXCEPT;
using LPALGETSOURCEI64VDIRECTSOFT = void (AL_APIENTRY*)(ALCcontext*,ALuint,ALenum,ALint64SOFT*) AL_API_NOEXCEPT;
/* AL_SOFT_deferred_updates */
using LPALDEFERUPDATESDIRECTSOFT =   void (AL_APIENTRY*)(ALCcontext *context) AL_API_NOEXCEPT;
using LPALPROCESSUPDATESDIRECTSOFT = void (AL_APIENTRY*)(ALCcontext *context) AL_API_NOEXCEPT;
/* AL_SOFT_source_resampler */
using LPALGETSTRINGIDIRECTSOFT = auto (AL_APIENTRY*)(ALCcontext *context, ALenum pname, ALsizei index) AL_API_NOEXCEPT -> const ALchar*;
/* AL_SOFT_events */
using LPALEVENTCONTROLDIRECTSOFT =  void (AL_APIENTRY*)(ALCcontext *context, ALsizei count, const ALenum *types, ALboolean enable) AL_API_NOEXCEPT;
using LPALEVENTCALLBACKDIRECTSOFT = void (AL_APIENTRY*)(ALCcontext *context, ALEVENTPROCSOFT callback, void *userParam) AL_API_NOEXCEPT;
using LPALGETPOINTERDIRECTSOFT =    auto (AL_APIENTRY*)(ALCcontext *context, ALenum pname) AL_API_NOEXCEPT -> void*;
using LPALGETPOINTERVDIRECTSOFT =   void (AL_APIENTRY*)(ALCcontext *context, ALenum pname, void **values) AL_API_NOEXCEPT;
/* AL_SOFT_callback_buffer */
using LPALBUFFERCALLBACKDIRECTSOFT = void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum format, ALsizei freq, ALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr) AL_API_NOEXCEPT;
using LPALGETBUFFERPTRDIRECTSOFT =   void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **value) AL_API_NOEXCEPT;
using LPALGETBUFFER3PTRDIRECTSOFT =  void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **value1, ALvoid **value2, ALvoid **value3) AL_API_NOEXCEPT;
using LPALGETBUFFERPTRVDIRECTSOFT =  void (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **values) AL_API_NOEXCEPT;
/* AL_SOFT_source_start_delay */
using LPALSOURCEPLAYATTIMEDIRECTSOFT =  void (AL_APIENTRY*)(ALCcontext *context, ALuint source, ALint64SOFT start_time) AL_API_NOEXCEPT;
using LPALSOURCEPLAYATTIMEVDIRECTSOFT = void (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *sources, ALint64SOFT start_time) AL_API_NOEXCEPT;
/* EAX */
using LPEAXSETDIRECT =           auto (AL_APIENTRY*)(ALCcontext *context, const _GUID *property_set_id, ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) AL_API_NOEXCEPT -> ALenum;
using LPEAXGETDIRECT =           auto (AL_APIENTRY*)(ALCcontext *context, const _GUID *property_set_id, ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) AL_API_NOEXCEPT -> ALenum;
using LPEAXSETBUFFERMODEDIRECT = auto (AL_APIENTRY*)(ALCcontext *context, ALsizei n, const ALuint *buffers, ALint value) AL_API_NOEXCEPT -> ALboolean;
using LPEAXGETBUFFERMODEDIRECT = auto (AL_APIENTRY*)(ALCcontext *context, ALuint buffer, ALint *pReserved) AL_API_NOEXCEPT -> ALenum;
#ifdef AL_ALEXT_PROTOTYPES
auto ALC_APIENTRY alcGetProcAddress2(ALCdevice *device, const ALCchar *funcName) AL_API_NOEXCEPT -> ALCvoid*;

void AL_APIENTRY alEnableDirect(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT;
void AL_APIENTRY alDisableDirect(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT;
auto AL_APIENTRY alIsEnabledDirect(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT -> ALboolean;

void AL_APIENTRY alDopplerFactorDirect(ALCcontext *context, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alSpeedOfSoundDirect(ALCcontext *context, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alDistanceModelDirect(ALCcontext *context, ALenum distanceModel) AL_API_NOEXCEPT;

auto AL_APIENTRY alGetStringDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> const ALchar*;
void AL_APIENTRY alGetBooleanvDirect(ALCcontext *context, ALenum param, ALboolean *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetIntegervDirect(ALCcontext *context, ALenum param, ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFloatvDirect(ALCcontext *context, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetDoublevDirect(ALCcontext *context, ALenum param, ALdouble *values) AL_API_NOEXCEPT;
auto AL_APIENTRY alGetBooleanDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> ALboolean;
auto AL_APIENTRY alGetIntegerDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> ALint;
auto AL_APIENTRY alGetFloatDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> ALfloat;
auto AL_APIENTRY alGetDoubleDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT -> ALdouble;

auto AL_APIENTRY alGetErrorDirect(ALCcontext *context) AL_API_NOEXCEPT -> ALenum;
auto AL_APIENTRY alIsExtensionPresentDirect(ALCcontext *context, const ALchar *extname) AL_API_NOEXCEPT -> ALboolean;
auto AL_APIENTRY alGetProcAddressDirect(ALCcontext *context, const ALchar *fname) AL_API_NOEXCEPT -> void*;
auto AL_APIENTRY alGetEnumValueDirect(ALCcontext *context, const ALchar *ename) AL_API_NOEXCEPT -> ALenum;

void AL_APIENTRY alListenerfDirect(ALCcontext *context, ALenum param, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alListener3fDirect(ALCcontext *context, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
void AL_APIENTRY alListenerfvDirect(ALCcontext *context, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alListeneriDirect(ALCcontext *context, ALenum param, ALint value) AL_API_NOEXCEPT;
void AL_APIENTRY alListener3iDirect(ALCcontext *context, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
void AL_APIENTRY alListenerivDirect(ALCcontext *context, ALenum param, const ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListenerfDirect(ALCcontext *context, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListener3fDirect(ALCcontext *context, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListenerfvDirect(ALCcontext *context, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListeneriDirect(ALCcontext *context, ALenum param, ALint *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListener3iDirect(ALCcontext *context, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListenerivDirect(ALCcontext *context, ALenum param, ALint *values) AL_API_NOEXCEPT;

void AL_APIENTRY alGenSourcesDirect(ALCcontext *context, ALsizei n, ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteSourcesDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
auto AL_APIENTRY alIsSourceDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT -> ALboolean;
void AL_APIENTRY alSourcefDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alSource3fDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcefvDirect(ALCcontext *context, ALuint source, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceiDirect(ALCcontext *context, ALuint source, ALenum param, ALint value) AL_API_NOEXCEPT;
void AL_APIENTRY alSource3iDirect(ALCcontext *context, ALuint source, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceivDirect(ALCcontext *context, ALuint source, ALenum param, const ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcefDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSource3fDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcefvDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourceiDirect(ALCcontext *context, ALuint source,  ALenum param, ALint *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSource3iDirect(ALCcontext *context, ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourceivDirect(ALCcontext *context, ALuint source,  ALenum param, ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePlayDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceStopDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceRewindDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePauseDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePlayvDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceStopvDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceRewindvDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePausevDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceQueueBuffersDirect(ALCcontext *context, ALuint source, ALsizei nb, const ALuint *buffers) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceUnqueueBuffersDirect(ALCcontext *context, ALuint source, ALsizei nb, ALuint *buffers) AL_API_NOEXCEPT;

void AL_APIENTRY alGenBuffersDirect(ALCcontext *context, ALsizei n, ALuint *buffers) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteBuffersDirect(ALCcontext *context, ALsizei n, const ALuint *buffers) AL_API_NOEXCEPT;
auto AL_APIENTRY alIsBufferDirect(ALCcontext *context, ALuint buffer) AL_API_NOEXCEPT -> ALboolean;
void AL_APIENTRY alBufferDataDirect(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei samplerate) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferfDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alBuffer3fDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferfvDirect(ALCcontext *context, ALuint buffer, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferiDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint value) AL_API_NOEXCEPT;
void AL_APIENTRY alBuffer3iDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferivDirect(ALCcontext *context, ALuint buffer, ALenum param, const ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferfDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBuffer3fDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferfvDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferiDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBuffer3iDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferivDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint *values) AL_API_NOEXCEPT;

void AL_APIENTRY alGenEffectsDirect(ALCcontext *context, ALsizei n, ALuint *effects) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteEffectsDirect(ALCcontext *context, ALsizei n, const ALuint *effects) AL_API_NOEXCEPT;
auto AL_APIENTRY alIsEffectDirect(ALCcontext *context, ALuint effect) AL_API_NOEXCEPT -> ALboolean;
void AL_APIENTRY alEffectiDirect(ALCcontext *context, ALuint effect, ALenum param, ALint iValue) AL_API_NOEXCEPT;
void AL_APIENTRY alEffectivDirect(ALCcontext *context, ALuint effect, ALenum param, const ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alEffectfDirect(ALCcontext *context, ALuint effect, ALenum param, ALfloat flValue) AL_API_NOEXCEPT;
void AL_APIENTRY alEffectfvDirect(ALCcontext *context, ALuint effect, ALenum param, const ALfloat *pflValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetEffectiDirect(ALCcontext *context, ALuint effect, ALenum param, ALint *piValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetEffectivDirect(ALCcontext *context, ALuint effect, ALenum param, ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetEffectfDirect(ALCcontext *context, ALuint effect, ALenum param, ALfloat *pflValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetEffectfvDirect(ALCcontext *context, ALuint effect, ALenum param, ALfloat *pflValues) AL_API_NOEXCEPT;

void AL_APIENTRY alGenFiltersDirect(ALCcontext *context, ALsizei n, ALuint *filters) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteFiltersDirect(ALCcontext *context, ALsizei n, const ALuint *filters) AL_API_NOEXCEPT;
auto AL_APIENTRY alIsFilterDirect(ALCcontext *context, ALuint filter) AL_API_NOEXCEPT -> ALboolean;
void AL_APIENTRY alFilteriDirect(ALCcontext *context, ALuint filter, ALenum param, ALint iValue) AL_API_NOEXCEPT;
void AL_APIENTRY alFilterivDirect(ALCcontext *context, ALuint filter, ALenum param, const ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alFilterfDirect(ALCcontext *context, ALuint filter, ALenum param, ALfloat flValue) AL_API_NOEXCEPT;
void AL_APIENTRY alFilterfvDirect(ALCcontext *context, ALuint filter, ALenum param, const ALfloat *pflValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFilteriDirect(ALCcontext *context, ALuint filter, ALenum param, ALint *piValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFilterivDirect(ALCcontext *context, ALuint filter, ALenum param, ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFilterfDirect(ALCcontext *context, ALuint filter, ALenum param, ALfloat *pflValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFilterfvDirect(ALCcontext *context, ALuint filter, ALenum param, ALfloat *pflValues) AL_API_NOEXCEPT;

void AL_APIENTRY alGenAuxiliaryEffectSlotsDirect(ALCcontext *context, ALsizei n, ALuint *effectslots) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteAuxiliaryEffectSlotsDirect(ALCcontext *context, ALsizei n, const ALuint *effectslots) AL_API_NOEXCEPT;
auto AL_APIENTRY alIsAuxiliaryEffectSlotDirect(ALCcontext *context, ALuint effectslot) AL_API_NOEXCEPT -> ALboolean;
void AL_APIENTRY alAuxiliaryEffectSlotiDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALint iValue) AL_API_NOEXCEPT;
void AL_APIENTRY alAuxiliaryEffectSlotivDirect(ALCcontext *context, ALuint effectslot, ALenum param, const ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alAuxiliaryEffectSlotfDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat flValue) AL_API_NOEXCEPT;
void AL_APIENTRY alAuxiliaryEffectSlotfvDirect(ALCcontext *context, ALuint effectslot, ALenum param, const ALfloat *pflValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetAuxiliaryEffectSlotiDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALint *piValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetAuxiliaryEffectSlotivDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetAuxiliaryEffectSlotfDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *pflValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetAuxiliaryEffectSlotfvDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *pflValues) AL_API_NOEXCEPT;

void AL_APIENTRY alBufferDataStaticDirect(ALCcontext *context, ALuint buffer, ALenum format, ALvoid *data, ALsizei size, ALsizei freq) AL_API_NOEXCEPT;

void AL_APIENTRY alDebugMessageCallbackDirectEXT(ALCcontext *context, ALDEBUGPROCEXT callback, void *userParam) AL_API_NOEXCEPT;
void AL_APIENTRY alDebugMessageInsertDirectEXT(ALCcontext *context, ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) AL_API_NOEXCEPT;
void AL_APIENTRY alDebugMessageControlDirectEXT(ALCcontext *context, ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) AL_API_NOEXCEPT;
void AL_APIENTRY alPushDebugGroupDirectEXT(ALCcontext *context, ALenum source, ALuint id, ALsizei length, const ALchar *message) AL_API_NOEXCEPT;
void AL_APIENTRY alPopDebugGroupDirectEXT(ALCcontext *context) AL_API_NOEXCEPT;
auto AL_APIENTRY alGetDebugMessageLogDirectEXT(ALCcontext *context, ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf) AL_API_NOEXCEPT -> ALuint;
void AL_APIENTRY alObjectLabelDirectEXT(ALCcontext *context, ALenum identifier, ALuint name, ALsizei length, const ALchar *label) AL_API_NOEXCEPT;
void AL_APIENTRY alGetObjectLabelDirectEXT(ALCcontext *context, ALenum identifier, ALuint name, ALsizei bufSize, ALsizei *length, ALchar *label) AL_API_NOEXCEPT;
auto AL_APIENTRY alGetPointerDirectEXT(ALCcontext *context, ALenum pname) AL_API_NOEXCEPT -> void*;
void AL_APIENTRY alGetPointervDirectEXT(ALCcontext *context, ALenum pname, void **values) AL_API_NOEXCEPT;

void AL_APIENTRY alRequestFoldbackStartDirect(ALCcontext *context, ALenum mode, ALsizei count, ALsizei length, ALfloat *mem, LPALFOLDBACKCALLBACK callback) AL_API_NOEXCEPT;
void AL_APIENTRY alRequestFoldbackStopDirect(ALCcontext *context) AL_API_NOEXCEPT;

void AL_APIENTRY alBufferSubDataDirectSOFT(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length) AL_API_NOEXCEPT;

void AL_APIENTRY alSourcedDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble value) AL_API_NOEXCEPT;
void AL_APIENTRY alSource3dDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble value1, ALdouble value2, ALdouble value3) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcedvDirectSOFT(ALCcontext *context, ALuint source, ALenum param, const ALdouble *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcedDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSource3dDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble *value1, ALdouble *value2, ALdouble *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcedvDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble *values) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcei64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT value) AL_API_NOEXCEPT;
void AL_APIENTRY alSource3i64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT value1, ALint64SOFT value2, ALint64SOFT value3) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcei64vDirectSOFT(ALCcontext *context, ALuint source, ALenum param, const ALint64SOFT *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcei64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSource3i64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT *value1, ALint64SOFT *value2, ALint64SOFT *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcei64vDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT *values) AL_API_NOEXCEPT;

void AL_APIENTRY alDeferUpdatesDirectSOFT(ALCcontext *context) AL_API_NOEXCEPT;
void AL_APIENTRY alProcessUpdatesDirectSOFT(ALCcontext *context) AL_API_NOEXCEPT;

auto AL_APIENTRY alGetStringiDirectSOFT(ALCcontext *context, ALenum pname, ALsizei index) AL_API_NOEXCEPT -> const ALchar*;

void AL_APIENTRY alEventControlDirectSOFT(ALCcontext *context, ALsizei count, const ALenum *types, ALboolean enable) AL_API_NOEXCEPT;
void AL_APIENTRY alEventCallbackDirectSOFT(ALCcontext *context, ALEVENTPROCSOFT callback, void *userParam) AL_API_NOEXCEPT;
auto AL_APIENTRY alGetPointerDirectSOFT(ALCcontext *context, ALenum pname) AL_API_NOEXCEPT -> void*;
void AL_APIENTRY alGetPointervDirectSOFT(ALCcontext *context, ALenum pname, void **values) AL_API_NOEXCEPT;

void AL_APIENTRY alBufferCallbackDirectSOFT(ALCcontext *context, ALuint buffer, ALenum format, ALsizei freq, ALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferPtrDirectSOFT(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **ptr) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBuffer3PtrDirectSOFT(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **ptr0, ALvoid **ptr1, ALvoid **ptr2) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferPtrvDirectSOFT(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **ptr) AL_API_NOEXCEPT;

void AL_APIENTRY alSourcePlayAtTimeDirectSOFT(ALCcontext *context, ALuint source, ALint64SOFT start_time) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePlayAtTimevDirectSOFT(ALCcontext *context, ALsizei n, const ALuint *sources, ALint64SOFT start_time) AL_API_NOEXCEPT;

auto AL_APIENTRY EAXSetDirect(ALCcontext *context, const _GUID *property_set_id, ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) AL_API_NOEXCEPT -> ALenum;
auto AL_APIENTRY EAXGetDirect(ALCcontext *context, const _GUID *property_set_id, ALuint property_id, ALuint source_id, ALvoid *value, ALuint value_size) AL_API_NOEXCEPT -> ALenum;
auto AL_APIENTRY EAXSetBufferModeDirect(ALCcontext *context, ALsizei n, const ALuint *buffers, ALint value) AL_API_NOEXCEPT -> ALboolean;
auto AL_APIENTRY EAXGetBufferModeDirect(ALCcontext *context, ALuint buffer, ALint *pReserved) AL_API_NOEXCEPT -> ALenum;
#endif

/*** AL_SOFT_bformat_hoa ***/
ENUMDCL AL_UNPACK_AMBISONIC_ORDER_SOFT =         0x199D;

#undef ENUMDCL

} /* extern "C" */
