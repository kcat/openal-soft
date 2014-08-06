
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "midi/base.h"

#include "alMain.h"
#include "alError.h"
#include "alMidi.h"
#include "alu.h"
#include "compat.h"
#include "evtqueue.h"
#include "rwlock.h"

#ifdef HAVE_FLUIDSYNTH

#include <fluidsynth.h>


#ifdef HAVE_DYNLOAD
#define FLUID_FUNCS(MAGIC)                                                    \
    MAGIC(new_fluid_synth);                                                   \
    MAGIC(delete_fluid_synth);                                                \
    MAGIC(new_fluid_settings);                                                \
    MAGIC(delete_fluid_settings);                                             \
    MAGIC(fluid_settings_setint);                                             \
    MAGIC(fluid_settings_setnum);                                             \
    MAGIC(fluid_synth_noteon);                                                \
    MAGIC(fluid_synth_noteoff);                                               \
    MAGIC(fluid_synth_program_change);                                        \
    MAGIC(fluid_synth_pitch_bend);                                            \
    MAGIC(fluid_synth_channel_pressure);                                      \
    MAGIC(fluid_synth_cc);                                                    \
    MAGIC(fluid_synth_sysex);                                                 \
    MAGIC(fluid_synth_bank_select);                                           \
    MAGIC(fluid_synth_set_channel_type);                                      \
    MAGIC(fluid_synth_all_sounds_off);                                        \
    MAGIC(fluid_synth_system_reset);                                          \
    MAGIC(fluid_synth_set_gain);                                              \
    MAGIC(fluid_synth_set_sample_rate);                                       \
    MAGIC(fluid_synth_write_float);                                           \
    MAGIC(fluid_synth_add_sfloader);                                          \
    MAGIC(fluid_synth_sfload);                                                \
    MAGIC(fluid_synth_sfunload);                                              \
    MAGIC(fluid_synth_alloc_voice);                                           \
    MAGIC(fluid_synth_start_voice);                                           \
    MAGIC(fluid_voice_gen_set);                                               \
    MAGIC(fluid_voice_add_mod);                                               \
    MAGIC(fluid_mod_set_source1);                                             \
    MAGIC(fluid_mod_set_source2);                                             \
    MAGIC(fluid_mod_set_amount);                                              \
    MAGIC(fluid_mod_set_dest);

void *fsynth_handle = NULL;
#define DECL_FUNC(x) __typeof(x) *p##x
FLUID_FUNCS(DECL_FUNC)
#undef DECL_FUNC

#define new_fluid_synth pnew_fluid_synth
#define delete_fluid_synth pdelete_fluid_synth
#define new_fluid_settings pnew_fluid_settings
#define delete_fluid_settings pdelete_fluid_settings
#define fluid_settings_setint pfluid_settings_setint
#define fluid_settings_setnum pfluid_settings_setnum
#define fluid_synth_noteon pfluid_synth_noteon
#define fluid_synth_noteoff pfluid_synth_noteoff
#define fluid_synth_program_change pfluid_synth_program_change
#define fluid_synth_pitch_bend pfluid_synth_pitch_bend
#define fluid_synth_channel_pressure pfluid_synth_channel_pressure
#define fluid_synth_cc pfluid_synth_cc
#define fluid_synth_sysex pfluid_synth_sysex
#define fluid_synth_bank_select pfluid_synth_bank_select
#define fluid_synth_set_channel_type pfluid_synth_set_channel_type
#define fluid_synth_all_sounds_off pfluid_synth_all_sounds_off
#define fluid_synth_system_reset pfluid_synth_system_reset
#define fluid_synth_set_gain pfluid_synth_set_gain
#define fluid_synth_set_sample_rate pfluid_synth_set_sample_rate
#define fluid_synth_write_float pfluid_synth_write_float
#define fluid_synth_add_sfloader pfluid_synth_add_sfloader
#define fluid_synth_sfload pfluid_synth_sfload
#define fluid_synth_sfunload pfluid_synth_sfunload
#define fluid_synth_alloc_voice pfluid_synth_alloc_voice
#define fluid_synth_start_voice pfluid_synth_start_voice
#define fluid_voice_gen_set pfluid_voice_gen_set
#define fluid_voice_add_mod pfluid_voice_add_mod
#define fluid_mod_set_source1 pfluid_mod_set_source1
#define fluid_mod_set_source2 pfluid_mod_set_source2
#define fluid_mod_set_amount pfluid_mod_set_amount
#define fluid_mod_set_dest pfluid_mod_set_dest

