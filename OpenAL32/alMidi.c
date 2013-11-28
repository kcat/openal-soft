
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "alMidi.h"
#include "alMain.h"
#include "alError.h"
#include "evtqueue.h"
#include "rwlock.h"
#include "alu.h"


/* Microsecond resolution */
#define TICKS_PER_SECOND (1000000)

static void MidiSynth_Construct(MidiSynth *self, ALCdevice *device);
static void MidiSynth_Destruct(MidiSynth *self);
static inline void MidiSynth_setState(MidiSynth *self, ALenum state);
ALuint64 MidiSynth_getTime(const MidiSynth *self);
static inline ALuint64 MidiSynth_getNextEvtTime(const MidiSynth *self);
static void MidiSynth_update(MidiSynth *self, ALCdevice *device);
static void MidiSynth_updateSpeed(MidiSynth *self);
static ALenum MidiSynth_insertEvent(MidiSynth *self, ALuint64 time, ALuint event, ALsizei param1, ALsizei param2);


static void MidiSynth_Construct(MidiSynth *self, ALCdevice *device)
{
    InitEvtQueue(&self->EventQueue);

    self->LastEvtTime = 0;
    self->NextEvtTime = UINT64_MAX;
    self->SamplesSinceLast = 0.0;
    self->SamplesToNext = 0.0;

    self->State = AL_INITIAL;

    self->SampleRate = device->Frequency;
    self->SamplesPerTick = (ALdouble)self->SampleRate / TICKS_PER_SECOND;
    MidiSynth_updateSpeed(self);
}

static void MidiSynth_Destruct(MidiSynth *self)
{
    ResetEvtQueue(&self->EventQueue);
}

static inline void MidiSynth_setState(MidiSynth *self, ALenum state)
{
    ExchangeInt(&self->State, state);
}

ALuint64 MidiSynth_getTime(const MidiSynth *self)
{
    ALuint64 time = self->LastEvtTime + (self->SamplesSinceLast/self->SamplesPerTick);
    return clampu(time, self->LastEvtTime, self->NextEvtTime);
}

static inline ALuint64 MidiSynth_getNextEvtTime(const MidiSynth *self)
{
    if(self->EventQueue.pos == self->EventQueue.size)
        return UINT64_MAX;
    return self->EventQueue.events[self->EventQueue.pos].time;
}

static void MidiSynth_update(MidiSynth *self, ALCdevice *device)
{
    self->SampleRate = device->Frequency;
    MidiSynth_updateSpeed(self);
}

static void MidiSynth_updateSpeed(MidiSynth *self)
{
    ALdouble sampletickrate = (ALdouble)self->SampleRate / TICKS_PER_SECOND;

    self->SamplesSinceLast = self->SamplesSinceLast * sampletickrate / self->SamplesPerTick;
    self->SamplesToNext = self->SamplesToNext * sampletickrate / self->SamplesPerTick;
    self->SamplesPerTick = sampletickrate;
}

static ALenum MidiSynth_insertEvent(MidiSynth *self, ALuint64 time, ALuint event, ALsizei param1, ALsizei param2)
{
    MidiEvent entry = { time, event, { param1, param2 } };
    ALenum err;

    err = InsertEvtQueue(&self->EventQueue, &entry);
    if(err != AL_NO_ERROR) return err;

    if(entry.time < self->NextEvtTime)
    {
        self->NextEvtTime = entry.time;

        self->SamplesToNext  = (self->NextEvtTime - self->LastEvtTime) * self->SamplesPerTick;
        self->SamplesToNext -= self->SamplesSinceLast;
    }

    return AL_NO_ERROR;
}


#ifdef HAVE_FLUIDSYNTH

#include <fluidsynth.h>

typedef struct FSynth {
    DERIVE_FROM_TYPE(MidiSynth);

    /* NOTE: This rwlock is for setting the soundfont. The EventQueue and
     * related must use the device lock as they're used in the mixer thread.
     */
    RWLock Lock;

    fluid_settings_t *Settings;
    fluid_synth_t *Synth;
    int FontID;
} FSynth;

