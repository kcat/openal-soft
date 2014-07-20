
#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "alMain.h"
#include "alMidi.h"
#include "alError.h"
#include "alu.h"

#include "midi/base.h"


static ALuint read_le32(Reader *stream)
{
    ALubyte buf[4];
    if(Reader_read(stream, buf, 4) != 4)
        return 0;
    return (buf[3]<<24) | (buf[2]<<16) | (buf[1]<<8) | buf[0];
}
static ALushort read_le16(Reader *stream)
{
    ALubyte buf[2];
    if(Reader_read(stream, buf, 2) != 2)
        return 0;
    return (buf[1]<<8) | buf[0];
}
static ALubyte read_8(Reader *stream)
{
    ALubyte buf[1];
    if(Reader_read(stream, buf, 1) != 1)
        return 0;
    return buf[0];
}
static void skip(Reader *stream, ALuint amt)
{
    while(amt > 0 && !READERR(stream))
    {
        char buf[4096];
        amt -= Reader_read(stream, buf, minu(sizeof(buf), amt));
    }
}

typedef struct Generator {
    ALushort mGenerator;
    ALushort mAmount;
} Generator;
static void Generator_read(Generator *self, Reader *stream)
{
    self->mGenerator = read_le16(stream);
    self->mAmount = read_le16(stream);
}

static const ALint DefaultGenValue[60] = {
    0, /* 0 - startAddrOffset */
    0, /* 1 - endAddrOffset */
    0, /* 2 - startloopAddrOffset */
    0, /* 3 - endloopAddrOffset */
    0, /* 4 - startAddrCoarseOffset */
    0, /* 5 - modLfoToPitch */
    0, /* 6 - vibLfoToPitch */
    0, /* 7 - modEnvToPitch */
    13500, /* 8 - initialFilterFc */
    0, /* 9 - initialFilterQ */
    0, /* 10 - modLfoToFilterFc */
    0, /* 11 - modEnvToFilterFc */
    0, /* 12 - endAddrCoarseOffset */
    0, /* 13 - modLfoToVolume */
    0, /* 14 -  */
    0, /* 15 - chorusEffectsSend */
    0, /* 16 - reverbEffectsSend */
    0, /* 17 - pan */
    0, /* 18 -  */
    0, /* 19 -  */
    0, /* 20 -  */
    -12000, /* 21 - delayModLFO */
    0, /* 22 - freqModLFO */
    -12000, /* 23 - delayVibLFO */
    0, /* 24 - freqVibLFO */
    -12000, /* 25 - delayModEnv */
    -12000, /* 26 - attackModEnv */
    -12000, /* 27 - holdModEnv */
    -12000, /* 28 - decayModEnv */
    0, /* 29 - sustainModEnv */
    -12000, /* 30 - releaseModEnv */
    0, /* 31 - keynumToModEnvHold */
    0, /* 32 - keynumToModEnvDecay */
    -12000, /* 33 - delayVolEnv */
    -12000, /* 34 - attackVolEnv */
    -12000, /* 35 - holdVolEnv */
    -12000, /* 36 - decayVolEnv */
    0, /* 37 - sustainVolEnv */
    -12000, /* 38 - releaseVolEnv */
    0, /* 39 - keynumToVolEnvHold */
    0, /* 40 - keynumToVolEnvDecay */
    0, /* 41 -  */
    0, /* 42 -  */
    0, /* 43 - keyRange */
    0, /* 44 - velRange */
    0, /* 45 - startloopAddrCoarseOffset */
    0, /* 46 - keynum */
    0, /* 47 - velocity */
    0, /* 48 - initialAttenuation */
    0, /* 49 -  */
    0, /* 50 - endloopAddrCoarseOffset */
    0, /* 51 - corseTune */
    0, /* 52 - fineTune */
    0, /* 53 -  */
    0, /* 54 - sampleModes */
    0, /* 55 -  */
    100, /* 56 - scaleTuning */
    0, /* 57 - exclusiveClass */
    0, /* 58 - overridingRootKey */
    0, /* 59 -  */
};

typedef struct Modulator {
    ALushort mSrcOp;
    ALushort mDstOp;
    ALshort mAmount;
    ALushort mAmtSrcOp;
    ALushort mTransOp;
} Modulator;
static void Modulator_read(Modulator *self, Reader *stream)
{
    self->mSrcOp = read_le16(stream);
    self->mDstOp = read_le16(stream);
    self->mAmount = read_le16(stream);
    self->mAmtSrcOp = read_le16(stream);
    self->mTransOp = read_le16(stream);
}

typedef struct Zone {
    ALushort mGenIdx;
    ALushort mModIdx;
} Zone;
static void Zone_read(Zone *self, Reader *stream)
{
    self->mGenIdx = read_le16(stream);
    self->mModIdx = read_le16(stream);
}

typedef struct PresetHeader {
    ALchar mName[20];
    ALushort mPreset; /* MIDI program number */
    ALushort mBank;
    ALushort mZoneIdx;
    ALuint mLibrary;
    ALuint mGenre;
    ALuint mMorphology;
} PresetHeader;
static void PresetHeader_read(PresetHeader *self, Reader *stream)
{
    Reader_read(stream, self->mName, sizeof(self->mName));
    self->mPreset = read_le16(stream);
    self->mBank = read_le16(stream);
    self->mZoneIdx = read_le16(stream);
    self->mLibrary = read_le32(stream);
    self->mGenre = read_le32(stream);
    self->mMorphology = read_le32(stream);
}

typedef struct InstrumentHeader {
    ALchar mName[20];
    ALushort mZoneIdx;
} InstrumentHeader;
static void InstrumentHeader_read(InstrumentHeader *self, Reader *stream)
{
    Reader_read(stream, self->mName, sizeof(self->mName));
    self->mZoneIdx = read_le16(stream);
}

typedef struct SampleHeader {
    ALchar mName[20];
    ALuint mStart;
    ALuint mEnd;
    ALuint mStartloop;
    ALuint mEndloop;
    ALuint mSampleRate;
    ALubyte mOriginalKey;
    ALbyte mCorrection;
    ALushort mSampleLink;
    ALushort mSampleType;
} SampleHeader;
static void SampleHeader_read(SampleHeader *self, Reader *stream)
{
    Reader_read(stream, self->mName, sizeof(self->mName));
    self->mStart = read_le32(stream);
    self->mEnd = read_le32(stream);
    self->mStartloop = read_le32(stream);
    self->mEndloop = read_le32(stream);
    self->mSampleRate = read_le32(stream);
    self->mOriginalKey = read_8(stream);
    self->mCorrection = read_8(stream);
    self->mSampleLink = read_le16(stream);
    self->mSampleType = read_le16(stream);
}


