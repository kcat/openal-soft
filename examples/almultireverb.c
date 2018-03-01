/*
 * OpenAL Multi-Zone Reverb Example
 *
 * Copyright (c) 2018 by Chris Robinson <chris.kcat@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* This file contains an example for controlling multiple reverb zones to
 * smoothly transition between reverb environments. The general concept is to
 * extend single-reverb by also tracking the closest adjacent environment, and
 * utilize EAX Reverb's panning vectors to position them relative to the
 * listener.
 */

#include <stdio.h>
#include <assert.h>

#include <SDL_sound.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx-presets.h"

#include "common/alhelpers.h"


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


/* Filter object functions */
static LPALGENFILTERS alGenFilters;
static LPALDELETEFILTERS alDeleteFilters;
static LPALISFILTER alIsFilter;
static LPALFILTERI alFilteri;
static LPALFILTERIV alFilteriv;
static LPALFILTERF alFilterf;
static LPALFILTERFV alFilterfv;
static LPALGETFILTERI alGetFilteri;
static LPALGETFILTERIV alGetFilteriv;
static LPALGETFILTERF alGetFilterf;
static LPALGETFILTERFV alGetFilterfv;

/* Effect object functions */
static LPALGENEFFECTS alGenEffects;
static LPALDELETEEFFECTS alDeleteEffects;
static LPALISEFFECT alIsEffect;
static LPALEFFECTI alEffecti;
static LPALEFFECTIV alEffectiv;
static LPALEFFECTF alEffectf;
static LPALEFFECTFV alEffectfv;
static LPALGETEFFECTI alGetEffecti;
static LPALGETEFFECTIV alGetEffectiv;
static LPALGETEFFECTF alGetEffectf;
static LPALGETEFFECTFV alGetEffectfv;

/* Auxiliary Effect Slot object functions */
static LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
static LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
static LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
static LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
static LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
static LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
static LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
static LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
static LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
static LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
static LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;


/* LoadEffect loads the given initial reverb properties into the given OpenAL
 * effect object, and returns non-zero on success.
 */
static int LoadEffect(ALuint effect, const EFXEAXREVERBPROPERTIES *reverb)
{
    ALenum err;

    alGetError();

    /* Prepare the effect for EAX Reverb (standard reverb doesn't contain
     * the needed panning vectors).
     */
    alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
    if((err=alGetError()) != AL_NO_ERROR)
    {
        fprintf(stderr, "Failed to set EAX Reverb: %s (0x%04x)\n", alGetString(err), err);
        return 0;
    }

    /* Load the reverb properties. */
    alEffectf(effect, AL_EAXREVERB_DENSITY, reverb->flDensity);
    alEffectf(effect, AL_EAXREVERB_DIFFUSION, reverb->flDiffusion);
    alEffectf(effect, AL_EAXREVERB_GAIN, reverb->flGain);
    alEffectf(effect, AL_EAXREVERB_GAINHF, reverb->flGainHF);
    alEffectf(effect, AL_EAXREVERB_GAINLF, reverb->flGainLF);
    alEffectf(effect, AL_EAXREVERB_DECAY_TIME, reverb->flDecayTime);
    alEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, reverb->flDecayHFRatio);
    alEffectf(effect, AL_EAXREVERB_DECAY_LFRATIO, reverb->flDecayLFRatio);
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, reverb->flReflectionsGain);
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, reverb->flReflectionsDelay);
    alEffectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, reverb->flReflectionsPan);
    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, reverb->flLateReverbGain);
    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, reverb->flLateReverbDelay);
    alEffectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, reverb->flLateReverbPan);
    alEffectf(effect, AL_EAXREVERB_ECHO_TIME, reverb->flEchoTime);
    alEffectf(effect, AL_EAXREVERB_ECHO_DEPTH, reverb->flEchoDepth);
    alEffectf(effect, AL_EAXREVERB_MODULATION_TIME, reverb->flModulationTime);
    alEffectf(effect, AL_EAXREVERB_MODULATION_DEPTH, reverb->flModulationDepth);
    alEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, reverb->flAirAbsorptionGainHF);
    alEffectf(effect, AL_EAXREVERB_HFREFERENCE, reverb->flHFReference);
    alEffectf(effect, AL_EAXREVERB_LFREFERENCE, reverb->flLFReference);
    alEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, reverb->flRoomRolloffFactor);
    alEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, reverb->iDecayHFLimit);

    /* Check if an error occured, and return failure if so. */
    if((err=alGetError()) != AL_NO_ERROR)
    {
        fprintf(stderr, "Error setting up reverb: %s\n", alGetString(err));
        return 0;
    }

    return 1;
}


