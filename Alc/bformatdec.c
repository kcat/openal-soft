
#include "config.h"

#include "bformatdec.h"
#include "ambdec.h"
#include "alu.h"

#include "threads.h"


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
    ALuint i;

    coeff = splitter->coeff*0.5f + 0.5f;
    for(i = 0;i < count;i++)
    {
        x = input[i];

        d = (x - splitter->lp_z1) * coeff;
        x = splitter->lp_z1 + d;
        splitter->lp_z1 = x + d;

        d = (x - splitter->lp_z2) * coeff;
        x = splitter->lp_z2 + d;
        splitter->lp_z2 = x + d;

        lpout[i] = x;
    }

    coeff = splitter->coeff;
    for(i = 0;i < count;i++)
    {
        x = input[i];

        d = x - coeff*splitter->hp_z1;
        x = splitter->hp_z1 + coeff*d;
        splitter->hp_z1 = d;

        hpout[i] = x - lpout[i];
    }
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
static ALfloat CubeEncoder[8][MAX_AMBI_COEFFS];

static alonce_flag encoder_inited = AL_ONCE_FLAG_INIT;

static void init_encoder(void)
{
    CalcXYZCoeffs(-0.577350269f,  0.577350269f, -0.577350269f, CubeEncoder[0]);
    CalcXYZCoeffs( 0.577350269f,  0.577350269f, -0.577350269f, CubeEncoder[1]);
    CalcXYZCoeffs(-0.577350269f,  0.577350269f,  0.577350269f, CubeEncoder[2]);
    CalcXYZCoeffs( 0.577350269f,  0.577350269f,  0.577350269f, CubeEncoder[3]);
    CalcXYZCoeffs(-0.577350269f, -0.577350269f, -0.577350269f, CubeEncoder[4]);
    CalcXYZCoeffs( 0.577350269f, -0.577350269f, -0.577350269f, CubeEncoder[5]);
    CalcXYZCoeffs(-0.577350269f, -0.577350269f,  0.577350269f, CubeEncoder[6]);
    CalcXYZCoeffs( 0.577350269f, -0.577350269f,  0.577350269f, CubeEncoder[7]);

    CalcXYZCoeffs(-0.707106781f,  0.0f, -0.707106781f, SquareEncoder[0]);
    CalcXYZCoeffs( 0.707106781f,  0.0f, -0.707106781f, SquareEncoder[1]);
    CalcXYZCoeffs(-0.707106781f,  0.0f,  0.707106781f, SquareEncoder[2]);
    CalcXYZCoeffs( 0.707106781f,  0.0f,  0.707106781f, SquareEncoder[3]);
}


/* NOTE: Low-frequency (LF) fields and BandSplitter filters are unused with
 * single-band decoding
 */
typedef struct BFormatDec {
    alignas(16) ALfloat MatrixHF[MAX_OUTPUT_CHANNELS][MAX_AMBI_COEFFS];
    alignas(16) ALfloat MatrixLF[MAX_OUTPUT_CHANNELS][MAX_AMBI_COEFFS];

    BandSplitter XOver[MAX_AMBI_COEFFS];

    ALfloat (*Samples)[BUFFERSIZE];
    /* These two alias into Samples */
    ALfloat (*SamplesHF)[BUFFERSIZE];
    ALfloat (*SamplesLF)[BUFFERSIZE];

    struct {
        const ALfloat (*restrict MatrixHF)[MAX_AMBI_COEFFS];
        const ALfloat (*restrict Encoder)[MAX_AMBI_COEFFS];
        ALuint NumChannels;
    } UpSampler;

    ALuint NumChannels;
    ALboolean DualBand;
} BFormatDec;

BFormatDec *bformatdec_alloc()
{
    alcall_once(&encoder_inited, init_encoder);
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
    if(dec->NumChannels > 9) return 3;
    if(dec->NumChannels > 4) return 2;
    if(dec->NumChannels > 1) return 1;
    return 0;
}