typedef struct Soundfont {
    ALuint ifil;
    ALchar *irom;

    PresetHeader *phdr;
    ALsizei phdr_size;

    Zone *pbag;
    ALsizei pbag_size;
    Modulator *pmod;
    ALsizei pmod_size;
    Generator *pgen;
    ALsizei pgen_size;

    InstrumentHeader *inst;
    ALsizei inst_size;

    Zone *ibag;
    ALsizei ibag_size;
    Modulator *imod;
    ALsizei imod_size;
    Generator *igen;
    ALsizei igen_size;

    SampleHeader *shdr;
    ALsizei shdr_size;
} Soundfont;

static void Soundfont_Construct(Soundfont *self)
{
    self->ifil = 0;
    self->irom = NULL;

    self->phdr = NULL;
    self->phdr_size = 0;

    self->pbag = NULL;
    self->pbag_size = 0;
    self->pmod = NULL;
    self->pmod_size = 0;
    self->pgen = NULL;
    self->pgen_size = 0;

    self->inst = NULL;
    self->inst_size = 0;

    self->ibag = NULL;
    self->ibag_size = 0;
    self->imod = NULL;
    self->imod_size = 0;
    self->igen = NULL;
    self->igen_size = 0;

    self->shdr = NULL;
    self->shdr_size = 0;
}

static void Soundfont_Destruct(Soundfont *self)
{
    free(self->irom);
    self->irom = NULL;

    free(self->phdr);
    self->phdr = NULL;
    self->phdr_size = 0;

    free(self->pbag);
    self->pbag = NULL;
    self->pbag_size = 0;
    free(self->pmod);
    self->pmod = NULL;
    self->pmod_size = 0;
    free(self->pgen);
    self->pgen = NULL;
    self->pgen_size = 0;

    free(self->inst);
    self->inst = NULL;
    self->inst_size = 0;

    free(self->ibag);
    self->ibag = NULL;
    self->ibag_size = 0;
    free(self->imod);
    self->imod = NULL;
    self->imod_size = 0;
    free(self->igen);
    self->igen = NULL;
    self->igen_size = 0;

    free(self->shdr);
    self->shdr = NULL;
    self->shdr_size = 0;
}


#define FOURCC(a,b,c,d) ((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#define FOURCCFMT     "%c%c%c%c"
#define FOURCCARGS(x)  (char)((x)&0xff), (char)(((x)>>8)&0xff), (char)(((x)>>16)&0xff), (char)(((x)>>24)&0xff)
typedef struct RiffHdr {
    ALuint mCode;
    ALuint mSize;
} RiffHdr;
static void RiffHdr_read(RiffHdr *self, Reader *stream)
{
    self->mCode = read_le32(stream);
    self->mSize = read_le32(stream);
}


typedef struct GenModList {
    VECTOR(Generator) gens;
    VECTOR(Modulator) mods;
} GenModList;

static void GenModList_Construct(GenModList *self)
{
    VECTOR_INIT(self->gens);
    VECTOR_INIT(self->mods);
}

static void GenModList_Destruct(GenModList *self)
{
    VECTOR_DEINIT(self->mods);
    VECTOR_DEINIT(self->gens);
}

static GenModList GenModList_clone(const GenModList *self)
{
    GenModList ret;

    GenModList_Construct(&ret);

    VECTOR_INSERT(ret.gens, VECTOR_ITER_END(ret.gens),
        VECTOR_ITER_BEGIN(self->gens), VECTOR_ITER_END(self->gens)
    );
    VECTOR_INSERT(ret.mods, VECTOR_ITER_END(ret.mods),
        VECTOR_ITER_BEGIN(self->mods), VECTOR_ITER_END(self->mods)
    );

    return ret;
}

static void GenModList_insertGen(GenModList *self, const Generator *gen, ALboolean ispreset)
{
    Generator *i = VECTOR_ITER_BEGIN(self->gens);
    Generator *end = VECTOR_ITER_END(self->gens);
    for(;i != end;i++)
    {
        if(i->mGenerator == gen->mGenerator)
        {
            i->mAmount = gen->mAmount;
            return;
        }
    }

    if(ispreset &&
       (gen->mGenerator == 0 || gen->mGenerator == 1 || gen->mGenerator == 2 ||
        gen->mGenerator == 3 || gen->mGenerator == 4 || gen->mGenerator == 12 ||
        gen->mGenerator == 45 || gen->mGenerator == 46 || gen->mGenerator == 47 ||
        gen->mGenerator == 50 || gen->mGenerator == 54 || gen->mGenerator == 57 ||
        gen->mGenerator == 58))
        return;

    if(VECTOR_PUSH_BACK(self->gens, *gen) == AL_FALSE)
    {
        ERR("Failed to insert generator (from %d elements)\n", VECTOR_SIZE(self->gens));
        return;
    }
}
static void GenModList_accumGen(GenModList *self, const Generator *gen)
{
    Generator *i = VECTOR_ITER_BEGIN(self->gens);
    Generator *end = VECTOR_ITER_END(self->gens);
    for(;i != end;i++)
    {
        if(i->mGenerator == gen->mGenerator)
        {
            if(gen->mGenerator == 43 || gen->mGenerator == 44)
            {
                /* Range generators accumulate by taking the intersection of
                 * the two ranges.
                 */
                ALushort low = maxu(i->mAmount&0x00ff, gen->mAmount&0x00ff);
                ALushort high = minu(i->mAmount&0xff00, gen->mAmount&0xff00);
                i->mAmount = low | high;
            }
            else
                i->mAmount += gen->mAmount;
            return;
        }
    }

    if(VECTOR_PUSH_BACK(self->gens, *gen) == AL_FALSE)
    {
        ERR("Failed to insert generator (from %d elements)\n", VECTOR_SIZE(self->gens));
        return;
    }
    if(gen->mGenerator < 60)
        VECTOR_BACK(self->gens).mAmount += DefaultGenValue[gen->mGenerator];
}