/* LoadBuffer loads the named audio file into an OpenAL buffer object, and
 * returns the new buffer ID.
 */
static ALuint LoadSound(const char *filename)
{
    Sound_Sample *sample;
    ALenum err, format;
    ALuint buffer;
    Uint32 slen;

    /* Open the audio file */
    sample = Sound_NewSampleFromFile(filename, NULL, 65536);
    if(!sample)
    {
        fprintf(stderr, "Could not open audio in %s\n", filename);
        return 0;
    }

    /* Get the sound format, and figure out the OpenAL format */
    if(sample->actual.channels == 1)
    {
        if(sample->actual.format == AUDIO_U8)
            format = AL_FORMAT_MONO8;
        else if(sample->actual.format == AUDIO_S16SYS)
            format = AL_FORMAT_MONO16;
        else
        {
            fprintf(stderr, "Unsupported sample format: 0x%04x\n", sample->actual.format);
            Sound_FreeSample(sample);
            return 0;
        }
    }
    else if(sample->actual.channels == 2)
    {
        if(sample->actual.format == AUDIO_U8)
            format = AL_FORMAT_STEREO8;
        else if(sample->actual.format == AUDIO_S16SYS)
            format = AL_FORMAT_STEREO16;
        else
        {
            fprintf(stderr, "Unsupported sample format: 0x%04x\n", sample->actual.format);
            Sound_FreeSample(sample);
            return 0;
        }
    }
    else
    {
        fprintf(stderr, "Unsupported channel count: %d\n", sample->actual.channels);
        Sound_FreeSample(sample);
        return 0;
    }

    /* Decode the whole audio stream to a buffer. */
    slen = Sound_DecodeAll(sample);
    if(!sample->buffer || slen == 0)
    {
        fprintf(stderr, "Failed to read audio from %s\n", filename);
        Sound_FreeSample(sample);
        return 0;
    }

    /* Buffer the audio data into a new buffer object, then free the data and
     * close the file. */
    buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, format, sample->buffer, slen, sample->actual.rate);
    Sound_FreeSample(sample);

    /* Check if an error occured, and clean up if so. */
    err = alGetError();
    if(err != AL_NO_ERROR)
    {
        fprintf(stderr, "OpenAL Error: %s\n", alGetString(err));
        if(buffer && alIsBuffer(buffer))
            alDeleteBuffers(1, &buffer);
        return 0;
    }

    return buffer;
}


/* Helper to calculate the dot-product of the two given vectors. */
static ALfloat dot_product(const ALfloat vec0[3], const ALfloat vec1[3])
{
    return vec0[0]*vec1[0] + vec0[1]*vec1[1] + vec0[2]*vec1[2];
}


int main(int argc, char **argv)
{
    static const int MaxTransitions = 8;
    EFXEAXREVERBPROPERTIES reverb0 = EFX_REVERB_PRESET_CASTLE_LARGEROOM;
    EFXEAXREVERBPROPERTIES reverb1 = EFX_REVERB_PRESET_CASTLE_LONGPASSAGE;
    struct timespec basetime;
    ALCdevice *device = NULL;
    ALCcontext *context = NULL;
    ALuint effects[2] = { 0, 0 };
    ALuint slots[2] = { 0, 0 };
    ALuint direct_filter = 0;
    ALuint buffer = 0;
    ALuint source = 0;
    ALCint num_sends = 0;
    ALenum state = AL_INITIAL;
    ALfloat direct_gain = 1.0f;
    int loops = 0;

    /* Print out usage if no arguments were specified */
    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s [-device <name>] [options] <filename>\n\n"
        "Options:\n"
        "\t-nodirect\tSilence direct path output (easier to hear reverb)\n\n",
        argv[0]);
        return 1;
    }

    /* Initialize OpenAL, and check for EFX support with at least 2 auxiliary
     * sends (if multiple sends are supported, 2 are provided by default; if
     * you want more, you have to request it through alcCreateContext).
     */
    argv++; argc--;
    if(InitAL(&argv, &argc) != 0)
        return 1;

    while(argc > 0)
    {
        if(strcmp(argv[0], "-nodirect") == 0)
            direct_gain = 0.0f;
        else
            break;
        argv++;
        argc--;
    }
    if(argc < 1)
    {
        fprintf(stderr, "No filename spacified.\n");
        CloseAL();
        return 1;
    }

    context = alcGetCurrentContext();
    device = alcGetContextsDevice(context);

    if(!alcIsExtensionPresent(device, "ALC_EXT_EFX"))
    {
        fprintf(stderr, "Error: EFX not supported\n");
        CloseAL();
        return 1;
    }

    num_sends = 0;
    alcGetIntegerv(device, ALC_MAX_AUXILIARY_SENDS, 1, &num_sends);
    if(alcGetError(device) != ALC_NO_ERROR || num_sends < 2)
    {
        fprintf(stderr, "Error: Device does not support multiple sends (got %d, need 2)\n",
                num_sends);
        CloseAL();
        return 1;
    }

    /* Define a macro to help load the function pointers. */
