
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "alMain.h"
#include "alMidi.h"
#include "alThunk.h"
#include "alError.h"
#include <alBuffer.h>

#include "midi/base.h"


extern inline struct ALsoundfont *LookupSfont(ALCdevice *device, ALuint id);
extern inline struct ALsoundfont *RemoveSfont(ALCdevice *device, ALuint id);

static void ALsoundfont_Construct(ALsoundfont *self);
static void ALsoundfont_Destruct(ALsoundfont *self);
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
        if(ReadRef(&sfont->ref) != 0)
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
    if(ReadRef(&sfont->ref) != 0)
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
                free(presets);
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
    if(ReadRef(&sfont->ref) != 0)
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


static void ALsoundfont_Construct(ALsoundfont *self)
{
    InitRef(&self->ref, 0);

    self->Presets = NULL;
    self->NumPresets = 0;

    RWLockInit(&self->Lock);

    self->id = 0;
}

static void ALsoundfont_Destruct(ALsoundfont *self)
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
}

ALsoundfont *ALsoundfont_getDefSoundfont(ALCcontext *context)
{
    ALCdevice *device = context->Device;
    al_string fname = AL_STRING_INIT_STATIC();
    const char *namelist;

    if(device->DefaultSfont)
        return device->DefaultSfont;

    device->DefaultSfont = calloc(1, sizeof(device->DefaultSfont[0]));
    ALsoundfont_Construct(device->DefaultSfont);

    namelist = getenv("ALSOFT_SOUNDFONT");
    if(!namelist || !namelist[0])
        ConfigValueStr("midi", "soundfont", &namelist);
    while(namelist && namelist[0])
    {
        const char *next, *end;
        FILE *f;

        while(*namelist && (isspace(*namelist) || *namelist == ','))
            namelist++;
        if(!*namelist)
            break;
        next = strchr(namelist, ',');
        end = next ? next++ : (namelist+strlen(namelist));
        while(--end != namelist && isspace(*end)) {
        }
        if(end == namelist)
            continue;
        al_string_append_range(&fname, namelist, end+1);
        namelist = next;

        f = OpenDataFile(al_string_get_cstr(fname), "openal/soundfonts");
        if(f == NULL)
            ERR("Failed to open %s\n", al_string_get_cstr(fname));
        else
        {
            Reader reader;
            reader.cb = ALsoundfont_read;
            reader.ptr = f;
            reader.error = 0;
            TRACE("Loading %s\n", al_string_get_cstr(fname));
            loadSf2(&reader, device->DefaultSfont, context);
            fclose(f);
        }

        al_string_clear(&fname);
    }
    AL_STRING_DEINIT(fname);

    return device->DefaultSfont;
}

void ALsoundfont_deleteSoundfont(ALsoundfont *self, ALCdevice *device)
{
    ALsfpreset **presets;
    ALsizei num_presets;
    VECTOR(ALbuffer*) buffers;
    ALsizei i;

    VECTOR_INIT(buffers);
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

        DeletePreset(device, preset);
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
                if(sounds[j] && ReadRef(&sounds[j]->ref) == 0)
                {
                    deleting = AL_TRUE;
                    if(sounds[j]->Buffer)
                    {
                        ALbuffer *buffer = sounds[j]->Buffer;
                        ALbuffer **iter;

#define MATCH_BUFFER(_i) (buffer == *(_i))
                        VECTOR_FIND_IF(iter, ALbuffer*, buffers, MATCH_BUFFER);
                        if(iter == VECTOR_ITER_END(buffers))
                            VECTOR_PUSH_BACK(buffers, buffer);
#undef MATCH_BUFFER
                    }
                    DeleteFontsound(device, sounds[j]);
                    sounds[j] = NULL;
                }
            }
        } while(deleting);
        free(sounds);
    }

    ALsoundfont_Destruct(self);
    free(self);

#define DELETE_BUFFER(iter) do {           \
    assert(ReadRef(&(*(iter))->ref) == 0); \
    DeleteBuffer(device, *(iter));         \
} while(0)
    VECTOR_FOR_EACH(ALbuffer*, buffers, DELETE_BUFFER);
    VECTOR_DEINIT(buffers);
#undef DELETE_BUFFER
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
