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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "version.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <ctype.h>
#include <signal.h>

#include <cmath>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <numeric>
#include <algorithm>
#include <functional>

#include "alMain.h"
#include "alcontext.h"
#include "alSource.h"
#include "alListener.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alFilter.h"
#include "alEffect.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "mastering.h"
#include "bformatdec.h"
#include "uhjfilter.h"
#include "alu.h"
#include "alconfig.h"
#include "ringbuffer.h"
#include "filters/splitter.h"
#include "bs2b.h"

#include "fpu_modes.h"
#include "cpu_caps.h"
#include "compat.h"
#include "threads.h"
#include "alexcpt.h"
#include "almalloc.h"

#include "backends/base.h"
#include "backends/null.h"
#include "backends/loopback.h"
#ifdef HAVE_JACK
#include "backends/jack.h"
#endif
#ifdef HAVE_PULSEAUDIO
#include "backends/pulseaudio.h"
#endif
#ifdef HAVE_ALSA
#include "backends/alsa.h"
#endif
#ifdef HAVE_WASAPI
#include "backends/wasapi.h"
#endif
#ifdef HAVE_COREAUDIO
#include "backends/coreaudio.h"
#endif
#ifdef HAVE_OPENSL
#include "backends/opensl.h"
#endif
#ifdef HAVE_SOLARIS
#include "backends/solaris.h"
#endif
#ifdef HAVE_SNDIO
#include "backends/sndio.h"
#endif
#ifdef HAVE_OSS
#include "backends/oss.h"
#endif
#ifdef HAVE_QSA
#include "backends/qsa.h"
#endif
#ifdef HAVE_DSOUND
#include "backends/dsound.h"
#endif
#ifdef HAVE_WINMM
#include "backends/winmm.h"
#endif
#ifdef HAVE_PORTAUDIO
#include "backends/portaudio.h"
#endif
#ifdef HAVE_SDL2
#include "backends/sdl2.h"
#endif
#ifdef HAVE_WAVE
#include "backends/wave.h"
#endif


namespace {

using namespace std::placeholders;
using std::chrono::seconds;
using std::chrono::nanoseconds;


/************************************************
 * Backends
 ************************************************/
struct BackendInfo {
    const char *name;
    BackendFactory& (*getFactory)(void);
};

BackendInfo PlaybackBackend;
BackendInfo CaptureBackend;


/************************************************
 * Functions, enums, and errors
 ************************************************/
#define DECL(x) { #x, (ALCvoid*)(x) }
constexpr struct {
    const ALCchar *funcName;
    ALCvoid *address;
} alcFunctions[] = {
    DECL(alcCreateContext),
    DECL(alcMakeContextCurrent),
    DECL(alcProcessContext),
    DECL(alcSuspendContext),
    DECL(alcDestroyContext),
    DECL(alcGetCurrentContext),
    DECL(alcGetContextsDevice),
    DECL(alcOpenDevice),
    DECL(alcCloseDevice),
    DECL(alcGetError),
    DECL(alcIsExtensionPresent),
    DECL(alcGetProcAddress),
    DECL(alcGetEnumValue),
    DECL(alcGetString),
    DECL(alcGetIntegerv),
    DECL(alcCaptureOpenDevice),
    DECL(alcCaptureCloseDevice),
    DECL(alcCaptureStart),
    DECL(alcCaptureStop),
    DECL(alcCaptureSamples),

    DECL(alcSetThreadContext),
    DECL(alcGetThreadContext),

    DECL(alcLoopbackOpenDeviceSOFT),
    DECL(alcIsRenderFormatSupportedSOFT),
    DECL(alcRenderSamplesSOFT),

    DECL(alcDevicePauseSOFT),
    DECL(alcDeviceResumeSOFT),

    DECL(alcGetStringiSOFT),
    DECL(alcResetDeviceSOFT),

    DECL(alcGetInteger64vSOFT),

    DECL(alEnable),
    DECL(alDisable),
    DECL(alIsEnabled),
    DECL(alGetString),
    DECL(alGetBooleanv),
    DECL(alGetIntegerv),
    DECL(alGetFloatv),
    DECL(alGetDoublev),
    DECL(alGetBoolean),
    DECL(alGetInteger),
    DECL(alGetFloat),
    DECL(alGetDouble),
    DECL(alGetError),
    DECL(alIsExtensionPresent),
    DECL(alGetProcAddress),
    DECL(alGetEnumValue),
    DECL(alListenerf),
    DECL(alListener3f),
    DECL(alListenerfv),
    DECL(alListeneri),
    DECL(alListener3i),
    DECL(alListeneriv),
    DECL(alGetListenerf),
    DECL(alGetListener3f),
    DECL(alGetListenerfv),
    DECL(alGetListeneri),
    DECL(alGetListener3i),
    DECL(alGetListeneriv),
    DECL(alGenSources),
    DECL(alDeleteSources),
    DECL(alIsSource),
    DECL(alSourcef),
    DECL(alSource3f),
    DECL(alSourcefv),
    DECL(alSourcei),
    DECL(alSource3i),
    DECL(alSourceiv),
    DECL(alGetSourcef),
    DECL(alGetSource3f),
    DECL(alGetSourcefv),
    DECL(alGetSourcei),
    DECL(alGetSource3i),
    DECL(alGetSourceiv),
    DECL(alSourcePlayv),
    DECL(alSourceStopv),
    DECL(alSourceRewindv),
    DECL(alSourcePausev),
    DECL(alSourcePlay),
    DECL(alSourceStop),
    DECL(alSourceRewind),
    DECL(alSourcePause),
    DECL(alSourceQueueBuffers),
    DECL(alSourceUnqueueBuffers),
    DECL(alGenBuffers),
    DECL(alDeleteBuffers),
    DECL(alIsBuffer),
    DECL(alBufferData),
    DECL(alBufferf),
    DECL(alBuffer3f),
    DECL(alBufferfv),
    DECL(alBufferi),
    DECL(alBuffer3i),
    DECL(alBufferiv),
    DECL(alGetBufferf),
    DECL(alGetBuffer3f),
    DECL(alGetBufferfv),
    DECL(alGetBufferi),
    DECL(alGetBuffer3i),
    DECL(alGetBufferiv),
    DECL(alDopplerFactor),
    DECL(alDopplerVelocity),
    DECL(alSpeedOfSound),
    DECL(alDistanceModel),

    DECL(alGenFilters),
    DECL(alDeleteFilters),
    DECL(alIsFilter),
    DECL(alFilteri),
    DECL(alFilteriv),
    DECL(alFilterf),
    DECL(alFilterfv),
    DECL(alGetFilteri),
    DECL(alGetFilteriv),
    DECL(alGetFilterf),
    DECL(alGetFilterfv),
    DECL(alGenEffects),
    DECL(alDeleteEffects),
    DECL(alIsEffect),
    DECL(alEffecti),
    DECL(alEffectiv),
    DECL(alEffectf),
    DECL(alEffectfv),
    DECL(alGetEffecti),
    DECL(alGetEffectiv),
    DECL(alGetEffectf),
    DECL(alGetEffectfv),
    DECL(alGenAuxiliaryEffectSlots),
    DECL(alDeleteAuxiliaryEffectSlots),
    DECL(alIsAuxiliaryEffectSlot),
    DECL(alAuxiliaryEffectSloti),
    DECL(alAuxiliaryEffectSlotiv),
    DECL(alAuxiliaryEffectSlotf),
    DECL(alAuxiliaryEffectSlotfv),
    DECL(alGetAuxiliaryEffectSloti),
    DECL(alGetAuxiliaryEffectSlotiv),
    DECL(alGetAuxiliaryEffectSlotf),
    DECL(alGetAuxiliaryEffectSlotfv),

    DECL(alDeferUpdatesSOFT),
    DECL(alProcessUpdatesSOFT),

    DECL(alSourcedSOFT),
    DECL(alSource3dSOFT),
    DECL(alSourcedvSOFT),
    DECL(alGetSourcedSOFT),
    DECL(alGetSource3dSOFT),
    DECL(alGetSourcedvSOFT),
    DECL(alSourcei64SOFT),
    DECL(alSource3i64SOFT),
    DECL(alSourcei64vSOFT),
    DECL(alGetSourcei64SOFT),
    DECL(alGetSource3i64SOFT),
    DECL(alGetSourcei64vSOFT),

    DECL(alGetStringiSOFT),

    DECL(alBufferStorageSOFT),
    DECL(alMapBufferSOFT),
    DECL(alUnmapBufferSOFT),
    DECL(alFlushMappedBufferSOFT),

    DECL(alEventControlSOFT),
    DECL(alEventCallbackSOFT),
    DECL(alGetPointerSOFT),
    DECL(alGetPointervSOFT),
};
#undef DECL

#define DECL(x) { #x, (x) }
constexpr struct {
    const ALCchar *enumName;
    ALCenum value;
} alcEnumerations[] = {
    DECL(ALC_INVALID),
    DECL(ALC_FALSE),
    DECL(ALC_TRUE),

    DECL(ALC_MAJOR_VERSION),
    DECL(ALC_MINOR_VERSION),
    DECL(ALC_ATTRIBUTES_SIZE),
    DECL(ALC_ALL_ATTRIBUTES),
    DECL(ALC_DEFAULT_DEVICE_SPECIFIER),
    DECL(ALC_DEVICE_SPECIFIER),
    DECL(ALC_ALL_DEVICES_SPECIFIER),
    DECL(ALC_DEFAULT_ALL_DEVICES_SPECIFIER),
    DECL(ALC_EXTENSIONS),
    DECL(ALC_FREQUENCY),
    DECL(ALC_REFRESH),
    DECL(ALC_SYNC),
    DECL(ALC_MONO_SOURCES),
    DECL(ALC_STEREO_SOURCES),
    DECL(ALC_CAPTURE_DEVICE_SPECIFIER),
    DECL(ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER),
    DECL(ALC_CAPTURE_SAMPLES),
    DECL(ALC_CONNECTED),

    DECL(ALC_EFX_MAJOR_VERSION),
    DECL(ALC_EFX_MINOR_VERSION),
    DECL(ALC_MAX_AUXILIARY_SENDS),

    DECL(ALC_FORMAT_CHANNELS_SOFT),
    DECL(ALC_FORMAT_TYPE_SOFT),

    DECL(ALC_MONO_SOFT),
    DECL(ALC_STEREO_SOFT),
    DECL(ALC_QUAD_SOFT),
    DECL(ALC_5POINT1_SOFT),
    DECL(ALC_6POINT1_SOFT),
    DECL(ALC_7POINT1_SOFT),
    DECL(ALC_BFORMAT3D_SOFT),

    DECL(ALC_BYTE_SOFT),
    DECL(ALC_UNSIGNED_BYTE_SOFT),
    DECL(ALC_SHORT_SOFT),
    DECL(ALC_UNSIGNED_SHORT_SOFT),
    DECL(ALC_INT_SOFT),
    DECL(ALC_UNSIGNED_INT_SOFT),
    DECL(ALC_FLOAT_SOFT),

    DECL(ALC_HRTF_SOFT),
    DECL(ALC_DONT_CARE_SOFT),
    DECL(ALC_HRTF_STATUS_SOFT),
    DECL(ALC_HRTF_DISABLED_SOFT),
    DECL(ALC_HRTF_ENABLED_SOFT),
    DECL(ALC_HRTF_DENIED_SOFT),
    DECL(ALC_HRTF_REQUIRED_SOFT),
    DECL(ALC_HRTF_HEADPHONES_DETECTED_SOFT),
    DECL(ALC_HRTF_UNSUPPORTED_FORMAT_SOFT),
    DECL(ALC_NUM_HRTF_SPECIFIERS_SOFT),
    DECL(ALC_HRTF_SPECIFIER_SOFT),
    DECL(ALC_HRTF_ID_SOFT),

    DECL(ALC_AMBISONIC_LAYOUT_SOFT),
    DECL(ALC_AMBISONIC_SCALING_SOFT),
    DECL(ALC_AMBISONIC_ORDER_SOFT),
    DECL(ALC_ACN_SOFT),
    DECL(ALC_FUMA_SOFT),
    DECL(ALC_N3D_SOFT),
    DECL(ALC_SN3D_SOFT),

    DECL(ALC_OUTPUT_LIMITER_SOFT),

    DECL(ALC_NO_ERROR),
    DECL(ALC_INVALID_DEVICE),
    DECL(ALC_INVALID_CONTEXT),
    DECL(ALC_INVALID_ENUM),
    DECL(ALC_INVALID_VALUE),
    DECL(ALC_OUT_OF_MEMORY),


    DECL(AL_INVALID),
    DECL(AL_NONE),
    DECL(AL_FALSE),
    DECL(AL_TRUE),

    DECL(AL_SOURCE_RELATIVE),
    DECL(AL_CONE_INNER_ANGLE),
    DECL(AL_CONE_OUTER_ANGLE),
    DECL(AL_PITCH),
    DECL(AL_POSITION),
    DECL(AL_DIRECTION),
    DECL(AL_VELOCITY),
    DECL(AL_LOOPING),
    DECL(AL_BUFFER),
    DECL(AL_GAIN),
    DECL(AL_MIN_GAIN),
    DECL(AL_MAX_GAIN),
    DECL(AL_ORIENTATION),
    DECL(AL_REFERENCE_DISTANCE),
    DECL(AL_ROLLOFF_FACTOR),
    DECL(AL_CONE_OUTER_GAIN),
    DECL(AL_MAX_DISTANCE),
    DECL(AL_SEC_OFFSET),
    DECL(AL_SAMPLE_OFFSET),
    DECL(AL_BYTE_OFFSET),
    DECL(AL_SOURCE_TYPE),
    DECL(AL_STATIC),
    DECL(AL_STREAMING),
    DECL(AL_UNDETERMINED),
    DECL(AL_METERS_PER_UNIT),
    DECL(AL_LOOP_POINTS_SOFT),
    DECL(AL_DIRECT_CHANNELS_SOFT),

    DECL(AL_DIRECT_FILTER),
    DECL(AL_AUXILIARY_SEND_FILTER),
    DECL(AL_AIR_ABSORPTION_FACTOR),
    DECL(AL_ROOM_ROLLOFF_FACTOR),
    DECL(AL_CONE_OUTER_GAINHF),
    DECL(AL_DIRECT_FILTER_GAINHF_AUTO),
    DECL(AL_AUXILIARY_SEND_FILTER_GAIN_AUTO),
    DECL(AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO),

    DECL(AL_SOURCE_STATE),
    DECL(AL_INITIAL),
    DECL(AL_PLAYING),
    DECL(AL_PAUSED),
    DECL(AL_STOPPED),

    DECL(AL_BUFFERS_QUEUED),
    DECL(AL_BUFFERS_PROCESSED),

    DECL(AL_FORMAT_MONO8),
    DECL(AL_FORMAT_MONO16),
    DECL(AL_FORMAT_MONO_FLOAT32),
    DECL(AL_FORMAT_MONO_DOUBLE_EXT),
    DECL(AL_FORMAT_STEREO8),
    DECL(AL_FORMAT_STEREO16),
    DECL(AL_FORMAT_STEREO_FLOAT32),
    DECL(AL_FORMAT_STEREO_DOUBLE_EXT),
    DECL(AL_FORMAT_MONO_IMA4),
    DECL(AL_FORMAT_STEREO_IMA4),
    DECL(AL_FORMAT_MONO_MSADPCM_SOFT),
    DECL(AL_FORMAT_STEREO_MSADPCM_SOFT),
    DECL(AL_FORMAT_QUAD8_LOKI),
    DECL(AL_FORMAT_QUAD16_LOKI),
    DECL(AL_FORMAT_QUAD8),
    DECL(AL_FORMAT_QUAD16),
    DECL(AL_FORMAT_QUAD32),
    DECL(AL_FORMAT_51CHN8),
    DECL(AL_FORMAT_51CHN16),
    DECL(AL_FORMAT_51CHN32),
    DECL(AL_FORMAT_61CHN8),
    DECL(AL_FORMAT_61CHN16),
    DECL(AL_FORMAT_61CHN32),
    DECL(AL_FORMAT_71CHN8),
    DECL(AL_FORMAT_71CHN16),
    DECL(AL_FORMAT_71CHN32),
    DECL(AL_FORMAT_REAR8),
    DECL(AL_FORMAT_REAR16),
    DECL(AL_FORMAT_REAR32),
    DECL(AL_FORMAT_MONO_MULAW),
    DECL(AL_FORMAT_MONO_MULAW_EXT),
    DECL(AL_FORMAT_STEREO_MULAW),
    DECL(AL_FORMAT_STEREO_MULAW_EXT),
    DECL(AL_FORMAT_QUAD_MULAW),
    DECL(AL_FORMAT_51CHN_MULAW),
    DECL(AL_FORMAT_61CHN_MULAW),
    DECL(AL_FORMAT_71CHN_MULAW),
    DECL(AL_FORMAT_REAR_MULAW),
    DECL(AL_FORMAT_MONO_ALAW_EXT),
    DECL(AL_FORMAT_STEREO_ALAW_EXT),

    DECL(AL_FORMAT_BFORMAT2D_8),
    DECL(AL_FORMAT_BFORMAT2D_16),
    DECL(AL_FORMAT_BFORMAT2D_FLOAT32),
    DECL(AL_FORMAT_BFORMAT2D_MULAW),
    DECL(AL_FORMAT_BFORMAT3D_8),
    DECL(AL_FORMAT_BFORMAT3D_16),
    DECL(AL_FORMAT_BFORMAT3D_FLOAT32),
    DECL(AL_FORMAT_BFORMAT3D_MULAW),

    DECL(AL_FREQUENCY),
    DECL(AL_BITS),
    DECL(AL_CHANNELS),
    DECL(AL_SIZE),
    DECL(AL_UNPACK_BLOCK_ALIGNMENT_SOFT),
    DECL(AL_PACK_BLOCK_ALIGNMENT_SOFT),

    DECL(AL_SOURCE_RADIUS),

    DECL(AL_STEREO_ANGLES),

    DECL(AL_UNUSED),
    DECL(AL_PENDING),
    DECL(AL_PROCESSED),

    DECL(AL_NO_ERROR),
    DECL(AL_INVALID_NAME),
    DECL(AL_INVALID_ENUM),
    DECL(AL_INVALID_VALUE),
    DECL(AL_INVALID_OPERATION),
    DECL(AL_OUT_OF_MEMORY),

    DECL(AL_VENDOR),
    DECL(AL_VERSION),
    DECL(AL_RENDERER),
    DECL(AL_EXTENSIONS),

    DECL(AL_DOPPLER_FACTOR),
    DECL(AL_DOPPLER_VELOCITY),
    DECL(AL_DISTANCE_MODEL),
    DECL(AL_SPEED_OF_SOUND),
    DECL(AL_SOURCE_DISTANCE_MODEL),
    DECL(AL_DEFERRED_UPDATES_SOFT),
    DECL(AL_GAIN_LIMIT_SOFT),

    DECL(AL_INVERSE_DISTANCE),
    DECL(AL_INVERSE_DISTANCE_CLAMPED),
    DECL(AL_LINEAR_DISTANCE),
    DECL(AL_LINEAR_DISTANCE_CLAMPED),
    DECL(AL_EXPONENT_DISTANCE),
    DECL(AL_EXPONENT_DISTANCE_CLAMPED),

    DECL(AL_FILTER_TYPE),
    DECL(AL_FILTER_NULL),
    DECL(AL_FILTER_LOWPASS),
    DECL(AL_FILTER_HIGHPASS),
    DECL(AL_FILTER_BANDPASS),

    DECL(AL_LOWPASS_GAIN),
    DECL(AL_LOWPASS_GAINHF),

    DECL(AL_HIGHPASS_GAIN),
    DECL(AL_HIGHPASS_GAINLF),

    DECL(AL_BANDPASS_GAIN),
    DECL(AL_BANDPASS_GAINHF),
    DECL(AL_BANDPASS_GAINLF),

    DECL(AL_EFFECT_TYPE),
    DECL(AL_EFFECT_NULL),
    DECL(AL_EFFECT_REVERB),
    DECL(AL_EFFECT_EAXREVERB),
    DECL(AL_EFFECT_CHORUS),
    DECL(AL_EFFECT_DISTORTION),
    DECL(AL_EFFECT_ECHO),
    DECL(AL_EFFECT_FLANGER),
    DECL(AL_EFFECT_PITCH_SHIFTER),
    DECL(AL_EFFECT_FREQUENCY_SHIFTER),
#if 0
    DECL(AL_EFFECT_VOCAL_MORPHER),
#endif
    DECL(AL_EFFECT_RING_MODULATOR),
    DECL(AL_EFFECT_AUTOWAH),
    DECL(AL_EFFECT_COMPRESSOR),
    DECL(AL_EFFECT_EQUALIZER),
    DECL(AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT),
    DECL(AL_EFFECT_DEDICATED_DIALOGUE),

    DECL(AL_EFFECTSLOT_EFFECT),
    DECL(AL_EFFECTSLOT_GAIN),
    DECL(AL_EFFECTSLOT_AUXILIARY_SEND_AUTO),
    DECL(AL_EFFECTSLOT_NULL),

    DECL(AL_EAXREVERB_DENSITY),
    DECL(AL_EAXREVERB_DIFFUSION),
    DECL(AL_EAXREVERB_GAIN),
    DECL(AL_EAXREVERB_GAINHF),
    DECL(AL_EAXREVERB_GAINLF),
    DECL(AL_EAXREVERB_DECAY_TIME),
    DECL(AL_EAXREVERB_DECAY_HFRATIO),
    DECL(AL_EAXREVERB_DECAY_LFRATIO),
    DECL(AL_EAXREVERB_REFLECTIONS_GAIN),
    DECL(AL_EAXREVERB_REFLECTIONS_DELAY),
    DECL(AL_EAXREVERB_REFLECTIONS_PAN),
    DECL(AL_EAXREVERB_LATE_REVERB_GAIN),
    DECL(AL_EAXREVERB_LATE_REVERB_DELAY),
    DECL(AL_EAXREVERB_LATE_REVERB_PAN),
    DECL(AL_EAXREVERB_ECHO_TIME),
    DECL(AL_EAXREVERB_ECHO_DEPTH),
    DECL(AL_EAXREVERB_MODULATION_TIME),
    DECL(AL_EAXREVERB_MODULATION_DEPTH),
    DECL(AL_EAXREVERB_AIR_ABSORPTION_GAINHF),
    DECL(AL_EAXREVERB_HFREFERENCE),
    DECL(AL_EAXREVERB_LFREFERENCE),
    DECL(AL_EAXREVERB_ROOM_ROLLOFF_FACTOR),
    DECL(AL_EAXREVERB_DECAY_HFLIMIT),

    DECL(AL_REVERB_DENSITY),
    DECL(AL_REVERB_DIFFUSION),
    DECL(AL_REVERB_GAIN),
    DECL(AL_REVERB_GAINHF),
    DECL(AL_REVERB_DECAY_TIME),
    DECL(AL_REVERB_DECAY_HFRATIO),
    DECL(AL_REVERB_REFLECTIONS_GAIN),
    DECL(AL_REVERB_REFLECTIONS_DELAY),
    DECL(AL_REVERB_LATE_REVERB_GAIN),
    DECL(AL_REVERB_LATE_REVERB_DELAY),
    DECL(AL_REVERB_AIR_ABSORPTION_GAINHF),
    DECL(AL_REVERB_ROOM_ROLLOFF_FACTOR),
    DECL(AL_REVERB_DECAY_HFLIMIT),

    DECL(AL_CHORUS_WAVEFORM),
    DECL(AL_CHORUS_PHASE),
    DECL(AL_CHORUS_RATE),
    DECL(AL_CHORUS_DEPTH),
    DECL(AL_CHORUS_FEEDBACK),
    DECL(AL_CHORUS_DELAY),

    DECL(AL_DISTORTION_EDGE),
    DECL(AL_DISTORTION_GAIN),
    DECL(AL_DISTORTION_LOWPASS_CUTOFF),
    DECL(AL_DISTORTION_EQCENTER),
    DECL(AL_DISTORTION_EQBANDWIDTH),

    DECL(AL_ECHO_DELAY),
    DECL(AL_ECHO_LRDELAY),
    DECL(AL_ECHO_DAMPING),
    DECL(AL_ECHO_FEEDBACK),
    DECL(AL_ECHO_SPREAD),

    DECL(AL_FLANGER_WAVEFORM),
    DECL(AL_FLANGER_PHASE),
    DECL(AL_FLANGER_RATE),
    DECL(AL_FLANGER_DEPTH),
    DECL(AL_FLANGER_FEEDBACK),
    DECL(AL_FLANGER_DELAY),

    DECL(AL_FREQUENCY_SHIFTER_FREQUENCY),
    DECL(AL_FREQUENCY_SHIFTER_LEFT_DIRECTION),
    DECL(AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION),

    DECL(AL_RING_MODULATOR_FREQUENCY),
    DECL(AL_RING_MODULATOR_HIGHPASS_CUTOFF),
    DECL(AL_RING_MODULATOR_WAVEFORM),

    DECL(AL_PITCH_SHIFTER_COARSE_TUNE),
    DECL(AL_PITCH_SHIFTER_FINE_TUNE),

    DECL(AL_COMPRESSOR_ONOFF),

    DECL(AL_EQUALIZER_LOW_GAIN),
    DECL(AL_EQUALIZER_LOW_CUTOFF),
    DECL(AL_EQUALIZER_MID1_GAIN),
    DECL(AL_EQUALIZER_MID1_CENTER),
    DECL(AL_EQUALIZER_MID1_WIDTH),
    DECL(AL_EQUALIZER_MID2_GAIN),
    DECL(AL_EQUALIZER_MID2_CENTER),
    DECL(AL_EQUALIZER_MID2_WIDTH),
    DECL(AL_EQUALIZER_HIGH_GAIN),
    DECL(AL_EQUALIZER_HIGH_CUTOFF),

    DECL(AL_DEDICATED_GAIN),

    DECL(AL_AUTOWAH_ATTACK_TIME),
    DECL(AL_AUTOWAH_RELEASE_TIME),
    DECL(AL_AUTOWAH_RESONANCE),
    DECL(AL_AUTOWAH_PEAK_GAIN),

    DECL(AL_NUM_RESAMPLERS_SOFT),
    DECL(AL_DEFAULT_RESAMPLER_SOFT),
    DECL(AL_SOURCE_RESAMPLER_SOFT),
    DECL(AL_RESAMPLER_NAME_SOFT),

    DECL(AL_SOURCE_SPATIALIZE_SOFT),
    DECL(AL_AUTO_SOFT),

    DECL(AL_MAP_READ_BIT_SOFT),
    DECL(AL_MAP_WRITE_BIT_SOFT),
    DECL(AL_MAP_PERSISTENT_BIT_SOFT),
    DECL(AL_PRESERVE_DATA_BIT_SOFT),

    DECL(AL_EVENT_CALLBACK_FUNCTION_SOFT),
    DECL(AL_EVENT_CALLBACK_USER_PARAM_SOFT),
    DECL(AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT),
    DECL(AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT),
    DECL(AL_EVENT_TYPE_ERROR_SOFT),
    DECL(AL_EVENT_TYPE_PERFORMANCE_SOFT),
    DECL(AL_EVENT_TYPE_DEPRECATED_SOFT),
};
#undef DECL

constexpr ALCchar alcNoError[] = "No Error";
constexpr ALCchar alcErrInvalidDevice[] = "Invalid Device";
constexpr ALCchar alcErrInvalidContext[] = "Invalid Context";
constexpr ALCchar alcErrInvalidEnum[] = "Invalid Enum";
constexpr ALCchar alcErrInvalidValue[] = "Invalid Value";
constexpr ALCchar alcErrOutOfMemory[] = "Out of Memory";


/************************************************
 * Global variables
 ************************************************/

/* Enumerated device names */
constexpr ALCchar alcDefaultName[] = "OpenAL Soft\0";

std::string alcAllDevicesList;
std::string alcCaptureDeviceList;

/* Default is always the first in the list */
std::string alcDefaultAllDevicesSpecifier;
std::string alcCaptureDefaultDeviceSpecifier;

/* Default context extensions */
constexpr ALchar alExtList[] =
    "AL_EXT_ALAW "
    "AL_EXT_BFORMAT "
    "AL_EXT_DOUBLE "
    "AL_EXT_EXPONENT_DISTANCE "
    "AL_EXT_FLOAT32 "
    "AL_EXT_IMA4 "
    "AL_EXT_LINEAR_DISTANCE "
    "AL_EXT_MCFORMATS "
    "AL_EXT_MULAW "
    "AL_EXT_MULAW_BFORMAT "
    "AL_EXT_MULAW_MCFORMATS "
    "AL_EXT_OFFSET "
    "AL_EXT_source_distance_model "
    "AL_EXT_SOURCE_RADIUS "
    "AL_EXT_STEREO_ANGLES "
    "AL_LOKI_quadriphonic "
    "AL_SOFT_block_alignment "
    "AL_SOFT_deferred_updates "
    "AL_SOFT_direct_channels "
    "AL_SOFTX_effect_chain "
    "AL_SOFTX_events "
    "AL_SOFTX_filter_gain_ex "
    "AL_SOFT_gain_clamp_ex "
    "AL_SOFT_loop_points "
    "AL_SOFTX_map_buffer "
    "AL_SOFT_MSADPCM "
    "AL_SOFT_source_latency "
    "AL_SOFT_source_length "
    "AL_SOFT_source_resampler "
    "AL_SOFT_source_spatialize";

std::atomic<ALCenum> LastNullDeviceError{ALC_NO_ERROR};

/* Thread-local current context */
void ReleaseThreadCtx(ALCcontext *context)
{
    auto ref = DecrementRef(&context->ref);
    TRACEREF("%p decreasing refcount to %u\n", context, ref);
    ERR("Context %p current for thread being destroyed, possible leak!\n", context);
}

std::atomic<void(*)(ALCcontext*)> ThreadCtxProc{ReleaseThreadCtx};
class ThreadCtx {
    ALCcontext *ctx{nullptr};

public:
    ~ThreadCtx()
    {
        auto destruct = ThreadCtxProc.load();
        if(destruct && ctx)
            destruct(ctx);
        ctx = nullptr;
    }

