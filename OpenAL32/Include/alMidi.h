#ifndef ALMIDI_H
#define ALMIDI_H

#include "alMain.h"
#include "evtqueue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MidiSynthVtable;

typedef struct MidiSynth {
    EvtQueue EventQueue;

    ALuint64 LastEvtTime;
    ALuint64 NextEvtTime;
    ALdouble SamplesSinceLast;
    ALdouble SamplesToNext;

    ALdouble SamplesPerTick;

    /* NOTE: This rwlock is for the state and soundfont. The EventQueue and
     * related must instead use the device lock as they're used in the mixer
     * thread.
     */
    RWLock Lock;

    volatile ALfloat Gain;
    volatile ALenum State;

    const struct MidiSynthVtable *vtbl;
} MidiSynth;

ALfloat MidiSynth_getGain(const MidiSynth *self);
ALuint64 MidiSynth_getTime(const MidiSynth *self);


struct MidiSynthVtable {
    void (*const Destruct)(MidiSynth *self);

    ALboolean (*const isSoundfont)(MidiSynth *self, const char *filename);
    ALenum (*const loadSoundfont)(MidiSynth *self, const char *filename);

    void (*const setGain)(MidiSynth *self, ALfloat gain);
    void (*const setState)(MidiSynth *self, ALenum state);

    void (*const reset)(MidiSynth *self);

    void (*const update)(MidiSynth *self, ALCdevice *device);
    void (*const process)(MidiSynth *self, ALuint samples, ALfloat (*restrict DryBuffer)[BUFFERSIZE]);

    void (*const Delete)(MidiSynth *self);
};

#define DEFINE_MIDISYNTH_VTABLE(T)                                            \
DECLARE_THUNK(T, MidiSynth, void, Destruct)                                   \
DECLARE_THUNK1(T, MidiSynth, ALboolean, isSoundfont, const char*)             \
DECLARE_THUNK1(T, MidiSynth, ALenum, loadSoundfont, const char*)              \
DECLARE_THUNK1(T, MidiSynth, void, setGain, ALfloat)                          \
DECLARE_THUNK1(T, MidiSynth, void, setState, ALenum)                          \
DECLARE_THUNK(T, MidiSynth, void, reset)                                      \
DECLARE_THUNK1(T, MidiSynth, void, update, ALCdevice*)                        \
DECLARE_THUNK2(T, MidiSynth, void, process, ALuint, ALfloatBUFFERSIZE*restrict) \
DECLARE_THUNK(T, MidiSynth, void, Delete)                                     \
                                                                              \
static const struct MidiSynthVtable T##_MidiSynth_vtable = {                  \
    T##_MidiSynth_Destruct,                                                   \
                                                                              \
    T##_MidiSynth_isSoundfont,                                                \
    T##_MidiSynth_loadSoundfont,                                              \
    T##_MidiSynth_setGain,                                                    \
    T##_MidiSynth_setState,                                                   \
    T##_MidiSynth_reset,                                                      \
    T##_MidiSynth_update,                                                     \
    T##_MidiSynth_process,                                                    \
                                                                              \
    T##_MidiSynth_Delete,                                                     \
}


MidiSynth *SynthCreate(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif /* ALMIDI_H */