static ALboolean LoadFSynth(void)
{
    ALboolean ret = AL_TRUE;
    if(!fsynth_handle)
    {
        fsynth_handle = LoadLib("libfluidsynth.so.1");
        if(!fsynth_handle) return AL_FALSE;

#define LOAD_FUNC(x) do {                                                     \
     p##x = GetSymbol(fsynth_handle, #x);                                     \
     if(!p##x) ret = AL_FALSE;                                                \
} while(0)
        FLUID_FUNCS(LOAD_FUNC)
#undef LOAD_FUNC

        if(ret == AL_FALSE)
        {
            CloseLib(fsynth_handle);
            fsynth_handle = NULL;
        }
    }
    return ret;
}
#else
static inline ALboolean LoadFSynth(void) { return AL_TRUE; }
#endif


/* MIDI events */
#define SYSEX_EVENT  (0xF0)

/* MIDI controllers */
#define CTRL_BANKSELECT_MSB  (0)
#define CTRL_BANKSELECT_LSB  (32)
#define CTRL_ALLNOTESOFF     (123)


static int getModInput(ALenum input)
{
    switch(input)
    {
        case AL_ONE_SOFT: return FLUID_MOD_NONE;
        case AL_NOTEON_VELOCITY_SOFT: return FLUID_MOD_VELOCITY;
        case AL_NOTEON_KEY_SOFT: return FLUID_MOD_KEY;
        case AL_KEYPRESSURE_SOFT: return FLUID_MOD_KEYPRESSURE;
        case AL_CHANNELPRESSURE_SOFT: return FLUID_MOD_CHANNELPRESSURE;
        case AL_PITCHBEND_SOFT: return FLUID_MOD_PITCHWHEEL;
        case AL_PITCHBEND_SENSITIVITY_SOFT: return FLUID_MOD_PITCHWHEELSENS;
    }
    return input&0x7F;
}

static int getModFlags(ALenum input, ALenum type, ALenum form)
{
    int ret = 0;

    switch(type)
    {
        case AL_UNORM_SOFT: ret |= FLUID_MOD_UNIPOLAR | FLUID_MOD_POSITIVE; break;
        case AL_UNORM_REV_SOFT: ret |= FLUID_MOD_UNIPOLAR | FLUID_MOD_NEGATIVE; break;
        case AL_SNORM_SOFT: ret |= FLUID_MOD_BIPOLAR | FLUID_MOD_POSITIVE; break;
        case AL_SNORM_REV_SOFT: ret |= FLUID_MOD_BIPOLAR | FLUID_MOD_NEGATIVE; break;
    }
    switch(form)
    {
        case AL_LINEAR_SOFT: ret |= FLUID_MOD_LINEAR; break;
        case AL_CONCAVE_SOFT: ret |= FLUID_MOD_CONCAVE; break;
        case AL_CONVEX_SOFT: ret |= FLUID_MOD_CONVEX; break;
        case AL_SWITCH_SOFT: ret |= FLUID_MOD_SWITCH; break;
    }
    /* Source input values less than 128 correspond to a MIDI continuous
     * controller. Otherwise, it's a general controller. */
    if(input < 128) ret |= FLUID_MOD_CC;
    else ret |= FLUID_MOD_GC;

    return ret;
}