    ALCcontext *get() const noexcept { return ctx; }
    void set(ALCcontext *ctx_) noexcept { ctx = ctx_; }
};
thread_local ThreadCtx LocalContext;
/* Process-wide current context */
std::atomic<ALCcontext*> GlobalContext{nullptr};

/* Flag to trap ALC device errors */
bool TrapALCError{false};

/* One-time configuration init control */
std::once_flag alc_config_once{};

/* Default effect that applies to sources that don't have an effect on send 0 */
ALeffect DefaultEffect;

/* Flag to specify if alcSuspendContext/alcProcessContext should defer/process
 * updates.
 */
bool SuspendDefers{true};


/************************************************
 * ALC information
 ************************************************/
constexpr ALCchar alcNoDeviceExtList[] =
    "ALC_ENUMERATE_ALL_EXT "
    "ALC_ENUMERATION_EXT "
    "ALC_EXT_CAPTURE "
    "ALC_EXT_thread_local_context "
    "ALC_SOFT_loopback";
constexpr ALCchar alcExtensionList[] =
    "ALC_ENUMERATE_ALL_EXT "
    "ALC_ENUMERATION_EXT "
    "ALC_EXT_CAPTURE "
    "ALC_EXT_DEDICATED "
    "ALC_EXT_disconnect "
    "ALC_EXT_EFX "
    "ALC_EXT_thread_local_context "
    "ALC_SOFT_device_clock "
    "ALC_SOFT_HRTF "
    "ALC_SOFT_loopback "
    "ALC_SOFT_output_limiter "
    "ALC_SOFT_pause_device";
constexpr ALCint alcMajorVersion = 1;
constexpr ALCint alcMinorVersion = 1;

constexpr ALCint alcEFXMajorVersion = 1;
constexpr ALCint alcEFXMinorVersion = 0;


/************************************************
 * Device lists
 ************************************************/
al::vector<ALCdevice*> DeviceList;
al::vector<ALCcontext*> ContextList;

std::recursive_mutex ListLock;

} // namespace

/* Mixing thread piority level */
ALint RTPrioLevel;

FILE *gLogFile{stderr};
#ifdef _DEBUG
LogLevel gLogLevel{LogWarning};
#else
LogLevel gLogLevel{LogError};
#endif

/************************************************
 * Library initialization
 ************************************************/
