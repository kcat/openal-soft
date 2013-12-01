
#include "config.h"

#include <stdio.h>
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

#define SYSEX_EVENT  (0xF0)


static void MidiSynth_Construct(MidiSynth *self, ALCdevice *device);
static void MidiSynth_Destruct(MidiSynth *self);
static inline const char *MidiSynth_getFontName(const MidiSynth *self, const char *filename);
static inline void MidiSynth_setGain(MidiSynth *self, ALfloat gain);
static inline void MidiSynth_setState(MidiSynth *self, ALenum state);
static inline void MidiSynth_reset(MidiSynth *self);
ALuint64 MidiSynth_getTime(const MidiSynth *self);
static inline ALuint64 MidiSynth_getNextEvtTime(const MidiSynth *self);
static inline void MidiSynth_update(MidiSynth *self, ALCdevice *device);
static void MidiSynth_setSampleRate(MidiSynth *self, ALdouble srate);
static ALenum MidiSynth_insertEvent(MidiSynth *self, ALuint64 time, ALuint event, ALsizei param1, ALsizei param2);


static void MidiSynth_Construct(MidiSynth *self, ALCdevice *device)
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

static void MidiSynth_Destruct(MidiSynth *self)
{
    ResetEvtQueue(&self->EventQueue);
}

static inline const char *MidiSynth_getFontName(const MidiSynth* UNUSED(self), const char *filename)
{
    if(!filename || !filename[0])
        filename = getenv("ALSOFT_SOUNDFONT");
    if(!filename || !filename[0])
        filename = GetConfigValue("midi", "soundfont", "");
    if(!filename[0])
        WARN("No default soundfont found\n");

    return filename;
}

static inline void MidiSynth_setGain(MidiSynth *self, ALfloat gain)
{
    self->Gain = gain;
}

ALfloat MidiSynth_getGain(const MidiSynth *self)
{
    return self->Gain;
}

static inline void MidiSynth_setState(MidiSynth *self, ALenum state)
{
    ExchangeInt(&self->State, state);
}

static inline void MidiSynth_reset(MidiSynth *self)
{
    ResetEvtQueue(&self->EventQueue);

    self->LastEvtTime = 0;
    self->NextEvtTime = UINT64_MAX;
    self->SamplesSinceLast = 0.0;
    self->SamplesToNext = 0.0;
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

static inline void MidiSynth_update(MidiSynth *self, ALCdevice *device)
{
    MidiSynth_setSampleRate(self, device->Frequency);
}

static void MidiSynth_setSampleRate(MidiSynth *self, ALdouble srate)
{
    ALdouble sampletickrate = srate / TICKS_PER_SECOND;

    self->SamplesSinceLast = self->SamplesSinceLast * sampletickrate / self->SamplesPerTick;
    self->SamplesToNext = self->SamplesToNext * sampletickrate / self->SamplesPerTick;
    self->SamplesPerTick = sampletickrate;
}


static ALenum MidiSynth_insertEvent(MidiSynth *self, ALuint64 time, ALuint event, ALsizei param1, ALsizei param2)
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

static ALenum MidiSynth_insertSysExEvent(MidiSynth *self, ALuint64 time, const ALbyte *data, ALsizei size)
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

    fluid_settings_t *Settings;
    fluid_synth_t *Synth;
    int FontID;

    ALboolean ForceGM2BankSelect;
} FSynth;

static void FSynth_Construct(FSynth *self, ALCdevice *device);
static void FSynth_Destruct(FSynth *self);
static ALboolean FSynth_init(FSynth *self, ALCdevice *device);
static ALboolean FSynth_isSoundfont(FSynth *self, const char *filename);
static ALenum FSynth_loadSoundfont(FSynth *self, const char *filename);
static void FSynth_setGain(FSynth *self, ALfloat gain);
static void FSynth_setState(FSynth *self, ALenum state);
static void FSynth_reset(FSynth *self);
static void FSynth_update(FSynth *self, ALCdevice *device);
static void FSynth_process(FSynth *self, ALuint SamplesToDo, ALfloat (*restrict DryBuffer)[BUFFERSIZE]);
static void FSynth_Delete(FSynth *self);
DEFINE_MIDISYNTH_VTABLE(FSynth);


static void FSynth_Construct(FSynth *self, ALCdevice *device)
{
    MidiSynth_Construct(STATIC_CAST(MidiSynth, self), device);
    SET_VTABLE2(FSynth, MidiSynth, self);

    self->Settings = NULL;
    self->Synth = NULL;
    self->FontID = FLUID_FAILED;
    self->ForceGM2BankSelect = AL_FALSE;
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
    fluid_settings_setnum(self->Settings, "synth.sample-rate", device->Frequency);

    self->Synth = new_fluid_synth(self->Settings);
    if(!self->Synth)
    {
        ERR("Failed to create FluidSynth\n");
        return AL_FALSE;
    }

    return AL_TRUE;
}


