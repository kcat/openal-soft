
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
static inline ALuint MidiSynth_getTime(const MidiSynth *self);
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

    self->FontName = NULL;

    self->SampleRate = device->Frequency;
    self->SamplesPerTick = (ALdouble)self->SampleRate / TICKS_PER_SECOND;
    MidiSynth_updateSpeed(self);
}

static void MidiSynth_Destruct(MidiSynth *self)
{
    free(self->FontName);
    self->FontName = NULL;

    ResetEvtQueue(&self->EventQueue);
}

static inline void MidiSynth_setState(MidiSynth *self, ALenum state)
{
    ExchangeInt(&self->State, state);
}

static inline ALuint MidiSynth_getTime(const MidiSynth *self)
{
    ALuint time = self->LastEvtTime + (self->SamplesSinceLast/self->SamplesPerTick);
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