#if defined(_WIN32) && !defined(AL_LIBTYPE_STATIC)
BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID /*reserved*/)
{
    switch(reason)
    {
        case DLL_PROCESS_ATTACH:
            /* Pin the DLL so we won't get unloaded until the process terminates */
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               (WCHAR*)module, &module);
            break;

        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
#endif

// With the environment variable "ALSOFT_DRIVERS", users can decide which
// drivers to use in priority and which shoulnd't be used.
// Examples:
//   ALSOFT_DRIVERS=pulse      # Use pulse in priority.
//   ALSOFT_DRIVERS=-pulse     # Do not use pulse.
//   ALSOFT_DRIVERS=jack,pulse # Use jack in priority, then pulse, otherwise the
//                             # next available driver.
//
// This function parses and applies |ALSOFT_DRIVERS| to |backendlist|, it
// returns the list of possible backend, sorted by priority.
static std::vector<BackendInfo>
ApplyDriversConfig(std::vector<BackendInfo> backendlist,
                   const char *ALSOFT_DRIVERS)
{
    // Parse ALSOFT_DRIVERS.
    std::vector<std::string> added;
    std::vector<std::string> removed;
    const char *current = ALSOFT_DRIVERS;
    while (true) {
        // Find the next separator;
        const char *separator = current;
        while (*separator != ',' && *separator != '\0') ++separator;

        // Remove extra space before and after the word.
        const char *first = current;
        const char *last = separator - 1;
        while (isspace(*first) && first < last) ++first;
        while (isspace(*last) && first < last) --last;

        if (*first == '-')
            removed.emplace_back(first + 1, last + 1);
        else
            added.emplace_back(first, last + 1);

        if (*separator == '\0')
            break;

        current = separator + 1;
    }

#ifdef HAVE_WASAPI
    // HACK: For backwards compatibility, convert backend references of mmdevapi
    // to wasapi. This should eventually be removed.
    std::replace(added.begin(), added.end(), "mmdevapi", "wasapi");
    std::replace(removed.begin(), removed.end(), "mmdevapi", "wasapi");
#endif

    auto find_backend = [&backendlist](const std::string& name)
    {
        return std::find_if(std::begin(backendlist), std::end(backendlist),
          [name](const BackendInfo &backend) { return backend.name == name; });
    };

    // Remove backends according to user's preferences.
    for (auto& backend_name : removed)
    {
        auto backend = find_backend(backend_name);
        if (backend == std::end(backendlist))
            continue;
        backendlist.erase(backend);
    }

    // Add user's backends first ...
    std::vector<BackendInfo> output;
    for (auto& backend_name : added)
    {
        auto backend = find_backend(backend_name);
        if (backend == std::end(backendlist))
            continue;
        output.push_back(*backend);
        backendlist.erase(backend);
    }

    // ... and complete with the remaining backends.
    for (auto& backend : backendlist)
        output.push_back(backend);

    return output;
}

static void alc_initconfig(void)
{
    const char *str{getenv("ALSOFT_LOGLEVEL")};
    if(str)
    {
        long lvl = strtol(str, nullptr, 0);
        if(lvl >= NoLog && lvl <= LogRef)
            gLogLevel = static_cast<LogLevel>(lvl);
    }

    str = getenv("ALSOFT_LOGFILE");
    if(str && str[0])
    {
#ifdef _WIN32
        std::wstring wname{utf8_to_wstr(str)};
        FILE *logfile = _wfopen(wname.c_str(), L"wt");
#else
        FILE *logfile = fopen(str, "wt");
#endif
        if(logfile) gLogFile = logfile;
        else ERR("Failed to open log file '%s'\n", str);
    }

    std::vector<BackendInfo> backendlist = {
#ifdef HAVE_JACK
        {"jack", JackBackendFactory::getFactory},
#endif
#ifdef HAVE_PULSEAUDIO
        {"pulse", PulseBackendFactory::getFactory},
#endif
#ifdef HAVE_ALSA
        {"alsa", AlsaBackendFactory::getFactory},
#endif
#ifdef HAVE_WASAPI
        {"wasapi", WasapiBackendFactory::getFactory},
#endif
#ifdef HAVE_COREAUDIO
        {"core", CoreAudioBackendFactory::getFactory},
#endif
#ifdef HAVE_OPENSL
        {"opensl", OSLBackendFactory::getFactory},
#endif
#ifdef HAVE_SOLARIS
        {"solaris", SolarisBackendFactory::getFactory},
#endif
#ifdef HAVE_SNDIO
        {"sndio", SndIOBackendFactory::getFactory},
#endif
#ifdef HAVE_OSS
        {"oss", OSSBackendFactory::getFactory},
#endif
#ifdef HAVE_QSA
        {"qsa", QSABackendFactory::getFactory},
#endif
#ifdef HAVE_DSOUND
        {"dsound", DSoundBackendFactory::getFactory},
#endif
#ifdef HAVE_WINMM
        {"winmm", WinMMBackendFactory::getFactory},
#endif
#ifdef HAVE_PORTAUDIO
        {"port", PortBackendFactory::getFactory},
#endif
#ifdef HAVE_SDL2
        {"sdl2", SDL2BackendFactory::getFactory},
#endif
        {"null", NullBackendFactory::getFactory},
#ifdef HAVE_WAVE
        {"wave", WaveBackendFactory::getFactory},
#endif
    };

    TRACE("Initializing library v%s-%s %s\n", ALSOFT_VERSION,
          ALSOFT_GIT_COMMIT_HASH, ALSOFT_GIT_BRANCH);
    {
        std::string names;
        for(auto& backend : backendlist)
          names += (names.empty() ? "" : ", ") + std::string(backend.name);
        TRACE("Supported backends: %s\n", names.c_str());
    }
    ReadALConfig();

    str = getenv("__ALSOFT_SUSPEND_CONTEXT");
    if(str && *str)
    {
        if(strcasecmp(str, "ignore") == 0)
        {
            SuspendDefers = false;
            TRACE("Selected context suspend behavior, \"ignore\"\n");
        }
        else
            ERR("Unhandled context suspend behavior setting: \"%s\"\n", str);
    }

    int capfilter{0};
#if defined(HAVE_SSE4_1)
    capfilter |= CPU_CAP_SSE | CPU_CAP_SSE2 | CPU_CAP_SSE3 | CPU_CAP_SSE4_1;
#elif defined(HAVE_SSE3)
    capfilter |= CPU_CAP_SSE | CPU_CAP_SSE2 | CPU_CAP_SSE3;
#elif defined(HAVE_SSE2)
    capfilter |= CPU_CAP_SSE | CPU_CAP_SSE2;
#elif defined(HAVE_SSE)
    capfilter |= CPU_CAP_SSE;
#endif
#ifdef HAVE_NEON
    capfilter |= CPU_CAP_NEON;
#endif
    if(ConfigValueStr(nullptr, nullptr, "disable-cpu-exts", &str))
    {
        if(strcasecmp(str, "all") == 0)
            capfilter = 0;
        else
        {
            const char *next = str;
            do {
                str = next;
                while(isspace(str[0]))
                    str++;
                next = strchr(str, ',');

                if(!str[0] || str[0] == ',')
                    continue;

                size_t len{next ? static_cast<size_t>(next-str) : strlen(str)};
                while(len > 0 && isspace(str[len-1]))
                    len--;
                if(len == 3 && strncasecmp(str, "sse", len) == 0)
                    capfilter &= ~CPU_CAP_SSE;
                else if(len == 4 && strncasecmp(str, "sse2", len) == 0)
                    capfilter &= ~CPU_CAP_SSE2;
                else if(len == 4 && strncasecmp(str, "sse3", len) == 0)
                    capfilter &= ~CPU_CAP_SSE3;
                else if(len == 6 && strncasecmp(str, "sse4.1", len) == 0)
                    capfilter &= ~CPU_CAP_SSE4_1;
                else if(len == 4 && strncasecmp(str, "neon", len) == 0)
                    capfilter &= ~CPU_CAP_NEON;
                else
                    WARN("Invalid CPU extension \"%s\"\n", str);
            } while(next++);
        }
    }
    FillCPUCaps(capfilter);

#ifdef _WIN32
    RTPrioLevel = 1;
#else
    RTPrioLevel = 0;
#endif
    ConfigValueInt(nullptr, nullptr, "rt-prio", &RTPrioLevel);

    aluInit();
    aluInitMixer();

    str = getenv("ALSOFT_TRAP_ERROR");
    if(str && (strcasecmp(str, "true") == 0 || strtol(str, nullptr, 0) == 1))
    {
        TrapALError  = AL_TRUE;
        TrapALCError = true;
    }
    else
    {
        str = getenv("ALSOFT_TRAP_AL_ERROR");
        if(str && (strcasecmp(str, "true") == 0 || strtol(str, nullptr, 0) == 1))
            TrapALError = AL_TRUE;
        TrapALError = GetConfigValueBool(nullptr, nullptr, "trap-al-error", TrapALError);

        str = getenv("ALSOFT_TRAP_ALC_ERROR");
        if(str && (strcasecmp(str, "true") == 0 || strtol(str, nullptr, 0) == 1))
            TrapALCError = true;
        TrapALCError = !!GetConfigValueBool(nullptr, nullptr, "trap-alc-error", TrapALCError);
    }

    float valf{};
    if(ConfigValueFloat(nullptr, "reverb", "boost", &valf))
        ReverbBoost *= std::pow(10.0f, valf / 20.0f);

    const char *ALSOFT_DRIVERS = getenv("ALSOFT_DRIVERS");
    if ((ALSOFT_DRIVERS && ALSOFT_DRIVERS[0]) ||
        ConfigValueStr(nullptr, nullptr, "drivers", &ALSOFT_DRIVERS))
    {
        backendlist = ApplyDriversConfig(std::move(backendlist), ALSOFT_DRIVERS);
    }

    for(auto& backend : backendlist)
    {
        BackendFactory &factory = backend.getFactory();
        if(!factory.init())
        {
            WARN("Failed to initialize backend \"%s\"\n", backend.name);
            continue;
        }
        TRACE("Initialized backend \"%s\"\n", backend.name);

        if(!PlaybackBackend.name && factory.querySupport(BackendType::Playback))
        {
            PlaybackBackend = backend;
            TRACE("Added \"%s\" for playback\n", PlaybackBackend.name);
        }

        if(!CaptureBackend.name && factory.querySupport(BackendType::Capture))
        {
            CaptureBackend = backend;
            TRACE("Added \"%s\" for capture\n", CaptureBackend.name);
        }

        if(PlaybackBackend.name && CaptureBackend.name)
            break;
    }

    LoopbackBackendFactory::getFactory().init();

    if(!PlaybackBackend.name)
        WARN("No playback backend available!\n");
    if(!CaptureBackend.name)
        WARN("No capture backend available!\n");

    if(ConfigValueStr(nullptr, nullptr, "excludefx", &str))
    {
        const char *next = str;
        do {
            str = next;
            next = strchr(str, ',');

            if(!str[0] || next == str)
                continue;

            size_t len{next ? static_cast<size_t>(next-str) : strlen(str)};
            for(size_t n{0u};n < countof(gEffectList);n++)
            {
                if(len == strlen(gEffectList[n].name) &&
                   strncmp(gEffectList[n].name, str, len) == 0)
                    DisabledEffects[gEffectList[n].type] = AL_TRUE;
            }
        } while(next++);
    }

    InitEffect(&DefaultEffect);
    str = getenv("ALSOFT_DEFAULT_REVERB");
    if((str && str[0]) || ConfigValueStr(nullptr, nullptr, "default-reverb", &str))
        LoadReverbPreset(str, &DefaultEffect);
}
#define DO_INITCONFIG() std::call_once(alc_config_once, [](){alc_initconfig();})

/************************************************
 * Device enumeration
 ************************************************/
static void ProbeDevices(std::string *list, BackendInfo *backendinfo, DevProbe type)
{
    DO_INITCONFIG();

    std::lock_guard<std::recursive_mutex> _{ListLock};
    list->clear();
    if(backendinfo->getFactory)
        backendinfo->getFactory().probe(type, list);
}
static void ProbeAllDevicesList(void)
{ ProbeDevices(&alcAllDevicesList, &PlaybackBackend, DevProbe::Playback); }
static void ProbeCaptureDeviceList(void)
{ ProbeDevices(&alcCaptureDeviceList, &CaptureBackend, DevProbe::Capture); }


/************************************************
 * Device format information
 ************************************************/
const ALCchar *DevFmtTypeString(DevFmtType type) noexcept
{
    switch(type)
    {
    case DevFmtByte: return "Signed Byte";
    case DevFmtUByte: return "Unsigned Byte";
    case DevFmtShort: return "Signed Short";
    case DevFmtUShort: return "Unsigned Short";
    case DevFmtInt: return "Signed Int";
    case DevFmtUInt: return "Unsigned Int";
    case DevFmtFloat: return "Float";
    }
    return "(unknown type)";
}
const ALCchar *DevFmtChannelsString(DevFmtChannels chans) noexcept
{
    switch(chans)
    {
    case DevFmtMono: return "Mono";
    case DevFmtStereo: return "Stereo";
    case DevFmtQuad: return "Quadraphonic";
    case DevFmtX51: return "5.1 Surround";
    case DevFmtX51Rear: return "5.1 Surround (Rear)";
    case DevFmtX61: return "6.1 Surround";
    case DevFmtX71: return "7.1 Surround";
    case DevFmtAmbi3D: return "Ambisonic 3D";
    }
    return "(unknown channels)";
}

ALsizei BytesFromDevFmt(DevFmtType type) noexcept
{
    switch(type)
    {
    case DevFmtByte: return sizeof(ALbyte);
    case DevFmtUByte: return sizeof(ALubyte);
    case DevFmtShort: return sizeof(ALshort);
    case DevFmtUShort: return sizeof(ALushort);
    case DevFmtInt: return sizeof(ALint);
    case DevFmtUInt: return sizeof(ALuint);
    case DevFmtFloat: return sizeof(ALfloat);
    }
    return 0;
}
ALsizei ChannelsFromDevFmt(DevFmtChannels chans, ALsizei ambiorder) noexcept
{
    switch(chans)
    {
    case DevFmtMono: return 1;
    case DevFmtStereo: return 2;
    case DevFmtQuad: return 4;
    case DevFmtX51: return 6;
    case DevFmtX51Rear: return 6;
    case DevFmtX61: return 7;
    case DevFmtX71: return 8;
    case DevFmtAmbi3D: return (ambiorder+1) * (ambiorder+1);
    }
    return 0;
}

static ALboolean DecomposeDevFormat(ALenum format, DevFmtChannels *chans, DevFmtType *type)
{
    static const struct {
        ALenum format;
        DevFmtChannels channels;
        DevFmtType type;
    } list[] = {
        { AL_FORMAT_MONO8,        DevFmtMono, DevFmtUByte },
        { AL_FORMAT_MONO16,       DevFmtMono, DevFmtShort },
        { AL_FORMAT_MONO_FLOAT32, DevFmtMono, DevFmtFloat },

        { AL_FORMAT_STEREO8,        DevFmtStereo, DevFmtUByte },
        { AL_FORMAT_STEREO16,       DevFmtStereo, DevFmtShort },
        { AL_FORMAT_STEREO_FLOAT32, DevFmtStereo, DevFmtFloat },

        { AL_FORMAT_QUAD8,  DevFmtQuad, DevFmtUByte },
        { AL_FORMAT_QUAD16, DevFmtQuad, DevFmtShort },
        { AL_FORMAT_QUAD32, DevFmtQuad, DevFmtFloat },

        { AL_FORMAT_51CHN8,  DevFmtX51, DevFmtUByte },
        { AL_FORMAT_51CHN16, DevFmtX51, DevFmtShort },
        { AL_FORMAT_51CHN32, DevFmtX51, DevFmtFloat },

        { AL_FORMAT_61CHN8,  DevFmtX61, DevFmtUByte },
        { AL_FORMAT_61CHN16, DevFmtX61, DevFmtShort },
        { AL_FORMAT_61CHN32, DevFmtX61, DevFmtFloat },

        { AL_FORMAT_71CHN8,  DevFmtX71, DevFmtUByte },
        { AL_FORMAT_71CHN16, DevFmtX71, DevFmtShort },
        { AL_FORMAT_71CHN32, DevFmtX71, DevFmtFloat },
    };
    ALuint i;

    for(i = 0;i < COUNTOF(list);i++)
    {
        if(list[i].format == format)
        {
            *chans = list[i].channels;
            *type  = list[i].type;
            return AL_TRUE;
        }
    }

    return AL_FALSE;
}

static ALCboolean IsValidALCType(ALCenum type)
{
    switch(type)
    {
        case ALC_BYTE_SOFT:
        case ALC_UNSIGNED_BYTE_SOFT:
        case ALC_SHORT_SOFT:
        case ALC_UNSIGNED_SHORT_SOFT:
        case ALC_INT_SOFT:
        case ALC_UNSIGNED_INT_SOFT:
        case ALC_FLOAT_SOFT:
            return ALC_TRUE;
    }
    return ALC_FALSE;
}

static ALCboolean IsValidALCChannels(ALCenum channels)
{
    switch(channels)
    {
        case ALC_MONO_SOFT:
        case ALC_STEREO_SOFT:
        case ALC_QUAD_SOFT:
        case ALC_5POINT1_SOFT:
        case ALC_6POINT1_SOFT:
        case ALC_7POINT1_SOFT:
        case ALC_BFORMAT3D_SOFT:
            return ALC_TRUE;
    }
    return ALC_FALSE;
}

static ALCboolean IsValidAmbiLayout(ALCenum layout)
{
    switch(layout)
    {
        case ALC_ACN_SOFT:
        case ALC_FUMA_SOFT:
            return ALC_TRUE;
    }
    return ALC_FALSE;
}

static ALCboolean IsValidAmbiScaling(ALCenum scaling)
{
    switch(scaling)
    {
        case ALC_N3D_SOFT:
        case ALC_SN3D_SOFT:
        case ALC_FUMA_SOFT:
            return ALC_TRUE;
    }
    return ALC_FALSE;
}

/************************************************
 * Miscellaneous ALC helpers
 ************************************************/

/* SetDefaultWFXChannelOrder
 *
 * Sets the default channel order used by WaveFormatEx.
 */
void SetDefaultWFXChannelOrder(ALCdevice *device)
{
    device->RealOut.ChannelIndex.fill(-1);

    switch(device->FmtChans)
    {
    case DevFmtMono:
        device->RealOut.ChannelIndex[FrontCenter] = 0;
        break;
    case DevFmtStereo:
        device->RealOut.ChannelIndex[FrontLeft]  = 0;
        device->RealOut.ChannelIndex[FrontRight] = 1;
        break;
    case DevFmtQuad:
        device->RealOut.ChannelIndex[FrontLeft]  = 0;
        device->RealOut.ChannelIndex[FrontRight] = 1;
        device->RealOut.ChannelIndex[BackLeft]   = 2;
        device->RealOut.ChannelIndex[BackRight]  = 3;
        break;
    case DevFmtX51:
        device->RealOut.ChannelIndex[FrontLeft]   = 0;
        device->RealOut.ChannelIndex[FrontRight]  = 1;
        device->RealOut.ChannelIndex[FrontCenter] = 2;
        device->RealOut.ChannelIndex[LFE]         = 3;
        device->RealOut.ChannelIndex[SideLeft]    = 4;
        device->RealOut.ChannelIndex[SideRight]   = 5;
        break;
    case DevFmtX51Rear:
        device->RealOut.ChannelIndex[FrontLeft]   = 0;
        device->RealOut.ChannelIndex[FrontRight]  = 1;
        device->RealOut.ChannelIndex[FrontCenter] = 2;
        device->RealOut.ChannelIndex[LFE]         = 3;
        device->RealOut.ChannelIndex[BackLeft]    = 4;
        device->RealOut.ChannelIndex[BackRight]   = 5;
        break;
    case DevFmtX61:
        device->RealOut.ChannelIndex[FrontLeft]   = 0;
        device->RealOut.ChannelIndex[FrontRight]  = 1;
        device->RealOut.ChannelIndex[FrontCenter] = 2;
        device->RealOut.ChannelIndex[LFE]         = 3;
        device->RealOut.ChannelIndex[BackCenter]  = 4;
        device->RealOut.ChannelIndex[SideLeft]    = 5;
        device->RealOut.ChannelIndex[SideRight]   = 6;
        break;
    case DevFmtX71:
        device->RealOut.ChannelIndex[FrontLeft]   = 0;
        device->RealOut.ChannelIndex[FrontRight]  = 1;
        device->RealOut.ChannelIndex[FrontCenter] = 2;
        device->RealOut.ChannelIndex[LFE]         = 3;
        device->RealOut.ChannelIndex[BackLeft]    = 4;
        device->RealOut.ChannelIndex[BackRight]   = 5;
        device->RealOut.ChannelIndex[SideLeft]    = 6;
        device->RealOut.ChannelIndex[SideRight]   = 7;
        break;
    case DevFmtAmbi3D:
        device->RealOut.ChannelIndex[Aux0] = 0;
        if(device->mAmbiOrder > 0)
        {
            device->RealOut.ChannelIndex[Aux1] = 1;
            device->RealOut.ChannelIndex[Aux2] = 2;
            device->RealOut.ChannelIndex[Aux3] = 3;
        }
        if(device->mAmbiOrder > 1)
        {
            device->RealOut.ChannelIndex[Aux4] = 4;
            device->RealOut.ChannelIndex[Aux5] = 5;
            device->RealOut.ChannelIndex[Aux6] = 6;
            device->RealOut.ChannelIndex[Aux7] = 7;
            device->RealOut.ChannelIndex[Aux8] = 8;
        }
        if(device->mAmbiOrder > 2)
        {
            device->RealOut.ChannelIndex[Aux9]  = 9;
            device->RealOut.ChannelIndex[Aux10] = 10;
            device->RealOut.ChannelIndex[Aux11] = 11;
            device->RealOut.ChannelIndex[Aux12] = 12;
            device->RealOut.ChannelIndex[Aux13] = 13;
            device->RealOut.ChannelIndex[Aux14] = 14;
            device->RealOut.ChannelIndex[Aux15] = 15;
        }
        break;
    }
}

/* SetDefaultChannelOrder
 *
 * Sets the default channel order used by most non-WaveFormatEx-based APIs.
 */
void SetDefaultChannelOrder(ALCdevice *device)
{
    device->RealOut.ChannelIndex.fill(-1);

    switch(device->FmtChans)
    {
    case DevFmtX51Rear:
        device->RealOut.ChannelIndex[FrontLeft]   = 0;
        device->RealOut.ChannelIndex[FrontRight]  = 1;
        device->RealOut.ChannelIndex[BackLeft]    = 2;
        device->RealOut.ChannelIndex[BackRight]   = 3;
        device->RealOut.ChannelIndex[FrontCenter] = 4;
        device->RealOut.ChannelIndex[LFE]         = 5;
        return;
    case DevFmtX71:
        device->RealOut.ChannelIndex[FrontLeft]   = 0;
        device->RealOut.ChannelIndex[FrontRight]  = 1;
        device->RealOut.ChannelIndex[BackLeft]    = 2;
        device->RealOut.ChannelIndex[BackRight]   = 3;
        device->RealOut.ChannelIndex[FrontCenter] = 4;
        device->RealOut.ChannelIndex[LFE]         = 5;
        device->RealOut.ChannelIndex[SideLeft]    = 6;
        device->RealOut.ChannelIndex[SideRight]   = 7;
        return;

    /* Same as WFX order */
    case DevFmtMono:
    case DevFmtStereo:
    case DevFmtQuad:
    case DevFmtX51:
    case DevFmtX61:
    case DevFmtAmbi3D:
        SetDefaultWFXChannelOrder(device);
        break;
    }
}


/* ALCcontext_DeferUpdates
 *
 * Defers/suspends updates for the given context's listener and sources. This
 * does *NOT* stop mixing, but rather prevents certain property changes from
 * taking effect.
 */
void ALCcontext_DeferUpdates(ALCcontext *context)
{
    context->DeferUpdates.store(true);
}

/* ALCcontext_ProcessUpdates
 *
 * Resumes update processing after being deferred.
 */
void ALCcontext_ProcessUpdates(ALCcontext *context)
{
    std::lock_guard<std::mutex> _{context->PropLock};
    if(context->DeferUpdates.exchange(false))
    {
        /* Tell the mixer to stop applying updates, then wait for any active
         * updating to finish, before providing updates.
         */
        context->HoldUpdates.store(true, std::memory_order_release);
        while((context->UpdateCount.load(std::memory_order_acquire)&1) != 0)
            std::this_thread::yield();

        if(!context->PropsClean.test_and_set(std::memory_order_acq_rel))
            UpdateContextProps(context);
        if(!context->Listener.PropsClean.test_and_set(std::memory_order_acq_rel))
            UpdateListenerProps(context);
        UpdateAllEffectSlotProps(context);
        UpdateAllSourceProps(context);

        /* Now with all updates declared, let the mixer continue applying them
         * so they all happen at once.
         */
        context->HoldUpdates.store(false, std::memory_order_release);
    }
}


/* alcSetError
 *
 * Stores the latest ALC device error
 */
static void alcSetError(ALCdevice *device, ALCenum errorCode)
{
    WARN("Error generated on device %p, code 0x%04x\n", device, errorCode);
    if(TrapALCError)
    {
#ifdef _WIN32
        /* DebugBreak() will cause an exception if there is no debugger */
        if(IsDebuggerPresent())
            DebugBreak();
#elif defined(SIGTRAP)
        raise(SIGTRAP);
#endif
    }

    if(device)
        device->LastError.store(errorCode);
    else
        LastNullDeviceError.store(errorCode);
}


static std::unique_ptr<Compressor> CreateDeviceLimiter(const ALCdevice *device, const ALfloat threshold)
{
    return CompressorInit(device->RealOut.NumChannels, device->Frequency,
        AL_TRUE, AL_TRUE, AL_TRUE, AL_TRUE, AL_TRUE, 0.001f, 0.002f,
        0.0f, 0.0f, threshold, INFINITY, 0.0f, 0.020f, 0.200f);
}

/* UpdateClockBase
 *
 * Updates the device's base clock time with however many samples have been
 * done. This is used so frequency changes on the device don't cause the time
 * to jump forward or back. Must not be called while the device is running/
 * mixing.
 */
static inline void UpdateClockBase(ALCdevice *device)
{
    IncrementRef(&device->MixCount);
    device->ClockBase += nanoseconds{seconds{device->SamplesDone}} / device->Frequency;
    device->SamplesDone = 0;
    IncrementRef(&device->MixCount);
}

/* UpdateDeviceParams
 *
 * Updates device parameters according to the attribute list (caller is
 * responsible for holding the list lock).
 */
static ALCenum UpdateDeviceParams(ALCdevice *device, const ALCint *attrList)
{
    HrtfRequestMode hrtf_userreq = Hrtf_Default;
    HrtfRequestMode hrtf_appreq = Hrtf_Default;
    ALCenum gainLimiter = device->LimiterState;
    const ALsizei old_sends = device->NumAuxSends;
    ALsizei new_sends = device->NumAuxSends;
    DevFmtChannels oldChans;
    DevFmtType oldType;
    ALboolean update_failed;
    ALCsizei hrtf_id = -1;
    ALCcontext *context;
    ALCuint oldFreq;
    int val;

    if((!attrList || !attrList[0]) && device->Type == Loopback)
    {
        WARN("Missing attributes for loopback device\n");
        return ALC_INVALID_VALUE;
    }

    // Check for attributes
    if(attrList && attrList[0])
    {
        ALCenum alayout = AL_NONE;
        ALCenum ascale = AL_NONE;
        ALCenum schans = AL_NONE;
        ALCenum stype = AL_NONE;
        ALCsizei attrIdx = 0;
        ALCsizei aorder = 0;
        ALCuint freq = 0;

        const char *devname{nullptr};
        const bool loopback{device->Type == Loopback};
        if(!loopback)
        {
            devname = device->DeviceName.c_str();
            /* If a context is already running on the device, stop playback so
             * the device attributes can be updated.
             */
            if((device->Flags&DEVICE_RUNNING))
                device->Backend->stop();
            device->Flags &= ~DEVICE_RUNNING;
        }

        auto numMono = static_cast<ALsizei>(device->NumMonoSources);
        auto numStereo = static_cast<ALsizei>(device->NumStereoSources);
        auto numSends = ALsizei{old_sends};

#define TRACE_ATTR(a, v) TRACE("%s = %d\n", #a, v)
        while(attrList[attrIdx])
        {
            switch(attrList[attrIdx])
            {
            case ALC_FORMAT_CHANNELS_SOFT:
                schans = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_FORMAT_CHANNELS_SOFT, schans);
                break;

            case ALC_FORMAT_TYPE_SOFT:
                stype = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_FORMAT_TYPE_SOFT, stype);
                break;

            case ALC_FREQUENCY:
                freq = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_FREQUENCY, freq);
                break;

            case ALC_AMBISONIC_LAYOUT_SOFT:
                alayout = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_AMBISONIC_LAYOUT_SOFT, alayout);
                break;

            case ALC_AMBISONIC_SCALING_SOFT:
                ascale = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_AMBISONIC_SCALING_SOFT, ascale);
                break;

            case ALC_AMBISONIC_ORDER_SOFT:
                aorder = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_AMBISONIC_ORDER_SOFT, aorder);
                break;

            case ALC_MONO_SOURCES:
                numMono = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_MONO_SOURCES, numMono);
                numMono = maxi(numMono, 0);
                break;

            case ALC_STEREO_SOURCES:
                numStereo = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_STEREO_SOURCES, numStereo);
                numStereo = maxi(numStereo, 0);
                break;

            case ALC_MAX_AUXILIARY_SENDS:
                numSends = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_MAX_AUXILIARY_SENDS, numSends);
                numSends = clampi(numSends, 0, MAX_SENDS);
                break;

            case ALC_HRTF_SOFT:
                TRACE_ATTR(ALC_HRTF_SOFT, attrList[attrIdx + 1]);
                if(attrList[attrIdx + 1] == ALC_FALSE)
                    hrtf_appreq = Hrtf_Disable;
                else if(attrList[attrIdx + 1] == ALC_TRUE)
                    hrtf_appreq = Hrtf_Enable;
                else
                    hrtf_appreq = Hrtf_Default;
                break;

            case ALC_HRTF_ID_SOFT:
                hrtf_id = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_HRTF_ID_SOFT, hrtf_id);
                break;

            case ALC_OUTPUT_LIMITER_SOFT:
                gainLimiter = attrList[attrIdx + 1];
                TRACE_ATTR(ALC_OUTPUT_LIMITER_SOFT, gainLimiter);
                break;

            default:
                TRACE("0x%04X = %d (0x%x)\n", attrList[attrIdx],
                    attrList[attrIdx + 1], attrList[attrIdx + 1]);
                break;
            }

            attrIdx += 2;
        }
