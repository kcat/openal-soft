
#include "config.h"

#include "bformatdec.h"
#include "ambdec.h"
#include "mixer_defs.h"
#include "alu.h"

#include "threads.h"
#include "almalloc.h"


typedef struct BandSplitter {
    ALfloat coeff;
    ALfloat lp_z1;
    ALfloat lp_z2;
    ALfloat hp_z1;
} BandSplitter;

static void bandsplit_init(BandSplitter *splitter, ALfloat freq_mult)
{
    ALfloat w = freq_mult * F_TAU;
    ALfloat cw = cosf(w);
    if(cw > FLT_EPSILON)
        splitter->coeff = (sinf(w) - 1.0f) / cw;
    else
        splitter->coeff = cw * -0.5f;

    splitter->lp_z1 = 0.0f;
    splitter->lp_z2 = 0.0f;
    splitter->hp_z1 = 0.0f;
}

static void bandsplit_process(BandSplitter *splitter, ALfloat *restrict hpout, ALfloat *restrict lpout,
                              const ALfloat *input, ALuint count)
{
    ALfloat coeff, d, x;
    ALfloat z1, z2;
    ALuint i;

    coeff = splitter->coeff*0.5f + 0.5f;
    z1 = splitter->lp_z1;
    z2 = splitter->lp_z2;
    for(i = 0;i < count;i++)
    {
        x = input[i];

        d = (x - z1) * coeff;
        x = z1 + d;
        z1 = x + d;

        d = (x - z2) * coeff;
        x = z2 + d;
        z2 = x + d;

        lpout[i] = x;
    }
    splitter->lp_z1 = z1;
    splitter->lp_z2 = z2;

    coeff = splitter->coeff;
    z1 = splitter->hp_z1;
    for(i = 0;i < count;i++)
    {
        x = input[i];

        d = x - coeff*z1;
        x = z1 + coeff*d;
        z1 = d;

        hpout[i] = x - lpout[i];
    }
    splitter->hp_z1 = z1;
}


static const ALfloat UnitScale[MAX_AMBI_COEFFS] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};
static const ALfloat SN3D2N3DScale[MAX_AMBI_COEFFS] = {
    1.000000000f, /* ACN  0 (W), sqrt(1) */
    1.732050808f, /* ACN  1 (Y), sqrt(3) */
    1.732050808f, /* ACN  2 (Z), sqrt(3) */
    1.732050808f, /* ACN  3 (X), sqrt(3) */
    2.236067978f, /* ACN  4 (V), sqrt(5) */
    2.236067978f, /* ACN  5 (T), sqrt(5) */
    2.236067978f, /* ACN  6 (R), sqrt(5) */
    2.236067978f, /* ACN  7 (S), sqrt(5) */
    2.236067978f, /* ACN  8 (U), sqrt(5) */
    2.645751311f, /* ACN  9 (Q), sqrt(7) */
    2.645751311f, /* ACN 10 (O), sqrt(7) */
    2.645751311f, /* ACN 11 (M), sqrt(7) */
    2.645751311f, /* ACN 12 (K), sqrt(7) */
    2.645751311f, /* ACN 13 (L), sqrt(7) */
    2.645751311f, /* ACN 14 (N), sqrt(7) */
    2.645751311f, /* ACN 15 (P), sqrt(7) */
};
static const ALfloat FuMa2N3DScale[MAX_AMBI_COEFFS] = {
    1.414213562f, /* ACN  0 (W), sqrt(2) */
    1.732050808f, /* ACN  1 (Y), sqrt(3) */
    1.732050808f, /* ACN  2 (Z), sqrt(3) */
    1.732050808f, /* ACN  3 (X), sqrt(3) */
    1.936491673f, /* ACN  4 (V), sqrt(15)/2 */
    1.936491673f, /* ACN  5 (T), sqrt(15)/2 */
    2.236067978f, /* ACN  6 (R), sqrt(5) */
    1.936491673f, /* ACN  7 (S), sqrt(15)/2 */
    1.936491673f, /* ACN  8 (U), sqrt(15)/2 */
    2.091650066f, /* ACN  9 (Q), sqrt(35/8) */
    1.972026594f, /* ACN 10 (O), sqrt(35)/3 */
    2.231093404f, /* ACN 11 (M), sqrt(224/45) */
    2.645751311f, /* ACN 12 (K), sqrt(7) */
    2.231093404f, /* ACN 13 (L), sqrt(224/45) */
    1.972026594f, /* ACN 14 (N), sqrt(35)/3 */
    2.091650066f, /* ACN 15 (P), sqrt(35/8) */
};