void bformatdec_reset(BFormatDec *dec, const AmbDecConf *conf, ALuint chancount, ALuint srate, const ALuint chanmap[MAX_OUTPUT_CHANNELS])
{
    const ALfloat *coeff_scale = UnitScale;
    ALfloat ratio;
    ALuint i;

    al_free(dec->Samples);
    dec->Samples = NULL;
    dec->SamplesHF = NULL;
    dec->SamplesLF = NULL;

    dec->NumChannels = chancount;
    dec->Samples = al_calloc(16, dec->NumChannels * conf->FreqBands *
                                 sizeof(dec->Samples[0]));
    dec->SamplesHF = dec->Samples;
    dec->SamplesLF = dec->SamplesHF + dec->NumChannels;

    if(conf->CoeffScale == ADS_SN3D)
        coeff_scale = SN3D2N3DScale;
    else if(conf->CoeffScale == ADS_FuMa)
        coeff_scale = FuMa2N3DScale;

    if((conf->ChanMask & ~0x831b))
    {
        dec->UpSampler.MatrixHF = CubeMatrixHF;
        dec->UpSampler.Encoder = (const ALfloat(*)[MAX_AMBI_COEFFS])CubeEncoder;
        dec->UpSampler.NumChannels = 8;
    }
    else
    {
        dec->UpSampler.MatrixHF = SquareMatrixHF;
        dec->UpSampler.Encoder = (const ALfloat(*)[MAX_AMBI_COEFFS])SquareEncoder;
        dec->UpSampler.NumChannels = 4;
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

            for(j = 0;j < MAX_AMBI_COEFFS;j++)
            {
                if(j == 0) gain = conf->LFOrderGain[0] / ratio;
                else if(j == 1) gain = conf->LFOrderGain[1] / ratio;
                else if(j == 4) gain = conf->LFOrderGain[2] / ratio;
                else if(j == 9) gain = conf->LFOrderGain[3] / ratio;
                if((conf->ChanMask&(1<<j)))
                    dec->MatrixLF[chan][j] = conf->LFMatrix[i][k++] / coeff_scale[j] * gain;
            }
        }
    }

    memset(dec->MatrixHF, 0, sizeof(dec->MatrixHF));
    for(i = 0;i < conf->NumSpeakers;i++)
    {
        ALuint chan = chanmap[i];
        ALuint j, k = 0;
        ALfloat gain;

        for(j = 0;j < MAX_AMBI_COEFFS;j++)
        {
            if(j == 0) gain = conf->HFOrderGain[0] * ratio;
            else if(j == 1) gain = conf->HFOrderGain[1] * ratio;
            else if(j == 4) gain = conf->HFOrderGain[2] * ratio;
            else if(j == 9) gain = conf->HFOrderGain[3] * ratio;
            if((conf->ChanMask&(1<<j)))
                dec->MatrixHF[chan][j] = conf->HFMatrix[i][k++] / coeff_scale[j] * gain;
        }
    }
}


static void apply_row(ALfloat *out, const ALfloat *mtx, ALfloat (*restrict in)[BUFFERSIZE], ALuint inchans, ALuint todo)
{
    ALuint c, i;

    for(c = 0;c < inchans;c++)
    {
        ALfloat gain = mtx[c];
        if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
            continue;
        for(i = 0;i < todo;i++)
            out[i] += in[c][i] * gain;
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
            apply_row(OutBuffer[chan], dec->MatrixHF[chan], dec->SamplesHF,
                      dec->NumChannels, SamplesToDo);
            apply_row(OutBuffer[chan], dec->MatrixLF[chan], dec->SamplesLF,
                      dec->NumChannels, SamplesToDo);
        }
    }
    else
    {
        for(chan = 0;chan < OutChannels;chan++)
            apply_row(OutBuffer[chan], dec->MatrixHF[chan], InSamples,
                      dec->NumChannels, SamplesToDo);
    }
}


void bformatdec_upSample(struct BFormatDec *dec, ALfloat (*restrict OutBuffer)[BUFFERSIZE], ALfloat (*restrict InSamples)[BUFFERSIZE], ALuint InChannels, ALuint SamplesToDo)
{
    ALuint i, j, k;

    /* This up-sampler is very simplistic. It essentially decodes the first-
     * order content to a square channel array (or cube if height is desired),
     * then encodes those points onto the higher order soundfield.
     */
    for(k = 0;k < dec->UpSampler.NumChannels;k++)
    {
        memset(dec->Samples[0], 0, SamplesToDo*sizeof(ALfloat));
        apply_row(dec->Samples[0], dec->UpSampler.MatrixHF[k], InSamples,
                  InChannels, SamplesToDo);

        for(j = 0;j < dec->NumChannels;j++)
        {
            ALfloat gain = dec->UpSampler.Encoder[k][j];
            if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
                continue;
            for(i = 0;i < SamplesToDo;i++)
                OutBuffer[j][i] += dec->Samples[0][i] * gain;
        }
    }
}