#undef TRACE_ATTR

        if(loopback)
        {
            if(!schans || !stype || !freq)
            {
                WARN("Missing format for loopback device\n");
                return ALC_INVALID_VALUE;
            }
            if(!IsValidALCChannels(schans) || !IsValidALCType(stype) || freq < MIN_OUTPUT_RATE)
                return ALC_INVALID_VALUE;
            if(schans == ALC_BFORMAT3D_SOFT)
            {
                if(!alayout || !ascale || !aorder)
                {
                    WARN("Missing ambisonic info for loopback device\n");
                    return ALC_INVALID_VALUE;
                }
                if(!IsValidAmbiLayout(alayout) || !IsValidAmbiScaling(ascale))
                    return ALC_INVALID_VALUE;
                if(aorder < 1 || aorder > MAX_AMBI_ORDER)
                    return ALC_INVALID_VALUE;
                if((alayout == ALC_FUMA_SOFT || ascale == ALC_FUMA_SOFT) && aorder > 3)
                    return ALC_INVALID_VALUE;
            }
        }

        if((device->Flags&DEVICE_RUNNING))
            device->Backend->stop();
        device->Flags &= ~DEVICE_RUNNING;

        UpdateClockBase(device);

        if(!loopback)
        {
            device->BufferSize = DEFAULT_UPDATE_SIZE * DEFAULT_NUM_UPDATES;
            device->UpdateSize = DEFAULT_UPDATE_SIZE;
            device->Frequency = DEFAULT_OUTPUT_RATE;

            ConfigValueUInt(devname, nullptr, "frequency", &freq);
            if(freq < 1)
                device->Flags &= ~DEVICE_FREQUENCY_REQUEST;
            else
            {
                freq = maxi(freq, MIN_OUTPUT_RATE);

                device->UpdateSize = (device->UpdateSize*freq + device->Frequency/2) /
                    device->Frequency;
                device->BufferSize = (device->BufferSize*freq + device->Frequency/2) /
                    device->Frequency;

                device->Frequency = freq;
                device->Flags |= DEVICE_FREQUENCY_REQUEST;
            }

            ConfigValueUInt(devname, nullptr, "period_size", &device->UpdateSize);
            device->UpdateSize = clampu(device->UpdateSize, 64, 8192);

            ALuint periods{};
            if(ConfigValueUInt(devname, nullptr, "periods", &periods))
                device->BufferSize = device->UpdateSize * clampu(periods, 2, 16);
            else
                device->BufferSize = maxu(device->BufferSize, device->UpdateSize*2);
        }
        else
        {
            device->Frequency = freq;
            device->FmtChans = static_cast<DevFmtChannels>(schans);
            device->FmtType = static_cast<DevFmtType>(stype);
            if(schans == ALC_BFORMAT3D_SOFT)
            {
                device->mAmbiOrder = aorder;
                device->mAmbiLayout = static_cast<AmbiLayout>(alayout);
                device->mAmbiScale = static_cast<AmbiNorm>(ascale);
            }
        }

        if(numMono > INT_MAX-numStereo)
            numMono = INT_MAX-numStereo;
        numMono += numStereo;
        if(ConfigValueInt(devname, nullptr, "sources", &numMono))
        {
            if(numMono <= 0)
                numMono = 256;
        }
        else
            numMono = maxi(numMono, 256);
        numStereo = mini(numStereo, numMono);
        numMono -= numStereo;
        device->SourcesMax = numMono + numStereo;

        device->NumMonoSources = numMono;
        device->NumStereoSources = numStereo;

        if(ConfigValueInt(devname, nullptr, "sends", &new_sends))
            new_sends = mini(numSends, clampi(new_sends, 0, MAX_SENDS));
        else
            new_sends = numSends;
    }

    if((device->Flags&DEVICE_RUNNING))
        return ALC_NO_ERROR;

    device->Uhj_Encoder = nullptr;
    device->Bs2b = nullptr;

    device->Limiter = nullptr;
    device->ChannelDelay.clear();
    device->ChannelDelay.shrink_to_fit();

    device->Dry.Buffer = nullptr;
    device->Dry.NumChannels = 0;
    device->RealOut.Buffer = nullptr;
    device->RealOut.NumChannels = 0;
    device->MixBuffer.clear();
    device->MixBuffer.shrink_to_fit();

    UpdateClockBase(device);
    device->FixedLatency = nanoseconds::zero();

    device->DitherSeed = DITHER_RNG_SEED;

    /*************************************************************************
     * Update device format request if HRTF is requested
     */
    device->HrtfStatus = ALC_HRTF_DISABLED_SOFT;
    if(device->Type != Loopback)
    {
        const char *hrtf;
        if(ConfigValueStr(device->DeviceName.c_str(), nullptr, "hrtf", &hrtf))
        {
            if(strcasecmp(hrtf, "true") == 0)
                hrtf_userreq = Hrtf_Enable;
            else if(strcasecmp(hrtf, "false") == 0)
                hrtf_userreq = Hrtf_Disable;
            else if(strcasecmp(hrtf, "auto") != 0)
                ERR("Unexpected hrtf value: %s\n", hrtf);
        }

        if(hrtf_userreq == Hrtf_Enable || (hrtf_userreq != Hrtf_Disable && hrtf_appreq == Hrtf_Enable))
        {
            HrtfEntry *hrtf{nullptr};
            if(device->HrtfList.empty())
                device->HrtfList = EnumerateHrtf(device->DeviceName.c_str());
            if(!device->HrtfList.empty())
            {
                if(hrtf_id >= 0 && static_cast<size_t>(hrtf_id) < device->HrtfList.size())
                    hrtf = GetLoadedHrtf(device->HrtfList[hrtf_id].hrtf);
                else
                    hrtf = GetLoadedHrtf(device->HrtfList.front().hrtf);
            }

            if(hrtf)
            {
                device->FmtChans = DevFmtStereo;
                device->Frequency = hrtf->sampleRate;
                device->Flags |= DEVICE_CHANNELS_REQUEST | DEVICE_FREQUENCY_REQUEST;
                if(HrtfEntry *oldhrtf{device->mHrtf})
                    oldhrtf->DecRef();
                device->mHrtf = hrtf;
            }
            else
            {
                hrtf_userreq = Hrtf_Default;
                hrtf_appreq = Hrtf_Disable;
                device->HrtfStatus = ALC_HRTF_UNSUPPORTED_FORMAT_SOFT;
            }
        }
    }

    oldFreq  = device->Frequency;
    oldChans = device->FmtChans;
    oldType  = device->FmtType;

    TRACE("Pre-reset: %s%s, %s%s, %s%uhz, %u / %u buffer\n",
        (device->Flags&DEVICE_CHANNELS_REQUEST)?"*":"", DevFmtChannelsString(device->FmtChans),
        (device->Flags&DEVICE_SAMPLE_TYPE_REQUEST)?"*":"", DevFmtTypeString(device->FmtType),
        (device->Flags&DEVICE_FREQUENCY_REQUEST)?"*":"", device->Frequency,
        device->UpdateSize, device->BufferSize);

    try {
        if(device->Backend->reset() == ALC_FALSE)
            return ALC_INVALID_DEVICE;
    }
    catch(std::exception &e) {
        ERR("Device reset failed: %s\n", e.what());
        return ALC_INVALID_DEVICE;
    }

    if(device->FmtChans != oldChans && (device->Flags&DEVICE_CHANNELS_REQUEST))
    {
        ERR("Failed to set %s, got %s instead\n", DevFmtChannelsString(oldChans),
            DevFmtChannelsString(device->FmtChans));
        device->Flags &= ~DEVICE_CHANNELS_REQUEST;
    }
    if(device->FmtType != oldType && (device->Flags&DEVICE_SAMPLE_TYPE_REQUEST))
    {
        ERR("Failed to set %s, got %s instead\n", DevFmtTypeString(oldType),
            DevFmtTypeString(device->FmtType));
        device->Flags &= ~DEVICE_SAMPLE_TYPE_REQUEST;
    }
    if(device->Frequency != oldFreq && (device->Flags&DEVICE_FREQUENCY_REQUEST))
    {
        ERR("Failed to set %uhz, got %uhz instead\n", oldFreq, device->Frequency);
        device->Flags &= ~DEVICE_FREQUENCY_REQUEST;
    }

    if((device->UpdateSize&3) != 0)
    {
        if((CPUCapFlags&CPU_CAP_SSE))
            WARN("SSE performs best with multiple of 4 update sizes (%u)\n", device->UpdateSize);
        if((CPUCapFlags&CPU_CAP_NEON))
            WARN("NEON performs best with multiple of 4 update sizes (%u)\n", device->UpdateSize);
    }

    TRACE("Post-reset: %s, %s, %uhz, %u / %u buffer\n",
        DevFmtChannelsString(device->FmtChans), DevFmtTypeString(device->FmtType),
        device->Frequency, device->UpdateSize, device->BufferSize);

    aluInitRenderer(device, hrtf_id, hrtf_appreq, hrtf_userreq);
    TRACE("Channel config, Main: %d, Real: %d\n", device->Dry.NumChannels,
        device->RealOut.NumChannels);

    /* Allocate extra channels for any post-filter output. */
    const ALsizei num_chans{device->Dry.NumChannels + device->RealOut.NumChannels};

    TRACE("Allocating %d channels, %zu bytes\n", num_chans,
        num_chans*sizeof(device->MixBuffer[0]));
    device->MixBuffer.resize(num_chans);

    device->Dry.Buffer = &reinterpret_cast<ALfloat(&)[BUFFERSIZE]>(device->MixBuffer[0]);
    if(device->RealOut.NumChannels != 0)
        device->RealOut.Buffer = device->Dry.Buffer + device->Dry.NumChannels;
    else
    {
        device->RealOut.Buffer = device->Dry.Buffer;
        device->RealOut.NumChannels = device->Dry.NumChannels;
    }

    device->NumAuxSends = new_sends;
    TRACE("Max sources: %d (%d + %d), effect slots: %d, sends: %d\n",
          device->SourcesMax, device->NumMonoSources, device->NumStereoSources,
          device->AuxiliaryEffectSlotMax, device->NumAuxSends);

    device->DitherDepth = 0.0f;
    if(GetConfigValueBool(device->DeviceName.c_str(), nullptr, "dither", 1))
    {
        ALint depth = 0;
        ConfigValueInt(device->DeviceName.c_str(), nullptr, "dither-depth", &depth);
        if(depth <= 0)
        {
            switch(device->FmtType)
            {
                case DevFmtByte:
                case DevFmtUByte:
                    depth = 8;
                    break;
                case DevFmtShort:
                case DevFmtUShort:
                    depth = 16;
                    break;
                case DevFmtInt:
                case DevFmtUInt:
                case DevFmtFloat:
                    break;
            }
        }

        if(depth > 0)
        {
            depth = clampi(depth, 2, 24);
            device->DitherDepth = std::pow(2.0f, static_cast<ALfloat>(depth-1));
        }
    }
    if(!(device->DitherDepth > 0.0f))
        TRACE("Dithering disabled\n");
    else
        TRACE("Dithering enabled (%d-bit, %g)\n", float2int(std::log2(device->DitherDepth)+0.5f)+1,
              device->DitherDepth);

    device->LimiterState = gainLimiter;
    if(ConfigValueBool(device->DeviceName.c_str(), nullptr, "output-limiter", &val))
        gainLimiter = val ? ALC_TRUE : ALC_FALSE;

    /* Valid values for gainLimiter are ALC_DONT_CARE_SOFT, ALC_TRUE, and
     * ALC_FALSE. For ALC_DONT_CARE_SOFT, use the limiter for integer-based
     * output (where samples must be clamped), and don't for floating-point
     * (which can take unclamped samples).
     */
    if(gainLimiter == ALC_DONT_CARE_SOFT)
    {
        switch(device->FmtType)
        {
            case DevFmtByte:
            case DevFmtUByte:
            case DevFmtShort:
            case DevFmtUShort:
            case DevFmtInt:
            case DevFmtUInt:
                gainLimiter = ALC_TRUE;
                break;
            case DevFmtFloat:
                gainLimiter = ALC_FALSE;
                break;
        }
    }
    if(gainLimiter == ALC_FALSE)
        TRACE("Output limiter disabled\n");
    else
    {
        ALfloat thrshld = 1.0f;
        switch(device->FmtType)
        {
            case DevFmtByte:
            case DevFmtUByte:
                thrshld = 127.0f / 128.0f;
                break;
            case DevFmtShort:
            case DevFmtUShort:
                thrshld = 32767.0f / 32768.0f;
                break;
            case DevFmtInt:
            case DevFmtUInt:
            case DevFmtFloat:
                break;
        }
        if(device->DitherDepth > 0.0f)
            thrshld -= 1.0f / device->DitherDepth;

        const float thrshld_dB{std::log10(thrshld) * 20.0f};
        auto limiter = CreateDeviceLimiter(device, thrshld_dB);
        /* Convert the lookahead from samples to nanosamples to nanoseconds. */
        device->FixedLatency += nanoseconds{seconds{limiter->getLookAhead()}} / device->Frequency;
        device->Limiter = std::move(limiter);
        TRACE("Output limiter enabled, %.4fdB limit\n", thrshld_dB);
    }

    aluSelectPostProcess(device);

    TRACE("Fixed device latency: %ldns\n", (long)device->FixedLatency.count());

    /* Need to delay returning failure until replacement Send arrays have been
     * allocated with the appropriate size.
     */
    update_failed = AL_FALSE;
    FPUCtl mixer_mode{};
    context = device->ContextList.load();
    while(context)
    {
        if(context->DefaultSlot)
        {
            ALeffectslot *slot = context->DefaultSlot.get();
            aluInitEffectPanning(slot, device);

            EffectState *state{slot->Effect.State};
            state->mOutBuffer = device->Dry.Buffer;
            state->mOutChannels = device->Dry.NumChannels;
            if(state->deviceUpdate(device) == AL_FALSE)
                update_failed = AL_TRUE;
            else
                UpdateEffectSlotProps(slot, context);
        }

        std::unique_lock<std::mutex> proplock{context->PropLock};
        std::unique_lock<std::mutex> slotlock{context->EffectSlotLock};
        for(auto &sublist : context->EffectSlotList)
        {
            uint64_t usemask = ~sublist.FreeMask;
            while(usemask)
            {
                ALsizei idx = CTZ64(usemask);
                ALeffectslot *slot = sublist.EffectSlots + idx;

                usemask &= ~(1_u64 << idx);

                aluInitEffectPanning(slot, device);

                EffectState *state{slot->Effect.State};
                state->mOutBuffer = device->Dry.Buffer;
                state->mOutChannels = device->Dry.NumChannels;
                if(state->deviceUpdate(device) == AL_FALSE)
                    update_failed = AL_TRUE;
                else
                    UpdateEffectSlotProps(slot, context);
            }
        }
        slotlock.unlock();

        std::unique_lock<std::mutex> srclock{context->SourceLock};
        for(auto &sublist : context->SourceList)
        {
            uint64_t usemask = ~sublist.FreeMask;
            while(usemask)
            {
                ALsizei idx = CTZ64(usemask);
                ALsource *source = sublist.Sources + idx;

                usemask &= ~(1_u64 << idx);

                if(old_sends != device->NumAuxSends)
                {
                    ALsizei s;
                    for(s = device->NumAuxSends;s < old_sends;s++)
                    {
                        if(source->Send[s].Slot)
                            DecrementRef(&source->Send[s].Slot->ref);
                        source->Send[s].Slot = nullptr;
                    }
                    source->Send.resize(device->NumAuxSends);
                    source->Send.shrink_to_fit();
                    for(s = old_sends;s < device->NumAuxSends;s++)
                    {
                        source->Send[s].Slot = nullptr;
                        source->Send[s].Gain = 1.0f;
                        source->Send[s].GainHF = 1.0f;
                        source->Send[s].HFReference = LOWPASSFREQREF;
                        source->Send[s].GainLF = 1.0f;
                        source->Send[s].LFReference = HIGHPASSFREQREF;
                    }
                }

                source->PropsClean.clear(std::memory_order_release);
            }
        }

        /* Clear any pre-existing voice property structs, in case the number of
         * auxiliary sends is changing. Active sources will have updates
         * respecified in UpdateAllSourceProps.
         */
        ALvoiceProps *vprops{context->FreeVoiceProps.exchange(nullptr, std::memory_order_acq_rel)};
        while(vprops)
        {
            ALvoiceProps *next = vprops->next.load(std::memory_order_relaxed);
            delete vprops;
            vprops = next;
        }

        AllocateVoices(context, context->MaxVoices, old_sends);
        auto voices_end = context->Voices + context->VoiceCount.load(std::memory_order_relaxed);
        std::for_each(context->Voices, voices_end,
            [device](ALvoice *voice) -> void
            {
                delete voice->mUpdate.exchange(nullptr, std::memory_order_acq_rel);

                /* Force the voice to stopped if it was stopping. */
                ALvoice::State vstate{ALvoice::Stopping};
                voice->mPlayState.compare_exchange_strong(vstate, ALvoice::Stopped,
                    std::memory_order_acquire, std::memory_order_acquire);
                if(voice->mSourceID.load(std::memory_order_relaxed) == 0u)
                    return;

                if(device->AvgSpeakerDist > 0.0f)
                {
                    /* Reinitialize the NFC filters for new parameters. */
                    ALfloat w1 = SPEEDOFSOUNDMETRESPERSEC /
                                 (device->AvgSpeakerDist * device->Frequency);
                    std::for_each(voice->mDirect.Params, voice->mDirect.Params+voice->mNumChannels,
                        [w1](DirectParams &params) noexcept -> void
                        { params.NFCtrlFilter.init(w1); }
                    );
                }
            }
        );
        srclock.unlock();

        context->PropsClean.test_and_set(std::memory_order_release);
        UpdateContextProps(context);
        context->Listener.PropsClean.test_and_set(std::memory_order_release);
        UpdateListenerProps(context);
        UpdateAllSourceProps(context);

        context = context->next.load(std::memory_order_relaxed);
    }
    mixer_mode.leave();
    if(update_failed)
        return ALC_INVALID_DEVICE;

    if(!(device->Flags&DEVICE_PAUSED))
    {
        if(device->Backend->start() == ALC_FALSE)
            return ALC_INVALID_DEVICE;
        device->Flags |= DEVICE_RUNNING;
    }

    return ALC_NO_ERROR;
}