#define LOAD_PROC(x)  ((x) = alGetProcAddress(#x))
    LOAD_PROC(alGenFilters);
    LOAD_PROC(alDeleteFilters);
    LOAD_PROC(alIsFilter);
    LOAD_PROC(alFilteri);
    LOAD_PROC(alFilteriv);
    LOAD_PROC(alFilterf);
    LOAD_PROC(alFilterfv);
    LOAD_PROC(alGetFilteri);
    LOAD_PROC(alGetFilteriv);
    LOAD_PROC(alGetFilterf);
    LOAD_PROC(alGetFilterfv);

    LOAD_PROC(alGenEffects);
    LOAD_PROC(alDeleteEffects);
    LOAD_PROC(alIsEffect);
    LOAD_PROC(alEffecti);
    LOAD_PROC(alEffectiv);
    LOAD_PROC(alEffectf);
    LOAD_PROC(alEffectfv);
    LOAD_PROC(alGetEffecti);
    LOAD_PROC(alGetEffectiv);
    LOAD_PROC(alGetEffectf);
    LOAD_PROC(alGetEffectfv);

    LOAD_PROC(alGenAuxiliaryEffectSlots);
    LOAD_PROC(alDeleteAuxiliaryEffectSlots);
    LOAD_PROC(alIsAuxiliaryEffectSlot);
    LOAD_PROC(alAuxiliaryEffectSloti);
    LOAD_PROC(alAuxiliaryEffectSlotiv);
    LOAD_PROC(alAuxiliaryEffectSlotf);
    LOAD_PROC(alAuxiliaryEffectSlotfv);
    LOAD_PROC(alGetAuxiliaryEffectSloti);
    LOAD_PROC(alGetAuxiliaryEffectSlotiv);
    LOAD_PROC(alGetAuxiliaryEffectSlotf);
    LOAD_PROC(alGetAuxiliaryEffectSlotfv);
