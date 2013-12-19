
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "midi/base.h"

#include "alMidi.h"
#include "alMain.h"
#include "alError.h"
#include "evtqueue.h"
#include "rwlock.h"
#include "alu.h"


/* Microsecond resolution */
#define TICKS_PER_SECOND (1000000)

/* MIDI events */
#define SYSEX_EVENT  (0xF0)


void InitEvtQueue(EvtQueue *queue)
{
    queue->events = NULL;
    queue->maxsize = 0;
    queue->size = 0;
    queue->pos = 0;
}

void ResetEvtQueue(EvtQueue *queue)
{
    ALsizei i;
    for(i = 0;i < queue->size;i++)
    {
        if(queue->events[i].event == SYSEX_EVENT)
        {
            free(queue->events[i].param.sysex.data);
            queue->events[i].param.sysex.data = NULL;
        }
    }

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
            for(pos = 0;pos < queue->pos;pos++)
            {
                if(queue->events[pos].event == SYSEX_EVENT)
                {
                    free(queue->events[pos].param.sysex.data);
                    queue->events[pos].param.sysex.data = NULL;
                }
            }
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


void MidiSynth_Construct(MidiSynth *self, ALCdevice *device)
{
    InitEvtQueue(&self->EventQueue);

    RWLockInit(&self->Lock);

    self->Gain = 1.0f;
    self->State = AL_INITIAL;

    self->LastEvtTime = 0;
    self->NextEvtTime = UINT64_MAX;
    self->SamplesSinceLast = 0.0;
    self->SamplesToNext = 0.0;

    self->SamplesPerTick = (ALdouble)device->Frequency / TICKS_PER_SECOND;
}

void MidiSynth_Destruct(MidiSynth *self)
{
    ResetEvtQueue(&self->EventQueue);
}

const char *MidiSynth_getFontName(const MidiSynth* UNUSED(self), const char *filename)
{
    if(!filename || !filename[0])
        filename = getenv("ALSOFT_SOUNDFONT");
    if(!filename || !filename[0])
        filename = GetConfigValue("midi", "soundfont", "");
    if(!filename[0])
        WARN("No default soundfont found\n");

    return filename;
}

extern inline void MidiSynth_setGain(MidiSynth *self, ALfloat gain);
extern inline ALfloat MidiSynth_getGain(const MidiSynth *self);
extern inline void MidiSynth_setState(MidiSynth *self, ALenum state);

void MidiSynth_stop(MidiSynth *self)
{
    ResetEvtQueue(&self->EventQueue);

    self->LastEvtTime = 0;
    self->NextEvtTime = UINT64_MAX;
    self->SamplesSinceLast = 0.0;
    self->SamplesToNext = 0.0;
}

extern inline void MidiSynth_reset(MidiSynth *self);

ALuint64 MidiSynth_getTime(const MidiSynth *self)
{
    ALuint64 time = self->LastEvtTime + (self->SamplesSinceLast/self->SamplesPerTick);
    return clampu(time, self->LastEvtTime, self->NextEvtTime);
}

extern inline ALuint64 MidiSynth_getNextEvtTime(const MidiSynth *self);

void MidiSynth_setSampleRate(MidiSynth *self, ALdouble srate)
{
    ALdouble sampletickrate = srate / TICKS_PER_SECOND;

    self->SamplesSinceLast = self->SamplesSinceLast * sampletickrate / self->SamplesPerTick;
    self->SamplesToNext = self->SamplesToNext * sampletickrate / self->SamplesPerTick;
    self->SamplesPerTick = sampletickrate;
}

extern inline void MidiSynth_update(MidiSynth *self, ALCdevice *device);

ALenum MidiSynth_insertEvent(MidiSynth *self, ALuint64 time, ALuint event, ALsizei param1, ALsizei param2)
{
    MidiEvent entry;
    ALenum err;

    entry.time = time;
    entry.event = event;
    entry.param.val[0] = param1;
    entry.param.val[1] = param2;

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

ALenum MidiSynth_insertSysExEvent(MidiSynth *self, ALuint64 time, const ALbyte *data, ALsizei size)
{
    MidiEvent entry;
    ALenum err;

    entry.time = time;
    entry.event = SYSEX_EVENT;
    entry.param.sysex.size = size;
    entry.param.sysex.data = malloc(size);
    if(!entry.param.sysex.data)
        return AL_OUT_OF_MEMORY;
    memcpy(entry.param.sysex.data, data, size);

    err = InsertEvtQueue(&self->EventQueue, &entry);
    if(err != AL_NO_ERROR)
    {
        free(entry.param.sysex.data);
        return err;
    }

    if(entry.time < self->NextEvtTime)
    {
        self->NextEvtTime = entry.time;

        self->SamplesToNext  = (self->NextEvtTime - self->LastEvtTime) * self->SamplesPerTick;
        self->SamplesToNext -= self->SamplesSinceLast;
    }

    return AL_NO_ERROR;
}


void ALsfzone_Construct(ALsfzone *self)
{
    self->Generators = NULL;
    self->NumGenerators = 0;
    self->GeneratorsMax = 0;

    self->Modulators = NULL;
    self->NumModulators = 0;
    self->ModulatorsMax = 0;

    self->Object = NULL;
}

void ALsfzone_Destruct(ALsfzone *self)
{
    self->Object = NULL;

    free(self->Modulators);
    self->Modulators = NULL;
    self->NumModulators = 0;
    self->ModulatorsMax = 0;

    free(self->Generators);
    self->Generators = NULL;
    self->NumGenerators = 0;
    self->GeneratorsMax = 0;
}

ALenum ALsfzone_addGenerator(ALsfzone *self, ALenum generator, ALint value)
{
    ALsizei i;
    for(i = 0;i < self->NumGenerators;i++)
    {
        if(self->Generators[i].Generator == generator)
        {
            self->Generators[i].Value = value;
            return AL_NO_ERROR;
        }
    }

    if(self->NumGenerators == self->GeneratorsMax)
    {
        ALsizei newmax = 0;
        ALvoid *temp = NULL;

        newmax = (self->GeneratorsMax ? self->GeneratorsMax<<1 : 1);
        if(newmax > self->GeneratorsMax)
            temp = realloc(self->Generators, newmax * sizeof(ALsfgenerator));
        if(!temp) return AL_OUT_OF_MEMORY;

        self->Generators = temp;
        self->GeneratorsMax = newmax;
    }

    self->Generators[self->NumGenerators].Generator = generator;
    self->Generators[self->NumGenerators].Value = value;
    self->NumGenerators++;

    return AL_NO_ERROR;
}

ALenum ALsfzone_addModulator(ALsfzone *self, ALenum sourceop, ALenum destop, ALint amount, ALenum amtsourceop, ALenum transop)
{
    ALsizei i;
    for(i = 0;i < self->NumModulators;i++)
    {
        if(self->Modulators[i].SourceOp == sourceop && self->Modulators[i].DestOp == destop &&
           self->Modulators[i].AmountSourceOp == amtsourceop &&
           self->Modulators[i].TransformOp == transop)
        {
            self->Modulators[i].Amount = amount;
            return AL_NO_ERROR;
        }
    }

    if(self->NumModulators == self->ModulatorsMax)
    {
        ALsizei newmax = 0;
        ALvoid *temp = NULL;

        newmax = (self->ModulatorsMax ? self->ModulatorsMax<<1 : 1);
        if(newmax > self->ModulatorsMax)
            temp = realloc(self->Modulators, newmax * sizeof(ALsfmodulator));
        if(!temp) return AL_OUT_OF_MEMORY;

        self->Modulators = temp;
        self->ModulatorsMax = newmax;
    }

    self->Modulators[self->NumModulators].SourceOp = sourceop;
    self->Modulators[self->NumModulators].DestOp = destop;
    self->Modulators[self->NumModulators].Amount = amount;
    self->Modulators[self->NumModulators].AmountSourceOp = amtsourceop;
    self->Modulators[self->NumModulators].TransformOp = transop;
    self->NumModulators++;

    return AL_NO_ERROR;
}

/* Stores a new object pointer in the zone. Returns the old object pointer. */
ALvoid *ALsfzone_setRefObject(ALsfzone *self, ALvoid *object)
{
    ALvoid *oldobj = self->Object;
    self->Object = object;
    return oldobj;
}


void ALsfsample_Construct(ALsfsample *self)
{
    self->ref = 0;

    self->id = 0;
}

void ALsfsample_Destruct(ALsfsample *self)
{
    self->id = 0;
}


void ALsfinstrument_Construct(ALsfinstrument *self)
{
    self->ref = 0;

    self->Zones = NULL;
    self->NumZones = 0;

    self->id = 0;
}

void ALsfinstrument_Destruct(ALsfinstrument *self)
{
    ALsizei i;

    self->id = 0;

    for(i = 0;i < self->NumZones;i++)
    {
        if(self->Zones[i].Object)
            DecrementRef(&((ALsfsample*)self->Zones[i].Object)->ref);
        ALsfzone_Destruct(&self->Zones[i]);
    }
    free(self->Zones);
    self->Zones = NULL;
    self->NumZones = 0;
}


void ALsfpreset_Construct(ALsfpreset *self)
{
    self->ref = 0;

    self->Zones = NULL;
    self->NumZones = 0;

    self->id = 0;
}

void ALsfpreset_Destruct(ALsfpreset *self)
{
    ALsizei i;

    self->id = 0;

    for(i = 0;i < self->NumZones;i++)
    {
        if(self->Zones[i].Object)
            DecrementRef(&((ALsfinstrument*)self->Zones[i].Object)->ref);
        ALsfzone_Destruct(&self->Zones[i]);
    }
    free(self->Zones);
    self->Zones = NULL;
    self->NumZones = 0;
}


void ALsoundfont_Construct(ALsoundfont *self)
{
    self->ref = 0;

    self->Presets = NULL;
    self->NumPresets = 0;

    self->Samples = NULL;
    self->NumSamples = 0;

    self->Mapped = AL_FALSE;

    self->id = 0;
}

void ALsoundfont_Destruct(ALsoundfont *self)
{
    ALsizei i;

    self->id = 0;

    for(i = 0;i < self->NumPresets;i++)
    {
        DecrementRef(&self->Presets[i]->ref);
        self->Presets[i] = NULL;
    }
    free(self->Presets);
    self->Presets = NULL;
    self->NumPresets = 0;

    free(self->Samples);
    self->Samples = NULL;
    self->NumSamples = 0;
}