ALCdevice::ALCdevice(DeviceType type) : Type{type}
{
}

/* ALCdevice::~ALCdevice
 *
 * Frees the device structure, and destroys any objects the app failed to
 * delete. Called once there's no more references on the device.
 */
ALCdevice::~ALCdevice()
{
    TRACE("%p\n", this);

    Backend = nullptr;

    size_t count{std::accumulate(BufferList.cbegin(), BufferList.cend(), size_t{0u},
        [](size_t cur, const BufferSubList &sublist) noexcept -> size_t
        { return cur + POPCNT64(~sublist.FreeMask); }
    )};
    if(count > 0)
        WARN("%zu Buffer%s not deleted\n", count, (count==1)?"":"s");

    count = std::accumulate(EffectList.cbegin(), EffectList.cend(), size_t{0u},
        [](size_t cur, const EffectSubList &sublist) noexcept -> size_t
        { return cur + POPCNT64(~sublist.FreeMask); }
    );
    if(count > 0)
        WARN("%zu Effect%s not deleted\n", count, (count==1)?"":"s");

    count = std::accumulate(FilterList.cbegin(), FilterList.cend(), size_t{0u},
        [](size_t cur, const FilterSubList &sublist) noexcept -> size_t
        { return cur + POPCNT64(~sublist.FreeMask); }
    );
    if(count > 0)
        WARN("%zu Filter%s not deleted\n", count, (count==1)?"":"s");

    if(mHrtf)
        mHrtf->DecRef();
    mHrtf = nullptr;
}


static void ALCdevice_IncRef(ALCdevice *device)
{
    auto ref = IncrementRef(&device->ref);
    TRACEREF("%p increasing refcount to %u\n", device, ref);
}

static void ALCdevice_DecRef(ALCdevice *device)
{
    auto ref = DecrementRef(&device->ref);
    TRACEREF("%p decreasing refcount to %u\n", device, ref);
    if(UNLIKELY(ref == 0)) delete device;
}

/* Simple RAII device reference. Takes the reference of the provided ALCdevice,
 * and decrements it when leaving scope. Movable (transfer reference) but not
 * copyable (no new references).
 */
class DeviceRef {
    ALCdevice *mDev{nullptr};

    void reset() noexcept
    {
        if(mDev)
            ALCdevice_DecRef(mDev);
        mDev = nullptr;
    }

public:
    DeviceRef() noexcept = default;
    DeviceRef(DeviceRef&& rhs) noexcept : mDev{rhs.mDev}
    { rhs.mDev = nullptr; }
    explicit DeviceRef(ALCdevice *dev) noexcept : mDev(dev) { }
    ~DeviceRef() { reset(); }

    DeviceRef& operator=(const DeviceRef&) = delete;
    DeviceRef& operator=(DeviceRef&& rhs) noexcept
    {
        reset();
        mDev = rhs.mDev;
        rhs.mDev = nullptr;
        return *this;
    }

    operator bool() const noexcept { return mDev != nullptr; }

    ALCdevice* operator->() noexcept { return mDev; }
    ALCdevice* get() noexcept { return mDev; }

    ALCdevice* release() noexcept
    {
        ALCdevice *ret{mDev};
        mDev = nullptr;
        return ret;
    }
};


/* VerifyDevice
 *
 * Checks if the device handle is valid, and returns a new reference if so.
 */
static DeviceRef VerifyDevice(ALCdevice *device)
{
    std::lock_guard<std::recursive_mutex> _{ListLock};
    auto iter = std::lower_bound(DeviceList.cbegin(), DeviceList.cend(), device);
    if(iter != DeviceList.cend() && *iter == device)
    {
        ALCdevice_IncRef(*iter);
        return DeviceRef{*iter};
    }
    return DeviceRef{};
}


ALCcontext::ALCcontext(ALCdevice *device) : Device{device}
{
    PropsClean.test_and_set(std::memory_order_relaxed);
}

/* InitContext
 *
 * Initializes context fields
 */
static ALvoid InitContext(ALCcontext *Context)
{
    ALlistener &listener = Context->Listener;
    ALeffectslotArray *auxslots;

    //Validate Context
    if(!Context->DefaultSlot)
        auxslots = ALeffectslot::CreatePtrArray(0);
    else
    {
        auxslots = ALeffectslot::CreatePtrArray(1);
        (*auxslots)[0] = Context->DefaultSlot.get();
    }
    Context->ActiveAuxSlots.store(auxslots, std::memory_order_relaxed);

    //Set globals
    Context->mDistanceModel = DistanceModel::Default;
    Context->SourceDistanceModel = AL_FALSE;
    Context->DopplerFactor = 1.0f;
    Context->DopplerVelocity = 1.0f;
    Context->SpeedOfSound = SPEEDOFSOUNDMETRESPERSEC;
    Context->MetersPerUnit = AL_DEFAULT_METERS_PER_UNIT;

    Context->ExtensionList = alExtList;


    listener.Params.Matrix = alu::Matrix::Identity();
    listener.Params.Velocity = alu::Vector{};
    listener.Params.Gain = listener.Gain;
    listener.Params.MetersPerUnit = Context->MetersPerUnit;
    listener.Params.DopplerFactor = Context->DopplerFactor;
    listener.Params.SpeedOfSound = Context->SpeedOfSound * Context->DopplerVelocity;
    listener.Params.ReverbSpeedOfSound = listener.Params.SpeedOfSound *
                                         listener.Params.MetersPerUnit;
    listener.Params.SourceDistanceModel = Context->SourceDistanceModel;
    listener.Params.mDistanceModel = Context->mDistanceModel;


    Context->AsyncEvents = CreateRingBuffer(511, sizeof(AsyncEvent), false);
    StartEventThrd(Context);
}


/* ALCcontext::~ALCcontext()
 *
 * Cleans up the context, and destroys any remaining objects the app failed to
 * delete. Called once there's no more references on the context.
 */
ALCcontext::~ALCcontext()
{
    TRACE("%p\n", this);

    ALcontextProps *cprops{Update.exchange(nullptr, std::memory_order_relaxed)};
    if(cprops)
    {
        TRACE("Freed unapplied context update %p\n", cprops);
        al_free(cprops);
    }
    size_t count{0};
    cprops = FreeContextProps.exchange(nullptr, std::memory_order_acquire);
    while(cprops)
    {
        ALcontextProps *next{cprops->next.load(std::memory_order_relaxed)};
        al_free(cprops);
        cprops = next;
        ++count;
    }
    TRACE("Freed %zu context property object%s\n", count, (count==1)?"":"s");

    count = std::accumulate(SourceList.cbegin(), SourceList.cend(), size_t{0u},
        [](size_t cur, const SourceSubList &sublist) noexcept -> size_t
        { return cur + POPCNT64(~sublist.FreeMask); }
    );
    if(count > 0)
        WARN("%zu Source%s not deleted\n", count, (count==1)?"":"s");
    SourceList.clear();
    NumSources = 0;

    count = 0;
    ALeffectslotProps *eprops{FreeEffectslotProps.exchange(nullptr, std::memory_order_acquire)};
    while(eprops)
    {
        ALeffectslotProps *next{eprops->next.load(std::memory_order_relaxed)};
        if(eprops->State) eprops->State->DecRef();
        al_free(eprops);
        eprops = next;
        ++count;
    }
    TRACE("Freed %zu AuxiliaryEffectSlot property object%s\n", count, (count==1)?"":"s");

    delete ActiveAuxSlots.exchange(nullptr, std::memory_order_relaxed);
    DefaultSlot = nullptr;

    count = std::accumulate(EffectSlotList.cbegin(), EffectSlotList.cend(), size_t{0u},
        [](size_t cur, const EffectSlotSubList &sublist) noexcept -> size_t
        { return cur + POPCNT64(~sublist.FreeMask); }
    );
    if(count > 0)
        WARN("%zu AuxiliaryEffectSlot%s not deleted\n", count, (count==1)?"":"s");
    EffectSlotList.clear();
    NumEffectSlots = 0;

    count = 0;
    ALvoiceProps *vprops{FreeVoiceProps.exchange(nullptr, std::memory_order_acquire)};
    while(vprops)
    {
        ALvoiceProps *next{vprops->next.load(std::memory_order_relaxed)};
        delete vprops;
        vprops = next;
        ++count;
    }
    TRACE("Freed %zu voice property object%s\n", count, (count==1)?"":"s");

    std::for_each(Voices, Voices + MaxVoices, DeinitVoice);
    al_free(Voices);
    Voices = nullptr;
    VoiceCount.store(0, std::memory_order_relaxed);
    MaxVoices = 0;

    ALlistenerProps *lprops{Listener.Update.exchange(nullptr, std::memory_order_relaxed)};
    if(lprops)
    {
        TRACE("Freed unapplied listener update %p\n", lprops);
        al_free(lprops);
    }
    count = 0;
    lprops = FreeListenerProps.exchange(nullptr, std::memory_order_acquire);
    while(lprops)
    {
        ALlistenerProps *next{lprops->next.load(std::memory_order_relaxed)};
        al_free(lprops);
        lprops = next;
        ++count;
    }
    TRACE("Freed %zu listener property object%s\n", count, (count==1)?"":"s");

    if(AsyncEvents)
    {
        count = 0;
        auto evt_vec = AsyncEvents->getReadVector();
        while(evt_vec.first.len > 0)
        {
            reinterpret_cast<AsyncEvent*>(evt_vec.first.buf)->~AsyncEvent();
            evt_vec.first.buf += sizeof(AsyncEvent);
            evt_vec.first.len -= 1;
            ++count;
        }
        while(evt_vec.second.len > 0)
        {
            reinterpret_cast<AsyncEvent*>(evt_vec.second.buf)->~AsyncEvent();
            evt_vec.second.buf += sizeof(AsyncEvent);
            evt_vec.second.len -= 1;
            ++count;
        }
        if(count > 0)
            TRACE("Destructed %zu orphaned event%s\n", count, (count==1)?"":"s");
    }

    ALCdevice_DecRef(Device);
}

/* ReleaseContext
 *
 * Removes the context reference from the given device and removes it from
 * being current on the running thread or globally. Returns true if other
 * contexts still exist on the device.
 */
static bool ReleaseContext(ALCcontext *context, ALCdevice *device)
{
    if(LocalContext.get() == context)
    {
        WARN("%p released while current on thread\n", context);
        LocalContext.set(nullptr);
        ALCcontext_DecRef(context);
    }

    ALCcontext *origctx{context};
    if(GlobalContext.compare_exchange_strong(origctx, nullptr))
        ALCcontext_DecRef(context);

    bool ret{true};
    { BackendLockGuard _{*device->Backend};
        origctx = context;
        ALCcontext *newhead{context->next.load(std::memory_order_relaxed)};
        if(!device->ContextList.compare_exchange_strong(origctx, newhead))
        {
            ALCcontext *list;
            do {
                /* origctx is what the desired context failed to match. Try
                * swapping out the next one in the list.
                */
                list = origctx;
                origctx = context;
            } while(!list->next.compare_exchange_strong(origctx, newhead));
        }
        else
            ret = !!newhead;
    }

    /* Make sure the context is finished and no longer processing in the mixer
     * before sending the message queue kill event. The backend's lock does
     * this, although waiting for a non-odd mix count would work too.
     */

    StopEventThrd(context);

    ALCcontext_DecRef(context);
    return ret;
}

static void ALCcontext_IncRef(ALCcontext *context)
{
    auto ref = IncrementRef(&context->ref);
    TRACEREF("%p increasing refcount to %u\n", context, ref);
}