static void FSynth_Construct(FSynth *self, ALCdevice *device);
static void FSynth_Destruct(FSynth *self);
static ALboolean FSynth_init(FSynth *self, ALCdevice *device);
static void FSynth_setState(FSynth *self, ALenum state);
static void FSynth_update(FSynth *self, ALCdevice *device);
static void FSynth_process(FSynth *self, ALuint SamplesToDo, ALfloat (*restrict DryBuffer)[BUFFERSIZE]);
static void FSynth_Delete(FSynth *self);
DEFINE_MIDISYNTH_VTABLE(FSynth);


static void FSynth_Construct(FSynth *self, ALCdevice *device)
{
    MidiSynth_Construct(STATIC_CAST(MidiSynth, self), device);
    SET_VTABLE2(FSynth, MidiSynth, self);

    RWLockInit(&self->Lock);

    self->Settings = NULL;
    self->Synth = NULL;
    self->FontID = FLUID_FAILED;
}

static void FSynth_Destruct(FSynth *self)
{
    if(self->FontID != FLUID_FAILED)
        fluid_synth_sfunload(self->Synth, self->FontID, 0);
    self->FontID = FLUID_FAILED;

    if(self->Synth != NULL)
        delete_fluid_synth(self->Synth);
    self->Synth = NULL;

    if(self->Settings != NULL)
        delete_fluid_settings(self->Settings);
    self->Settings = NULL;

    MidiSynth_Destruct(STATIC_CAST(MidiSynth, self));
}

static ALboolean FSynth_init(FSynth *self, ALCdevice *device)
{
    self->Settings = new_fluid_settings();
    if(!self->Settings)
    {
        ERR("Failed to create FluidSettings\n");
        return AL_FALSE;
    }

    fluid_settings_setint(self->Settings, "synth.reverb.active", 1);
    fluid_settings_setint(self->Settings, "synth.chorus.active", 1);
    fluid_settings_setint(self->Settings, "synth.polyphony", 256);
    fluid_settings_setstr(self->Settings, "synth.midi-bank-select", "mma");
    fluid_settings_setnum(self->Settings, "synth.sample-rate", device->Frequency);

    self->Synth = new_fluid_synth(self->Settings);
    if(!self->Synth)
    {
        ERR("Failed to create FluidSynth\n");
        return AL_FALSE;
    }

    return AL_TRUE;
}

static void FSynth_setState(FSynth *self, ALenum state)
{
    WriteLock(&self->Lock);
    if(state == AL_PLAYING)
    {
        if(self->FontID == FLUID_FAILED)
        {
            int fontid = FLUID_FAILED;
            const char *fname = getenv("FLUID_SOUNDFONT");
            if(fname && fname[0])
                fontid = fluid_synth_sfload(self->Synth, fname, 1);
            if(fontid != FLUID_FAILED)
                self->FontID = fontid;
            else
                ERR("Failed to load soundfont '%s'\n", fname?fname:"(nil)");
        }
    }
    MidiSynth_setState(STATIC_CAST(MidiSynth, self), state);
    WriteUnlock(&self->Lock);
}

static void FSynth_update(FSynth *self, ALCdevice *device)
{
    fluid_settings_setnum(self->Settings, "synth.sample-rate", device->Frequency);
    fluid_synth_set_sample_rate(self->Synth, device->Frequency);
    MidiSynth_update(STATIC_CAST(MidiSynth, self), device);
}


static void FSynth_processQueue(FSynth *self, ALuint64 time)
{
    EvtQueue *queue = &STATIC_CAST(MidiSynth, self)->EventQueue;

    while(queue->pos < queue->size && queue->events[queue->pos].time <= time)
    {
        const MidiEvent *evt = &queue->events[queue->pos];

        switch((evt->event&0xF0))
        {
            case AL_NOTEOFF_SOFT:
                fluid_synth_noteoff(self->Synth, (evt->event&0x0F), evt->param[0]);
                break;
            case AL_NOTEON_SOFT:
                fluid_synth_noteon(self->Synth, (evt->event&0x0F), evt->param[0], evt->param[1]);
                break;
            case AL_AFTERTOUCH_SOFT:
                break;

            case AL_CONTROLLERCHANGE_SOFT:
                fluid_synth_cc(self->Synth, (evt->event&0x0F), evt->param[0], evt->param[1]);
                break;
            case AL_PROGRAMCHANGE_SOFT:
                fluid_synth_program_change(self->Synth, (evt->event&0x0F), evt->param[0]);
                break;

            case AL_CHANNELPRESSURE_SOFT:
                fluid_synth_channel_pressure(self->Synth, (evt->event&0x0F), evt->param[0]);
                break;

            case AL_PITCHBEND_SOFT:
                fluid_synth_pitch_bend(self->Synth, (evt->event&0x0F), (evt->param[0]&0x7F) |
                                                                       ((evt->param[1]&0x7F)<<7));
                break;
        }

        queue->pos++;
    }

    if(queue->pos == queue->size)
    {
        queue->pos = 0;
        queue->size = 0;
    }
}

