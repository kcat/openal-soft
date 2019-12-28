#ifndef AL_SOURCE_H
#define AL_SOURCE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <iterator>

#include "AL/al.h"
#include "AL/alc.h"

#include "alcontext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "alu.h"
#include "vector.h"

struct ALbuffer;
struct ALeffectslot;


#define DEFAULT_SENDS  2

#define INVALID_VOICE_IDX static_cast<ALuint>(-1)

struct ALbufferlistitem {
    std::atomic<ALbufferlistitem*> mNext{nullptr};
    ALuint mSampleLen{0u};
    ALbuffer *mBuffer{nullptr};

    DEF_NEWDEL(ALbufferlistitem)
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
    std::array<ALfloat,3> Position;
    std::array<ALfloat,3> Velocity;
    std::array<ALfloat,3> Direction;
    std::array<ALfloat,3> OrientAt;
    std::array<ALfloat,3> OrientUp;
    bool HeadRelative;
    bool Looping;
    DistanceModel mDistanceModel;
    Resampler mResampler;
    DirectMode DirectChannels;
    SpatializeMode mSpatialize;

    bool DryGainHFAuto;
    bool WetGainAuto;
    bool WetGainHFAuto;
    ALfloat OuterGainHF;

    ALfloat AirAbsorptionFactor;
    ALfloat RoomRolloffFactor;
    ALfloat DopplerFactor;

    /* NOTE: Stereo pan angles are specified in radians, counter-clockwise
     * rather than clockwise.
     */
    std::array<ALfloat,2> StereoPan;

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
    ALdouble Offset{0.0};
    ALenum   OffsetType{AL_NONE};

    /** Source type (static, streaming, or undetermined) */
    ALenum SourceType{AL_UNDETERMINED};

    /** Source state (initial, playing, paused, or stopped) */
    ALenum state{AL_INITIAL};

    /** Source Buffer Queue head. */
    ALbufferlistitem *queue{nullptr};

    std::atomic_flag PropsClean;

    /* Index into the context's Voices array. Lazily updated, only checked and
     * reset when looking up the voice.
     */
    ALuint VoiceIdx{INVALID_VOICE_IDX};

    /** Self ID */
    ALuint id{0};


    ALsource(ALuint num_sends);
    ~ALsource();

    ALsource(const ALsource&) = delete;
    ALsource& operator=(const ALsource&) = delete;
};

void UpdateAllSourceProps(ALCcontext *context);

#endif
