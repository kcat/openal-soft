#ifndef AL_MIDI_BASE_H
#define AL_MIDI_BASE_H

#include "alMain.h"
#include "atomic.h"
#include "evtqueue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ALsoundfont;

typedef size_t (*ReaderCb)(void *ptr, size_t size, void *stream);
typedef struct Reader {
    ReaderCb cb;
    void *ptr;
    int error;
} Reader;
inline size_t Reader_read(Reader *self, void *buf, size_t len)
{
    size_t got = (!self->error) ? self->cb(buf, len, self->ptr) : 0;
    if(got < len) self->error = 1;
    return got;
}
#define READERR(x_) ((x_)->error)

ALboolean loadSf2(Reader *stream, struct ALsoundfont *sfont, ALCcontext *context);


#define MIDI_CLOCK_RES  U64(1000000000)


struct MidiSynthVtable;

typedef struct MidiSynth {
    EvtQueue EventQueue;

    ALuint64 ClockBase;
    ALuint SamplesDone;
    ALuint SampleRate;

    /* NOTE: This rwlock is for the state and soundfont. The EventQueue and
     * related must instead use the device lock as they're used in the mixer
     * thread.
     */
    RWLock Lock;

    struct ALsoundfont **Soundfonts;
    ALsizei NumSoundfonts;

    volatile ALfloat Gain;
    volatile ALenum State;

    const struct MidiSynthVtable *vtbl;
} MidiSynth;

void MidiSynth_Construct(MidiSynth *self, ALCdevice *device);
void MidiSynth_Destruct(MidiSynth *self);
ALenum MidiSynth_selectSoundfonts(MidiSynth *self, ALCcontext *context, ALsizei count, const ALuint *ids);
inline void MidiSynth_setGain(MidiSynth *self, ALfloat gain) { self->Gain = gain; }
inline ALfloat MidiSynth_getGain(const MidiSynth *self) { return self->Gain; }
inline void MidiSynth_setState(MidiSynth *self, ALenum state) { ExchangeInt(&self->State, state); }
inline ALenum MidiSynth_getState(const MidiSynth *self) { return self->State; }
void MidiSynth_stop(MidiSynth *self);
inline void MidiSynth_reset(MidiSynth *self) { MidiSynth_stop(self); }
inline ALuint64 MidiSynth_getTime(const MidiSynth *self)
{ return self->ClockBase + (self->SamplesDone*MIDI_CLOCK_RES/self->SampleRate); }
inline ALuint64 MidiSynth_getNextEvtTime(const MidiSynth *self)
{
    if(self->EventQueue.pos == self->EventQueue.size)
        return UINT64_MAX;
    return self->EventQueue.events[self->EventQueue.pos].time;
}
void MidiSynth_setSampleRate(MidiSynth *self, ALuint srate);
inline void MidiSynth_update(MidiSynth *self, ALCdevice *device)
{ MidiSynth_setSampleRate(self, device->Frequency); }
ALenum MidiSynth_insertEvent(MidiSynth *self, ALuint64 time, ALuint event, ALsizei param1, ALsizei param2);
ALenum MidiSynth_insertSysExEvent(MidiSynth *self, ALuint64 time, const ALbyte *data, ALsizei size);


struct MidiSynthVtable {
    void (*const Destruct)(MidiSynth *self);

    ALenum (*const selectSoundfonts)(MidiSynth *self, ALCcontext *context, ALsizei count, const ALuint *ids);

    void (*const setGain)(MidiSynth *self, ALfloat gain);

    void (*const stop)(MidiSynth *self);
    void (*const reset)(MidiSynth *self);

    void (*const update)(MidiSynth *self, ALCdevice *device);
    void (*const process)(MidiSynth *self, ALuint samples, ALfloat (*restrict DryBuffer)[BUFFERSIZE]);

    void (*const Delete)(void *ptr);
};

#define DEFINE_MIDISYNTH_VTABLE(T)                                            \
DECLARE_THUNK(T, MidiSynth, void, Destruct)                                   \
DECLARE_THUNK3(T, MidiSynth, ALenum, selectSoundfonts, ALCcontext*, ALsizei, const ALuint*) \
DECLARE_THUNK1(T, MidiSynth, void, setGain, ALfloat)                          \
DECLARE_THUNK(T, MidiSynth, void, stop)                                       \
DECLARE_THUNK(T, MidiSynth, void, reset)                                      \
DECLARE_THUNK1(T, MidiSynth, void, update, ALCdevice*)                        \
DECLARE_THUNK2(T, MidiSynth, void, process, ALuint, ALfloatBUFFERSIZE*restrict) \
static void T##_MidiSynth_Delete(void *ptr)                                   \
{ T##_Delete(STATIC_UPCAST(T, MidiSynth, (MidiSynth*)ptr)); }                 \
                                                                              \
static const struct MidiSynthVtable T##_MidiSynth_vtable = {                  \
    T##_MidiSynth_Destruct,                                                   \
                                                                              \
    T##_MidiSynth_selectSoundfonts,                                           \
    T##_MidiSynth_setGain,                                                    \
    T##_MidiSynth_stop,                                                       \
    T##_MidiSynth_reset,                                                      \
    T##_MidiSynth_update,                                                     \
    T##_MidiSynth_process,                                                    \
                                                                              \
    T##_MidiSynth_Delete,                                                     \
}


MidiSynth *SSynth_create(ALCdevice *device);
MidiSynth *FSynth_create(ALCdevice *device);
MidiSynth *DSynth_create(ALCdevice *device);

MidiSynth *SynthCreate(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif /* AL_MIDI_BASE_H */