static void FSynth_process(FSynth *self, ALuint SamplesToDo, ALfloat (*restrict DryBuffer)[BUFFERSIZE])
{
    MidiSynth *synth = STATIC_CAST(MidiSynth, self);
    ALuint total = 0;

    if(synth->State != AL_PLAYING)
    {
        if(synth->State == AL_PAUSED)
            fluid_synth_write_float(self->Synth, SamplesToDo, DryBuffer[FrontLeft], 0, 1,
                                                              DryBuffer[FrontRight], 0, 1);
        return;
    }

    while(total < SamplesToDo)
    {
        if(synth->SamplesToNext >= 1.0)
        {
            ALuint todo = minu(SamplesToDo - total, fastf2u(synth->SamplesToNext));

            fluid_synth_write_float(self->Synth, todo,
                                    &DryBuffer[FrontLeft][total], 0, 1,
                                    &DryBuffer[FrontRight][total], 0, 1);
            total += todo;
            synth->SamplesSinceLast += todo;
            synth->SamplesToNext -= todo;
        }
        else
        {
            ALuint64 time = synth->NextEvtTime;
            if(time == UINT64_MAX)
            {
                synth->SamplesSinceLast += SamplesToDo-total;
                fluid_synth_write_float(self->Synth, SamplesToDo-total,
                                        &DryBuffer[FrontLeft][total], 0, 1,
                                        &DryBuffer[FrontRight][total], 0, 1);
                break;
            }

            synth->SamplesSinceLast -= (time - synth->LastEvtTime) * synth->SamplesPerTick;
            synth->SamplesSinceLast = maxd(synth->SamplesSinceLast, 0.0);
            synth->LastEvtTime = time;
            FSynth_processQueue(self, time);

            synth->NextEvtTime = MidiSynth_getNextEvtTime(synth);
            if(synth->NextEvtTime != UINT64_MAX)
                synth->SamplesToNext += (synth->NextEvtTime - synth->LastEvtTime) * synth->SamplesPerTick;
        }
    }
}


static void FSynth_Delete(FSynth *self)
{
    free(self);
}


#endif /* HAVE_FLUIDSYNTH */


typedef struct DSynth {
    DERIVE_FROM_TYPE(MidiSynth);
} DSynth;

static void DSynth_Construct(DSynth *self, ALCdevice *device);
static DECLARE_FORWARD(DSynth, MidiSynth, void, Destruct)
static DECLARE_FORWARD1(DSynth, MidiSynth, void, setState, ALenum)
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

    if(queue->pos == queue->size)
    {
        queue->pos = 0;
        queue->size = 0;
    }
}

static void DSynth_process(DSynth *self, ALuint SamplesToDo, ALfloatBUFFERSIZE*restrict UNUSED(DryBuffer))
{
    MidiSynth *synth = STATIC_CAST(MidiSynth, self);
    ALuint total = 0;

    if(synth->State == AL_INITIAL)
        return;
    if(synth->State == AL_PAUSED)
        return;

    while(total < SamplesToDo)
    {
        if(synth->SamplesToNext >= 1.0)
        {
            ALuint todo = minu(SamplesToDo - total, fastf2u(synth->SamplesToNext));

            total += todo;
            synth->SamplesSinceLast += todo;
            synth->SamplesToNext -= todo;
        }
        else
        {
            ALuint64 time = synth->NextEvtTime;
            if(time == UINT64_MAX)
            {
                synth->SamplesSinceLast += SamplesToDo-total;
                break;
            }

            synth->SamplesSinceLast -= (time - synth->LastEvtTime) * synth->SamplesPerTick;
            synth->SamplesSinceLast = maxd(synth->SamplesSinceLast, 0.0);
            synth->LastEvtTime = time;
            DSynth_processQueue(self, time);

            synth->NextEvtTime = MidiSynth_getNextEvtTime(synth);
            if(synth->NextEvtTime != UINT64_MAX)
                synth->SamplesToNext += (synth->NextEvtTime - synth->LastEvtTime) * synth->SamplesPerTick;
        }
    }
}