static enum fluid_gen_type getModDest(ALenum gen)
{
    switch(gen)
    {
        case AL_MOD_LFO_TO_PITCH_SOFT: return GEN_MODLFOTOPITCH;
        case AL_VIBRATO_LFO_TO_PITCH_SOFT: return GEN_VIBLFOTOPITCH;
        case AL_MOD_ENV_TO_PITCH_SOFT: return GEN_MODENVTOPITCH;
        case AL_FILTER_CUTOFF_SOFT: return GEN_FILTERFC;
        case AL_FILTER_RESONANCE_SOFT: return GEN_FILTERQ;
        case AL_MOD_LFO_TO_FILTER_CUTOFF_SOFT: return GEN_MODLFOTOFILTERFC;
        case AL_MOD_ENV_TO_FILTER_CUTOFF_SOFT: return GEN_MODENVTOFILTERFC;
        case AL_MOD_LFO_TO_VOLUME_SOFT: return GEN_MODLFOTOVOL;
        case AL_CHORUS_SEND_SOFT: return GEN_CHORUSSEND;
        case AL_REVERB_SEND_SOFT: return GEN_REVERBSEND;
        case AL_PAN_SOFT: return GEN_PAN;
        case AL_MOD_LFO_DELAY_SOFT: return GEN_MODLFODELAY;
        case AL_MOD_LFO_FREQUENCY_SOFT: return GEN_MODLFOFREQ;
        case AL_VIBRATO_LFO_DELAY_SOFT: return GEN_VIBLFODELAY;
        case AL_VIBRATO_LFO_FREQUENCY_SOFT: return GEN_VIBLFOFREQ;
        case AL_MOD_ENV_DELAYTIME_SOFT: return GEN_MODENVDELAY;
        case AL_MOD_ENV_ATTACKTIME_SOFT: return GEN_MODENVATTACK;
        case AL_MOD_ENV_HOLDTIME_SOFT: return GEN_MODENVHOLD;
        case AL_MOD_ENV_DECAYTIME_SOFT: return GEN_MODENVDECAY;
        case AL_MOD_ENV_SUSTAINVOLUME_SOFT: return GEN_MODENVSUSTAIN;
        case AL_MOD_ENV_RELEASETIME_SOFT: return GEN_MODENVRELEASE;
        case AL_MOD_ENV_KEY_TO_HOLDTIME_SOFT: return GEN_KEYTOMODENVHOLD;
        case AL_MOD_ENV_KEY_TO_DECAYTIME_SOFT: return GEN_KEYTOMODENVDECAY;
        case AL_VOLUME_ENV_DELAYTIME_SOFT: return GEN_VOLENVDELAY;
        case AL_VOLUME_ENV_ATTACKTIME_SOFT: return GEN_VOLENVATTACK;
        case AL_VOLUME_ENV_HOLDTIME_SOFT: return GEN_VOLENVHOLD;
        case AL_VOLUME_ENV_DECAYTIME_SOFT: return GEN_VOLENVDECAY;
        case AL_VOLUME_ENV_SUSTAINVOLUME_SOFT: return GEN_VOLENVSUSTAIN;
        case AL_VOLUME_ENV_RELEASETIME_SOFT: return GEN_VOLENVRELEASE;
        case AL_VOLUME_ENV_KEY_TO_HOLDTIME_SOFT: return GEN_KEYTOVOLENVHOLD;
        case AL_VOLUME_ENV_KEY_TO_DECAYTIME_SOFT: return GEN_KEYTOVOLENVDECAY;
        case AL_ATTENUATION_SOFT: return GEN_ATTENUATION;
        case AL_TUNING_COARSE_SOFT: return GEN_COARSETUNE;
        case AL_TUNING_FINE_SOFT: return GEN_FINETUNE;
        case AL_TUNING_SCALE_SOFT: return GEN_SCALETUNE;
    }
    ERR("Unhandled generator: 0x%04x\n", gen);
    return 0;
}

static int getSf2LoopMode(ALenum mode)
{
    switch(mode)
    {
        case AL_NONE: return 0;
        case AL_LOOP_CONTINUOUS_SOFT: return 1;
        case AL_LOOP_UNTIL_RELEASE_SOFT: return 3;
    }
    return 0;
}

static int getSampleType(ALenum type)
{
    switch(type)
    {
        case AL_MONO_SOFT: return FLUID_SAMPLETYPE_MONO;
        case AL_RIGHT_SOFT: return FLUID_SAMPLETYPE_RIGHT;
        case AL_LEFT_SOFT: return FLUID_SAMPLETYPE_LEFT;
    }
    return FLUID_SAMPLETYPE_MONO;
}

typedef struct FSample {
    DERIVE_FROM_TYPE(fluid_sample_t);

    ALfontsound *Sound;

    fluid_mod_t *Mods;
    ALsizei NumMods;
} FSample;