static const ALfloat SquareMatrixHF[4][MAX_AMBI_COEFFS] = {
    { 0.353553f,  0.204094f,  0.0f,  0.204094f },
    { 0.353553f, -0.204094f,  0.0f,  0.204094f },
    { 0.353553f,  0.204094f,  0.0f, -0.204094f },
    { 0.353553f, -0.204094f,  0.0f, -0.204094f },
};
static const ALfloat SquareMatrixLF[4][MAX_AMBI_COEFFS] = {
    { 0.25f,  0.204094f,  0.0f,  0.204094f },
    { 0.25f, -0.204094f,  0.0f,  0.204094f },
    { 0.25f,  0.204094f,  0.0f, -0.204094f },
    { 0.25f, -0.204094f,  0.0f, -0.204094f },
};
static ALfloat SquareEncoder[4][MAX_AMBI_COEFFS];

static const ALfloat CubeMatrixHF[8][MAX_AMBI_COEFFS] = {
    { 0.25f,  0.14425f,  0.14425f,  0.14425f },
    { 0.25f, -0.14425f,  0.14425f,  0.14425f },
    { 0.25f,  0.14425f,  0.14425f, -0.14425f },
    { 0.25f, -0.14425f,  0.14425f, -0.14425f },
    { 0.25f,  0.14425f, -0.14425f,  0.14425f },
    { 0.25f, -0.14425f, -0.14425f,  0.14425f },
    { 0.25f,  0.14425f, -0.14425f, -0.14425f },
    { 0.25f, -0.14425f, -0.14425f, -0.14425f },
};
static const ALfloat CubeMatrixLF[8][MAX_AMBI_COEFFS] = {
    { 0.125f,  0.125f,  0.125f,  0.125f },
    { 0.125f, -0.125f,  0.125f,  0.125f },
    { 0.125f,  0.125f,  0.125f, -0.125f },
    { 0.125f, -0.125f,  0.125f, -0.125f },
    { 0.125f,  0.125f, -0.125f,  0.125f },
    { 0.125f, -0.125f, -0.125f,  0.125f },
    { 0.125f,  0.125f, -0.125f, -0.125f },
    { 0.125f, -0.125f, -0.125f, -0.125f },
};
static ALfloat CubeEncoder[8][MAX_AMBI_COEFFS];


static inline MatrixMixerFunc SelectMixer(void)
{
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return MixRow_SSE;
#endif
#ifdef HAVE_NEON
    if((CPUCapFlags&CPU_CAP_NEON))
        return MixRow_Neon;
#endif
    return MixRow_C;
}

static MatrixMixerFunc MixMatrixRow = MixRow_C;


static alonce_flag bformatdec_inited = AL_ONCE_FLAG_INIT;