static ALboolean FSynth_isSoundfont(FSynth *self, const char *filename)
{
    filename = MidiSynth_getFontName(STATIC_CAST(MidiSynth, self), filename);
    if(!filename[0])
        return AL_FALSE;

    if(!fluid_is_soundfont(filename))
        return AL_FALSE;
    return AL_TRUE;
}

static ALenum FSynth_loadSoundfont(FSynth *self, const char *filename)
{
    int fontid;

    filename = MidiSynth_getFontName(STATIC_CAST(MidiSynth, self), filename);
    if(!filename[0])
        return AL_INVALID_VALUE;

    fontid = fluid_synth_sfload(self->Synth, filename, 1);
    if(fontid == FLUID_FAILED)
    {
        ERR("Failed to load soundfont '%s'\n", filename);
        return AL_INVALID_VALUE;
    }

    if(self->FontID != FLUID_FAILED)
        fluid_synth_sfunload(self->Synth, self->FontID, 1);
    self->FontID = fontid;

    return AL_NO_ERROR;
}


static void FSynth_setGain(FSynth *self, ALfloat gain)
{
    /* Scale gain by an additional 0.2 (-14dB), to help keep the mix from clipping. */
    fluid_settings_setnum(self->Settings, "synth.gain", 0.2 * gain);
    fluid_synth_set_gain(self->Synth, 0.2f * gain);
    MidiSynth_setGain(STATIC_CAST(MidiSynth, self), gain);
}


static void FSynth_setState(FSynth *self, ALenum state)
{
    if(state == AL_PLAYING && self->FontID == FLUID_FAILED)
        FSynth_loadSoundfont(self, NULL);

    MidiSynth_setState(STATIC_CAST(MidiSynth, self), state);
}

static void FSynth_reset(FSynth *self)
{
    ALsizei chan;
    for(chan = 0;chan < 16;chan++)
    {
        /* All sounds off + reset all controllers */
        fluid_synth_cc(self->Synth, chan, 120, 0);
        fluid_synth_cc(self->Synth, chan, 121, 0);
    }

    MidiSynth_reset(STATIC_CAST(MidiSynth, self));
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

        if(evt->event == SYSEX_EVENT)
        {
            static const ALbyte gm2_on[] = { 0x7E, 0x7F, 0x09, 0x03 };
            static const ALbyte gm2_off[] = { 0x7E, 0x7F, 0x09, 0x02 };
            int handled = 0;

            fluid_synth_sysex(self->Synth, evt->param.sysex.data, evt->param.sysex.size, NULL, NULL, &handled, 0);
            if(!handled && evt->param.sysex.size >= (ALsizei)sizeof(gm2_on))
            {
                if(memcmp(evt->param.sysex.data, gm2_on, sizeof(gm2_on)) == 0)
                    self->ForceGM2BankSelect = AL_TRUE;
                else if(memcmp(evt->param.sysex.data, gm2_off, sizeof(gm2_off)) == 0)
                    self->ForceGM2BankSelect = AL_FALSE;
            }
        }
        else switch((evt->event&0xF0))
        {
            case AL_NOTEOFF_SOFT:
                fluid_synth_noteoff(self->Synth, (evt->event&0x0F), evt->param.val[0]);
                break;
            case AL_NOTEON_SOFT:
                fluid_synth_noteon(self->Synth, (evt->event&0x0F), evt->param.val[0], evt->param.val[1]);
                break;
            case AL_AFTERTOUCH_SOFT:
                break;

            case AL_CONTROLLERCHANGE_SOFT:
                if(self->ForceGM2BankSelect)
                {
                    int chan = (evt->event&0x0F);
                    if(evt->param.val[0] == 0)
                    {
                        if(evt->param.val[1] == 120 && (chan == 9 || chan == 10))
                            fluid_synth_set_channel_type(self->Synth, chan, CHANNEL_TYPE_DRUM);
                        else if(evt->param.val[1] == 121)
                            fluid_synth_set_channel_type(self->Synth, chan, CHANNEL_TYPE_MELODIC);
                        break;
                    }
                    if(evt->param.val[0] == 32)
                    {
                        fluid_synth_bank_select(self->Synth, chan, evt->param.val[1]);
                        break;
                    }
                }
                fluid_synth_cc(self->Synth, (evt->event&0x0F), evt->param.val[0], evt->param.val[1]);
                break;
            case AL_PROGRAMCHANGE_SOFT:
                fluid_synth_program_change(self->Synth, (evt->event&0x0F), evt->param.val[0]);
                break;

            case AL_CHANNELPRESSURE_SOFT:
                fluid_synth_channel_pressure(self->Synth, (evt->event&0x0F), evt->param.val[0]);
                break;

            case AL_PITCHBEND_SOFT:
                fluid_synth_pitch_bend(self->Synth, (evt->event&0x0F), (evt->param.val[0]&0x7F) |
                                                                       ((evt->param.val[1]&0x7F)<<7));
                break;
        }

        queue->pos++;
    }
}