static void FSample_Construct(FSample *self, ALfontsound *sound)
{
    fluid_sample_t *sample = STATIC_CAST(fluid_sample_t, self);
    memset(sample->name, 0, sizeof(sample->name));
    sample->start = sound->Start;
    sample->end = sound->End;
    sample->loopstart = sound->LoopStart;
    sample->loopend = sound->LoopEnd;
    sample->samplerate = sound->SampleRate;
    sample->origpitch = sound->PitchKey;
    sample->pitchadj = sound->PitchCorrection;
    sample->sampletype = getSampleType(sound->SampleType);
    sample->valid = !!sound->Buffer;
    sample->data = sound->Buffer ? sound->Buffer->data : NULL;

    sample->amplitude_that_reaches_noise_floor_is_valid = 0;
    sample->amplitude_that_reaches_noise_floor = 0.0;

    sample->refcount = 0;

    sample->notify = NULL;

    sample->userdata = self;

    self->Sound = sound;

    self->NumMods = 0;
    self->Mods = calloc(sound->ModulatorMap.size*4, sizeof(fluid_mod_t[4]));
    if(self->Mods)
    {
        ALsizei i, j, k;

        for(i = j = 0;i < sound->ModulatorMap.size;i++)
        {
            ALsfmodulator *mod = sound->ModulatorMap.array[i].value;
            for(k = 0;k < 4;k++,mod++)
            {
                if(mod->Dest == AL_NONE)
                    continue;
                fluid_mod_set_source1(&self->Mods[j], getModInput(mod->Source[0].Input),
                                      getModFlags(mod->Source[0].Input, mod->Source[0].Type,
                                                  mod->Source[0].Form));
                fluid_mod_set_source2(&self->Mods[j], getModInput(mod->Source[1].Input),
                                      getModFlags(mod->Source[1].Input, mod->Source[1].Type,
                                                  mod->Source[1].Form));
                fluid_mod_set_amount(&self->Mods[j], mod->Amount);
                fluid_mod_set_dest(&self->Mods[j], getModDest(mod->Dest));
                self->Mods[j++].next = NULL;
            }
        }
        self->NumMods = j;
    }
}

static void FSample_Destruct(FSample *self)
{
    free(self->Mods);
    self->Mods = NULL;
    self->NumMods = 0;
}


typedef struct FPreset {
    DERIVE_FROM_TYPE(fluid_preset_t);

    char Name[16];

    int Preset;
    int Bank;

    FSample *Samples;
    ALsizei NumSamples;
} FPreset;

static char* FPreset_getName(fluid_preset_t *preset);
static int FPreset_getPreset(fluid_preset_t *preset);
static int FPreset_getBank(fluid_preset_t *preset);
static int FPreset_noteOn(fluid_preset_t *preset, fluid_synth_t *synth, int channel, int key, int velocity);

static void FPreset_Construct(FPreset *self, ALsfpreset *preset, fluid_sfont_t *parent)
{
    STATIC_CAST(fluid_preset_t, self)->data = self;
    STATIC_CAST(fluid_preset_t, self)->sfont = parent;
    STATIC_CAST(fluid_preset_t, self)->free = NULL;
    STATIC_CAST(fluid_preset_t, self)->get_name = FPreset_getName;
    STATIC_CAST(fluid_preset_t, self)->get_banknum = FPreset_getBank;
    STATIC_CAST(fluid_preset_t, self)->get_num = FPreset_getPreset;
    STATIC_CAST(fluid_preset_t, self)->noteon = FPreset_noteOn;
    STATIC_CAST(fluid_preset_t, self)->notify = NULL;

    memset(self->Name, 0, sizeof(self->Name));
    self->Preset = preset->Preset;
    self->Bank = preset->Bank;

    self->NumSamples = 0;
    self->Samples = calloc(1, preset->NumSounds * sizeof(self->Samples[0]));
    if(self->Samples)
    {
        ALsizei i;
        self->NumSamples = preset->NumSounds;
        for(i = 0;i < self->NumSamples;i++)
            FSample_Construct(&self->Samples[i], preset->Sounds[i]);
    }
}

static void FPreset_Destruct(FPreset *self)
{
    ALsizei i;

    for(i = 0;i < self->NumSamples;i++)
        FSample_Destruct(&self->Samples[i]);
    free(self->Samples);
    self->Samples = NULL;
    self->NumSamples = 0;
}

static ALboolean FPreset_canDelete(FPreset *self)
{
    ALsizei i;
    for(i = 0;i < self->NumSamples;i++)
    {
        if(fluid_sample_refcount(STATIC_CAST(fluid_sample_t, &self->Samples[i])) != 0)
            return AL_FALSE;
    }
    return AL_TRUE;
}

static char* FPreset_getName(fluid_preset_t *preset)
{
    return ((FPreset*)preset->data)->Name;
}