static void init_bformatdec(void)
{
    ALuint i, j;

    MixMatrixRow = SelectMixer();

    CalcXYZCoeffs(-0.577350269f,  0.577350269f, -0.577350269f, 0.0f, CubeEncoder[0]);
    CalcXYZCoeffs( 0.577350269f,  0.577350269f, -0.577350269f, 0.0f, CubeEncoder[1]);
    CalcXYZCoeffs(-0.577350269f,  0.577350269f,  0.577350269f, 0.0f, CubeEncoder[2]);
    CalcXYZCoeffs( 0.577350269f,  0.577350269f,  0.577350269f, 0.0f, CubeEncoder[3]);
    CalcXYZCoeffs(-0.577350269f, -0.577350269f, -0.577350269f, 0.0f, CubeEncoder[4]);
    CalcXYZCoeffs( 0.577350269f, -0.577350269f, -0.577350269f, 0.0f, CubeEncoder[5]);
    CalcXYZCoeffs(-0.577350269f, -0.577350269f,  0.577350269f, 0.0f, CubeEncoder[6]);
    CalcXYZCoeffs( 0.577350269f, -0.577350269f,  0.577350269f, 0.0f, CubeEncoder[7]);

    CalcXYZCoeffs(-0.707106781f,  0.0f, -0.707106781f, 0.0f, SquareEncoder[0]);
    CalcXYZCoeffs( 0.707106781f,  0.0f, -0.707106781f, 0.0f, SquareEncoder[1]);
    CalcXYZCoeffs(-0.707106781f,  0.0f,  0.707106781f, 0.0f, SquareEncoder[2]);
    CalcXYZCoeffs( 0.707106781f,  0.0f,  0.707106781f, 0.0f, SquareEncoder[3]);

    for(i = 0;i < 4;i++)
    {
        /* Remove the skipped height-related coefficients for 2D rendering. */
        SquareEncoder[i][2] = SquareEncoder[i][3];
        SquareEncoder[i][3] = SquareEncoder[i][4];
        SquareEncoder[i][4] = SquareEncoder[i][8];
        SquareEncoder[i][5] = SquareEncoder[i][9];
        SquareEncoder[i][6] = SquareEncoder[i][15];
        for(j = 7;j < MAX_AMBI_COEFFS;j++)
            SquareEncoder[i][j] = 0.0f;
    }
}


#define MAX_DELAY_LENGTH 128

/* NOTE: Low-frequency (LF) fields and BandSplitter filters are unused with
 * single-band decoding
 */
typedef struct BFormatDec {
    ALboolean Enabled[MAX_OUTPUT_CHANNELS];

    alignas(16) ALfloat MatrixHF[MAX_OUTPUT_CHANNELS][MAX_AMBI_COEFFS];
    alignas(16) ALfloat MatrixLF[MAX_OUTPUT_CHANNELS][MAX_AMBI_COEFFS];

    BandSplitter XOver[MAX_AMBI_COEFFS];

    ALfloat (*Samples)[BUFFERSIZE];
    /* These two alias into Samples */
    ALfloat (*SamplesHF)[BUFFERSIZE];
    ALfloat (*SamplesLF)[BUFFERSIZE];

    alignas(16) ALfloat ChannelMix[BUFFERSIZE];

    struct {
        alignas(16) ALfloat Buffer[MAX_DELAY_LENGTH];
        ALuint Length; /* Valid range is [0...MAX_DELAY_LENGTH). */
    } Delay[MAX_OUTPUT_CHANNELS];

    struct {
        BandSplitter XOver[4];

        const ALfloat (*restrict MatrixHF)[MAX_AMBI_COEFFS];
        const ALfloat (*restrict MatrixLF)[MAX_AMBI_COEFFS];
        const ALfloat (*restrict Encoder)[MAX_AMBI_COEFFS];
        ALuint NumChannels;
    } UpSampler;

    ALuint NumChannels;
    ALboolean DualBand;
    ALboolean Periphonic;
} BFormatDec;

BFormatDec *bformatdec_alloc()
{
    alcall_once(&bformatdec_inited, init_bformatdec);
    return al_calloc(16, sizeof(BFormatDec));
}

void bformatdec_free(BFormatDec *dec)
{
    if(dec)
    {
        al_free(dec->Samples);
        dec->Samples = NULL;
        dec->SamplesHF = NULL;
        dec->SamplesLF = NULL;

        memset(dec, 0, sizeof(*dec));
        al_free(dec);
    }
}

int bformatdec_getOrder(const struct BFormatDec *dec)
{
    if(dec->Periphonic)
    {
        if(dec->NumChannels > 9) return 3;
        if(dec->NumChannels > 4) return 2;
        if(dec->NumChannels > 1) return 1;
    }
    else
    {
        if(dec->NumChannels > 5) return 3;
        if(dec->NumChannels > 3) return 2;
        if(dec->NumChannels > 1) return 1;
    }
    return 0;
}