static void FSynth_process(FSynth *self, ALuint SamplesToDo, ALfloat (*restrict DryBuffer)[BUFFERSIZE])
{
    MidiSynth *synth = STATIC_CAST(MidiSynth, self);
    ALenum state = synth->State;
    ALuint total = 0;

    if(state != AL_PLAYING)
    {
        if(state == AL_PAUSED)
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
static ALboolean DSynth_isSoundfont(DSynth *self, const char *filename);
static ALenum DSynth_loadSoundfont(DSynth *self, const char *filename);
static DECLARE_FORWARD1(DSynth, MidiSynth, void, setGain, ALfloat)
static DECLARE_FORWARD1(DSynth, MidiSynth, void, setState, ALenum)
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


static ALboolean DSynth_isSoundfont(DSynth *self, const char *filename)
{
    char buf[12];
    FILE *f;

    filename = MidiSynth_getFontName(STATIC_CAST(MidiSynth, self), filename);
    if(!filename[0])
        return AL_FALSE;

    f = fopen(filename, "rb");
    if(!f) return AL_FALSE;

    if(fread(buf, 1, sizeof(buf), f) != sizeof(buf))
    {
        fclose(f);
        return AL_FALSE;
    }

    if(memcmp(buf, "RIFF", 4) != 0 || memcmp(buf+8, "sfbk", 4) != 0)
    {
        fclose(f);
        return AL_FALSE;
    }

    fclose(f);
    return AL_TRUE;
}

static ALenum DSynth_loadSoundfont(DSynth *self, const char *filename)
{
    if(!DSynth_isSoundfont(self, filename))
        return AL_INVALID_VALUE;
    return AL_NO_ERROR;
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


MidiSynth *SynthCreate(ALCdevice *device)
{
#ifdef HAVE_FLUIDSYNTH
    {
        FSynth *synth = calloc(1, sizeof(*synth));
        if(!synth)
            ERR("Failed to allocate FSynth\n");
        else
        {
            FSynth_Construct(synth, device);
            if(FSynth_init(synth, device))
                return STATIC_CAST(MidiSynth, synth);
            DELETE_OBJ(STATIC_CAST(MidiSynth, synth));
        }
    }
#endif

    {
        DSynth *synth = calloc(1, sizeof(*synth));
        if(!synth)
            ERR("Failed to allocate DSynth\n");
        else
        {
            DSynth_Construct(synth, device);
            return STATIC_CAST(MidiSynth, synth);
        }
    }

    return NULL;
}


AL_API ALboolean AL_APIENTRY alIsSoundfontSOFT(const char *filename)
{
    ALCdevice *device;
    ALCcontext *context;
    ALboolean ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    device = context->Device;
    ret = V(device->Synth,isSoundfont)(filename);

    ALCcontext_DecRef(context);

    return ret;
}

AL_API void AL_APIENTRY alMidiSoundfontSOFT(const char *filename)
{
    ALCdevice *device;
    ALCcontext *context;
    MidiSynth *synth;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    synth = device->Synth;

    WriteLock(&synth->Lock);
    if(synth->State == AL_PLAYING || synth->State == AL_PAUSED)
        alSetError(context, AL_INVALID_OPERATION);
    else
    {
        err = V(synth,loadSoundfont)(filename);
        if(err != AL_NO_ERROR)
            alSetError(context, err);
    }
    WriteUnlock(&synth->Lock);

    ALCcontext_DecRef(context);
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

AL_API void AL_APIENTRY alMidiSysExSOFT(ALuint64SOFT time, const ALbyte *data, ALsizei size)
{
    ALCdevice *device;
    ALCcontext *context;
    ALenum err;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    if(!data || size < 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    for(i = 0;i < size;i++)
    {
        if((data[i]&0x80))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    }

    device = context->Device;
    ALCdevice_Lock(device);
    err = MidiSynth_insertSysExEvent(device->Synth, time, data, size);
    ALCdevice_Unlock(device);
    if(err != AL_NO_ERROR)
        alSetError(context, err);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiPlaySOFT(void)
{
    ALCcontext *context;
    MidiSynth *synth;

    context = GetContextRef();
    if(!context) return;

    synth = context->Device->Synth;
    WriteLock(&synth->Lock);
    V(synth,setState)(AL_PLAYING);
    WriteUnlock(&synth->Lock);

    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiPauseSOFT(void)
{
    ALCcontext *context;
    MidiSynth *synth;

    context = GetContextRef();
    if(!context) return;

    synth = context->Device->Synth;
    WriteLock(&synth->Lock);
    V(synth,setState)(AL_PAUSED);
    WriteUnlock(&synth->Lock);

    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alMidiStopSOFT(void)
{
    ALCdevice *device;
    ALCcontext *context;
    MidiSynth *synth;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    synth = device->Synth;

    WriteLock(&synth->Lock);
    V(synth,setState)(AL_STOPPED);

    ALCdevice_Lock(device);
    V0(synth,reset)();
    ALCdevice_Unlock(device);
    WriteUnlock(&synth->Lock);

    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alMidiGainSOFT(ALfloat value)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value >= 0.0f && isfinite(value)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    V(device->Synth,setGain)(value);

done:
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
