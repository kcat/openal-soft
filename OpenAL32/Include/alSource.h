#ifndef _AL_SOURCE_H_
#define _AL_SOURCE_H_

#include "alMain.h"
#include "alu.h"
#include "hrtf.h"
#include "almalloc.h"
#include "atomic.h"

#define DEFAULT_SENDS  2

struct ALbuffer;
struct ALsource;
struct ALeffectslot;


struct ALbufferlistitem {
    std::atomic<ALbufferlistitem*> next;
    ALsizei max_samples;
    ALsizei num_buffers;
    ALbuffer *buffers[];
};


struct ALsource {
    /** Source properties. */
    ALfloat   Pitch;
    ALfloat   Gain;
    ALfloat   OuterGain;
    ALfloat   MinGain;
    ALfloat   MaxGain;
    ALfloat   InnerAngle;
    ALfloat   OuterAngle;
    ALfloat   RefDistance;
    ALfloat   MaxDistance;
    ALfloat   RolloffFactor;
    ALfloat   Position[3];
    ALfloat   Velocity[3];
    ALfloat   Direction[3];
    ALfloat   Orientation[2][3];
    ALboolean HeadRelative;
    ALboolean Looping;
    DistanceModel mDistanceModel;
    Resampler mResampler;
    ALboolean DirectChannels;
    SpatializeMode mSpatialize;

    ALboolean DryGainHFAuto;
    ALboolean WetGainAuto;
    ALboolean WetGainHFAuto;
    ALfloat   OuterGainHF;

    ALfloat AirAbsorptionFactor;
    ALfloat RoomRolloffFactor;
    ALfloat DopplerFactor;

    /* NOTE: Stereo pan angles are specified in radians, counter-clockwise
     * rather than clockwise.
     */
    ALfloat StereoPan[2];

    ALfloat Radius;

    /** Direct filter and auxiliary send info. */
    struct {
        ALfloat Gain;
        ALfloat GainHF;
        ALfloat HFReference;
        ALfloat GainLF;
        ALfloat LFReference;
    } Direct;
    struct SendData {
        ALeffectslot *Slot;
        ALfloat Gain;
        ALfloat GainHF;
        ALfloat HFReference;
        ALfloat GainLF;
        ALfloat LFReference;
    };
    al::vector<SendData> Send;

    /**
     * Last user-specified offset, and the offset type (bytes, samples, or
     * seconds).
     */
    ALdouble Offset;
    ALenum   OffsetType;

    /** Source type (static, streaming, or undetermined) */
    ALint SourceType;

    /** Source state (initial, playing, paused, or stopped) */
    ALenum state;

    /** Source Buffer Queue head. */
    ALbufferlistitem *queue;

    std::atomic_flag PropsClean{true};

    /* Index into the context's Voices array. Lazily updated, only checked and
     * reset when looking up the voice.
     */
    ALint VoiceIdx;

    /** Self ID */
    ALuint id;


    ALsource(ALsizei num_sends);
    ~ALsource();

    ALsource(const ALsource&) = delete;
    ALsource& operator=(const ALsource&) = delete;
};

void UpdateAllSourceProps(ALCcontext *context);

#endif