void bformatdec_reset(BFormatDec *dec, const AmbDecConf *conf, ALuint chancount, ALuint srate, const ALuint chanmap[MAX_OUTPUT_CHANNELS], int flags)
{
    static const ALuint map2DTo3D[7] = {
        0,  1, 3,  4, 8,  9, 15
    };
    const ALfloat *coeff_scale = UnitScale;
    ALfloat distgain[MAX_OUTPUT_CHANNELS];
    ALfloat maxdist, ratio;
    ALuint i;

    al_free(dec->Samples);
    dec->Samples = NULL;
    dec->SamplesHF = NULL;
    dec->SamplesLF = NULL;

    dec->NumChannels = chancount;
    dec->Samples = al_calloc(16, dec->NumChannels*2 * sizeof(dec->Samples[0]));
    dec->SamplesHF = dec->Samples;
    dec->SamplesLF = dec->SamplesHF + dec->NumChannels;

    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
        dec->Enabled[i] = AL_FALSE;
    for(i = 0;i < conf->NumSpeakers;i++)
        dec->Enabled[chanmap[i]] = AL_TRUE;

    if(conf->CoeffScale == ADS_SN3D)
        coeff_scale = SN3D2N3DScale;
    else if(conf->CoeffScale == ADS_FuMa)
        coeff_scale = FuMa2N3DScale;

    ratio = 400.0f / (ALfloat)srate;
    for(i = 0;i < 4;i++)
        bandsplit_init(&dec->UpSampler.XOver[i], ratio);
    if((conf->ChanMask&AMBI_PERIPHONIC_MASK))
    {
        dec->UpSampler.MatrixHF = CubeMatrixHF;
        dec->UpSampler.MatrixLF = CubeMatrixLF;
        dec->UpSampler.Encoder = (const ALfloat(*)[MAX_AMBI_COEFFS])CubeEncoder;
        dec->UpSampler.NumChannels = 8;
        dec->Periphonic = AL_TRUE;
    }
    else
    {
        dec->UpSampler.MatrixHF = SquareMatrixHF;
        dec->UpSampler.MatrixLF = SquareMatrixLF;
        dec->UpSampler.Encoder = (const ALfloat(*)[MAX_AMBI_COEFFS])SquareEncoder;
        dec->UpSampler.NumChannels = 4;
        dec->Periphonic = AL_FALSE;
    }

    maxdist = 0.0f;
    for(i = 0;i < conf->NumSpeakers;i++)
    {
        maxdist = maxf(maxdist, conf->Speakers[i].Distance);
        distgain[i] = 1.0f;
    }

    memset(dec->Delay, 0, sizeof(dec->Delay));
    if((flags&BFDF_DistanceComp) && maxdist > 0.0f)
    {
        for(i = 0;i < conf->NumSpeakers;i++)
        {
            ALuint chan = chanmap[i];
            ALfloat delay;

            /* Distance compensation only delays in steps of the sample rate.
             * This is a bit less accurate since the delay time falls to the
             * nearest sample time, but it's far simpler as it doesn't have to
             * deal with phase offsets. This means at 48khz, for instance, the
             * distance delay will be in steps of about 7 millimeters.
             */
            delay = floorf((maxdist-conf->Speakers[i].Distance) / SPEEDOFSOUNDMETRESPERSEC *
                           (ALfloat)srate + 0.5f);
            if(delay >= (ALfloat)MAX_DELAY_LENGTH)
                ERR("Delay for speaker \"%s\" exceeds buffer length (%f >= %u)\n",
                    al_string_get_cstr(conf->Speakers[i].Name), delay, MAX_DELAY_LENGTH);

            dec->Delay[chan].Length = (ALuint)clampf(delay, 0.0f, (ALfloat)(MAX_DELAY_LENGTH-1));
            distgain[i] = conf->Speakers[i].Distance / maxdist;
            TRACE("Channel %u \"%s\" distance compensation: %u samples, %f gain\n", chan,
                al_string_get_cstr(conf->Speakers[i].Name), dec->Delay[chan].Length, distgain[i]
            );
        }
    }

    if(conf->FreqBands == 1)
    {
        dec->DualBand = AL_FALSE;
        ratio = 1.0f;
    }
    else
    {
        dec->DualBand = AL_TRUE;

        ratio = conf->XOverFreq / (ALfloat)srate;
        for(i = 0;i < MAX_AMBI_COEFFS;i++)
            bandsplit_init(&dec->XOver[i], ratio);

        ratio = powf(10.0f, conf->XOverRatio / 40.0f);
        memset(dec->MatrixLF, 0, sizeof(dec->MatrixLF));
        for(i = 0;i < conf->NumSpeakers;i++)
        {
            ALuint chan = chanmap[i];
            ALuint j, k = 0;
            ALfloat gain;

            if(!dec->Periphonic)
            {
                for(j = 0;j < 7;j++)
                {
                    ALuint l = map2DTo3D[j];
                    if(j == 0) gain = conf->LFOrderGain[0] / ratio;
                    else if(j == 1) gain = conf->LFOrderGain[1] / ratio;
                    else if(j == 3) gain = conf->LFOrderGain[2] / ratio;
                    else if(j == 5) gain = conf->LFOrderGain[3] / ratio;
                    if((conf->ChanMask&(1<<l)))
                        dec->MatrixLF[chan][j] = conf->LFMatrix[i][k++] / coeff_scale[l] *
                                                 gain * distgain[i];
                }
            }
            else
            {
                for(j = 0;j < MAX_AMBI_COEFFS;j++)
                {
                    if(j == 0) gain = conf->LFOrderGain[0] / ratio;
                    else if(j == 1) gain = conf->LFOrderGain[1] / ratio;
                    else if(j == 4) gain = conf->LFOrderGain[2] / ratio;
                    else if(j == 9) gain = conf->LFOrderGain[3] / ratio;
                    if((conf->ChanMask&(1<<j)))
                        dec->MatrixLF[chan][j] = conf->LFMatrix[i][k++] / coeff_scale[j] *
                                                 gain * distgain[i];
                }
            }
        }
    }

    memset(dec->MatrixHF, 0, sizeof(dec->MatrixHF));
    for(i = 0;i < conf->NumSpeakers;i++)
    {
        ALuint chan = chanmap[i];
        ALuint j, k = 0;
        ALfloat gain;

        if(!dec->Periphonic)
        {
            for(j = 0;j < 7;j++)
            {
                ALuint l = map2DTo3D[j];
                if(j == 0) gain = conf->HFOrderGain[0] * ratio;
                else if(j == 1) gain = conf->HFOrderGain[1] * ratio;
                else if(j == 3) gain = conf->HFOrderGain[2] * ratio;
                else if(j == 5) gain = conf->HFOrderGain[3] * ratio;
                if((conf->ChanMask&(1<<l)))
                    dec->MatrixHF[chan][j] = conf->HFMatrix[i][k++] / coeff_scale[l] *
                                             gain * distgain[i];
            }
        }
        else
        {
            for(j = 0;j < MAX_AMBI_COEFFS;j++)
            {
                if(j == 0) gain = conf->HFOrderGain[0] * ratio;
                else if(j == 1) gain = conf->HFOrderGain[1] * ratio;
                else if(j == 4) gain = conf->HFOrderGain[2] * ratio;
                else if(j == 9) gain = conf->HFOrderGain[3] * ratio;
                if((conf->ChanMask&(1<<j)))
                    dec->MatrixHF[chan][j] = conf->HFMatrix[i][k++] / coeff_scale[j] *
                                             gain * distgain[i];
            }
        }
    }
}