static void DSynth_Delete(DSynth *self)
{
    free(self);
}


MidiSynth *SynthCreate(ALCdevice *device)
{
    FSynth *fsynth;
    DSynth *dsynth;

    fsynth = calloc(1, sizeof(*fsynth));
    if(!fsynth)
        ERR("Failed to allocate FSynth\n");
    else
    {
        FSynth_Construct(fsynth, device);
        if(FSynth_init(fsynth, device))
            return STATIC_CAST(MidiSynth, fsynth);
        DELETE_OBJ(STATIC_CAST(MidiSynth, fsynth));
        fsynth = NULL;
    }

    dsynth = calloc(1, sizeof(*dsynth));
    if(!dsynth)
        ERR("Failed to allocate DSynth\n");
    else
    {
        DSynth_Construct(dsynth, device);
        return STATIC_CAST(MidiSynth, dsynth);
    }

    return NULL;
}


AL_API void AL_APIENTRY alMidiEventSOFT(ALuint64SOFT time, ALenum event, ALsizei channel, ALsizei param1, ALsizei param2)
{
    ALCdevice *device;
    ALCcontext *context;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    if(!(event == AL_NOTEOFF_SOFT || event == AL_NOTEON_SOFT ||
         event == AL_AFTERTOUCH_SOFT || event == AL_CONTROLLERCHANGE_SOFT ||
         event == AL_PROGRAMCHANGE_SOFT || event == AL_CHANNELPRESSURE_SOFT ||
         event == AL_PITCHBEND_SOFT))
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    if(!(channel >= 0 && channel <= 15))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(!(param1 >= 0 && param1 <= 127))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(!(param2 >= 0 && param2 <= 127))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    ALCdevice_Lock(device);
    err = MidiSynth_insertEvent(device->Synth, time, event|channel, param1, param2);
    ALCdevice_Unlock(device);
    if(err != AL_NO_ERROR)
        alSetError(context, err);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiPlaySOFT(void)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    V(device->Synth,setState)(AL_PLAYING);

    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiPauseSOFT(void)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    V(device->Synth,setState)(AL_PAUSED);

    ALCcontext_DecRef(context);
}


void InitEvtQueue(EvtQueue *queue)
{
    queue->events = NULL;
    queue->maxsize = 0;
    queue->size = 0;
    queue->pos = 0;
}

void ResetEvtQueue(EvtQueue *queue)
{
    free(queue->events);
    queue->events = NULL;
    queue->maxsize = 0;
    queue->size = 0;
    queue->pos = 0;
}

ALenum InsertEvtQueue(EvtQueue *queue, const MidiEvent *evt)
{
    ALsizei pos;

    if(queue->maxsize == queue->size)
    {
        if(queue->pos > 0)
        {
            /* Queue has some stale entries, remove them to make space for more
             * events. */
            memmove(&queue->events[0], &queue->events[queue->pos],
                    (queue->size-queue->pos)*sizeof(queue->events[0]));
            queue->size -= queue->pos;
            queue->pos = 0;
        }
        else
        {
            /* Queue is full, double the allocated space. */
            void *temp = NULL;
            ALsizei newsize;

            newsize = (queue->maxsize ? (queue->maxsize<<1) : 16);
            if(newsize > queue->maxsize)
                temp = realloc(queue->events, newsize * sizeof(queue->events[0]));
            if(!temp)
                return AL_OUT_OF_MEMORY;

            queue->events = temp;
            queue->maxsize = newsize;
        }
    }

    pos = queue->pos;
    if(queue->size > 0)
    {
        ALsizei high = queue->size - 1;
        while(pos < high)
        {
            ALsizei mid = pos + (high-pos)/2;
            if(queue->events[mid].time < evt->time)
                pos = mid + 1;
            else
                high = mid;
        }
        while(pos < queue->size && queue->events[pos].time <= evt->time)
            pos++;

        if(pos < queue->size)
            memmove(&queue->events[pos+1], &queue->events[pos],
                    (queue->size-pos)*sizeof(queue->events[0]));
    }

    queue->events[pos] = *evt;
    queue->size++;

    return AL_NO_ERROR;
}
