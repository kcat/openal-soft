#ifndef AL_SOURCE_H
#define AL_SOURCE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <iterator>

#include "AL/al.h"
#include "AL/alc.h"

#include "alcontext.h"
#include "alnumeric.h"
#include "alu.h"
#include "vector.h"

struct ALbuffer;
struct ALeffectslot;


#define DEFAULT_SENDS  2


struct ALbufferlistitem {
    using element_type = ALbuffer*;
    using value_type = ALbuffer*;
    using index_type = size_t;
    using difference_type = ptrdiff_t;

    using pointer = ALbuffer**;
    using const_pointer = ALbuffer*const*;
    using reference = ALbuffer*&;
    using const_reference = ALbuffer*const&;

    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;


    std::atomic<ALbufferlistitem*> mNext;
    ALuint mMaxSamples;
    ALuint mNumBuffers;
    element_type mBuffers[];

    static constexpr size_t Sizeof(size_t num_buffers) noexcept
    {
        return maxz(offsetof(ALbufferlistitem, mBuffers) + sizeof(element_type)*num_buffers,
            sizeof(ALbufferlistitem));
    }

    reference front() { return mBuffers[0]; }
    const_reference front() const { return mBuffers[0]; }
    reference back() { return mBuffers[mNumBuffers-1]; }
    const_reference back() const { return mBuffers[mNumBuffers-1]; }
    reference operator[](index_type idx) { return mBuffers[idx]; }
    const_reference operator[](index_type idx) const { return mBuffers[idx]; }
    pointer data() noexcept { return mBuffers; }
    const_pointer data() const noexcept { return mBuffers; }

    index_type size() const noexcept { return mNumBuffers; }
    bool empty() const noexcept { return mNumBuffers == 0; }

    iterator begin() noexcept { return mBuffers; }
    iterator end() noexcept { return mBuffers+mNumBuffers; }
    const_iterator begin() const noexcept { return mBuffers; }
    const_iterator end() const noexcept { return mBuffers+mNumBuffers; }
    const_iterator cbegin() const noexcept { return mBuffers; }
    const_iterator cend() const noexcept { return mBuffers+mNumBuffers; }

    reverse_iterator rbegin() noexcept { return reverse_iterator{end()}; }
    reverse_iterator rend() noexcept { return reverse_iterator{begin()}; }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{end()}; }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator{begin()}; }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator{cend()}; }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator{cbegin()}; }
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
    ALdouble Offset;
    ALenum   OffsetType;

    /** Source type (static, streaming, or undetermined) */
    ALint SourceType;

    /** Source state (initial, playing, paused, or stopped) */
    ALenum state;

    /** Source Buffer Queue head. */
    ALbufferlistitem *queue;

    std::atomic_flag PropsClean;

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