void ALCcontext_DecRef(ALCcontext *context)
{
    auto ref = DecrementRef(&context->ref);
    TRACEREF("%p decreasing refcount to %u\n", context, ref);
    if(UNLIKELY(ref == 0)) delete context;
}

/* VerifyContext
 *
 * Checks if the given context is valid, returning a new reference to it if so.
 */
static ContextRef VerifyContext(ALCcontext *context)
{
    std::lock_guard<std::recursive_mutex> _{ListLock};
    auto iter = std::lower_bound(ContextList.cbegin(), ContextList.cend(), context);
    if(iter != ContextList.cend() && *iter == context)
    {
        ALCcontext_IncRef(*iter);
        return ContextRef{*iter};
    }
    return ContextRef{};
}


/* GetContextRef
 *
 * Returns a new reference to the currently active context for this thread.
 */
ContextRef GetContextRef(void)
{
    ALCcontext *context{LocalContext.get()};
    if(context)
        ALCcontext_IncRef(context);
    else
    {
        std::lock_guard<std::recursive_mutex> _{ListLock};
        context = GlobalContext.load(std::memory_order_acquire);
        if(context) ALCcontext_IncRef(context);
    }
    return ContextRef{context};
}


void AllocateVoices(ALCcontext *context, ALsizei num_voices, ALsizei old_sends)
{
    ALCdevice *device{context->Device};
    const ALsizei num_sends{device->NumAuxSends};

    if(num_voices == context->MaxVoices && num_sends == old_sends)
        return;

    /* Allocate the voice pointers, voices, and the voices' stored source
     * property set (including the dynamically-sized Send[] array) in one
     * chunk.
     */
    const size_t sizeof_voice{RoundUp(ALvoice::Sizeof(num_sends), 16)};
    const size_t size{sizeof(ALvoice*) + sizeof_voice};

    auto voices = static_cast<ALvoice**>(al_calloc(16, RoundUp(size*num_voices, 16)));
    auto voice = reinterpret_cast<ALvoice*>(reinterpret_cast<char*>(voices) + RoundUp(num_voices*sizeof(ALvoice*), 16));

    auto viter = voices;
    if(context->Voices)
    {
        const ALsizei v_count = mini(context->VoiceCount.load(std::memory_order_relaxed),
                                     num_voices);
        const ALsizei s_count = mini(old_sends, num_sends);

        /* Copy the old voice data to the new storage. */
        auto copy_voice = [&voice,num_sends,sizeof_voice,s_count](ALvoice *old_voice) -> ALvoice*
        {
            voice = new (voice) ALvoice{static_cast<size_t>(num_sends)};

            /* Make sure the old voice's Update (if any) is cleared so it
             * doesn't get deleted on deinit.
             */
            voice->mUpdate.store(old_voice->mUpdate.exchange(nullptr, std::memory_order_relaxed),
                std::memory_order_relaxed);

            voice->mSourceID.store(old_voice->mSourceID.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            voice->mPlayState.store(old_voice->mPlayState.load(std::memory_order_relaxed),
                std::memory_order_relaxed);

            voice->mProps = old_voice->mProps;
            /* Clear extraneous property set sends. */
            std::fill(std::begin(voice->mProps.Send)+s_count, std::end(voice->mProps.Send),
                ALvoiceProps::SendData{});

            voice->mPosition.store(old_voice->mPosition.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            voice->mPositionFrac.store(old_voice->mPositionFrac.load(std::memory_order_relaxed),
                std::memory_order_relaxed);

            voice->mCurrentBuffer.store(old_voice->mCurrentBuffer.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            voice->mLoopBuffer.store(old_voice->mLoopBuffer.load(std::memory_order_relaxed),
                std::memory_order_relaxed);

            voice->mFrequency = old_voice->mFrequency;
            voice->mFmtChannels = old_voice->mFmtChannels;
            voice->mNumChannels = old_voice->mNumChannels;
            voice->mSampleSize = old_voice->mSampleSize;

            voice->mStep = old_voice->mStep;
            voice->mResampler = old_voice->mResampler;

            voice->mFlags = old_voice->mFlags;

            std::copy(std::begin(old_voice->mPrevSamples), std::end(old_voice->mPrevSamples),
                std::begin(voice->mPrevSamples));

            voice->mResampleState = old_voice->mResampleState;

            voice->mAmbiScales = old_voice->mAmbiScales;
            voice->mAmbiSplitter = old_voice->mAmbiSplitter;
            std::for_each(voice->mAmbiSplitter.begin(),voice->mAmbiSplitter.end(),
                std::bind(std::mem_fn(&BandSplitter::clear), _1));

            voice->mDirect = old_voice->mDirect;
            std::copy_n(old_voice->mSend.begin(), s_count, voice->mSend.begin());

            /* Set this voice's reference. */
            ALvoice *ret = voice;
            /* Increment pointer to the next storage space. */
            voice = reinterpret_cast<ALvoice*>(reinterpret_cast<char*>(voice) + sizeof_voice);
            return ret;
        };
        viter = std::transform(context->Voices, context->Voices+v_count, viter, copy_voice);

        /* Deinit old voices. */
        auto voices_end = context->Voices + context->MaxVoices;
        std::for_each(context->Voices, voices_end, DeinitVoice);
    }
    /* Finish setting the voices and references. */
    auto init_voice = [&voice,num_sends,sizeof_voice]() -> ALvoice*
    {
        ALvoice *ret = new (voice) ALvoice{static_cast<size_t>(num_sends)};
        voice = reinterpret_cast<ALvoice*>(reinterpret_cast<char*>(voice) + sizeof_voice);
        return ret;
    };
    std::generate(viter, voices+num_voices, init_voice);

    al_free(context->Voices);
    context->Voices = voices;
    context->MaxVoices = num_voices;
    context->VoiceCount = mini(context->VoiceCount.load(std::memory_order_relaxed), num_voices);
}


/************************************************
 * Standard ALC functions
 ************************************************/

/* alcGetError
 *
 * Return last ALC generated error code for the given device
 */
ALC_API ALCenum ALC_APIENTRY alcGetError(ALCdevice *device)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(dev) return dev->LastError.exchange(ALC_NO_ERROR);
    return LastNullDeviceError.exchange(ALC_NO_ERROR);
}
END_API_FUNC


/* alcSuspendContext
 *
 * Suspends updates for the given context
 */
ALC_API ALCvoid ALC_APIENTRY alcSuspendContext(ALCcontext *context)
START_API_FUNC
{
    if(!SuspendDefers)
        return;

    ContextRef ctx{VerifyContext(context)};
    if(!ctx)
        alcSetError(nullptr, ALC_INVALID_CONTEXT);
    else
        ALCcontext_DeferUpdates(ctx.get());
}
END_API_FUNC

/* alcProcessContext
 *
 * Resumes processing updates for the given context
 */
ALC_API ALCvoid ALC_APIENTRY alcProcessContext(ALCcontext *context)
START_API_FUNC
{
    if(!SuspendDefers)
        return;

    ContextRef ctx{VerifyContext(context)};
    if(!ctx)
        alcSetError(nullptr, ALC_INVALID_CONTEXT);
    else
        ALCcontext_ProcessUpdates(ctx.get());
}
END_API_FUNC


/* alcGetString
 *
 * Returns information about the device, and error strings
 */
ALC_API const ALCchar* ALC_APIENTRY alcGetString(ALCdevice *Device, ALCenum param)
START_API_FUNC
{
    const ALCchar *value = nullptr;
    DeviceRef dev;

    switch(param)
    {
    case ALC_NO_ERROR:
        value = alcNoError;
        break;

    case ALC_INVALID_ENUM:
        value = alcErrInvalidEnum;
        break;

    case ALC_INVALID_VALUE:
        value = alcErrInvalidValue;
        break;

    case ALC_INVALID_DEVICE:
        value = alcErrInvalidDevice;
        break;

    case ALC_INVALID_CONTEXT:
        value = alcErrInvalidContext;
        break;

    case ALC_OUT_OF_MEMORY:
        value = alcErrOutOfMemory;
        break;

    case ALC_DEVICE_SPECIFIER:
        value = alcDefaultName;
        break;

    case ALC_ALL_DEVICES_SPECIFIER:
        dev = VerifyDevice(Device);
        if(dev)
            value = dev->DeviceName.c_str();
        else
        {
            ProbeAllDevicesList();
            value = alcAllDevicesList.c_str();
        }
        break;

    case ALC_CAPTURE_DEVICE_SPECIFIER:
        dev = VerifyDevice(Device);
        if(dev)
            value = dev->DeviceName.c_str();
        else
        {
            ProbeCaptureDeviceList();
            value = alcCaptureDeviceList.c_str();
        }
        break;

    /* Default devices are always first in the list */
    case ALC_DEFAULT_DEVICE_SPECIFIER:
        value = alcDefaultName;
        break;

    case ALC_DEFAULT_ALL_DEVICES_SPECIFIER:
        if(alcAllDevicesList.empty())
            ProbeAllDevicesList();

        /* Copy first entry as default. */
        alcDefaultAllDevicesSpecifier = alcAllDevicesList.c_str();
        value = alcDefaultAllDevicesSpecifier.c_str();
        break;

    case ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER:
        if(alcCaptureDeviceList.empty())
            ProbeCaptureDeviceList();

        /* Copy first entry as default. */
        alcCaptureDefaultDeviceSpecifier = alcCaptureDeviceList.c_str();
        value = alcCaptureDefaultDeviceSpecifier.c_str();
        break;

    case ALC_EXTENSIONS:
        dev = VerifyDevice(Device);
        if(dev) value = alcExtensionList;
        else value = alcNoDeviceExtList;
        break;

    case ALC_HRTF_SPECIFIER_SOFT:
        dev = VerifyDevice(Device);
        if(!dev)
            alcSetError(nullptr, ALC_INVALID_DEVICE);
        else
        {
            std::lock_guard<std::mutex> _{dev->StateLock};
            value = (dev->mHrtf ? dev->HrtfName.c_str() : "");
        }
        break;

    default:
        dev = VerifyDevice(Device);
        alcSetError(dev.get(), ALC_INVALID_ENUM);
        break;
    }

    return value;
}
END_API_FUNC


static inline ALCsizei NumAttrsForDevice(ALCdevice *device)
{
    if(device->Type == Capture) return 9;
    if(device->Type != Loopback) return 29;
    if(device->FmtChans == DevFmtAmbi3D)
        return 35;
    return 29;
}

static ALCsizei GetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values)
{
    ALCsizei i;

    if(size <= 0 || values == nullptr)
    {
        alcSetError(device, ALC_INVALID_VALUE);
        return 0;
    }

    if(!device)
    {
        switch(param)
        {
            case ALC_MAJOR_VERSION:
                values[0] = alcMajorVersion;
                return 1;
            case ALC_MINOR_VERSION:
                values[0] = alcMinorVersion;
                return 1;

            case ALC_ATTRIBUTES_SIZE:
            case ALC_ALL_ATTRIBUTES:
            case ALC_FREQUENCY:
            case ALC_REFRESH:
            case ALC_SYNC:
            case ALC_MONO_SOURCES:
            case ALC_STEREO_SOURCES:
            case ALC_CAPTURE_SAMPLES:
            case ALC_FORMAT_CHANNELS_SOFT:
            case ALC_FORMAT_TYPE_SOFT:
            case ALC_AMBISONIC_LAYOUT_SOFT:
            case ALC_AMBISONIC_SCALING_SOFT:
            case ALC_AMBISONIC_ORDER_SOFT:
            case ALC_MAX_AMBISONIC_ORDER_SOFT:
                alcSetError(nullptr, ALC_INVALID_DEVICE);
                return 0;

            default:
                alcSetError(nullptr, ALC_INVALID_ENUM);
                return 0;
        }
        return 0;
    }

    if(device->Type == Capture)
    {
        switch(param)
        {
            case ALC_ATTRIBUTES_SIZE:
                values[0] = NumAttrsForDevice(device);
                return 1;

            case ALC_ALL_ATTRIBUTES:
                i = 0;
                if(size < NumAttrsForDevice(device))
                    alcSetError(device, ALC_INVALID_VALUE);
                else
                {
                    std::lock_guard<std::mutex> _{device->StateLock};
                    values[i++] = ALC_MAJOR_VERSION;
                    values[i++] = alcMajorVersion;
                    values[i++] = ALC_MINOR_VERSION;
                    values[i++] = alcMinorVersion;
                    values[i++] = ALC_CAPTURE_SAMPLES;
                    values[i++] = device->Backend->availableSamples();
                    values[i++] = ALC_CONNECTED;
                    values[i++] = device->Connected.load(std::memory_order_relaxed);
                    values[i++] = 0;
                }
                return i;

            case ALC_MAJOR_VERSION:
                values[0] = alcMajorVersion;
                return 1;
            case ALC_MINOR_VERSION:
                values[0] = alcMinorVersion;
                return 1;

            case ALC_CAPTURE_SAMPLES:
                { std::lock_guard<std::mutex> _{device->StateLock};
                    values[0] = device->Backend->availableSamples();
                }
                return 1;

            case ALC_CONNECTED:
                { std::lock_guard<std::mutex> _{device->StateLock};
                    values[0] = device->Connected.load(std::memory_order_acquire);
                }
                return 1;

            default:
                alcSetError(device, ALC_INVALID_ENUM);
                return 0;
        }
        return 0;
    }

    /* render device */
    switch(param)
    {
        case ALC_ATTRIBUTES_SIZE:
            values[0] = NumAttrsForDevice(device);
            return 1;

        case ALC_ALL_ATTRIBUTES:
            i = 0;
            if(size < NumAttrsForDevice(device))
                alcSetError(device, ALC_INVALID_VALUE);
            else
            {
                std::lock_guard<std::mutex> _{device->StateLock};
                values[i++] = ALC_MAJOR_VERSION;
                values[i++] = alcMajorVersion;
                values[i++] = ALC_MINOR_VERSION;
                values[i++] = alcMinorVersion;
                values[i++] = ALC_EFX_MAJOR_VERSION;
                values[i++] = alcEFXMajorVersion;
                values[i++] = ALC_EFX_MINOR_VERSION;
                values[i++] = alcEFXMinorVersion;

                values[i++] = ALC_FREQUENCY;
                values[i++] = device->Frequency;
                if(device->Type != Loopback)
                {
                    values[i++] = ALC_REFRESH;
                    values[i++] = device->Frequency / device->UpdateSize;

                    values[i++] = ALC_SYNC;
                    values[i++] = ALC_FALSE;
                }
                else
                {
                    if(device->FmtChans == DevFmtAmbi3D)
                    {
                        values[i++] = ALC_AMBISONIC_LAYOUT_SOFT;
                        values[i++] = static_cast<ALCint>(device->mAmbiLayout);

                        values[i++] = ALC_AMBISONIC_SCALING_SOFT;
                        values[i++] = static_cast<ALCint>(device->mAmbiScale);

                        values[i++] = ALC_AMBISONIC_ORDER_SOFT;
                        values[i++] = device->mAmbiOrder;
                    }

                    values[i++] = ALC_FORMAT_CHANNELS_SOFT;
                    values[i++] = device->FmtChans;

                    values[i++] = ALC_FORMAT_TYPE_SOFT;
                    values[i++] = device->FmtType;
                }

                values[i++] = ALC_MONO_SOURCES;
                values[i++] = device->NumMonoSources;

                values[i++] = ALC_STEREO_SOURCES;
                values[i++] = device->NumStereoSources;

                values[i++] = ALC_MAX_AUXILIARY_SENDS;
                values[i++] = device->NumAuxSends;

                values[i++] = ALC_HRTF_SOFT;
                values[i++] = (device->mHrtf ? ALC_TRUE : ALC_FALSE);

                values[i++] = ALC_HRTF_STATUS_SOFT;
                values[i++] = device->HrtfStatus;

                values[i++] = ALC_OUTPUT_LIMITER_SOFT;
                values[i++] = device->Limiter ? ALC_TRUE : ALC_FALSE;

                values[i++] = ALC_MAX_AMBISONIC_ORDER_SOFT;
                values[i++] = MAX_AMBI_ORDER;

                values[i++] = 0;
            }
            return i;

        case ALC_MAJOR_VERSION:
            values[0] = alcMajorVersion;
            return 1;

        case ALC_MINOR_VERSION:
            values[0] = alcMinorVersion;
            return 1;

        case ALC_EFX_MAJOR_VERSION:
            values[0] = alcEFXMajorVersion;
            return 1;

        case ALC_EFX_MINOR_VERSION:
            values[0] = alcEFXMinorVersion;
            return 1;

        case ALC_FREQUENCY:
            values[0] = device->Frequency;
            return 1;

        case ALC_REFRESH:
            if(device->Type == Loopback)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            { std::lock_guard<std::mutex> _{device->StateLock};
                values[0] = device->Frequency / device->UpdateSize;
            }
            return 1;

        case ALC_SYNC:
            if(device->Type == Loopback)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = ALC_FALSE;
            return 1;

        case ALC_FORMAT_CHANNELS_SOFT:
            if(device->Type != Loopback)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = device->FmtChans;
            return 1;

        case ALC_FORMAT_TYPE_SOFT:
            if(device->Type != Loopback)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = device->FmtType;
            return 1;

        case ALC_AMBISONIC_LAYOUT_SOFT:
            if(device->Type != Loopback || device->FmtChans != DevFmtAmbi3D)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = static_cast<ALCint>(device->mAmbiLayout);
            return 1;

        case ALC_AMBISONIC_SCALING_SOFT:
            if(device->Type != Loopback || device->FmtChans != DevFmtAmbi3D)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = static_cast<ALCint>(device->mAmbiScale);
            return 1;

        case ALC_AMBISONIC_ORDER_SOFT:
            if(device->Type != Loopback || device->FmtChans != DevFmtAmbi3D)
            {
                alcSetError(device, ALC_INVALID_DEVICE);
                return 0;
            }
            values[0] = device->mAmbiOrder;
            return 1;

        case ALC_MONO_SOURCES:
            values[0] = device->NumMonoSources;
            return 1;

        case ALC_STEREO_SOURCES:
            values[0] = device->NumStereoSources;
            return 1;

        case ALC_MAX_AUXILIARY_SENDS:
            values[0] = device->NumAuxSends;
            return 1;

        case ALC_CONNECTED:
            { std::lock_guard<std::mutex> _{device->StateLock};
                values[0] = device->Connected.load(std::memory_order_acquire);
            }
            return 1;

        case ALC_HRTF_SOFT:
            values[0] = (device->mHrtf ? ALC_TRUE : ALC_FALSE);
            return 1;

        case ALC_HRTF_STATUS_SOFT:
            values[0] = device->HrtfStatus;
            return 1;

        case ALC_NUM_HRTF_SPECIFIERS_SOFT:
            { std::lock_guard<std::mutex> _{device->StateLock};
                device->HrtfList.clear();
                device->HrtfList = EnumerateHrtf(device->DeviceName.c_str());
                values[0] = static_cast<ALCint>(device->HrtfList.size());
            }
            return 1;

        case ALC_OUTPUT_LIMITER_SOFT:
            values[0] = device->Limiter ? ALC_TRUE : ALC_FALSE;
            return 1;

        case ALC_MAX_AMBISONIC_ORDER_SOFT:
            values[0] = MAX_AMBI_ORDER;
            return 1;

        default:
            alcSetError(device, ALC_INVALID_ENUM);
            return 0;
    }
    return 0;
}

/* alcGetIntegerv
 *
 * Returns information about the device and the version of OpenAL
 */
ALC_API void ALC_APIENTRY alcGetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(size <= 0 || values == nullptr)
        alcSetError(dev.get(), ALC_INVALID_VALUE);
    else
        GetIntegerv(dev.get(), param, size, values);
}
END_API_FUNC