void bformatdec_process(struct BFormatDec *dec, ALfloat (*restrict OutBuffer)[BUFFERSIZE], ALuint OutChannels, ALfloat (*restrict InSamples)[BUFFERSIZE], ALuint SamplesToDo)
{
    ALuint chan, i;

    if(dec->DualBand)
    {
        for(i = 0;i < dec->NumChannels;i++)
            bandsplit_process(&dec->XOver[i], dec->SamplesHF[i], dec->SamplesLF[i],
                              InSamples[i], SamplesToDo);

        for(chan = 0;chan < OutChannels;chan++)
        {
            if(!dec->Enabled[chan])
                continue;

            memset(dec->ChannelMix, 0, SamplesToDo*sizeof(ALfloat));
            MixMatrixRow(dec->ChannelMix, dec->MatrixHF[chan], dec->SamplesHF,
                         dec->NumChannels, SamplesToDo);
            MixMatrixRow(dec->ChannelMix, dec->MatrixLF[chan], dec->SamplesLF,
                         dec->NumChannels, SamplesToDo);

            if(dec->Delay[chan].Length > 0)
            {
                const ALuint base = dec->Delay[chan].Length;
                if(SamplesToDo >= base)
                {
                    for(i = 0;i < base;i++)
                        OutBuffer[chan][i] += dec->Delay[chan].Buffer[i];
                    for(;i < SamplesToDo;i++)
                        OutBuffer[chan][i] += dec->ChannelMix[i-base];
                    memcpy(dec->Delay[chan].Buffer, &dec->ChannelMix[SamplesToDo-base],
                           base*sizeof(ALfloat));
                }
                else
                {
                    for(i = 0;i < SamplesToDo;i++)
                        OutBuffer[chan][i] += dec->Delay[chan].Buffer[i];
                    memmove(dec->Delay[chan].Buffer, dec->Delay[chan].Buffer+SamplesToDo,
                            base - SamplesToDo);
                    memcpy(dec->Delay[chan].Buffer+base-SamplesToDo, dec->ChannelMix,
                           SamplesToDo*sizeof(ALfloat));
                }
            }
            else for(i = 0;i < SamplesToDo;i++)
                OutBuffer[chan][i] += dec->ChannelMix[i];
        }
    }
    else
    {
        for(chan = 0;chan < OutChannels;chan++)
        {
            if(!dec->Enabled[chan])
                continue;

            memset(dec->ChannelMix, 0, SamplesToDo*sizeof(ALfloat));
            MixMatrixRow(dec->ChannelMix, dec->MatrixHF[chan], InSamples,
                         dec->NumChannels, SamplesToDo);

            if(dec->Delay[chan].Length > 0)
            {
                const ALuint base = dec->Delay[chan].Length;
                if(SamplesToDo >= base)
                {
                    for(i = 0;i < base;i++)
                        OutBuffer[chan][i] += dec->Delay[chan].Buffer[i];
                    for(;i < SamplesToDo;i++)
                        OutBuffer[chan][i] += dec->ChannelMix[i-base];
                    memcpy(dec->Delay[chan].Buffer, &dec->ChannelMix[SamplesToDo-base],
                           base*sizeof(ALfloat));
                }
                else
                {
                    for(i = 0;i < SamplesToDo;i++)
                        OutBuffer[chan][i] += dec->Delay[chan].Buffer[i];
                    memmove(dec->Delay[chan].Buffer, dec->Delay[chan].Buffer+SamplesToDo,
                            base - SamplesToDo);
                    memcpy(dec->Delay[chan].Buffer+base-SamplesToDo, dec->ChannelMix,
                           SamplesToDo*sizeof(ALfloat));
                }
            }
            else for(i = 0;i < SamplesToDo;i++)
                OutBuffer[chan][i] += dec->ChannelMix[i];
        }
    }
}