static int FPreset_getPreset(fluid_preset_t *preset)
{
    return ((FPreset*)preset->data)->Preset;
}

static int FPreset_getBank(fluid_preset_t *preset)
{
    return ((FPreset*)preset->data)->Bank;
}

static int FPreset_noteOn(fluid_preset_t *preset, fluid_synth_t *synth, int channel, int key, int vel)
{
    FPreset *self = ((FPreset*)preset->data);
    ALsizei i;

    for(i = 0;i < self->NumSamples;i++)
    {
        FSample *sample = &self->Samples[i];
        ALfontsound *sound = sample->Sound;
        fluid_voice_t *voice;
        ALsizei m;

        if(!(key >= sound->MinKey && key <= sound->MaxKey && vel >= sound->MinVelocity && vel <= sound->MaxVelocity))
            continue;

        voice = fluid_synth_alloc_voice(synth, STATIC_CAST(fluid_sample_t, sample), channel, key, vel);
        if(voice == NULL) return FLUID_FAILED;

        fluid_voice_gen_set(voice, GEN_MODLFOTOPITCH, sound->ModLfoToPitch);
        fluid_voice_gen_set(voice, GEN_VIBLFOTOPITCH, sound->VibratoLfoToPitch);
        fluid_voice_gen_set(voice, GEN_MODENVTOPITCH, sound->ModEnvToPitch);
        fluid_voice_gen_set(voice, GEN_FILTERFC, sound->FilterCutoff);
        fluid_voice_gen_set(voice, GEN_FILTERQ, sound->FilterQ);
        fluid_voice_gen_set(voice, GEN_MODLFOTOFILTERFC, sound->ModLfoToFilterCutoff);
        fluid_voice_gen_set(voice, GEN_MODENVTOFILTERFC, sound->ModEnvToFilterCutoff);
        fluid_voice_gen_set(voice, GEN_MODLFOTOVOL, sound->ModLfoToVolume);
        fluid_voice_gen_set(voice, GEN_CHORUSSEND, sound->ChorusSend);
        fluid_voice_gen_set(voice, GEN_REVERBSEND, sound->ReverbSend);
        fluid_voice_gen_set(voice, GEN_PAN, sound->Pan);
        fluid_voice_gen_set(voice, GEN_MODLFODELAY, sound->ModLfo.Delay);
        fluid_voice_gen_set(voice, GEN_MODLFOFREQ, sound->ModLfo.Frequency);
        fluid_voice_gen_set(voice, GEN_VIBLFODELAY, sound->VibratoLfo.Delay);
        fluid_voice_gen_set(voice, GEN_VIBLFOFREQ, sound->VibratoLfo.Frequency);
        fluid_voice_gen_set(voice, GEN_MODENVDELAY, sound->ModEnv.DelayTime);
        fluid_voice_gen_set(voice, GEN_MODENVATTACK, sound->ModEnv.AttackTime);
        fluid_voice_gen_set(voice, GEN_MODENVHOLD, sound->ModEnv.HoldTime);
        fluid_voice_gen_set(voice, GEN_MODENVDECAY, sound->ModEnv.DecayTime);
        fluid_voice_gen_set(voice, GEN_MODENVSUSTAIN, sound->ModEnv.SustainAttn);
        fluid_voice_gen_set(voice, GEN_MODENVRELEASE, sound->ModEnv.ReleaseTime);
        fluid_voice_gen_set(voice, GEN_KEYTOMODENVHOLD, sound->ModEnv.KeyToHoldTime);
        fluid_voice_gen_set(voice, GEN_KEYTOMODENVDECAY, sound->ModEnv.KeyToDecayTime);
        fluid_voice_gen_set(voice, GEN_VOLENVDELAY, sound->VolEnv.DelayTime);
        fluid_voice_gen_set(voice, GEN_VOLENVATTACK, sound->VolEnv.AttackTime);
        fluid_voice_gen_set(voice, GEN_VOLENVHOLD, sound->VolEnv.HoldTime);
        fluid_voice_gen_set(voice, GEN_VOLENVDECAY, sound->VolEnv.DecayTime);
        fluid_voice_gen_set(voice, GEN_VOLENVSUSTAIN, sound->VolEnv.SustainAttn);
        fluid_voice_gen_set(voice, GEN_VOLENVRELEASE, sound->VolEnv.ReleaseTime);
        fluid_voice_gen_set(voice, GEN_KEYTOVOLENVHOLD, sound->VolEnv.KeyToHoldTime);
        fluid_voice_gen_set(voice, GEN_KEYTOVOLENVDECAY, sound->VolEnv.KeyToDecayTime);
        fluid_voice_gen_set(voice, GEN_ATTENUATION, sound->Attenuation);
        fluid_voice_gen_set(voice, GEN_COARSETUNE, sound->CoarseTuning);
        fluid_voice_gen_set(voice, GEN_FINETUNE, sound->FineTuning);
        fluid_voice_gen_set(voice, GEN_SAMPLEMODE, getSf2LoopMode(sound->LoopMode));
        fluid_voice_gen_set(voice, GEN_SCALETUNE, sound->TuningScale);
        fluid_voice_gen_set(voice, GEN_EXCLUSIVECLASS, sound->ExclusiveClass);
        for(m = 0;m < sample->NumMods;m++)
            fluid_voice_add_mod(voice, &sample->Mods[m], FLUID_VOICE_OVERWRITE);

        fluid_synth_start_voice(synth, voice);
    }

    return FLUID_OK;
}