#undef LOAD_PROC

    /* Initialize SDL_sound. */
    Sound_Init();

    /* Load the sound into a buffer. */
    buffer = LoadSound(argv[0]);
    if(!buffer)
    {
        CloseAL();
        Sound_Quit();
        return 1;
    }

    /* Generate two effects for two "zones", and load a reverb into each one.
     * Note that unlike single-zone reverb, where you can store one effect per
     * preset, for multi-zone reverb you should have one effect per environment
     * instance, or one per audible zone. This is because we'll be changing the
     * effects' properties in real-time based on the environment instance
     * relative to the listener.
     */
    alGenEffects(2, effects);
    if(!LoadEffect(effects[0], &reverb0) || !LoadEffect(effects[1], &reverb1))
    {
        alDeleteEffects(2, effects);
        alDeleteBuffers(1, &buffer);
        Sound_Quit();
        CloseAL();
        return 1;
    }

    /* Create the effect slot objects, one for each "active" effect. */
    alGenAuxiliaryEffectSlots(2, slots);

    /* Tell the effect slots to use the loaded effect objects, with slot 0 for
     * Zone 0 and slot 1 for Zone 1. Note that this effectively copies the
     * effect properties. Modifying or deleting the effect object afterward
     * won't directly affect the effect slot until they're reapplied like this.
     */
    alAuxiliaryEffectSloti(slots[0], AL_EFFECTSLOT_EFFECT, effects[0]);
    alAuxiliaryEffectSloti(slots[1], AL_EFFECTSLOT_EFFECT, effects[1]);
    assert(alGetError()==AL_NO_ERROR && "Failed to set effect slot");

    /* For the purposes of this example, prepare a filter that optionally
     * silences the direct path which allows us to hear just the reverberation.
     * A filter like this is normally used for obstruction, where the path
     * directly between the listener and source is blocked (the exact
     * properties depending on the type and thickness of the obstructing
     * material).
     */
    alGenFilters(1, &direct_filter);
    alFilteri(direct_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
    alFilterf(direct_filter, AL_LOWPASS_GAIN, direct_gain);
    assert(alGetError()==AL_NO_ERROR && "Failed to set direct filter");

    /* Create the source to play the sound with. */
    source = 0;
    alGenSources(1, &source);
    alSourcei(source, AL_LOOPING, AL_TRUE);
    alSourcei(source, AL_DIRECT_FILTER, direct_filter);
    alSourcei(source, AL_BUFFER, buffer);

    /* Connect the source to the effect slots. Here, we connect source send 0
     * to Zone 0's slot, and send 1 to Zone 1's slot. Filters can be specified
     * to occlude the source from each zone by varying amounts; for example, a
     * source within a particular zone would be unfiltered, while a source that
     * can only see a zone through a window may be attenuated for that zone.
     */
    alSource3i(source, AL_AUXILIARY_SEND_FILTER, slots[0], 0, AL_FILTER_NULL);
    alSource3i(source, AL_AUXILIARY_SEND_FILTER, slots[1], 1, AL_FILTER_NULL);
    assert(alGetError()==AL_NO_ERROR && "Failed to setup sound source");

    /* Get the current time as the base for timing in the main loop. */
    altimespec_get(&basetime, AL_TIME_UTC);
    loops = 0;
    printf("Transition %d of %d...\n", loops+1, MaxTransitions);

    /* Play the sound for a while. */
    alSourcePlay(source);
    do {
        /* Individual reverb zones are connected via "portals". Each portal has
         * a position (center point of the connecting area), a normal (facing
         * direction), and a radius (approximate size of the connecting area).
         * For this example it also has movement velocity, although normally it
         * would be the listener that moves relative to the portal instead of
         * the portal itself.
         */
        const ALfloat portal_pos[3] = { -10.0f, 0.0f, 0.0f };
        const ALfloat portal_norm[3] = { 1.0f, 0.0f, 0.0f };
        const ALfloat portal_vel[3] = { 5.0f, 0.0f, 0.0f };
        const ALfloat portal_radius = 2.5f;
        ALfloat other_dir[3], this_dir[3];
        ALfloat local_norm[3];
        ALfloat local_dir[3];
        ALfloat local_radius;
        ALfloat dist, timediff;
        struct timespec curtime;

        /* Start a batch update, to ensure all changes apply simultaneously. */
        alcSuspendContext(context);

        /* Get the current time to track the amount of time that passed.
         * Convert the difference to seconds.
         */
        altimespec_get(&curtime, AL_TIME_UTC);
        timediff = (ALfloat)(curtime.tv_sec - basetime.tv_sec);
        timediff += (ALfloat)(curtime.tv_nsec - basetime.tv_nsec) / 1000000000.0f;

        /* Avoid negative time deltas, in case of non-monotonic clocks. */
        if(timediff < 0.0f)
            timediff = 0.0f;
        else while(timediff >= 4.0f)
        {
            /* For this example, each transition occurs over 4 seconds.
             * Decrease the delta and increase the base time to start a new
             * transition.
             */
            timediff -= 4.0f;
            basetime.tv_sec += 4;
            if(++loops < MaxTransitions)
                printf("Transition %d of %d...\n", loops+1, MaxTransitions);
        }

        /* Move the portal according to the amount of time passed. local_dir
         * represents the listener-relative point to the adjacent zone.
         */
        local_dir[0] = portal_pos[0] + portal_vel[0]*timediff;
        local_dir[1] = portal_pos[1] + portal_vel[1]*timediff;
        local_dir[2] = portal_pos[2] + portal_vel[2]*timediff;
        /* A normal application would also rotate the portal's normal given the
         * listener orientation, to get the listener-relative normal.
         *
         * For this example, the portal is always head-on but every other
         * transition negates the normal. This effectively simulates a
         * different portal moving in closer than the last one that faces the
         * other way, switching the old adjacent zone to a new one.
         */
        local_norm[0] = portal_norm[0] * ((loops&1) ? -1.0f : 1.0f);
        local_norm[1] = portal_norm[1] * ((loops&1) ? -1.0f : 1.0f);
        local_norm[2] = portal_norm[2] * ((loops&1) ? -1.0f : 1.0f);

        /* Calculate the distance from the listener to the portal. */
        dist = sqrtf(dot_product(local_dir, local_dir));
        if(!(dist > 0.00001f))
        {
            /* We're practically in the center of the portal. Give the panning
             * vectors a 50/50 split, with Zone 0 covering the half in front of
             * the normal, and Zone 1 covering the half behind.
             */
            this_dir[0] = local_norm[0] / 2.0f;
            this_dir[1] = local_norm[1] / 2.0f;
            this_dir[2] = local_norm[2] / 2.0f;

            other_dir[0] = local_norm[0] / -2.0f;
            other_dir[1] = local_norm[1] / -2.0f;
            other_dir[2] = local_norm[2] / -2.0f;

            alEffectf(effects[0], AL_EAXREVERB_GAIN, reverb0.flGain);
            alEffectfv(effects[0], AL_EAXREVERB_REFLECTIONS_PAN, this_dir);
            alEffectfv(effects[0], AL_EAXREVERB_LATE_REVERB_PAN, this_dir);

            alEffectf(effects[1], AL_EAXREVERB_GAIN, reverb1.flGain);
            alEffectfv(effects[1], AL_EAXREVERB_REFLECTIONS_PAN, other_dir);
            alEffectfv(effects[1], AL_EAXREVERB_LATE_REVERB_PAN, other_dir);
        }
        else
        {
            const EFXEAXREVERBPROPERTIES *other_reverb;
            const EFXEAXREVERBPROPERTIES *this_reverb;
            ALuint other_effect, this_effect;
            ALfloat spread, attn;

            /* Normalize the direction to the portal. */
            local_dir[0] /= dist;
            local_dir[1] /= dist;
            local_dir[2] /= dist;

            /* Scale the radius according to its local angle. The visibility to
             * the other zone reduces as the portal becomes perpendicular.
             */
            local_radius = portal_radius * fabsf(dot_product(local_dir, local_norm));

            /* Calculate distance attenuation for the other zone, using the
             * standard inverse distance model with the radius as a reference.
             */
            attn = local_radius / dist;
            if(attn > 1.0f) attn = 1.0f;

            /* Calculate the 'spread' of the portal, which is the amount of
             * coverage the other zone has around the listener.
             */
            spread = atan2f(local_radius, dist) / (ALfloat)M_PI;

            /* Figure out which zone we're in, given the direction to the
             * portal and its normal.
             */
            if(dot_product(local_dir, local_norm) <= 0.0f)
            {
                /* We're in front of the portal, so we're in Zone 0. */
                this_effect = effects[0];
                other_effect = effects[1];
                this_reverb = &reverb0;
                other_reverb = &reverb1;
            }
            else
            {
                /* We're behind the portal, so we're in Zone 1. */
                this_effect = effects[1];
                other_effect = effects[0];
                this_reverb = &reverb1;
                other_reverb = &reverb0;
            }

            /* Scale the other zone's panning vector down as the portal's
             * spread increases, so that it envelops the listener more.
             */
            other_dir[0] = local_dir[0] * (1.0f-spread);
            other_dir[1] = local_dir[1] * (1.0f-spread);
            other_dir[2] = local_dir[2] * (1.0f-spread);
            /* Pan the current zone to the opposite direction of the portal,
             * and take the remaining percentage of the portal's spread.
             */
            this_dir[0] = local_dir[0] * -spread;
            this_dir[1] = local_dir[1] * -spread;
            this_dir[2] = local_dir[2] * -spread;

            /* Now set the effects' panning vectors and distance attenuation. */
            alEffectf(this_effect, AL_EAXREVERB_GAIN, this_reverb->flGain);
            alEffectfv(this_effect, AL_EAXREVERB_REFLECTIONS_PAN, this_dir);
            alEffectfv(this_effect, AL_EAXREVERB_LATE_REVERB_PAN, this_dir);

            alEffectf(other_effect, AL_EAXREVERB_GAIN, other_reverb->flGain * attn);
            alEffectfv(other_effect, AL_EAXREVERB_REFLECTIONS_PAN, other_dir);
            alEffectfv(other_effect, AL_EAXREVERB_LATE_REVERB_PAN, other_dir);
        }

        /* Finally, update the effect slots with the updated effect parameters,
         * and finish the update batch.
         */
        alAuxiliaryEffectSloti(slots[0], AL_EFFECTSLOT_EFFECT, effects[0]);
        alAuxiliaryEffectSloti(slots[1], AL_EFFECTSLOT_EFFECT, effects[1]);
        alcProcessContext(context);

        al_nssleep(10000000);

        alGetSourcei(source, AL_SOURCE_STATE, &state);
    } while(alGetError() == AL_NO_ERROR && state == AL_PLAYING && loops < MaxTransitions);

    /* All done. Delete resources, and close down SDL_sound and OpenAL. */
    alDeleteSources(1, &source);
    alDeleteAuxiliaryEffectSlots(2, slots);
    alDeleteEffects(2, effects);
    alDeleteFilters(1, &direct_filter);
    alDeleteBuffers(1, &buffer);

    Sound_Quit();
    CloseAL();

    return 0;
}
