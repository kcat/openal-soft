
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "alMain.h"
#include "alError.h"
#include "evtqueue.h"
#include "alu.h"

#include "midi/base.h"


typedef struct SSynth {
    DERIVE_FROM_TYPE(MidiSynth);
} SSynth;

static void SSynth_mixSamples(SSynth *self, ALuint SamplesToDo, ALfloat (*restrict DryBuffer)[BUFFERSIZE]);

static void SSynth_Construct(SSynth *self, ALCdevice *device);
static void SSynth_Destruct(SSynth *self);
static DECLARE_FORWARD3(SSynth, MidiSynth, ALenum, selectSoundfonts, ALCcontext*, ALsizei, const ALuint*)
static DECLARE_FORWARD1(SSynth, MidiSynth, void, setGain, ALfloat)
static DECLARE_FORWARD(SSynth, MidiSynth, void, stop)
static DECLARE_FORWARD(SSynth, MidiSynth, void, reset)
static void SSynth_update(SSynth *self, ALCdevice *device);
static void SSynth_process(SSynth *self, ALuint SamplesToDo, ALfloat (*restrict DryBuffer)[BUFFERSIZE]);
DECLARE_DEFAULT_ALLOCATORS(SSynth)
DEFINE_MIDISYNTH_VTABLE(SSynth);


static void SSynth_Construct(SSynth *self, ALCdevice *device)
{
    MidiSynth_Construct(STATIC_CAST(MidiSynth, self), device);
    SET_VTABLE2(SSynth, MidiSynth, self);
}

static void SSynth_Destruct(SSynth* UNUSED(self))
{
}


static void SSynth_update(SSynth* UNUSED(self), ALCdevice* UNUSED(device))
{
}


static void SSynth_mixSamples(SSynth* UNUSED(self), ALuint UNUSED(SamplesToDo), ALfloatBUFFERSIZE *restrict UNUSED(DryBuffer))
{
}


static void SSynth_processQueue(SSynth *self, ALuint64 time)
{
    EvtQueue *queue = &STATIC_CAST(MidiSynth, self)->EventQueue;

    while(queue->pos < queue->size && queue->events[queue->pos].time <= time)
        queue->pos++;
}

static void SSynth_process(SSynth *self, ALuint SamplesToDo, ALfloat (*restrict DryBuffer)[BUFFERSIZE])
{
    MidiSynth *synth = STATIC_CAST(MidiSynth, self);
    ALenum state = synth->State;
    ALuint64 curtime;
    ALuint total = 0;

    if(state == AL_INITIAL)
        return;
    if(state != AL_PLAYING)
    {
        SSynth_mixSamples(self, SamplesToDo, DryBuffer);
        return;
    }

    curtime = MidiSynth_getTime(synth);
    while(total < SamplesToDo)
    {
        ALuint64 time, diff;
        ALint tonext;

        time = MidiSynth_getNextEvtTime(synth);
        diff = maxu64(time, curtime) - curtime;
        if(diff >= MIDI_CLOCK_RES || time == UINT64_MAX)
        {
            /* If there's no pending event, or if it's more than 1 second
             * away, do as many samples as we can. */
            tonext = INT_MAX;
        }
        else
        {
            /* Figure out how many samples until the next event. */
            tonext  = (ALint)((diff*synth->SampleRate + (MIDI_CLOCK_RES-1)) / MIDI_CLOCK_RES);
            tonext -= total;
            /* For efficiency reasons, try to mix a multiple of 64 samples
             * (~1ms @ 44.1khz) before processing the next event. */
            tonext = (tonext+63) & ~63;
        }

        if(tonext > 0)
        {
            ALuint todo = mini(tonext, SamplesToDo-total);
            SSynth_mixSamples(self, todo, DryBuffer);
            total += todo;
            tonext -= todo;
        }
        if(total < SamplesToDo && tonext <= 0)
            SSynth_processQueue(self, time);
    }

    synth->SamplesDone += SamplesToDo;
    synth->ClockBase += (synth->SamplesDone/synth->SampleRate) * MIDI_CLOCK_RES;
    synth->SamplesDone %= synth->SampleRate;
}


MidiSynth *SSynth_create(ALCdevice *device)
{
    SSynth *synth;

    /* This option is temporary. Once this synth is in a more usable state, a
     * more generic selector should be used. */
    if(!GetConfigValueBool("midi", "internal-synth", 0))
    {
        TRACE("Not using internal MIDI synth\n");
        return NULL;
    }

    synth = SSynth_New(sizeof(*synth));
    if(!synth)
    {
        ERR("Failed to allocate SSynth\n");
        return NULL;
    }
    SSynth_Construct(synth, device);
    return STATIC_CAST(MidiSynth, synth);
}