typedef struct FSfont {
    DERIVE_FROM_TYPE(fluid_sfont_t);

    char Name[16];

    FPreset *Presets;
    ALsizei NumPresets;

    ALsizei CurrentPos;
} FSfont;

static int FSfont_free(fluid_sfont_t *sfont);
static char* FSfont_getName(fluid_sfont_t *sfont);
static fluid_preset_t* FSfont_getPreset(fluid_sfont_t *sfont, unsigned int bank, unsigned int prenum);
static void FSfont_iterStart(fluid_sfont_t *sfont);
static int FSfont_iterNext(fluid_sfont_t *sfont, fluid_preset_t *preset);

static void FSfont_Construct(FSfont *self, ALsoundfont *sfont)
{
    STATIC_CAST(fluid_sfont_t, self)->data = self;
    STATIC_CAST(fluid_sfont_t, self)->id = FLUID_FAILED;
    STATIC_CAST(fluid_sfont_t, self)->free = FSfont_free;
    STATIC_CAST(fluid_sfont_t, self)->get_name = FSfont_getName;
    STATIC_CAST(fluid_sfont_t, self)->get_preset = FSfont_getPreset;
    STATIC_CAST(fluid_sfont_t, self)->iteration_start = FSfont_iterStart;
    STATIC_CAST(fluid_sfont_t, self)->iteration_next = FSfont_iterNext;

    memset(self->Name, 0, sizeof(self->Name));
    self->CurrentPos = 0;
    self->NumPresets = 0;
    self->Presets = calloc(1, sfont->NumPresets * sizeof(self->Presets[0]));
    if(self->Presets)
    {
        ALsizei i;
        self->NumPresets = sfont->NumPresets;
        for(i = 0;i < self->NumPresets;i++)
            FPreset_Construct(&self->Presets[i], sfont->Presets[i], STATIC_CAST(fluid_sfont_t, self));
    }
}

static void FSfont_Destruct(FSfont *self)
{
    ALsizei i;

    for(i = 0;i < self->NumPresets;i++)
        FPreset_Destruct(&self->Presets[i]);
    free(self->Presets);
    self->Presets = NULL;
    self->NumPresets = 0;
    self->CurrentPos = 0;
}

static int FSfont_free(fluid_sfont_t *sfont)
{
    FSfont *self = STATIC_UPCAST(FSfont, fluid_sfont_t, sfont);
    ALsizei i;

    for(i = 0;i < self->NumPresets;i++)
    {
        if(!FPreset_canDelete(&self->Presets[i]))
            return 1;
    }

    FSfont_Destruct(self);
    free(self);
    return 0;
}

static char* FSfont_getName(fluid_sfont_t *sfont)
{
    return STATIC_UPCAST(FSfont, fluid_sfont_t, sfont)->Name;
}

static fluid_preset_t *FSfont_getPreset(fluid_sfont_t *sfont, unsigned int bank, unsigned int prenum)
{
    FSfont *self = STATIC_UPCAST(FSfont, fluid_sfont_t, sfont);
    ALsizei i;

    for(i = 0;i < self->NumPresets;i++)
    {
        FPreset *preset = &self->Presets[i];
        if(preset->Bank == (int)bank && preset->Preset == (int)prenum)
            return STATIC_CAST(fluid_preset_t, preset);
    }

    return NULL;
}