static void GenModList_insertMod(GenModList *self, const Modulator *mod)
{
    Modulator *i = VECTOR_ITER_BEGIN(self->mods);
    Modulator *end = VECTOR_ITER_END(self->mods);
    for(;i != end;i++)
    {
        if(i->mDstOp == mod->mDstOp && i->mSrcOp == mod->mSrcOp &&
           i->mAmtSrcOp == mod->mAmtSrcOp && i->mTransOp == mod->mTransOp)
        {
            i->mAmount = mod->mAmount;
            return;
        }
    }

    if(VECTOR_PUSH_BACK(self->mods, *mod) == AL_FALSE)
    {
        ERR("Failed to insert modulator (from %d elements)\n", VECTOR_SIZE(self->mods));
        return;
    }
}
static void GenModList_accumMod(GenModList *self, const Modulator *mod)
{
    Modulator *i = VECTOR_ITER_BEGIN(self->mods);
    Modulator *end = VECTOR_ITER_END(self->mods);
    for(;i != end;i++)
    {
        if(i->mDstOp == mod->mDstOp && i->mSrcOp == mod->mSrcOp &&
           i->mAmtSrcOp == mod->mAmtSrcOp && i->mTransOp == mod->mTransOp)
        {
            i->mAmount += mod->mAmount;
            return;
        }
    }

    if(VECTOR_PUSH_BACK(self->mods, *mod) == AL_FALSE)
    {
        ERR("Failed to insert modulator (from %d elements)\n", VECTOR_SIZE(self->mods));
        return;
    }

    if(mod->mSrcOp == 0x0502 && mod->mDstOp == 48 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += 960;
    else if(mod->mSrcOp == 0x0102 && mod->mDstOp == 8 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += -2400;
    else if(mod->mSrcOp == 0x000D && mod->mDstOp == 6 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += 50;
    else if(mod->mSrcOp == 0x0081 && mod->mDstOp == 6 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += 50;
    else if(mod->mSrcOp == 0x0582 && mod->mDstOp == 48 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += 960;
    else if(mod->mSrcOp == 0x028A && mod->mDstOp == 17 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += 1000;
    else if(mod->mSrcOp == 0x058B && mod->mDstOp == 48 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += 960;
    else if(mod->mSrcOp == 0x00DB && mod->mDstOp == 16 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += 200;
    else if(mod->mSrcOp == 0x00DD && mod->mDstOp == 15 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += 200;
    /*else if(mod->mSrcOp == 0x020E && mod->mDstOp == ?initialpitch? && mod->mAmtSrcOp == 0x0010 && mod->mTransOp == 0)
        VECTOR_BACK(self->mods).mAmount += 12700;*/
}


#define ERROR_GOTO(lbl_, ...)  do {                                           \
    ERR(__VA_ARGS__);                                                         \
    goto lbl_;                                                                \
} while(0)

static ALboolean ensureFontSanity(const Soundfont *sfont)
{
    ALsizei i;

    for(i = 0;i < sfont->phdr_size;i++)
    {
        if(sfont->phdr[i].mZoneIdx >= sfont->pbag_size)
        {
            WARN("Preset %d has invalid zone index %d (max: %d)\n", i,
                 sfont->phdr[i].mZoneIdx, sfont->pbag_size);
            return AL_FALSE;
        }
        if(i+1 < sfont->phdr_size && sfont->phdr[i+1].mZoneIdx < sfont->phdr[i].mZoneIdx)
        {
            WARN("Preset %d has invalid zone index (%d does not follow %d)\n", i+1,
                 sfont->phdr[i+1].mZoneIdx, sfont->phdr[i].mZoneIdx);
            return AL_FALSE;
        }
    }

    for(i = 0;i < sfont->pbag_size;i++)
    {
        if(sfont->pbag[i].mGenIdx >= sfont->pgen_size)
        {
            WARN("Preset zone %d has invalid generator index %d (max: %d)\n", i,
                 sfont->pbag[i].mGenIdx, sfont->pgen_size);
            return AL_FALSE;
        }
        if(i+1 < sfont->pbag_size && sfont->pbag[i+1].mGenIdx < sfont->pbag[i].mGenIdx)
        {
            WARN("Preset zone %d has invalid generator index (%d does not follow %d)\n", i+1,
                 sfont->pbag[i+1].mGenIdx, sfont->pbag[i].mGenIdx);
            return AL_FALSE;
        }
        if(sfont->pbag[i].mModIdx >= sfont->pmod_size)
        {
            WARN("Preset zone %d has invalid modulator index %d (max: %d)\n", i,
                 sfont->pbag[i].mModIdx, sfont->pmod_size);
            return AL_FALSE;
        }
        if(i+1 < sfont->pbag_size && sfont->pbag[i+1].mModIdx < sfont->pbag[i].mModIdx)
        {
            WARN("Preset zone %d has invalid modulator index (%d does not follow %d)\n", i+1,
                 sfont->pbag[i+1].mModIdx, sfont->pbag[i].mModIdx);
            return AL_FALSE;
        }
    }


    for(i = 0;i < sfont->inst_size;i++)
    {
        if(sfont->inst[i].mZoneIdx >= sfont->ibag_size)
        {
            WARN("Instrument %d has invalid zone index %d (max: %d)\n", i,
                 sfont->inst[i].mZoneIdx, sfont->ibag_size);
            return AL_FALSE;
        }
        if(i+1 < sfont->inst_size && sfont->inst[i+1].mZoneIdx < sfont->inst[i].mZoneIdx)
        {
            WARN("Instrument %d has invalid zone index (%d does not follow %d)\n", i+1,
                 sfont->inst[i+1].mZoneIdx, sfont->inst[i].mZoneIdx);
            return AL_FALSE;
        }
    }

    for(i = 0;i < sfont->ibag_size;i++)
    {
        if(sfont->ibag[i].mGenIdx >= sfont->igen_size)
        {
            WARN("Instrument zone %d has invalid generator index %d (max: %d)\n", i,
                 sfont->ibag[i].mGenIdx, sfont->igen_size);
            return AL_FALSE;
        }
        if(i+1 < sfont->ibag_size && sfont->ibag[i+1].mGenIdx < sfont->ibag[i].mGenIdx)
        {
            WARN("Instrument zone %d has invalid generator index (%d does not follow %d)\n", i+1,
                 sfont->ibag[i+1].mGenIdx, sfont->ibag[i].mGenIdx);
            return AL_FALSE;
        }
        if(sfont->ibag[i].mModIdx >= sfont->imod_size)
        {
            WARN("Instrument zone %d has invalid modulator index %d (max: %d)\n", i,
                 sfont->ibag[i].mModIdx, sfont->imod_size);
            return AL_FALSE;
        }
        if(i+1 < sfont->ibag_size && sfont->ibag[i+1].mModIdx < sfont->ibag[i].mModIdx)
        {
            WARN("Instrument zone %d has invalid modulator index (%d does not follow %d)\n", i+1,
                 sfont->ibag[i+1].mModIdx, sfont->ibag[i].mModIdx);
            return AL_FALSE;
        }
    }


    for(i = 0;i < sfont->shdr_size-1;i++)
    {
        if((sfont->shdr[i].mSampleType&0x8000) && sfont->irom == NULL)
        {
            WARN("Sample header %d has ROM sample type without an irom sub-chunk\n", i);
            return AL_FALSE;
        }
    }


    return AL_TRUE;
}

static ALboolean checkZone(const GenModList *zone, const PresetHeader *preset, const InstrumentHeader *inst, const SampleHeader *samp)
{
    Generator *gen = VECTOR_ITER_BEGIN(zone->gens);
    Generator *gen_end = VECTOR_ITER_END(zone->gens);
    for(;gen != gen_end;gen++)
    {
        if(gen->mGenerator == 43 || gen->mGenerator == 44)
        {
            int high = gen->mAmount>>8;
            int low = gen->mAmount&0xff;

            if(!(low >= 0 && high <= 127 && high >= low))
            {
                TRACE("Preset \"%s\", inst \"%s\", sample \"%s\": invalid %s range %d...%d\n",
                      preset->mName, inst->mName, samp->mName,
                      (gen->mGenerator == 43) ? "key" :
                      (gen->mGenerator == 44) ? "velocity" : "(unknown)",
                      low, high);
                return AL_FALSE;
            }
        }
    }

    return AL_TRUE;
}

static ALenum getModSrcInput(int input)
{
    if(input == 0) return AL_ONE_SOFT;
    if(input == 2) return AL_NOTEON_VELOCITY_SOFT;
    if(input == 3) return AL_NOTEON_KEY_SOFT;
    if(input == 10) return AL_KEYPRESSURE_SOFT;
    if(input == 13) return AL_CHANNELPRESSURE_SOFT;
    if(input == 14) return AL_PITCHBEND_SOFT;
    if(input == 16) return AL_PITCHBEND_SENSITIVITY_SOFT;
    if((input&0x80))
    {
        if(IsValidCtrlInput(input^0x80))
            return input^0x80;
    }
    ERR("Unhandled modulator source input: 0x%02x\n", input);
    return AL_INVALID;
}

static ALenum getModSrcType(int type)
{
    if(type == 0x0000) return AL_UNORM_SOFT;
    if(type == 0x0100) return AL_UNORM_REV_SOFT;
    if(type == 0x0200) return AL_SNORM_SOFT;
    if(type == 0x0300) return AL_SNORM_REV_SOFT;
    ERR("Unhandled modulator source type: 0x%04x\n", type);
    return AL_INVALID;
}

static ALenum getModSrcForm(int form)
{
    if(form == 0x0000) return AL_LINEAR_SOFT;
    if(form == 0x0400) return AL_CONCAVE_SOFT;
    if(form == 0x0800) return AL_CONVEX_SOFT;
    if(form == 0x0C00) return AL_SWITCH_SOFT;
    ERR("Unhandled modulator source form: 0x%04x\n", form);
    return AL_INVALID;
}

static ALenum getModTransOp(int op)
{
    if(op == 0) return AL_LINEAR_SOFT;
    if(op == 2) return AL_ABSOLUTE_SOFT;
    ERR("Unhandled modulator transform op: 0x%04x\n", op);
    return AL_INVALID;
}

static ALenum getLoopMode(int mode)
{
    if(mode == 0) return AL_NONE;
    if(mode == 1) return AL_LOOP_CONTINUOUS_SOFT;
    if(mode == 3) return AL_LOOP_UNTIL_RELEASE_SOFT;
    ERR("Unhandled loop mode: %d\n", mode);
    return AL_NONE;
}

static ALenum getSampleType(int type)
{
    if(type == 1) return AL_MONO_SOFT;
    if(type == 2) return AL_RIGHT_SOFT;
    if(type == 4) return AL_LEFT_SOFT;
    if(type == 8)
    {
        WARN("Sample type \"linked\" ignored; pretending mono\n");
        return AL_MONO_SOFT;
    }
    ERR("Unhandled sample type: 0x%04x\n", type);
    return AL_MONO_SOFT;
}

static void fillZone(ALfontsound *sound, ALCcontext *context, const GenModList *zone)
{
    static const ALenum Gen2Param[60] = {
        0, /* 0 - startAddrOffset */
        0, /* 1 - endAddrOffset */
        0, /* 2 - startloopAddrOffset */
        0, /* 3 - endloopAddrOffset */
        0, /* 4 - startAddrCoarseOffset */
        AL_MOD_LFO_TO_PITCH_SOFT, /* 5 - modLfoToPitch */
        AL_VIBRATO_LFO_TO_PITCH_SOFT, /* 6 - vibLfoToPitch */
        AL_MOD_ENV_TO_PITCH_SOFT, /* 7 - modEnvToPitch */
        AL_FILTER_CUTOFF_SOFT, /* 8 - initialFilterFc */
        AL_FILTER_RESONANCE_SOFT, /* 9 - initialFilterQ */
        AL_MOD_LFO_TO_FILTER_CUTOFF_SOFT, /* 10 - modLfoToFilterFc */
        AL_MOD_ENV_TO_FILTER_CUTOFF_SOFT, /* 11 - modEnvToFilterFc */
        0, /* 12 - endAddrCoarseOffset */
        AL_MOD_LFO_TO_VOLUME_SOFT, /* 13 - modLfoToVolume */
        0, /* 14 -  */
        AL_CHORUS_SEND_SOFT, /* 15 - chorusEffectsSend */
        AL_REVERB_SEND_SOFT, /* 16 - reverbEffectsSend */
        AL_PAN_SOFT, /* 17 - pan */
        0, /* 18 -  */
        0, /* 19 -  */
        0, /* 20 -  */
        AL_MOD_LFO_DELAY_SOFT, /* 21 - delayModLFO */
        AL_MOD_LFO_FREQUENCY_SOFT, /* 22 - freqModLFO */
        AL_VIBRATO_LFO_DELAY_SOFT, /* 23 - delayVibLFO */
        AL_VIBRATO_LFO_FREQUENCY_SOFT, /* 24 - freqVibLFO */
        AL_MOD_ENV_DELAYTIME_SOFT, /* 25 - delayModEnv */
        AL_MOD_ENV_ATTACKTIME_SOFT, /* 26 - attackModEnv */
        AL_MOD_ENV_HOLDTIME_SOFT, /* 27 - holdModEnv */
        AL_MOD_ENV_DECAYTIME_SOFT, /* 28 - decayModEnv */
        AL_MOD_ENV_SUSTAINVOLUME_SOFT, /* 29 - sustainModEnv */
        AL_MOD_ENV_RELEASETIME_SOFT, /* 30 - releaseModEnv */
        AL_MOD_ENV_KEY_TO_HOLDTIME_SOFT, /* 31 - keynumToModEnvHold */
        AL_MOD_ENV_KEY_TO_DECAYTIME_SOFT, /* 32 - keynumToModEnvDecay */
        AL_VOLUME_ENV_DELAYTIME_SOFT, /* 33 - delayVolEnv */
        AL_VOLUME_ENV_ATTACKTIME_SOFT, /* 34 - attackVolEnv */
        AL_VOLUME_ENV_HOLDTIME_SOFT, /* 35 - holdVolEnv */
        AL_VOLUME_ENV_DECAYTIME_SOFT, /* 36 - decayVolEnv */
        AL_VOLUME_ENV_SUSTAINVOLUME_SOFT, /* 37 - sustainVolEnv */
        AL_VOLUME_ENV_RELEASETIME_SOFT, /* 38 - releaseVolEnv */
        AL_VOLUME_ENV_KEY_TO_HOLDTIME_SOFT, /* 39 - keynumToVolEnvHold */
        AL_VOLUME_ENV_KEY_TO_DECAYTIME_SOFT, /* 40 - keynumToVolEnvDecay */
        0, /* 41 -  */
        0, /* 42 -  */
        AL_KEY_RANGE_SOFT, /* 43 - keyRange */
        AL_VELOCITY_RANGE_SOFT, /* 44 - velRange */
        0, /* 45 - startloopAddrCoarseOffset */
        0, /* 46 - keynum */
        0, /* 47 - velocity */
        AL_ATTENUATION_SOFT, /* 48 - initialAttenuation */
        0, /* 49 -  */
        0, /* 50 - endloopAddrCoarseOffset */
        AL_TUNING_COARSE_SOFT, /* 51 - corseTune */
        AL_TUNING_FINE_SOFT, /* 52 - fineTune */
        0, /* 53 -  */
        AL_LOOP_MODE_SOFT, /* 54 - sampleModes */
        0, /* 55 -  */
        AL_TUNING_SCALE_SOFT, /* 56 - scaleTuning */
        AL_EXCLUSIVE_CLASS_SOFT, /* 57 - exclusiveClass */
        AL_BASE_KEY_SOFT, /* 58 - overridingRootKey */
        0, /* 59 -  */
    };
    const Generator *gen, *gen_end;
    const Modulator *mod, *mod_end;

    mod = VECTOR_ITER_BEGIN(zone->mods);
    mod_end = VECTOR_ITER_END(zone->mods);
    for(;mod != mod_end;mod++)
    {
        ALenum src0in = getModSrcInput(mod->mSrcOp&0xFF);
        ALenum src0type = getModSrcType(mod->mSrcOp&0x0300);
        ALenum src0form = getModSrcForm(mod->mSrcOp&0xFC00);
        ALenum src1in = getModSrcInput(mod->mAmtSrcOp&0xFF);
        ALenum src1type = getModSrcType(mod->mAmtSrcOp&0x0300);
        ALenum src1form = getModSrcForm(mod->mAmtSrcOp&0xFC00);
        ALenum trans = getModTransOp(mod->mTransOp);
        ALenum dst = (mod->mDstOp < 60) ? Gen2Param[mod->mDstOp] : 0;
        if(!dst || dst == AL_KEY_RANGE_SOFT || dst == AL_VELOCITY_RANGE_SOFT ||
           dst == AL_LOOP_MODE_SOFT || dst == AL_EXCLUSIVE_CLASS_SOFT ||
           dst == AL_BASE_KEY_SOFT)
            ERR("Unhandled modulator destination: %d\n", mod->mDstOp);
        else if(src0in != AL_INVALID && src0form != AL_INVALID && src0type != AL_INVALID &&
                src1in != AL_INVALID && src1form != AL_INVALID && src0type != AL_INVALID &&
                trans != AL_INVALID)
        {
            ALsizei idx = (ALsizei)(mod - VECTOR_ITER_BEGIN(zone->mods));
            ALfontsound_setModStagei(sound, context, idx, AL_SOURCE0_INPUT_SOFT, src0in);
            ALfontsound_setModStagei(sound, context, idx, AL_SOURCE0_TYPE_SOFT, src0type);
            ALfontsound_setModStagei(sound, context, idx, AL_SOURCE0_FORM_SOFT, src0form);
            ALfontsound_setModStagei(sound, context, idx, AL_SOURCE1_INPUT_SOFT, src1in);
            ALfontsound_setModStagei(sound, context, idx, AL_SOURCE1_TYPE_SOFT, src1type);
            ALfontsound_setModStagei(sound, context, idx, AL_SOURCE1_FORM_SOFT, src1form);
            ALfontsound_setModStagei(sound, context, idx, AL_AMOUNT_SOFT, mod->mAmount);
            ALfontsound_setModStagei(sound, context, idx, AL_TRANSFORM_OP_SOFT, trans);
            ALfontsound_setModStagei(sound, context, idx, AL_DESTINATION_SOFT, dst);
        }
    }

    gen = VECTOR_ITER_BEGIN(zone->gens);
    gen_end = VECTOR_ITER_END(zone->gens);
    for(;gen != gen_end;gen++)
    {
        ALint value = (ALshort)gen->mAmount;
        if(gen->mGenerator == 0)
            sound->Start += value;
        else if(gen->mGenerator == 1)
            sound->End += value;
        else if(gen->mGenerator == 2)
            sound->LoopStart += value;
        else if(gen->mGenerator == 3)
            sound->LoopEnd += value;
        else if(gen->mGenerator == 4)
            sound->Start += value<<15;
        else if(gen->mGenerator == 12)
            sound->End += value<<15;
        else if(gen->mGenerator == 45)
            sound->LoopStart += value<<15;
        else if(gen->mGenerator == 50)
            sound->LoopEnd += value<<15;
        else if(gen->mGenerator == 43)
        {
            sound->MinKey = mini((value&0xff), 127);
            sound->MaxKey = mini(((value>>8)&0xff), 127);
        }
        else if(gen->mGenerator == 44)
        {
            sound->MinVelocity = mini((value&0xff), 127);
            sound->MaxVelocity = mini(((value>>8)&0xff), 127);
        }
        else
        {
            ALenum param = 0;
            if(gen->mGenerator < 60)
                param = Gen2Param[gen->mGenerator];
            if(param)
            {
                if(param == AL_BASE_KEY_SOFT)
                {
                    if(!(value >= 0 && value <= 127))
                    {
                        if(value != -1)
                            WARN("Invalid overridingRootKey generator value %d\n", value);
                        continue;
                    }
                }
                if(param == AL_FILTER_RESONANCE_SOFT || param == AL_ATTENUATION_SOFT)
                    value = maxi(0, value);
                else if(param == AL_CHORUS_SEND_SOFT || param == AL_REVERB_SEND_SOFT)
                    value = clampi(value, 0, 1000);
                else if(param == AL_LOOP_MODE_SOFT)
                    value = getLoopMode(value);
                ALfontsound_setPropi(sound, context, param, value);
            }
            else
            {
                static ALuint warned[65536/32];
                if(!(warned[gen->mGenerator/32]&(1<<(gen->mGenerator&31))))
                {
                    warned[gen->mGenerator/32] |= 1<<(gen->mGenerator&31);
                    ERR("Unhandled generator %d\n", gen->mGenerator);
                }
            }
        }
    }
}

static void processInstrument(ALfontsound ***sounds, ALsizei *sounds_size, ALCcontext *context, ALbuffer *buffer, InstrumentHeader *inst, const PresetHeader *preset, const Soundfont *sfont, const GenModList *pzone)
{
    const Generator *gen, *gen_end;
    const Modulator *mod, *mod_end;
    const Zone *zone, *zone_end;
    GenModList gzone;
    ALvoid *temp;

    if((inst+1)->mZoneIdx == inst->mZoneIdx)
        ERR("Instrument with no zones!");

    GenModList_Construct(&gzone);
    zone = sfont->ibag + inst->mZoneIdx;
    zone_end = sfont->ibag + (inst+1)->mZoneIdx;
    if(zone_end-zone > 1)
    {
        gen = sfont->igen + zone->mGenIdx;
        gen_end = sfont->igen + (zone+1)->mGenIdx;

        // If no generators, or last generator is not a sample, this is a global zone
        for(;gen != gen_end;gen++)
        {
            if(gen->mGenerator == 53)
                break;
        }

        if(gen == gen_end)
        {
            gen = sfont->igen + zone->mGenIdx;
            gen_end = sfont->igen + (zone+1)->mGenIdx;
            for(;gen != gen_end;gen++)
                GenModList_insertGen(&gzone, gen, AL_FALSE);

            mod = sfont->imod + zone->mModIdx;
            mod_end = sfont->imod + (zone+1)->mModIdx;
            for(;mod != mod_end;mod++)
                GenModList_insertMod(&gzone, mod);

            zone++;
        }
    }

    temp = realloc(*sounds, (zone_end-zone + *sounds_size)*sizeof((*sounds)[0]));
    if(!temp)
    {
        ERR("Failed reallocating fontsound storage to %d elements (from %d)\n",
            (ALsizei)(zone_end-zone) + *sounds_size, *sounds_size);
        return;
    }
    *sounds = temp;
    for(;zone != zone_end;zone++)
    {
        GenModList lzone = GenModList_clone(&gzone);
        mod = sfont->imod + zone->mModIdx;
        mod_end = sfont->imod + (zone+1)->mModIdx;
        for(;mod != mod_end;mod++)
            GenModList_insertMod(&lzone, mod);

        gen = sfont->igen + zone->mGenIdx;
        gen_end = sfont->igen + (zone+1)->mGenIdx;
        for(;gen != gen_end;gen++)
        {
            if(gen->mGenerator == 53)
            {
                const SampleHeader *samp;
                ALfontsound *sound;

                if(gen->mAmount >= sfont->shdr_size-1)
                {
                    ERR("Generator %ld has invalid sample ID (%d of %d)\n",
                        (long)(gen-sfont->igen), gen->mAmount, sfont->shdr_size-1);
                    break;
                }
                samp = &sfont->shdr[gen->mAmount];

                gen = VECTOR_ITER_BEGIN(pzone->gens);
                gen_end = VECTOR_ITER_END(pzone->gens);
                for(;gen != gen_end;gen++)
                    GenModList_accumGen(&lzone, gen);

                mod = VECTOR_ITER_BEGIN(pzone->mods);
                mod_end = VECTOR_ITER_END(pzone->mods);
                for(;mod != mod_end;mod++)
                    GenModList_accumMod(&lzone, mod);

                if(!checkZone(&lzone, preset, inst, samp))
                    break;
                /* Ignore ROM samples for now. */
                if((samp->mSampleType&0x8000))
                    break;

                sound = NewFontsound(context);
                (*sounds)[(*sounds_size)++] = sound;
                ALfontsound_setPropi(sound, context, AL_BUFFER, buffer->id);
                ALfontsound_setPropi(sound, context, AL_SAMPLE_START_SOFT, samp->mStart);
                ALfontsound_setPropi(sound, context, AL_SAMPLE_END_SOFT, samp->mEnd);
                ALfontsound_setPropi(sound, context, AL_SAMPLE_LOOP_START_SOFT, samp->mStartloop);
                ALfontsound_setPropi(sound, context, AL_SAMPLE_LOOP_END_SOFT, samp->mEndloop);
                ALfontsound_setPropi(sound, context, AL_SAMPLE_RATE_SOFT, samp->mSampleRate);
                ALfontsound_setPropi(sound, context, AL_BASE_KEY_SOFT, (samp->mOriginalKey <= 127) ? samp->mOriginalKey : 60);
                ALfontsound_setPropi(sound, context, AL_KEY_CORRECTION_SOFT, samp->mCorrection);
                ALfontsound_setPropi(sound, context, AL_SAMPLE_TYPE_SOFT, getSampleType(samp->mSampleType&0x7fff));
                fillZone(sound, context, &lzone);

                break;
            }
            GenModList_insertGen(&lzone, gen, AL_FALSE);
        }

        GenModList_Destruct(&lzone);
    }

    GenModList_Destruct(&gzone);
}

static size_t printStringChunk(Reader *stream, const RiffHdr *chnk, const char *title)
{
    size_t len = 0;
    if(chnk->mSize == 0 || (chnk->mSize&1))
        ERR("Invalid "FOURCCFMT" size: %d\n", FOURCCARGS(chnk->mCode), chnk->mSize);
    else
    {
        char *str = calloc(1, chnk->mSize+1);
        len = Reader_read(stream, str, chnk->mSize);

        TRACE("%s: %s\n", title, str);
        free(str);
    }
    return len;
}

ALboolean loadSf2(Reader *stream, ALsoundfont *soundfont, ALCcontext *context)
{
    ALsfpreset **presets = NULL;
    ALsizei presets_size = 0;
    ALbuffer *buffer = NULL;
    ALuint ltype;
    Soundfont sfont;
    RiffHdr riff;
    RiffHdr list;
    ALsizei i;

    Soundfont_Construct(&sfont);

    RiffHdr_read(&riff, stream);
    if(riff.mCode != FOURCC('R','I','F','F'))
        ERROR_GOTO(error, "Invalid Format, expected RIFF got '"FOURCCFMT"'\n", FOURCCARGS(riff.mCode));
    if((ltype=read_le32(stream)) != FOURCC('s','f','b','k'))
        ERROR_GOTO(error, "Invalid Format, expected sfbk got '"FOURCCFMT"'\n", FOURCCARGS(ltype));

    if(READERR(stream) != 0)
        ERROR_GOTO(error, "Error reading file header\n");

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('L','I','S','T'))
        ERROR_GOTO(error, "Invalid Format, expected LIST (INFO) got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((ltype=read_le32(stream)) != FOURCC('I','N','F','O'))
        ERROR_GOTO(error, "Invalid Format, expected INFO got '"FOURCCFMT"'\n", FOURCCARGS(ltype));
    list.mSize -= 4;
    while(list.mSize > 0 && !READERR(stream))
    {
        RiffHdr chnk;

        if(list.mSize < 8)
        {
            WARN("Unexpected end of INFO list (%u extra bytes)\n", list.mSize);
            skip(stream, list.mSize);
            list.mSize = 0;
            break;
        }

        RiffHdr_read(&chnk, stream);
        list.mSize -= 8;
        if(list.mSize < chnk.mSize)
        {
            WARN("INFO sub-chunk '"FOURCCFMT"' has %u bytes, but only %u bytes remain\n",
                 FOURCCARGS(chnk.mCode), chnk.mSize, list.mSize);
            skip(stream, list.mSize);
            list.mSize = 0;
            break;
        }
        list.mSize -= chnk.mSize;

        if(chnk.mCode == FOURCC('i','f','i','l'))
        {
            if(chnk.mSize != 4)
                ERR("Invalid ifil chunk size: %d\n", chnk.mSize);
            else
            {
                ALushort major = read_le16(stream);
                ALushort minor = read_le16(stream);
                chnk.mSize -= 4;

                if(major != 2)
                    ERROR_GOTO(error, "Unsupported SF2 format version: %d.%02d\n", major, minor);
                TRACE("SF2 format version: %d.%02d\n", major, minor);

                sfont.ifil = (major<<16) | minor;
            }
        }
        else if(chnk.mCode == FOURCC('i','r','o','m'))
        {
            if(chnk.mSize == 0 || (chnk.mSize&1))
                ERR("Invalid irom size: %d\n", chnk.mSize);
            else
            {
                free(sfont.irom);
                sfont.irom = calloc(1, chnk.mSize+1);
                chnk.mSize -= Reader_read(stream, sfont.irom, chnk.mSize);

                TRACE("SF2 ROM ID: %s\n", sfont.irom);
            }
        }
        else
        {
            static const struct {
                ALuint code;
                char title[16];
            } listinfos[] = {
                { FOURCC('i','s','n','g'), "Engine ID" },
                { FOURCC('I','N','A','M'), "Name" },
                { FOURCC('I','C','R','D'), "Creation Date" },
                { FOURCC('I','E','N','G'), "Creator" },
                { FOURCC('I','P','R','D'), "Product ID" },
                { FOURCC('I','C','O','P'), "Copyright" },
                { FOURCC('I','C','M','T'), "Comment" },
                { FOURCC('I','S','F','T'), "Created With" },
                { 0, "" },
            };

            for(i = 0;listinfos[i].code;i++)
            {
                if(listinfos[i].code == chnk.mCode)
                {
                    chnk.mSize -= printStringChunk(stream, &chnk, listinfos[i].title);
                    break;
                }
            }
            if(!listinfos[i].code)
                TRACE("Skipping INFO sub-chunk '"FOURCCFMT"' (%u bytes)\n", FOURCCARGS(chnk.mCode), chnk.mSize);
        }
        skip(stream, chnk.mSize);
    }

    if(READERR(stream) != 0)
        ERROR_GOTO(error, "Error reading INFO chunk\n");
    if(sfont.ifil == 0)
        ERROR_GOTO(error, "Missing ifil sub-chunk\n");

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('L','I','S','T'))
        ERROR_GOTO(error, "Invalid Format, expected LIST (sdta) got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((ltype=read_le32(stream)) != FOURCC('s','d','t','a'))
        ERROR_GOTO(error, "Invalid Format, expected sdta got '"FOURCCFMT"'\n", FOURCCARGS(ltype));
    list.mSize -= 4;
    {
        ALbyte *ptr;
        RiffHdr smpl;
        ALenum err;

        RiffHdr_read(&smpl, stream);
        if(smpl.mCode != FOURCC('s','m','p','l'))
            ERROR_GOTO(error, "Invalid Format, expected smpl got '"FOURCCFMT"'\n", FOURCCARGS(smpl.mCode));
        list.mSize -= 8;

        if(smpl.mSize > list.mSize)
            ERROR_GOTO(error, "Invalid Format, sample chunk size mismatch\n");
        list.mSize -= smpl.mSize;

        buffer = NewBuffer(context);
        if(!buffer)
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, error);
        /* Sample rate is unimportant, the individual fontsounds will specify it. */
        if((err=LoadData(buffer, 22050, AL_MONO16_SOFT, smpl.mSize/2, UserFmtMono, UserFmtShort, NULL, 1, AL_FALSE)) != AL_NO_ERROR)
            SET_ERROR_AND_GOTO(context, err, error);

        ptr = buffer->data;
        if(IS_LITTLE_ENDIAN)
            smpl.mSize -= Reader_read(stream, ptr, smpl.mSize);
        else
        {
            ALuint total = 0;
            while(total < smpl.mSize && !READERR(stream))
            {
                ALbyte buf[4096];
                ALuint todo = minu(smpl.mSize-total, sizeof(buf));
                ALuint i;

                smpl.mSize -= Reader_read(stream, buf, todo);
                for(i = 0;i < todo;i++)
                    ptr[total+i] = buf[i^1];

                total += todo;
            }
        }

        skip(stream, list.mSize);
    }

    if(READERR(stream) != 0)
        ERROR_GOTO(error, "Error reading sdta chunk\n");

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('L','I','S','T'))
        ERROR_GOTO(error, "Invalid Format, expected LIST (pdta) got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((ltype=read_le32(stream)) != FOURCC('p','d','t','a'))
        ERROR_GOTO(error, "Invalid Format, expected pdta got '"FOURCCFMT"'\n", FOURCCARGS(ltype));

    //
    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('p','h','d','r'))
        ERROR_GOTO(error, "Invalid Format, expected phdr got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((list.mSize%38) != 0 || list.mSize == 0)
        ERROR_GOTO(error, "Invalid Format, bad phdr size: %u\n", list.mSize);
    sfont.phdr_size = list.mSize/38;
    sfont.phdr = calloc(sfont.phdr_size, sizeof(sfont.phdr[0]));
    for(i = 0;i < sfont.phdr_size;i++)
        PresetHeader_read(&sfont.phdr[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('p','b','a','g'))
        ERROR_GOTO(error, "Invalid Format, expected pbag got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((list.mSize%4) != 0 || list.mSize == 0)
        ERROR_GOTO(error, "Invalid Format, bad pbag size: %u\n", list.mSize);
    sfont.pbag_size = list.mSize/4;
    sfont.pbag = calloc(sfont.pbag_size, sizeof(sfont.pbag[0]));
    for(i = 0;i < sfont.pbag_size;i++)
        Zone_read(&sfont.pbag[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('p','m','o','d'))
        ERROR_GOTO(error, "Invalid Format, expected pmod got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((list.mSize%10) != 0 || list.mSize == 0)
        ERROR_GOTO(error, "Invalid Format, bad pmod size: %u\n", list.mSize);
    sfont.pmod_size = list.mSize/10;
    sfont.pmod = calloc(sfont.pmod_size, sizeof(sfont.pmod[0]));
    for(i = 0;i < sfont.pmod_size;i++)
        Modulator_read(&sfont.pmod[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('p','g','e','n'))
        ERROR_GOTO(error, "Invalid Format, expected pgen got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((list.mSize%4) != 0 || list.mSize == 0)
        ERROR_GOTO(error, "Invalid Format, bad pgen size: %u\n", list.mSize);
    sfont.pgen_size = list.mSize/4;
    sfont.pgen = calloc(sfont.pgen_size, sizeof(sfont.pgen[0]));
    for(i = 0;i < sfont.pgen_size;i++)
        Generator_read(&sfont.pgen[i], stream);

    //
    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('i','n','s','t'))
        ERROR_GOTO(error, "Invalid Format, expected inst got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((list.mSize%22) != 0 || list.mSize == 0)
        ERROR_GOTO(error, "Invalid Format, bad inst size: %u\n", list.mSize);
    sfont.inst_size = list.mSize/22;
    sfont.inst = calloc(sfont.inst_size, sizeof(sfont.inst[0]));
    for(i = 0;i < sfont.inst_size;i++)
        InstrumentHeader_read(&sfont.inst[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('i','b','a','g'))
        ERROR_GOTO(error, "Invalid Format, expected ibag got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((list.mSize%4) != 0 || list.mSize == 0)
        ERROR_GOTO(error, "Invalid Format, bad ibag size: %u\n", list.mSize);
    sfont.ibag_size = list.mSize/4;
    sfont.ibag = calloc(sfont.ibag_size, sizeof(sfont.ibag[0]));
    for(i = 0;i < sfont.ibag_size;i++)
        Zone_read(&sfont.ibag[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('i','m','o','d'))
        ERROR_GOTO(error, "Invalid Format, expected imod got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((list.mSize%10) != 0 || list.mSize == 0)
        ERROR_GOTO(error, "Invalid Format, bad imod size: %u\n", list.mSize);
    sfont.imod_size = list.mSize/10;
    sfont.imod = calloc(sfont.imod_size, sizeof(sfont.imod[0]));
    for(i = 0;i < sfont.imod_size;i++)
        Modulator_read(&sfont.imod[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('i','g','e','n'))
        ERROR_GOTO(error, "Invalid Format, expected igen got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((list.mSize%4) != 0 || list.mSize == 0)
        ERROR_GOTO(error, "Invalid Format, bad igen size: %u\n", list.mSize);
    sfont.igen_size = list.mSize/4;
    sfont.igen = calloc(sfont.igen_size, sizeof(sfont.igen[0]));
    for(i = 0;i < sfont.igen_size;i++)
        Generator_read(&sfont.igen[i], stream);

    //
    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('s','h','d','r'))
        ERROR_GOTO(error, "Invalid Format, expected shdr got '"FOURCCFMT"'\n", FOURCCARGS(list.mCode));
    if((list.mSize%46) != 0 || list.mSize == 0)
        ERROR_GOTO(error, "Invalid Format, bad shdr size: %u\n", list.mSize);
    sfont.shdr_size = list.mSize/46;
    sfont.shdr = calloc(sfont.shdr_size, sizeof(sfont.shdr[0]));
    for(i = 0;i < sfont.shdr_size;i++)
        SampleHeader_read(&sfont.shdr[i], stream);

    if(READERR(stream) != 0)
        ERROR_GOTO(error, "Error reading pdta chunk\n");

    if(!ensureFontSanity(&sfont))
        goto error;

    presets = calloc(1, (soundfont->NumPresets+sfont.phdr_size-1)*sizeof(presets[0]));
    if(!presets)
        ERROR_GOTO(error, "Error allocating presets\n");
    memcpy(presets, soundfont->Presets, soundfont->NumPresets*sizeof(presets[0]));
    presets_size = soundfont->NumPresets;

    for(i = 0;i < sfont.phdr_size-1;i++)
    {
        const Generator *gen, *gen_end;
        const Modulator *mod, *mod_end;
        const Zone *zone, *zone_end;
        ALfontsound **sounds = NULL;
        ALsizei sounds_size = 0;
        GenModList gzone;

        if(sfont.phdr[i+1].mZoneIdx == sfont.phdr[i].mZoneIdx)
            continue;

        GenModList_Construct(&gzone);
        zone = sfont.pbag + sfont.phdr[i].mZoneIdx;
        zone_end = sfont.pbag + sfont.phdr[i+1].mZoneIdx;
        if(zone_end-zone > 1)
        {
            gen = sfont.pgen + zone->mGenIdx;
            gen_end = sfont.pgen + (zone+1)->mGenIdx;

            // If no generators, or last generator is not an instrument, this is a global zone
            for(;gen != gen_end;gen++)
            {
                if(gen->mGenerator == 41)
                    break;
            }

            if(gen == gen_end)
            {
                gen = sfont.pgen + zone->mGenIdx;
                gen_end = sfont.pgen + (zone+1)->mGenIdx;
                for(;gen != gen_end;gen++)
                    GenModList_insertGen(&gzone, gen, AL_TRUE);

                mod = sfont.pmod + zone->mModIdx;
                mod_end = sfont.pmod + (zone+1)->mModIdx;
                for(;mod != mod_end;mod++)
                    GenModList_insertMod(&gzone, mod);

                zone++;
            }
        }

        for(;zone != zone_end;zone++)
        {
            GenModList lzone = GenModList_clone(&gzone);

            mod = sfont.pmod + zone->mModIdx;
            mod_end = sfont.pmod + (zone+1)->mModIdx;
            for(;mod != mod_end;mod++)
                GenModList_insertMod(&lzone, mod);

            gen = sfont.pgen + zone->mGenIdx;
            gen_end = sfont.pgen + (zone+1)->mGenIdx;
            for(;gen != gen_end;gen++)
            {
                if(gen->mGenerator == 41)
                {
                    if(gen->mAmount >= sfont.inst_size-1)
                        ERR("Generator %ld has invalid instrument ID (%d of %d)\n",
                            (long)(gen-sfont.pgen), gen->mAmount, sfont.inst_size-1);
                    else
                        processInstrument(
                            &sounds, &sounds_size, context, buffer, &sfont.inst[gen->mAmount],
                            &sfont.phdr[i], &sfont, &lzone
                        );
                    break;
                }
                GenModList_insertGen(&lzone, gen, AL_TRUE);
            }
            GenModList_Destruct(&lzone);
        }

        if(sounds_size > 0)
        {
            ALsizei j;

            presets[presets_size] = NewPreset(context);
            presets[presets_size]->Preset = sfont.phdr[i].mPreset;
            presets[presets_size]->Bank = sfont.phdr[i].mBank;

            for(j = 0;j < sounds_size;j++)
                IncrementRef(&sounds[j]->ref);
            sounds = ExchangePtr((XchgPtr*)&presets[presets_size]->Sounds, sounds);
            ExchangeInt(&presets[presets_size]->NumSounds, sounds_size);
            presets_size++;
        }
        free(sounds);

        GenModList_Destruct(&gzone);
    }

    for(i = soundfont->NumPresets;i < presets_size;i++)
        IncrementRef(&presets[i]->ref);
    presets = ExchangePtr((XchgPtr*)&soundfont->Presets, presets);
    ExchangeInt(&soundfont->NumPresets, presets_size);

    free(presets);

    Soundfont_Destruct(&sfont);
    /* If the buffer ends up unused, delete it. */
    if(ReadRef(&buffer->ref) == 0)
    {
        TRACE("Deleting unused buffer...\n");
        DeleteBuffer(context->Device, buffer);
    }

    return AL_TRUE;

error:
    if(presets)
    {
        ALCdevice *device = context->Device;
        for(i = soundfont->NumPresets;i < presets_size;i++)
            DeletePreset(device, presets[i]);
        free(presets);
    }

    Soundfont_Destruct(&sfont);
    if(buffer)
        DeleteBuffer(context->Device, buffer);

    return AL_FALSE;
}
