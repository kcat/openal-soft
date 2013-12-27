
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "midi/base.h"

#include "alMain.h"
#include "alError.h"
#include "evtqueue.h"
#include "rwlock.h"
#include "alu.h"

#ifdef HAVE_FLUIDSYNTH

#include <fluidsynth.h>


/* MIDI events */
#define SYSEX_EVENT  (0xF0)

/* MIDI controllers */
#define CTRL_BANKSELECT_MSB  (0)
#define CTRL_BANKSELECT_LSB  (32)
#define CTRL_ALLNOTESOFF     (123)


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
static DECLARE_FORWARD3(FSynth, MidiSynth, ALenum, selectSoundfonts, ALCdevice*, ALsizei, const ALuint*)
static void FSynth_setGain(FSynth *self, ALfloat gain);
static void FSynth_setState(FSynth *self, ALenum state);
static void FSynth_stop(FSynth *self);
static void FSynth_reset(FSynth *self);
static void FSynth_update(FSynth *self, ALCdevice *device);
static void FSynth_processQueue(FSynth *self, ALuint64 time);
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
    if(!filename[0]) return AL_FALSE;

    if(!fluid_is_soundfont(filename))
        return AL_FALSE;
    return AL_TRUE;
}

static ALenum FSynth_loadSoundfont(FSynth *self, const char *filename)
{
    int fontid;

    filename = MidiSynth_getFontName(STATIC_CAST(MidiSynth, self), filename);
    if(!filename[0]) return AL_INVALID_VALUE;

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

static void FSynth_stop(FSynth *self)
{
    MidiSynth *synth = STATIC_CAST(MidiSynth, self);
    ALsizei chan;

    /* Make sure all pending events are processed. */
    while(!(synth->SamplesToNext >= 1.0))
    {
        ALuint64 time = synth->NextEvtTime;
        if(time == UINT64_MAX)
            break;

        synth->SamplesSinceLast -= (time - synth->LastEvtTime) * synth->SamplesPerTick;
        synth->SamplesSinceLast = maxd(synth->SamplesSinceLast, 0.0);
        synth->LastEvtTime = time;
        FSynth_processQueue(self, time);

        synth->NextEvtTime = MidiSynth_getNextEvtTime(synth);
        if(synth->NextEvtTime != UINT64_MAX)
            synth->SamplesToNext += (synth->NextEvtTime - synth->LastEvtTime) * synth->SamplesPerTick;
    }

    /* All notes off */
    for(chan = 0;chan < 16;chan++)
        fluid_synth_cc(self->Synth, chan, CTRL_ALLNOTESOFF, 0);

    MidiSynth_stop(STATIC_CAST(MidiSynth, self));
}

static void FSynth_reset(FSynth *self)
{
    /* Reset to power-up status. */
    fluid_synth_system_reset(self->Synth);

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
                    if(evt->param.val[0] == CTRL_BANKSELECT_MSB)
                    {
                        if(evt->param.val[1] == 120 && (chan == 9 || chan == 10))
                            fluid_synth_set_channel_type(self->Synth, chan, CHANNEL_TYPE_DRUM);
                        else if(evt->param.val[1] == 121)
                            fluid_synth_set_channel_type(self->Synth, chan, CHANNEL_TYPE_MELODIC);
                        break;
                    }
                    if(evt->param.val[0] == CTRL_BANKSELECT_LSB)
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

    if(state == AL_INITIAL)
        return;
    if(state != AL_PLAYING)
    {
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


MidiSynth *FSynth_create(ALCdevice *device)
{
    FSynth *synth = calloc(1, sizeof(*synth));
    if(!synth)
    {
        ERR("Failed to allocate FSynth\n");
        return NULL;
    }
    FSynth_Construct(synth, device);

    if(FSynth_init(synth, device) == AL_FALSE)
    {
        DELETE_OBJ(STATIC_CAST(MidiSynth, synth));
        return NULL;
    }

    return STATIC_CAST(MidiSynth, synth);
}

#else

MidiSynth *FSynth_create(ALCdevice* UNUSED(device))
{
    return NULL;
}

#endif