void bformatdec_upSample(struct BFormatDec *dec, ALfloat (*restrict OutBuffer)[BUFFERSIZE], ALfloat (*restrict InSamples)[BUFFERSIZE], ALuint InChannels, ALuint SamplesToDo)
{
    ALuint i, j, k;

    /* First, split the first-order components into low and high frequency
     * bands. This assumes SamplesHF and SamplesLF have enough space for first-
     * order content (to which, this up-sampler is only used with second-order
     * or higher decoding, so it will).
     */
    for(i = 0;i < InChannels;i++)
        bandsplit_process(&dec->UpSampler.XOver[i], dec->SamplesHF[i], dec->SamplesLF[i],
                          InSamples[i], SamplesToDo);

    /* This up-sampler is very simplistic. It essentially decodes the first-
     * order content to a square channel array (or cube if height is desired),
     * then encodes those points onto the higher order soundfield.
     */
    for(k = 0;k < dec->UpSampler.NumChannels;k++)
    {
        memset(dec->ChannelMix, 0, SamplesToDo*sizeof(ALfloat));
        MixMatrixRow(dec->ChannelMix, dec->UpSampler.MatrixHF[k], dec->SamplesHF,
                     InChannels, SamplesToDo);
        MixMatrixRow(dec->ChannelMix, dec->UpSampler.MatrixLF[k], dec->SamplesLF,
                     InChannels, SamplesToDo);

        for(j = 0;j < dec->NumChannels;j++)
        {
            ALfloat gain = dec->UpSampler.Encoder[k][j];
            if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
                continue;
            for(i = 0;i < SamplesToDo;i++)
                OutBuffer[j][i] += dec->ChannelMix[i] * gain;
        }
    }
}
