
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "alMain.h"
#include "alError.h"
#include "evtqueue.h"
#include "rwlock.h"
#include "alu.h"

#include "midi/base.h"

typedef struct DSynth {
    DERIVE_FROM_TYPE(MidiSynth);
} DSynth;

static void DSynth_Construct(DSynth *self, ALCdevice *device);
static DECLARE_FORWARD(DSynth, MidiSynth, void, Destruct)
static DECLARE_FORWARD3(DSynth, MidiSynth, ALenum, selectSoundfonts, ALCcontext*, ALsizei, const ALuint*)
static DECLARE_FORWARD1(DSynth, MidiSynth, void, setGain, ALfloat)
static DECLARE_FORWARD1(DSynth, MidiSynth, void, setState, ALenum)
static DECLARE_FORWARD(DSynth, MidiSynth, void, stop)
static DECLARE_FORWARD(DSynth, MidiSynth, void, reset)
static DECLARE_FORWARD1(DSynth, MidiSynth, void, update, ALCdevice*)
static void DSynth_process(DSynth *self, ALuint SamplesToDo, ALfloat (*restrict DryBuffer)[BUFFERSIZE]);
static void DSynth_Delete(DSynth *self);
DEFINE_MIDISYNTH_VTABLE(DSynth);


static void DSynth_Construct(DSynth *self, ALCdevice *device)
{
    MidiSynth_Construct(STATIC_CAST(MidiSynth, self), device);
    SET_VTABLE2(DSynth, MidiSynth, self);
}


static void DSynth_processQueue(DSynth *self, ALuint64 time)
{
    EvtQueue *queue = &STATIC_CAST(MidiSynth, self)->EventQueue;

    while(queue->pos < queue->size && queue->events[queue->pos].time <= time)
        queue->pos++;
}

static void DSynth_process(DSynth *self, ALuint SamplesToDo, ALfloatBUFFERSIZE*restrict UNUSED(DryBuffer))
{
    MidiSynth *synth = STATIC_CAST(MidiSynth, self);

    if(synth->State != AL_PLAYING)
        return;

    synth->SamplesSinceLast += SamplesToDo;
    synth->SamplesToNext -= SamplesToDo;
    while(synth->SamplesToNext < 1.0f)
    {
        ALuint64 time = synth->NextEvtTime;
        if(time == UINT64_MAX)
        {
            synth->SamplesToNext = 0.0;
            break;
        }

        synth->SamplesSinceLast -= (time - synth->LastEvtTime) * synth->SamplesPerTick;
        synth->SamplesSinceLast  = maxd(synth->SamplesSinceLast, 0.0);
        synth->LastEvtTime = time;
        DSynth_processQueue(self, time);

        synth->NextEvtTime = MidiSynth_getNextEvtTime(synth);
        if(synth->NextEvtTime != UINT64_MAX)
            synth->SamplesToNext += (synth->NextEvtTime - synth->LastEvtTime) * synth->SamplesPerTick;
    }
}


static void DSynth_Delete(DSynth *self)
{
    free(self);
}


MidiSynth *DSynth_create(ALCdevice *device)
{
    DSynth *synth = calloc(1, sizeof(*synth));
    if(!synth)
    {
        ERR("Failed to allocate DSynth\n");
        return NULL;
    }
    DSynth_Construct(synth, device);
    return STATIC_CAST(MidiSynth, synth);
}