ALC_API void ALC_APIENTRY alcGetInteger64vSOFT(ALCdevice *device, ALCenum pname, ALCsizei size, ALCint64SOFT *values)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(size <= 0 || values == nullptr)
        alcSetError(dev.get(), ALC_INVALID_VALUE);
    else if(!dev || dev->Type == Capture)
    {
        al::vector<ALCint> ivals(size);
        size = GetIntegerv(dev.get(), pname, size, ivals.data());
        std::copy(ivals.begin(), ivals.begin()+size, values);
    }
    else /* render device */
    {
        switch(pname)
        {
            case ALC_ATTRIBUTES_SIZE:
                *values = NumAttrsForDevice(dev.get())+4;
                break;

            case ALC_ALL_ATTRIBUTES:
                if(size < NumAttrsForDevice(dev.get())+4)
                    alcSetError(dev.get(), ALC_INVALID_VALUE);
                else
                {
                    ALsizei i{0};
                    std::lock_guard<std::mutex> _{dev->StateLock};
                    values[i++] = ALC_FREQUENCY;
                    values[i++] = dev->Frequency;

                    if(dev->Type != Loopback)
                    {
                        values[i++] = ALC_REFRESH;
                        values[i++] = dev->Frequency / dev->UpdateSize;

                        values[i++] = ALC_SYNC;
                        values[i++] = ALC_FALSE;
                    }
                    else
                    {
                        if(dev->FmtChans == DevFmtAmbi3D)
                        {
                            values[i++] = ALC_AMBISONIC_LAYOUT_SOFT;
                            values[i++] = static_cast<ALCint64SOFT>(dev->mAmbiLayout);

                            values[i++] = ALC_AMBISONIC_SCALING_SOFT;
                            values[i++] = static_cast<ALCint64SOFT>(dev->mAmbiScale);

                            values[i++] = ALC_AMBISONIC_ORDER_SOFT;
                            values[i++] = dev->mAmbiOrder;
                        }

                        values[i++] = ALC_FORMAT_CHANNELS_SOFT;
                        values[i++] = dev->FmtChans;

                        values[i++] = ALC_FORMAT_TYPE_SOFT;
                        values[i++] = dev->FmtType;
                    }

                    values[i++] = ALC_MONO_SOURCES;
                    values[i++] = dev->NumMonoSources;

                    values[i++] = ALC_STEREO_SOURCES;
                    values[i++] = dev->NumStereoSources;

                    values[i++] = ALC_MAX_AUXILIARY_SENDS;
                    values[i++] = dev->NumAuxSends;

                    values[i++] = ALC_HRTF_SOFT;
                    values[i++] = (dev->mHrtf ? ALC_TRUE : ALC_FALSE);

                    values[i++] = ALC_HRTF_STATUS_SOFT;
                    values[i++] = dev->HrtfStatus;

                    values[i++] = ALC_OUTPUT_LIMITER_SOFT;
                    values[i++] = dev->Limiter ? ALC_TRUE : ALC_FALSE;

                    ClockLatency clock{GetClockLatency(dev.get())};
                    values[i++] = ALC_DEVICE_CLOCK_SOFT;
                    values[i++] = clock.ClockTime.count();

                    values[i++] = ALC_DEVICE_LATENCY_SOFT;
                    values[i++] = clock.Latency.count();

                    values[i++] = 0;
                }
                break;

            case ALC_DEVICE_CLOCK_SOFT:
                { std::lock_guard<std::mutex> _{dev->StateLock};
                    nanoseconds basecount;
                    ALuint samplecount;
                    ALuint refcount;
                    do {
                        while(((refcount=ReadRef(&dev->MixCount))&1) != 0)
                            std::this_thread::yield();
                        basecount = dev->ClockBase;
                        samplecount = dev->SamplesDone;
                    } while(refcount != ReadRef(&dev->MixCount));
                    basecount += nanoseconds{seconds{samplecount}} / dev->Frequency;
                    *values = basecount.count();
                }
                break;

            case ALC_DEVICE_LATENCY_SOFT:
                { std::lock_guard<std::mutex> _{dev->StateLock};
                    ClockLatency clock{GetClockLatency(dev.get())};
                    *values = clock.Latency.count();
                }
                break;

            case ALC_DEVICE_CLOCK_LATENCY_SOFT:
                if(size < 2)
                    alcSetError(dev.get(), ALC_INVALID_VALUE);
                else
                {
                    std::lock_guard<std::mutex> _{dev->StateLock};
                    ClockLatency clock{GetClockLatency(dev.get())};
                    values[0] = clock.ClockTime.count();
                    values[1] = clock.Latency.count();
                }
                break;

            default:
                al::vector<ALCint> ivals(size);
                size = GetIntegerv(dev.get(), pname, size, ivals.data());
                std::copy(ivals.begin(), ivals.begin()+size, values);
                break;
        }
    }
}
END_API_FUNC


/* alcIsExtensionPresent
 *
 * Determines if there is support for a particular extension
 */
ALC_API ALCboolean ALC_APIENTRY alcIsExtensionPresent(ALCdevice *device, const ALCchar *extName)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(!extName)
        alcSetError(dev.get(), ALC_INVALID_VALUE);
    else
    {
        size_t len = strlen(extName);
        const char *ptr = (dev ? alcExtensionList : alcNoDeviceExtList);
        while(ptr && *ptr)
        {
            if(strncasecmp(ptr, extName, len) == 0 &&
               (ptr[len] == '\0' || isspace(ptr[len])))
                return ALC_TRUE;

            if((ptr=strchr(ptr, ' ')) != nullptr)
            {
                do {
                    ++ptr;
                } while(isspace(*ptr));
            }
        }
    }
    return ALC_FALSE;
}
END_API_FUNC


/* alcGetProcAddress
 *
 * Retrieves the function address for a particular extension function
 */
ALC_API ALCvoid* ALC_APIENTRY alcGetProcAddress(ALCdevice *device, const ALCchar *funcName)
START_API_FUNC
{
    if(!funcName)
    {
        DeviceRef dev{VerifyDevice(device)};
        alcSetError(dev.get(), ALC_INVALID_VALUE);
    }
    else
    {
        for(const auto &func : alcFunctions)
        {
            if(strcmp(func.funcName, funcName) == 0)
                return func.address;
        }
    }
    return nullptr;
}
END_API_FUNC


/* alcGetEnumValue
 *
 * Get the value for a particular ALC enumeration name
 */
ALC_API ALCenum ALC_APIENTRY alcGetEnumValue(ALCdevice *device, const ALCchar *enumName)
START_API_FUNC
{
    if(!enumName)
    {
        DeviceRef dev{VerifyDevice(device)};
        alcSetError(dev.get(), ALC_INVALID_VALUE);
    }
    else
    {
        for(const auto &enm : alcEnumerations)
        {
            if(strcmp(enm.enumName, enumName) == 0)
                return enm.value;
        }
    }
    return 0;
}
END_API_FUNC


/* alcCreateContext
 *
 * Create and attach a context to the given device.
 */
ALC_API ALCcontext* ALC_APIENTRY alcCreateContext(ALCdevice *device, const ALCint *attrList)
START_API_FUNC
{
    /* Explicitly hold the list lock while taking the StateLock in case the
     * device is asynchronously destroyed, to ensure this new context is
     * properly cleaned up after being made.
     */
    std::unique_lock<std::recursive_mutex> listlock{ListLock};
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type == Capture || !dev->Connected.load(std::memory_order_relaxed))
    {
        listlock.unlock();
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
        return nullptr;
    }
    std::unique_lock<std::mutex> statelock{dev->StateLock};
    listlock.unlock();

    dev->LastError.store(ALC_NO_ERROR);

    ContextRef context{new ALCcontext{dev.get()}};
    ALCdevice_IncRef(context->Device);

    ALCenum err{UpdateDeviceParams(dev.get(), attrList)};
    if(err != ALC_NO_ERROR)
    {
        alcSetError(dev.get(), err);
        if(err == ALC_INVALID_DEVICE)
            aluHandleDisconnect(dev.get(), "Device update failure");
        statelock.unlock();

        return nullptr;
    }
    AllocateVoices(context.get(), 256, dev->NumAuxSends);

    if(DefaultEffect.type != AL_EFFECT_NULL && dev->Type == Playback)
    {
        void *ptr{al_calloc(16, sizeof(ALeffectslot))};
        context->DefaultSlot = std::unique_ptr<ALeffectslot>{new (ptr) ALeffectslot{}};
        if(InitEffectSlot(context->DefaultSlot.get()) == AL_NO_ERROR)
            aluInitEffectPanning(context->DefaultSlot.get(), dev.get());
        else
        {
            context->DefaultSlot = nullptr;
            ERR("Failed to initialize the default effect slot\n");
        }
    }

    InitContext(context.get());

    ALfloat valf{};
    if(ConfigValueFloat(dev->DeviceName.c_str(), nullptr, "volume-adjust", &valf))
    {
        if(!std::isfinite(valf))
            ERR("volume-adjust must be finite: %f\n", valf);
        else
        {
            ALfloat db = clampf(valf, -24.0f, 24.0f);
            if(db != valf)
                WARN("volume-adjust clamped: %f, range: +/-%f\n", valf, 24.0f);
            context->GainBoost = std::pow(10.0f, db/20.0f);
            TRACE("volume-adjust gain: %f\n", context->GainBoost);
        }
    }
    UpdateListenerProps(context.get());

    {
        {
            std::lock_guard<std::recursive_mutex> _{ListLock};
            auto iter = std::lower_bound(ContextList.cbegin(), ContextList.cend(), context.get());
            ContextList.insert(iter, context.get());
            ALCcontext_IncRef(context.get());
        }

        ALCcontext *head = dev->ContextList.load();
        do {
            context->next.store(head, std::memory_order_relaxed);
        } while(!dev->ContextList.compare_exchange_weak(head, context.get()));
    }
    statelock.unlock();

    if(context->DefaultSlot)
    {
        if(InitializeEffect(context.get(), context->DefaultSlot.get(), &DefaultEffect) == AL_NO_ERROR)
            UpdateEffectSlotProps(context->DefaultSlot.get(), context.get());
        else
            ERR("Failed to initialize the default effect\n");
    }

    TRACE("Created context %p\n", context.get());
    return context.get();
}
END_API_FUNC

/* alcDestroyContext
 *
 * Remove a context from its device
 */
ALC_API ALCvoid ALC_APIENTRY alcDestroyContext(ALCcontext *context)
START_API_FUNC
{
    std::unique_lock<std::recursive_mutex> listlock{ListLock};
    auto iter = std::lower_bound(ContextList.cbegin(), ContextList.cend(), context);
    if(iter == ContextList.cend() || *iter != context)
    {
        listlock.unlock();
        alcSetError(nullptr, ALC_INVALID_CONTEXT);
        return;
    }
    /* Hold an extra reference to this context so it remains valid until the
     * ListLock is released.
     */
    ALCcontext_IncRef(*iter);
    ContextRef ctx{*iter};
    ContextList.erase(iter);

    if(ALCdevice *Device{ctx->Device})
    {
        std::lock_guard<std::mutex> _{Device->StateLock};
        if(!ReleaseContext(ctx.get(), Device) && (Device->Flags&DEVICE_RUNNING))
        {
            Device->Backend->stop();
            Device->Flags &= ~DEVICE_RUNNING;
        }
    }
    listlock.unlock();
}
END_API_FUNC


/* alcGetCurrentContext
 *
 * Returns the currently active context on the calling thread
 */
ALC_API ALCcontext* ALC_APIENTRY alcGetCurrentContext(void)
START_API_FUNC
{
    ALCcontext *Context{LocalContext.get()};
    if(!Context) Context = GlobalContext.load();
    return Context;
}
END_API_FUNC

/* alcGetThreadContext
 *
 * Returns the currently active thread-local context
 */
ALC_API ALCcontext* ALC_APIENTRY alcGetThreadContext(void)
START_API_FUNC
{ return LocalContext.get(); }
END_API_FUNC

/* alcMakeContextCurrent
 *
 * Makes the given context the active process-wide context, and removes the
 * thread-local context for the calling thread.
 */
ALC_API ALCboolean ALC_APIENTRY alcMakeContextCurrent(ALCcontext *context)
START_API_FUNC
{
    /* context must be valid or nullptr */
    ContextRef ctx;
    if(context)
    {
        ctx = VerifyContext(context);
        if(!ctx)
        {
            alcSetError(nullptr, ALC_INVALID_CONTEXT);
            return ALC_FALSE;
        }
    }
    /* Release this reference (if any) to store it in the GlobalContext
     * pointer. Take ownership of the reference (if any) that was previously
     * stored there.
     */
    ctx = ContextRef{GlobalContext.exchange(ctx.release())};

    /* Reset (decrement) the previous global reference by replacing it with the
     * thread-local context. Take ownership of the thread-local context
     * reference (if any), clearing the storage to null.
     */
    ctx = ContextRef{LocalContext.get()};
    if(ctx) LocalContext.set(nullptr);
    /* Reset (decrement) the previous thread-local reference. */

    return ALC_TRUE;
}
END_API_FUNC

/* alcSetThreadContext
 *
 * Makes the given context the active context for the current thread
 */
ALC_API ALCboolean ALC_APIENTRY alcSetThreadContext(ALCcontext *context)
START_API_FUNC
{
    /* context must be valid or nullptr */
    ContextRef ctx;
    if(context)
    {
        ctx = VerifyContext(context);
        if(!ctx)
        {
            alcSetError(nullptr, ALC_INVALID_CONTEXT);
            return ALC_FALSE;
        }
    }
    /* context's reference count is already incremented */
    ContextRef old{LocalContext.get()};
    LocalContext.set(ctx.release());

    return ALC_TRUE;
}
END_API_FUNC


/* alcGetContextsDevice
 *
 * Returns the device that a particular context is attached to
 */
ALC_API ALCdevice* ALC_APIENTRY alcGetContextsDevice(ALCcontext *Context)
START_API_FUNC
{
    ContextRef ctx{VerifyContext(Context)};
    if(!ctx)
    {
        alcSetError(nullptr, ALC_INVALID_CONTEXT);
        return nullptr;
    }
    return ctx->Device;
}
END_API_FUNC


/* alcOpenDevice
 *
 * Opens the named device.
 */