static void FSfont_iterStart(fluid_sfont_t *sfont)
{
    STATIC_UPCAST(FSfont, fluid_sfont_t, sfont)->CurrentPos = 0;
}

static int FSfont_iterNext(fluid_sfont_t *sfont, fluid_preset_t *preset)
{
    FSfont *self = STATIC_UPCAST(FSfont, fluid_sfont_t, sfont);
    if(self->CurrentPos >= self->NumPresets)
        return 0;
    *preset = *STATIC_CAST(fluid_preset_t, &self->Presets[self->CurrentPos++]);
    preset->free = NULL;
    return 1;
}


typedef struct FSynth {
    DERIVE_FROM_TYPE(MidiSynth);
    DERIVE_FROM_TYPE(fluid_sfloader_t);

    fluid_settings_t *Settings;
    fluid_synth_t *Synth;
    int *FontIDs;
    ALsizei NumFontIDs;

    ALboolean ForceGM2BankSelect;
    ALfloat GainScale;
} FSynth;

static void FSynth_Construct(FSynth *self, ALCdevice *device);
static void FSynth_Destruct(FSynth *self);
static ALboolean FSynth_init(FSynth *self, ALCdevice *device);
static ALenum FSynth_selectSoundfonts(FSynth *self, ALCcontext *context, ALsizei count, const ALuint *ids);
static void FSynth_setGain(FSynth *self, ALfloat gain);
static void FSynth_stop(FSynth *self);
static void FSynth_reset(FSynth *self);
static void FSynth_update(FSynth *self, ALCdevice *device);
static void FSynth_processQueue(FSynth *self, ALuint64 time);
static void FSynth_process(FSynth *self, ALuint SamplesToDo, ALfloat (*restrict DryBuffer)[BUFFERSIZE]);
DECLARE_DEFAULT_ALLOCATORS(FSynth)
DEFINE_MIDISYNTH_VTABLE(FSynth);

static fluid_sfont_t *FSynth_loadSfont(fluid_sfloader_t *loader, const char *filename);


static void FSynth_Construct(FSynth *self, ALCdevice *device)
{
    MidiSynth_Construct(STATIC_CAST(MidiSynth, self), device);
    SET_VTABLE2(FSynth, MidiSynth, self);

    STATIC_CAST(fluid_sfloader_t, self)->data = self;
    STATIC_CAST(fluid_sfloader_t, self)->free = NULL;
    STATIC_CAST(fluid_sfloader_t, self)->load = FSynth_loadSfont;

    self->Settings = NULL;
    self->Synth = NULL;
    self->FontIDs = NULL;
    self->NumFontIDs = 0;
    self->ForceGM2BankSelect = AL_FALSE;
    self->GainScale = 0.2f;
}

static void FSynth_Destruct(FSynth *self)
{
    ALsizei i;

    for(i = 0;i < self->NumFontIDs;i++)
        fluid_synth_sfunload(self->Synth, self->FontIDs[i], 0);
    free(self->FontIDs);
    self->FontIDs = NULL;
    self->NumFontIDs = 0;

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
    ALfloat vol;

    if(ConfigValueFloat("midi", "volume", &vol))
    {
        if(!(vol <= 0.0f))
        {
            ERR("MIDI volume %f clamped to 0\n", vol);
            vol = 0.0f;
        }
        self->GainScale = powf(10.0f, vol / 20.0f);
    }

    self->Settings = new_fluid_settings();
    if(!self->Settings)
    {
        ERR("Failed to create FluidSettings\n");
        return AL_FALSE;
    }

    fluid_settings_setint(self->Settings, "synth.polyphony", 256);
    fluid_settings_setnum(self->Settings, "synth.gain", self->GainScale);
    fluid_settings_setnum(self->Settings, "synth.sample-rate", device->Frequency);

    self->Synth = new_fluid_synth(self->Settings);
    if(!self->Synth)
    {
        ERR("Failed to create FluidSynth\n");
        return AL_FALSE;
    }

    fluid_synth_add_sfloader(self->Synth, STATIC_CAST(fluid_sfloader_t, self));

    return AL_TRUE;
}


