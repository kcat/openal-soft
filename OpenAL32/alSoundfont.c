
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "alMidi.h"
#include "alThunk.h"
#include "alError.h"

#include "midi/base.h"


extern inline struct ALsoundfont *LookupSfont(ALCdevice *device, ALuint id);
extern inline struct ALsoundfont *RemoveSfont(ALCdevice *device, ALuint id);

void ALsoundfont_Construct(ALsoundfont *self);
void ALsoundfont_Destruct(ALsoundfont *self);
void ALsoundfont_deleteSoundfont(ALsoundfont *self, ALCdevice *device);
ALsoundfont *ALsoundfont_getDefSoundfont(ALCcontext *context);
static size_t ALsoundfont_read(ALvoid *buf, size_t bytes, ALvoid *ptr);


AL_API void AL_APIENTRY alGenSoundfontsSOFT(ALsizei n, ALuint *ids)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsizei cur = 0;
    ALenum err;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    for(cur = 0;cur < n;cur++)
    {
        ALsoundfont *sfont = calloc(1, sizeof(ALsoundfont));
        if(!sfont)
        {
            alDeleteSoundfontsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, done);
        }
        ALsoundfont_Construct(sfont);

        err = NewThunkEntry(&sfont->id);
        if(err == AL_NO_ERROR)
            err = InsertUIntMapEntry(&device->SfontMap, sfont->id, sfont);
        if(err != AL_NO_ERROR)
        {
            ALsoundfont_Destruct(sfont);
            memset(sfont, 0, sizeof(ALsoundfont));
            free(sfont);

            alDeleteSoundfontsSOFT(cur, ids);
            SET_ERROR_AND_GOTO(context, err, done);
        }

        ids[cur] = sfont->id;
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDeleteSoundfontsSOFT(ALsizei n, const ALuint *ids)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsoundfont *sfont;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    device = context->Device;
    for(i = 0;i < n;i++)
    {
        /* Check for valid soundfont ID */
        if(ids[i] == 0)
        {
            if(!(sfont=device->DefaultSfont))
                continue;
        }
        else if((sfont=LookupSfont(device, ids[i])) == NULL)
            SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
        if(sfont->Mapped != AL_FALSE || sfont->ref != 0)
            SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    for(i = 0;i < n;i++)
    {
        if(ids[i] == 0)
        {
            MidiSynth *synth = device->Synth;
            WriteLock(&synth->Lock);
            if(device->DefaultSfont != NULL)
                ALsoundfont_deleteSoundfont(device->DefaultSfont, device);
            device->DefaultSfont = NULL;
            WriteUnlock(&synth->Lock);
            continue;
        }
        else if((sfont=RemoveSfont(device, ids[i])) == NULL)
            continue;

        ALsoundfont_Destruct(sfont);

        memset(sfont, 0, sizeof(*sfont));
        free(sfont);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsSoundfontSOFT(ALuint id)
{
    ALCcontext *context;
    ALboolean ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    ret = ((!id || LookupSfont(context->Device, id)) ?
           AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(context);

    return ret;
}

AL_API ALvoid AL_APIENTRY alSoundfontSamplesSOFT(ALuint id, ALenum type, ALsizei count, const ALvoid *samples)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsoundfont *sfont;
    void *ptr;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(id == 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    if(!(sfont=LookupSfont(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(type != AL_SHORT_SOFT)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(count <= 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    WriteLock(&sfont->Lock);
    if(sfont->ref != 0)
        alSetError(context, AL_INVALID_OPERATION);
    else if(sfont->Mapped)
        alSetError(context, AL_INVALID_OPERATION);
    else if(!(ptr=realloc(sfont->Samples, count * sizeof(ALshort))))
        alSetError(context, AL_OUT_OF_MEMORY);
    else
    {
        sfont->Samples = ptr;
        sfont->NumSamples = count;
        if(samples != NULL)
            memcpy(sfont->Samples, samples, count * sizeof(ALshort));
    }
    WriteUnlock(&sfont->Lock);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alGetSoundfontSamplesSOFT(ALuint id, ALsizei offset, ALsizei count, ALenum type, ALvoid *samples)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsoundfont *sfont;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(id == 0)
        sfont = ALsoundfont_getDefSoundfont(context);
    else if(!(sfont=LookupSfont(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(type != AL_SHORT_SOFT)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(offset < 0 || count <= 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    ReadLock(&sfont->Lock);
    if(offset >= sfont->NumSamples || count > (sfont->NumSamples-offset))
        alSetError(context, AL_INVALID_VALUE);
    else if(sfont->Mapped)
        alSetError(context, AL_INVALID_OPERATION);
    else
    {
        /* TODO: Allow conversion. */
        memcpy(samples, sfont->Samples + offset*sizeof(ALshort), count * sizeof(ALshort));
    }
    ReadUnlock(&sfont->Lock);

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid* AL_APIENTRY alSoundfontMapSamplesSOFT(ALuint id, ALsizei offset, ALsizei length)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsoundfont *sfont;
    ALvoid *ptr = NULL;

    context = GetContextRef();
    if(!context) return NULL;

    device = context->Device;
    if(id == 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    if(!(sfont=LookupSfont(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(offset < 0 || (ALuint)offset > sfont->NumSamples*sizeof(ALshort))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    if(length <= 0 || (ALuint)length > (sfont->NumSamples*sizeof(ALshort) - offset))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    ReadLock(&sfont->Lock);
    if(sfont->ref != 0)
        alSetError(context, AL_INVALID_OPERATION);
    else if(ExchangeInt(&sfont->Mapped, AL_TRUE) == AL_TRUE)
        alSetError(context, AL_INVALID_OPERATION);
    else
        ptr = (ALbyte*)sfont->Samples + offset;
    ReadUnlock(&sfont->Lock);

done:
    ALCcontext_DecRef(context);

    return ptr;
}

AL_API ALvoid AL_APIENTRY alSoundfontUnmapSamplesSOFT(ALuint id)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsoundfont *sfont;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(id == 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    if(!(sfont=LookupSfont(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(ExchangeInt(&sfont->Mapped, AL_FALSE) == AL_FALSE)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alGetSoundfontivSOFT(ALuint id, ALenum param, ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsoundfont *sfont;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(id == 0)
        sfont = ALsoundfont_getDefSoundfont(context);
    else if(!(sfont=LookupSfont(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    switch(param)
    {
        case AL_PRESETS_SIZE_SOFT:
            values[0] = sfont->NumPresets;
            break;

        case AL_PRESETS_SOFT:
            for(i = 0;i < sfont->NumPresets;i++)
                values[i] = sfont->Presets[i]->id;
            break;

        case AL_SAMPLE_LENGTH_SOFT:
            values[0] = sfont->NumSamples;
            break;

        case AL_FORMAT_TYPE_SOFT:
            values[0] = AL_SHORT_SOFT;
            break;

        default:
            SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alSoundfontPresetsSOFT(ALuint id, ALsizei count, const ALuint *pids)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsoundfont *sfont;
    ALsfpreset **presets;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(id == 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    if(!(sfont=LookupSfont(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);
    if(count < 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    WriteLock(&sfont->Lock);
    if(sfont->ref != 0)
    {
        WriteUnlock(&sfont->Lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    if(count == 0)
        presets = NULL;
    else
    {
        presets = calloc(count, sizeof(presets[0]));
        if(!presets)
        {
            WriteUnlock(&sfont->Lock);
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, done);
        }

        for(i = 0;i < count;i++)
        {
            if(!(presets[i]=LookupPreset(device, pids[i])))
            {
                WriteUnlock(&sfont->Lock);
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            }
        }
    }

    for(i = 0;i < count;i++)
        IncrementRef(&presets[i]->ref);

    presets = ExchangePtr((XchgPtr*)&sfont->Presets, presets);
    count = ExchangeInt(&sfont->NumPresets, count);
    WriteUnlock(&sfont->Lock);

    for(i = 0;i < count;i++)
        DecrementRef(&presets[i]->ref);
    free(presets);

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alLoadSoundfontSOFT(ALuint id, size_t(*cb)(ALvoid*,size_t,ALvoid*), ALvoid *user)
{
    ALCdevice *device;
    ALCcontext *context;
    ALsoundfont *sfont;
    Reader reader;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(id == 0)
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    if(!(sfont=LookupSfont(device, id)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_NAME, done);

    WriteLock(&sfont->Lock);
    if(sfont->ref != 0)
    {
        WriteUnlock(&sfont->Lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }
    if(sfont->Mapped)
    {
        WriteUnlock(&sfont->Lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }
    if(sfont->NumPresets > 0)
    {
        WriteUnlock(&sfont->Lock);
        SET_ERROR_AND_GOTO(context, AL_INVALID_OPERATION, done);
    }

    reader.cb = cb;
    reader.ptr = user;
    reader.error = 0;
    loadSf2(&reader, sfont, context);
    WriteUnlock(&sfont->Lock);

done:
    ALCcontext_DecRef(context);
}


void ALsoundfont_Construct(ALsoundfont *self)
{
    self->ref = 0;

    self->Presets = NULL;
    self->NumPresets = 0;

    self->Samples = NULL;
    self->NumSamples = 0;

    RWLockInit(&self->Lock);
    self->Mapped = AL_FALSE;

    self->id = 0;
}

void ALsoundfont_Destruct(ALsoundfont *self)
{
    ALsizei i;

    FreeThunkEntry(self->id);
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

ALsoundfont *ALsoundfont_getDefSoundfont(ALCcontext *context)
{
    ALCdevice *device = context->Device;
    const char *fname;

    if(device->DefaultSfont)
        return device->DefaultSfont;

    device->DefaultSfont = calloc(1, sizeof(device->DefaultSfont[0]));
    ALsoundfont_Construct(device->DefaultSfont);

    fname = getenv("ALSOFT_SOUNDFONT");
    if((fname && fname[0]) || ConfigValueStr("midi", "soundfont", &fname))
    {
        FILE *f;

        f = fopen(fname, "rb");
        if(f == NULL)
            ERR("Failed to open %s\n", fname);
        else
        {
            Reader reader;
            reader.cb = ALsoundfont_read;
            reader.ptr = f;
            reader.error = 0;
            TRACE("Loading %s\n", fname);
            loadSf2(&reader, device->DefaultSfont, context);
            fclose(f);
        }
    }

    return device->DefaultSfont;
}

void ALsoundfont_deleteSoundfont(ALsoundfont *self, ALCdevice *device)
{
    ALsfpreset **presets;
    ALsizei num_presets;
    ALsizei i;

    presets = ExchangePtr((XchgPtr*)&self->Presets, NULL);
    num_presets = ExchangeInt(&self->NumPresets, 0);

    for(i = 0;i < num_presets;i++)
    {
        ALsfpreset *preset = presets[i];
        ALfontsound **sounds;
        ALsizei num_sounds;
        ALboolean deleting;
        ALsizei j;

        sounds = ExchangePtr((XchgPtr*)&preset->Sounds, NULL);
        num_sounds = ExchangeInt(&preset->NumSounds, 0);
        DeletePreset(preset, device);
        preset = NULL;

        for(j = 0;j < num_sounds;j++)
            DecrementRef(&sounds[j]->ref);
        /* Some fontsounds may not be immediately deletable because they're
         * linked to another fontsound. When those fontsounds are deleted
         * they should become deletable, so use a loop until all fontsounds
         * are deleted. */
        do {
            deleting = AL_FALSE;
            for(j = 0;j < num_sounds;j++)
            {
                if(sounds[j] && sounds[j]->ref == 0)
                {
                    deleting = AL_TRUE;
                    RemoveFontsound(device, sounds[j]->id);
                    ALfontsound_Destruct(sounds[j]);
                    free(sounds[j]);
                    sounds[j] = NULL;
                }
            }
        } while(deleting);
        free(sounds);
    }

    ALsoundfont_Destruct(self);
    free(self);
}


static size_t ALsoundfont_read(ALvoid *buf, size_t bytes, ALvoid *ptr)
{
    return fread(buf, 1, bytes, (FILE*)ptr);
}


/* ReleaseALSoundfonts
 *
 * Called to destroy any soundfonts that still exist on the device
 */
void ReleaseALSoundfonts(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->SfontMap.size;i++)
    {
        ALsoundfont *temp = device->SfontMap.array[i].value;
        device->SfontMap.array[i].value = NULL;

        ALsoundfont_Destruct(temp);

        memset(temp, 0, sizeof(*temp));
        free(temp);
    }
}