ALC_API ALCdevice* ALC_APIENTRY alcOpenDevice(const ALCchar *deviceName)
START_API_FUNC
{
    DO_INITCONFIG();

    if(!PlaybackBackend.name)
    {
        alcSetError(nullptr, ALC_INVALID_VALUE);
        return nullptr;
    }

    if(deviceName && (!deviceName[0] || strcasecmp(deviceName, alcDefaultName) == 0 || strcasecmp(deviceName, "openal-soft") == 0
#ifdef _WIN32
        /* Some old Windows apps hardcode these expecting OpenAL to use a
         * specific audio API, even when they're not enumerated. Creative's
         * router effectively ignores them too.
         */
        || strcasecmp(deviceName, "DirectSound3D") == 0 || strcasecmp(deviceName, "DirectSound") == 0
        || strcasecmp(deviceName, "MMSYSTEM") == 0
#endif
    ))
        deviceName = nullptr;

    DeviceRef device{new ALCdevice{Playback}};

    /* Set output format */
    device->FmtChans = DevFmtChannelsDefault;
    device->FmtType = DevFmtTypeDefault;
    device->Frequency = DEFAULT_OUTPUT_RATE;
    device->UpdateSize = DEFAULT_UPDATE_SIZE;
    device->BufferSize = DEFAULT_UPDATE_SIZE * DEFAULT_NUM_UPDATES;
    device->LimiterState = ALC_TRUE;

    device->SourcesMax = 256;
    device->AuxiliaryEffectSlotMax = 64;
    device->NumAuxSends = DEFAULT_SENDS;

    try {
        /* Create the device backend. */
        device->Backend = PlaybackBackend.getFactory().createBackend(device.get(),
            BackendType::Playback);

        /* Find a playback device to open */
        ALCenum err{device->Backend->open(deviceName)};
        if(err != ALC_NO_ERROR)
        {
            alcSetError(nullptr, err);
            return nullptr;
        }
    }
    catch(al::backend_exception &e) {
        WARN("Failed to open playback device: %s\n", e.what());
        alcSetError(nullptr, e.errorCode());
        return nullptr;
    }

    deviceName = device->DeviceName.c_str();
    const ALCchar *fmt{};
    if(ConfigValueStr(deviceName, nullptr, "channels", &fmt))
    {
        static constexpr struct ChannelMap {
            const char name[16];
            DevFmtChannels chans;
            ALsizei order;
        } chanlist[] = {
            { "mono",       DevFmtMono,   0 },
            { "stereo",     DevFmtStereo, 0 },
            { "quad",       DevFmtQuad,   0 },
            { "surround51", DevFmtX51,    0 },
            { "surround61", DevFmtX61,    0 },
            { "surround71", DevFmtX71,    0 },
            { "surround51rear", DevFmtX51Rear, 0 },
            { "ambi1", DevFmtAmbi3D, 1 },
            { "ambi2", DevFmtAmbi3D, 2 },
            { "ambi3", DevFmtAmbi3D, 3 },
        };

        auto iter = std::find_if(std::begin(chanlist), std::end(chanlist),
            [fmt](const ChannelMap &entry) -> bool
            { return strcasecmp(entry.name, fmt) == 0; }
        );
        if(iter == std::end(chanlist))
            ERR("Unsupported channels: %s\n", fmt);
        else
        {
            device->FmtChans = iter->chans;
            device->mAmbiOrder = iter->order;
            device->Flags |= DEVICE_CHANNELS_REQUEST;
        }
    }
    if(ConfigValueStr(deviceName, nullptr, "sample-type", &fmt))
    {
        static constexpr struct TypeMap {
            const char name[16];
            DevFmtType type;
        } typelist[] = {
            { "int8",    DevFmtByte   },
            { "uint8",   DevFmtUByte  },
            { "int16",   DevFmtShort  },
            { "uint16",  DevFmtUShort },
            { "int32",   DevFmtInt    },
            { "uint32",  DevFmtUInt   },
            { "float32", DevFmtFloat  },
        };

        auto iter = std::find_if(std::begin(typelist), std::end(typelist),
            [fmt](const TypeMap &entry) -> bool
            { return strcasecmp(entry.name, fmt) == 0; }
        );
        if(iter == std::end(typelist))
            ERR("Unsupported sample-type: %s\n", fmt);
        else
        {
            device->FmtType = iter->type;
            device->Flags |= DEVICE_SAMPLE_TYPE_REQUEST;
        }
    }

    ALuint freq{};
    if(ConfigValueUInt(deviceName, nullptr, "frequency", &freq) && freq > 0)
    {
        if(freq < MIN_OUTPUT_RATE)
        {
            ERR("%uhz request clamped to %uhz minimum\n", freq, MIN_OUTPUT_RATE);
            freq = MIN_OUTPUT_RATE;
        }
        device->UpdateSize = (device->UpdateSize*freq + device->Frequency/2) / device->Frequency;
        device->BufferSize = (device->BufferSize*freq + device->Frequency/2) / device->Frequency;
        device->Frequency = freq;
        device->Flags |= DEVICE_FREQUENCY_REQUEST;
    }

    ConfigValueUInt(deviceName, nullptr, "period_size", &device->UpdateSize);
    device->UpdateSize = clampu(device->UpdateSize, 64, 8192);

    ALuint periods{};
    if(ConfigValueUInt(deviceName, nullptr, "periods", &periods))
        device->BufferSize = device->UpdateSize * clampu(periods, 2, 16);
    else
        device->BufferSize = maxu(device->BufferSize, device->UpdateSize*2);

    ConfigValueUInt(deviceName, nullptr, "sources", &device->SourcesMax);
    if(device->SourcesMax == 0) device->SourcesMax = 256;

    ConfigValueUInt(deviceName, nullptr, "slots", &device->AuxiliaryEffectSlotMax);
    if(device->AuxiliaryEffectSlotMax == 0) device->AuxiliaryEffectSlotMax = 64;
    else device->AuxiliaryEffectSlotMax = minu(device->AuxiliaryEffectSlotMax, INT_MAX);

    if(ConfigValueInt(deviceName, nullptr, "sends", &device->NumAuxSends))
        device->NumAuxSends = clampi(
            DEFAULT_SENDS, 0, clampi(device->NumAuxSends, 0, MAX_SENDS)
        );

    device->NumStereoSources = 1;
    device->NumMonoSources = device->SourcesMax - device->NumStereoSources;

    if(ConfigValueStr(deviceName, nullptr, "ambi-format", &fmt))
    {
        if(strcasecmp(fmt, "fuma") == 0)
        {
            if(device->mAmbiOrder > 3)
                ERR("FuMa is incompatible with %d%s order ambisonics (up to third-order only)\n",
                    device->mAmbiOrder,
                    (((device->mAmbiOrder%100)/10) == 1) ? "th" :
                    ((device->mAmbiOrder%10) == 1) ? "st" :
                    ((device->mAmbiOrder%10) == 2) ? "nd" :
                    ((device->mAmbiOrder%10) == 3) ? "rd" : "th");
            else
            {
                device->mAmbiLayout = AmbiLayout::FuMa;
                device->mAmbiScale = AmbiNorm::FuMa;
            }
        }
        else if(strcasecmp(fmt, "ambix") == 0 || strcasecmp(fmt, "acn+sn3d") == 0)
        {
            device->mAmbiLayout = AmbiLayout::ACN;
            device->mAmbiScale = AmbiNorm::SN3D;
        }
        else if(strcasecmp(fmt, "acn+n3d") == 0)
        {
            device->mAmbiLayout = AmbiLayout::ACN;
            device->mAmbiScale = AmbiNorm::N3D;
        }
        else
            ERR("Unsupported ambi-format: %s\n", fmt);
    }

    {
        std::lock_guard<std::recursive_mutex> _{ListLock};
        auto iter = std::lower_bound(DeviceList.cbegin(), DeviceList.cend(), device.get());
        DeviceList.insert(iter, device.get());
        ALCdevice_IncRef(device.get());
    }

    TRACE("Created device %p, \"%s\"\n", device.get(), device->DeviceName.c_str());
    return device.get();
}
END_API_FUNC

/* alcCloseDevice
 *
 * Closes the given device.
 */
ALC_API ALCboolean ALC_APIENTRY alcCloseDevice(ALCdevice *device)
START_API_FUNC
{
    std::unique_lock<std::recursive_mutex> listlock{ListLock};
    auto iter = std::lower_bound(DeviceList.cbegin(), DeviceList.cend(), device);
    if(iter == DeviceList.cend() || *iter != device)
    {
        alcSetError(nullptr, ALC_INVALID_DEVICE);
        return ALC_FALSE;
    }
    if((*iter)->Type == Capture)
    {
        alcSetError(*iter, ALC_INVALID_DEVICE);
        return ALC_FALSE;
    }
    std::unique_lock<std::mutex> statelock{device->StateLock};

    /* Erase the device, and any remaining contexts left on it, from their
     * respective lists.
     */
    DeviceList.erase(iter);
    ALCcontext *ctx{device->ContextList.load()};
    while(ctx != nullptr)
    {
        ALCcontext *next = ctx->next.load(std::memory_order_relaxed);
        auto iter = std::lower_bound(ContextList.cbegin(), ContextList.cend(), ctx);
        if(iter != ContextList.cend() && *iter == ctx)
            ContextList.erase(iter);
        ctx = next;
    }
    listlock.unlock();

    ctx = device->ContextList.load(std::memory_order_relaxed);
    while(ctx != nullptr)
    {
        ALCcontext *next = ctx->next.load(std::memory_order_relaxed);
        WARN("Releasing context %p\n", ctx);
        ReleaseContext(ctx, device);
        ctx = next;
    }
    if((device->Flags&DEVICE_RUNNING))
        device->Backend->stop();
    device->Flags &= ~DEVICE_RUNNING;
    statelock.unlock();

    ALCdevice_DecRef(device);

    return ALC_TRUE;
}
END_API_FUNC


/************************************************
 * ALC capture functions
 ************************************************/
ALC_API ALCdevice* ALC_APIENTRY alcCaptureOpenDevice(const ALCchar *deviceName, ALCuint frequency, ALCenum format, ALCsizei samples)
START_API_FUNC
{
    DO_INITCONFIG();

    if(!CaptureBackend.name)
    {
        alcSetError(nullptr, ALC_INVALID_VALUE);
        return nullptr;
    }

    if(samples <= 0)
    {
        alcSetError(nullptr, ALC_INVALID_VALUE);
        return nullptr;
    }

    if(deviceName && (!deviceName[0] || strcasecmp(deviceName, alcDefaultName) == 0 || strcasecmp(deviceName, "openal-soft") == 0))
        deviceName = nullptr;

    DeviceRef device{new ALCdevice{Capture}};

    device->Frequency = frequency;
    device->Flags |= DEVICE_FREQUENCY_REQUEST;

    if(DecomposeDevFormat(format, &device->FmtChans, &device->FmtType) == AL_FALSE)
    {
        alcSetError(nullptr, ALC_INVALID_ENUM);
        return nullptr;
    }
    device->Flags |= DEVICE_CHANNELS_REQUEST | DEVICE_SAMPLE_TYPE_REQUEST;

    device->UpdateSize = samples;
    device->BufferSize = samples;

    try {
        device->Backend = CaptureBackend.getFactory().createBackend(device.get(),
            BackendType::Capture);

        TRACE("Capture format: %s, %s, %uhz, %u / %u buffer\n",
            DevFmtChannelsString(device->FmtChans), DevFmtTypeString(device->FmtType),
            device->Frequency, device->UpdateSize, device->BufferSize);
        ALCenum err{device->Backend->open(deviceName)};
        if(err != ALC_NO_ERROR)
        {
            alcSetError(nullptr, err);
            return nullptr;
        }
    }
    catch(al::backend_exception &e) {
        WARN("Failed to open capture device: %s\n", e.what());
        alcSetError(nullptr, e.errorCode());
        return nullptr;
    }

    {
        std::lock_guard<std::recursive_mutex> _{ListLock};
        auto iter = std::lower_bound(DeviceList.cbegin(), DeviceList.cend(), device.get());
        DeviceList.insert(iter, device.get());
        ALCdevice_IncRef(device.get());
    }

    TRACE("Created device %p, \"%s\"\n", device.get(), device->DeviceName.c_str());
    return device.get();
}
END_API_FUNC

ALC_API ALCboolean ALC_APIENTRY alcCaptureCloseDevice(ALCdevice *device)
START_API_FUNC
{
    std::unique_lock<std::recursive_mutex> listlock{ListLock};
    auto iter = std::lower_bound(DeviceList.cbegin(), DeviceList.cend(), device);
    if(iter == DeviceList.cend() || *iter != device)
    {
        alcSetError(nullptr, ALC_INVALID_DEVICE);
        return ALC_FALSE;
    }
    if((*iter)->Type != Capture)
    {
        alcSetError(*iter, ALC_INVALID_DEVICE);
        return ALC_FALSE;
    }

    DeviceList.erase(iter);
    listlock.unlock();

    { std::lock_guard<std::mutex> _{device->StateLock};
        if((device->Flags&DEVICE_RUNNING))
            device->Backend->stop();
        device->Flags &= ~DEVICE_RUNNING;
    }

    ALCdevice_DecRef(device);

    return ALC_TRUE;
}
END_API_FUNC

ALC_API void ALC_APIENTRY alcCaptureStart(ALCdevice *device)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type != Capture)
    {
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
        return;
    }

    std::lock_guard<std::mutex> _{dev->StateLock};
    if(!dev->Connected.load(std::memory_order_acquire))
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
    else if(!(dev->Flags&DEVICE_RUNNING))
    {
        if(dev->Backend->start())
            dev->Flags |= DEVICE_RUNNING;
        else
        {
            aluHandleDisconnect(dev.get(), "Device start failure");
            alcSetError(dev.get(), ALC_INVALID_DEVICE);
        }
    }
}
END_API_FUNC

ALC_API void ALC_APIENTRY alcCaptureStop(ALCdevice *device)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type != Capture)
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
    else
    {
        std::lock_guard<std::mutex> _{dev->StateLock};
        if((dev->Flags&DEVICE_RUNNING))
            dev->Backend->stop();
        dev->Flags &= ~DEVICE_RUNNING;
    }
}
END_API_FUNC

ALC_API void ALC_APIENTRY alcCaptureSamples(ALCdevice *device, ALCvoid *buffer, ALCsizei samples)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type != Capture)
    {
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
        return;
    }

    ALCenum err{ALC_INVALID_VALUE};
    { std::lock_guard<std::mutex> _{dev->StateLock};
        BackendBase *backend{dev->Backend.get()};
        if(samples >= 0 && backend->availableSamples() >= static_cast<ALCuint>(samples))
            err = backend->captureSamples(buffer, samples);
    }
    if(err != ALC_NO_ERROR)
        alcSetError(dev.get(), err);
}
END_API_FUNC


/************************************************
 * ALC loopback functions
 ************************************************/

/* alcLoopbackOpenDeviceSOFT
 *
 * Open a loopback device, for manual rendering.
 */
ALC_API ALCdevice* ALC_APIENTRY alcLoopbackOpenDeviceSOFT(const ALCchar *deviceName)
START_API_FUNC
{
    DO_INITCONFIG();

    /* Make sure the device name, if specified, is us. */
    if(deviceName && strcmp(deviceName, alcDefaultName) != 0)
    {
        alcSetError(nullptr, ALC_INVALID_VALUE);
        return nullptr;
    }

    DeviceRef device{new ALCdevice{Loopback}};

    device->SourcesMax = 256;
    device->AuxiliaryEffectSlotMax = 64;
    device->NumAuxSends = DEFAULT_SENDS;

    //Set output format
    device->BufferSize = 0;
    device->UpdateSize = 0;

    device->Frequency = DEFAULT_OUTPUT_RATE;
    device->FmtChans = DevFmtChannelsDefault;
    device->FmtType = DevFmtTypeDefault;

    ConfigValueUInt(nullptr, nullptr, "sources", &device->SourcesMax);
    if(device->SourcesMax == 0) device->SourcesMax = 256;

    ConfigValueUInt(nullptr, nullptr, "slots", &device->AuxiliaryEffectSlotMax);
    if(device->AuxiliaryEffectSlotMax == 0) device->AuxiliaryEffectSlotMax = 64;
    else device->AuxiliaryEffectSlotMax = minu(device->AuxiliaryEffectSlotMax, INT_MAX);

    if(ConfigValueInt(nullptr, nullptr, "sends", &device->NumAuxSends))
        device->NumAuxSends = clampi(
            DEFAULT_SENDS, 0, clampi(device->NumAuxSends, 0, MAX_SENDS)
        );

    device->NumStereoSources = 1;
    device->NumMonoSources = device->SourcesMax - device->NumStereoSources;

    try {
        device->Backend = LoopbackBackendFactory::getFactory().createBackend(device.get(),
            BackendType::Playback);

        // Open the "backend"
        device->Backend->open("Loopback");
    }
    catch(al::backend_exception &e) {
        WARN("Failed to open loopback device: %s\n", e.what());
        alcSetError(nullptr, e.errorCode());
        return nullptr;
    }

    {
        std::lock_guard<std::recursive_mutex> _{ListLock};
        auto iter = std::lower_bound(DeviceList.cbegin(), DeviceList.cend(), device.get());
        DeviceList.insert(iter, device.get());
        ALCdevice_IncRef(device.get());
    }

    TRACE("Created device %p\n", device.get());
    return device.get();
}
END_API_FUNC

/* alcIsRenderFormatSupportedSOFT
 *
 * Determines if the loopback device supports the given format for rendering.
 */
ALC_API ALCboolean ALC_APIENTRY alcIsRenderFormatSupportedSOFT(ALCdevice *device, ALCsizei freq, ALCenum channels, ALCenum type)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type != Loopback)
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
    else if(freq <= 0)
        alcSetError(dev.get(), ALC_INVALID_VALUE);
    else
    {
        if(IsValidALCType(type) && IsValidALCChannels(channels) && freq >= MIN_OUTPUT_RATE)
            return ALC_TRUE;
    }

    return ALC_FALSE;
}
END_API_FUNC

/* alcRenderSamplesSOFT
 *
 * Renders some samples into a buffer, using the format last set by the
 * attributes given to alcCreateContext.
 */
FORCE_ALIGN ALC_API void ALC_APIENTRY alcRenderSamplesSOFT(ALCdevice *device, ALCvoid *buffer, ALCsizei samples)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type != Loopback)
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
    else if(samples < 0 || (samples > 0 && buffer == nullptr))
        alcSetError(dev.get(), ALC_INVALID_VALUE);
    else
    {
        BackendLockGuard _{*device->Backend};
        aluMixData(dev.get(), buffer, samples);
    }
}
END_API_FUNC


/************************************************
 * ALC DSP pause/resume functions
 ************************************************/

/* alcDevicePauseSOFT
 *
 * Pause the DSP to stop audio processing.
 */
ALC_API void ALC_APIENTRY alcDevicePauseSOFT(ALCdevice *device)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type != Playback)
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
    else
    {
        std::lock_guard<std::mutex> _{dev->StateLock};
        if((dev->Flags&DEVICE_RUNNING))
            dev->Backend->stop();
        dev->Flags &= ~DEVICE_RUNNING;
        dev->Flags |= DEVICE_PAUSED;
    }
}
END_API_FUNC

/* alcDeviceResumeSOFT
 *
 * Resume the DSP to restart audio processing.
 */
ALC_API void ALC_APIENTRY alcDeviceResumeSOFT(ALCdevice *device)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type != Playback)
    {
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
        return;
    }

    std::lock_guard<std::mutex> _{dev->StateLock};
    if(!(dev->Flags&DEVICE_PAUSED))
        return;
    dev->Flags &= ~DEVICE_PAUSED;
    if(dev->ContextList.load() == nullptr)
        return;

    if(dev->Backend->start() == ALC_FALSE)
    {
        aluHandleDisconnect(dev.get(), "Device start failure");
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
        return;
    }
    dev->Flags |= DEVICE_RUNNING;
}
END_API_FUNC


/************************************************
 * ALC HRTF functions
 ************************************************/

/* alcGetStringiSOFT
 *
 * Gets a string parameter at the given index.
 */
ALC_API const ALCchar* ALC_APIENTRY alcGetStringiSOFT(ALCdevice *device, ALCenum paramName, ALCsizei index)
START_API_FUNC
{
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type == Capture)
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
    else switch(paramName)
    {
        case ALC_HRTF_SPECIFIER_SOFT:
            if(index >= 0 && static_cast<size_t>(index) < dev->HrtfList.size())
                return dev->HrtfList[index].name.c_str();
            alcSetError(dev.get(), ALC_INVALID_VALUE);
            break;

        default:
            alcSetError(dev.get(), ALC_INVALID_ENUM);
            break;
    }

    return nullptr;
}
END_API_FUNC

/* alcResetDeviceSOFT
 *
 * Resets the given device output, using the specified attribute list.
 */
ALC_API ALCboolean ALC_APIENTRY alcResetDeviceSOFT(ALCdevice *device, const ALCint *attribs)
START_API_FUNC
{
    std::unique_lock<std::recursive_mutex> listlock{ListLock};
    DeviceRef dev{VerifyDevice(device)};
    if(!dev || dev->Type == Capture)
    {
        listlock.unlock();
        alcSetError(dev.get(), ALC_INVALID_DEVICE);
        return ALC_FALSE;
    }
    std::lock_guard<std::mutex> _{dev->StateLock};
    listlock.unlock();

    /* Force the backend to stop mixing first since we're resetting. Also reset
     * the connected state so lost devices can attempt recover.
     */
    if((dev->Flags&DEVICE_RUNNING))
        dev->Backend->stop();
    dev->Flags &= ~DEVICE_RUNNING;
    device->Connected.store(true);

    ALCenum err{UpdateDeviceParams(dev.get(), attribs)};
    if(LIKELY(err == ALC_NO_ERROR)) return ALC_TRUE;

    alcSetError(dev.get(), err);
    if(err == ALC_INVALID_DEVICE)
        aluHandleDisconnect(dev.get(), "Device start failure");
    return ALC_FALSE;
}
END_API_FUNC