static fluid_sfont_t *FSynth_loadSfont(fluid_sfloader_t *loader, const char *filename)
{
    FSynth *self = STATIC_UPCAST(FSynth, fluid_sfloader_t, loader);
    FSfont *sfont;
    int idx;

    if(!filename || sscanf(filename, "_al_internal %d", &idx) != 1)
        return NULL;
    if(idx < 0 || idx >= STATIC_CAST(MidiSynth, self)->NumSoundfonts)
    {
        ERR("Received invalid soundfont index %d (max: %d)\n", idx, STATIC_CAST(MidiSynth, self)->NumSoundfonts);
        return NULL;
    }

    sfont = calloc(1, sizeof(sfont[0]));
    if(!sfont) return NULL;

    FSfont_Construct(sfont, STATIC_CAST(MidiSynth, self)->Soundfonts[idx]);
    return STATIC_CAST(fluid_sfont_t, sfont);
}

static ALenum FSynth_selectSoundfonts(FSynth *self, ALCcontext *context, ALsizei count, const ALuint *ids)
{
    int *fontid;
    ALenum ret;
    ALsizei i;

    ret = MidiSynth_selectSoundfonts(STATIC_CAST(MidiSynth, self), context, count, ids);
    if(ret != AL_NO_ERROR) return ret;

    ALCdevice_Lock(context->Device);
    for(i = 0;i < 16;i++)
        fluid_synth_all_sounds_off(self->Synth, i);
    ALCdevice_Unlock(context->Device);

    fontid = malloc(count * sizeof(fontid[0]));
    if(fontid)
    {
        for(i = 0;i < STATIC_CAST(MidiSynth, self)->NumSoundfonts;i++)
        {
            char name[16];
            snprintf(name, sizeof(name), "_al_internal %d", i);

            fontid[i] = fluid_synth_sfload(self->Synth, name, 0);
            if(fontid[i] == FLUID_FAILED)
                ERR("Failed to load selected soundfont %d\n", i);
        }

        fontid = ExchangePtr((XchgPtr*)&self->FontIDs, fontid);
        count = ExchangeInt(&self->NumFontIDs, count);
    }
    else
    {
        ERR("Failed to allocate space for %d font IDs!\n", count);
        fontid = ExchangePtr((XchgPtr*)&self->FontIDs, NULL);
        count = ExchangeInt(&self->NumFontIDs, 0);
    }

    for(i = 0;i < count;i++)
        fluid_synth_sfunload(self->Synth, fontid[i], 0);
    free(fontid);

    return ret;
}


static void FSynth_setGain(FSynth *self, ALfloat gain)
{
    fluid_settings_setnum(self->Settings, "synth.gain", self->GainScale * gain);
    fluid_synth_set_gain(self->Synth, self->GainScale * gain);
    MidiSynth_setGain(STATIC_CAST(MidiSynth, self), gain);
}


static void FSynth_stop(FSynth *self)
{
    MidiSynth *synth = STATIC_CAST(MidiSynth, self);
    ALuint64 curtime;
    ALsizei chan;

    /* Make sure all pending events are processed. */
    curtime = MidiSynth_getTime(synth);
    FSynth_processQueue(self, curtime);

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
            case AL_KEYPRESSURE_SOFT:
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
    ALuint64 curtime;
    ALuint total = 0;

    if(state == AL_INITIAL)
        return;
    if(state != AL_PLAYING)
    {
        fluid_synth_write_float(self->Synth, SamplesToDo, DryBuffer[FrontLeft], 0, 1,
                                                          DryBuffer[FrontRight], 0, 1);
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
        }

        if(tonext > 0)
        {
            ALuint todo = minu(tonext, SamplesToDo-total);
            fluid_synth_write_float(self->Synth, todo, DryBuffer[FrontLeft], total, 1,
                                                       DryBuffer[FrontRight], total, 1);
            total += todo;
            tonext -= todo;
        }
        if(total < SamplesToDo && tonext <= 0)
            FSynth_processQueue(self, time);
    }

    synth->SamplesDone += SamplesToDo;
    synth->ClockBase += (synth->SamplesDone/synth->SampleRate) * MIDI_CLOCK_RES;
    synth->SamplesDone %= synth->SampleRate;
}


MidiSynth *FSynth_create(ALCdevice *device)
{
    FSynth *synth;

    if(!LoadFSynth())
        return NULL;

    synth = FSynth_New(sizeof(*synth));
    if(!synth)
    {
        ERR("Failed to allocate FSynth\n");
        return NULL;
    }
    memset(synth, 0, sizeof(*synth));
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
