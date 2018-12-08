
#include "config.h"

#include <cmath>
#include <array>
#include <vector>
#include <numeric>
#include <algorithm>
#include <functional>

#include "bformatdec.h"
#include "ambdec.h"
#include "filters/splitter.h"
#include "alu.h"

#include "threads.h"
#include "almalloc.h"


/* NOTE: These are scale factors as applied to Ambisonics content. Decoder
 * coefficients should be divided by these values to get proper N3D scalings.
 */
const ALfloat N3D2N3DScale[MAX_AMBI_COEFFS] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};
const ALfloat SN3D2N3DScale[MAX_AMBI_COEFFS] = {
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
const ALfloat FuMa2N3DScale[MAX_AMBI_COEFFS] = {
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


namespace {

#define HF_BAND 0
#define LF_BAND 1
static_assert(BFormatDec::sNumBands == 2, "Unexpected BFormatDec::sNumBands");
static_assert(AmbiUpsampler::sNumBands == 2, "Unexpected AmbiUpsampler::sNumBands");

/* These points are in AL coordinates! */
constexpr ALfloat Ambi3DPoints[8][3] = {
    { -0.577350269f,  0.577350269f, -0.577350269f },
    {  0.577350269f,  0.577350269f, -0.577350269f },
    { -0.577350269f,  0.577350269f,  0.577350269f },
    {  0.577350269f,  0.577350269f,  0.577350269f },
    { -0.577350269f, -0.577350269f, -0.577350269f },
    {  0.577350269f, -0.577350269f, -0.577350269f },
    { -0.577350269f, -0.577350269f,  0.577350269f },
    {  0.577350269f, -0.577350269f,  0.577350269f },
};
constexpr ALfloat Ambi3DDecoder[8][MAX_AMBI_COEFFS] = {
    { 0.125f,  0.125f,  0.125f,  0.125f },
    { 0.125f, -0.125f,  0.125f,  0.125f },
    { 0.125f,  0.125f,  0.125f, -0.125f },
    { 0.125f, -0.125f,  0.125f, -0.125f },
    { 0.125f,  0.125f, -0.125f,  0.125f },
    { 0.125f, -0.125f, -0.125f,  0.125f },
    { 0.125f,  0.125f, -0.125f, -0.125f },
    { 0.125f, -0.125f, -0.125f, -0.125f },
};
constexpr ALfloat Ambi3DDecoderHFScale[MAX_AMBI_COEFFS] = {
    2.0f,
    1.15470054f, 1.15470054f, 1.15470054f
};


#define INVALID_UPSAMPLE_INDEX INT_MAX
ALsizei GetACNIndex(const BFChannelConfig *chans, ALsizei numchans, ALsizei acn)
{
    ALsizei i;
    for(i = 0;i < numchans;i++)
    {
        if(chans[i].Index == acn)
            return i;
    }
    return INVALID_UPSAMPLE_INDEX;
}
#define GetChannelForACN(b, a) GetACNIndex((b).Ambi.Map, (b).NumChannels, (a))

} // namespace


void BFormatDec::reset(const AmbDecConf *conf, ALsizei chancount, ALuint srate, const ALsizei (&chanmap)[MAX_OUTPUT_CHANNELS])
{
    static constexpr ALsizei map2DTo3D[MAX_AMBI2D_COEFFS] = {
        0,  1, 3,  4, 8,  9, 15
    };
    const ALfloat *coeff_scale = N3D2N3DScale;

    mSamples.clear();
    mSamplesHF = nullptr;
    mSamplesLF = nullptr;

    mNumChannels = chancount;
    mSamples.resize(mNumChannels * 2);
    mSamplesHF = mSamples.data();
    mSamplesLF = mSamplesHF + mNumChannels;

    mEnabled = std::accumulate(std::begin(chanmap), std::begin(chanmap)+conf->NumSpeakers, 0u,
        [](ALuint mask, const ALsizei &chan) noexcept -> ALuint
        { return mask | (1 << chan); }
    );

    if(conf->CoeffScale == AmbDecScale::SN3D)
        coeff_scale = SN3D2N3DScale;
    else if(conf->CoeffScale == AmbDecScale::FuMa)
        coeff_scale = FuMa2N3DScale;

    mUpSampler[0].XOver.init(400.0f / (float)srate);
    std::fill(std::begin(mUpSampler[0].Gains), std::end(mUpSampler[0].Gains), 0.0f);
    std::fill(std::begin(mUpSampler)+1, std::end(mUpSampler), mUpSampler[0]);

    const bool periphonic{(conf->ChanMask&AMBI_PERIPHONIC_MASK) != 0};
    if(periphonic)
    {
        mUpSampler[0].Gains[HF_BAND] = (conf->ChanMask > 0x1ff) ? W_SCALE_3H3P :
                                       (conf->ChanMask > 0xf) ? W_SCALE_2H2P : 1.0f;
        mUpSampler[0].Gains[LF_BAND] = 1.0f;
        for(ALsizei i{1};i < 4;i++)
        {
            mUpSampler[i].Gains[HF_BAND] = (conf->ChanMask > 0x1ff) ? XYZ_SCALE_3H3P :
                                           (conf->ChanMask > 0xf) ? XYZ_SCALE_2H2P : 1.0f;
            mUpSampler[i].Gains[LF_BAND] = 1.0f;
        }
    }
    else
    {
        mUpSampler[0].Gains[HF_BAND] = (conf->ChanMask > 0x1ff) ? W_SCALE_3H0P :
                                       (conf->ChanMask > 0xf) ? W_SCALE_2H0P : 1.0f;
        mUpSampler[0].Gains[LF_BAND] = 1.0f;
        for(ALsizei i{1};i < 3;i++)
        {
            mUpSampler[i].Gains[HF_BAND] = (conf->ChanMask > 0x1ff) ? XYZ_SCALE_3H0P :
                                           (conf->ChanMask > 0xf) ? XYZ_SCALE_2H0P : 1.0f;
            mUpSampler[i].Gains[LF_BAND] = 1.0f;
        }
        mUpSampler[3].Gains[HF_BAND] = 0.0f;
        mUpSampler[3].Gains[LF_BAND] = 0.0f;
    }

    mMatrix = MatrixU{};
    if(conf->FreqBands == 1)
    {
        mDualBand = AL_FALSE;
        for(ALsizei i{0};i < conf->NumSpeakers;i++)
        {
            ALsizei chan = chanmap[i];
            ALfloat gain;
            ALsizei j, k;

            if(!periphonic)
            {
                for(j = 0,k = 0;j < MAX_AMBI2D_COEFFS;j++)
                {
                    ALsizei l = map2DTo3D[j];
                    if(j == 0) gain = conf->HFOrderGain[0];
                    else if(j == 1) gain = conf->HFOrderGain[1];
                    else if(j == 3) gain = conf->HFOrderGain[2];
                    else if(j == 5) gain = conf->HFOrderGain[3];
                    if((conf->ChanMask&(1<<l)))
                        mMatrix.Single[chan][j] = conf->HFMatrix[i][k++] / coeff_scale[l] * gain;
                }
            }
            else
            {
                for(j = 0,k = 0;j < MAX_AMBI_COEFFS;j++)
                {
                    if(j == 0) gain = conf->HFOrderGain[0];
                    else if(j == 1) gain = conf->HFOrderGain[1];
                    else if(j == 4) gain = conf->HFOrderGain[2];
                    else if(j == 9) gain = conf->HFOrderGain[3];
                    if((conf->ChanMask&(1<<j)))
                        mMatrix.Single[chan][j] = conf->HFMatrix[i][k++] / coeff_scale[j] * gain;
                }
            }
        }
    }
    else
    {
        mDualBand = AL_TRUE;

        mXOver[0].init(conf->XOverFreq / (float)srate);
        std::fill(std::begin(mXOver)+1, std::end(mXOver), mXOver[0]);

        float ratio{std::pow(10.0f, conf->XOverRatio / 40.0f)};
        for(ALsizei i{0};i < conf->NumSpeakers;i++)
        {
            ALsizei chan = chanmap[i];

            ALfloat gain{};
            if(!periphonic)
            {
                for(ALsizei j{0},k{0};j < MAX_AMBI2D_COEFFS;j++)
                {
                    ALsizei l = map2DTo3D[j];
                    if(j == 0) gain = conf->HFOrderGain[0] * ratio;
                    else if(j == 1) gain = conf->HFOrderGain[1] * ratio;
                    else if(j == 3) gain = conf->HFOrderGain[2] * ratio;
                    else if(j == 5) gain = conf->HFOrderGain[3] * ratio;
                    if((conf->ChanMask&(1<<l)))
                        mMatrix.Dual[chan][HF_BAND][j] = conf->HFMatrix[i][k++] / coeff_scale[l] *
                                                         gain;
                }
                for(ALsizei j{0},k{0};j < MAX_AMBI2D_COEFFS;j++)
                {
                    ALsizei l = map2DTo3D[j];
                    if(j == 0) gain = conf->LFOrderGain[0] / ratio;
                    else if(j == 1) gain = conf->LFOrderGain[1] / ratio;
                    else if(j == 3) gain = conf->LFOrderGain[2] / ratio;
                    else if(j == 5) gain = conf->LFOrderGain[3] / ratio;
                    if((conf->ChanMask&(1<<l)))
                        mMatrix.Dual[chan][LF_BAND][j] = conf->LFMatrix[i][k++] / coeff_scale[l] *
                                                         gain;
                }
            }
            else
            {
                for(ALsizei j{0},k{0};j < MAX_AMBI_COEFFS;j++)
                {
                    if(j == 0) gain = conf->HFOrderGain[0] * ratio;
                    else if(j == 1) gain = conf->HFOrderGain[1] * ratio;
                    else if(j == 4) gain = conf->HFOrderGain[2] * ratio;
                    else if(j == 9) gain = conf->HFOrderGain[3] * ratio;
                    if((conf->ChanMask&(1<<j)))
                        mMatrix.Dual[chan][HF_BAND][j] = conf->HFMatrix[i][k++] / coeff_scale[j] *
                                                         gain;
                }
                for(ALsizei j{0},k{0};j < MAX_AMBI_COEFFS;j++)
                {
                    if(j == 0) gain = conf->LFOrderGain[0] / ratio;
                    else if(j == 1) gain = conf->LFOrderGain[1] / ratio;
                    else if(j == 4) gain = conf->LFOrderGain[2] / ratio;
                    else if(j == 9) gain = conf->LFOrderGain[3] / ratio;
                    if((conf->ChanMask&(1<<j)))
                        mMatrix.Dual[chan][LF_BAND][j] = conf->LFMatrix[i][k++] / coeff_scale[j] *
                                                         gain;
                }
            }
        }
    }
}

void BFormatDec::process(ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], const ALsizei OutChannels, const ALfloat (*RESTRICT InSamples)[BUFFERSIZE], const ALsizei SamplesToDo)
{
    ASSUME(OutChannels > 0);
    ASSUME(SamplesToDo > 0);

    ALsizei chan, i;
    if(mDualBand)
    {
        for(i = 0;i < mNumChannels;i++)
            mXOver[i].process(mSamplesHF[i].data(), mSamplesLF[i].data(), InSamples[i],
                              SamplesToDo);

        for(chan = 0;chan < OutChannels;chan++)
        {
            if(UNLIKELY(!(mEnabled&(1<<chan))))
                continue;

            std::fill(std::begin(mChannelMix), std::begin(mChannelMix)+SamplesToDo, 0.0f);
            MixRowSamples(mChannelMix, mMatrix.Dual[chan][HF_BAND],
                &reinterpret_cast<ALfloat(&)[BUFFERSIZE]>(mSamplesHF[0]),
                mNumChannels, 0, SamplesToDo
            );
            MixRowSamples(mChannelMix, mMatrix.Dual[chan][LF_BAND],
                &reinterpret_cast<ALfloat(&)[BUFFERSIZE]>(mSamplesLF[0]),
                mNumChannels, 0, SamplesToDo
            );

            std::transform(std::begin(mChannelMix), std::begin(mChannelMix)+SamplesToDo,
                OutBuffer[chan], OutBuffer[chan], std::plus<float>());
        }
    }
    else
    {
        for(chan = 0;chan < OutChannels;chan++)
        {
            if(UNLIKELY(!(mEnabled&(1<<chan))))
                continue;

            std::fill(std::begin(mChannelMix), std::begin(mChannelMix)+SamplesToDo, 0.0f);
            MixRowSamples(mChannelMix, mMatrix.Single[chan], InSamples,
                          mNumChannels, 0, SamplesToDo);

            std::transform(std::begin(mChannelMix), std::begin(mChannelMix)+SamplesToDo,
                OutBuffer[chan], OutBuffer[chan], std::plus<float>());
        }
    }
}

void BFormatDec::upSample(ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], const ALfloat (*RESTRICT InSamples)[BUFFERSIZE], const ALsizei InChannels, const ALsizei SamplesToDo)
{
    ASSUME(InChannels > 0);
    ASSUME(SamplesToDo > 0);

    /* This up-sampler leverages the differences observed in dual-band second-
     * and third-order decoder matrices compared to first-order. For the same
     * output channel configuration, the low-frequency matrix has identical
     * coefficients in the shared input channels, while the high-frequency
     * matrix has extra scalars applied to the W channel and X/Y/Z channels.
     * Mixing the first-order content into the higher-order stream with the
     * appropriate counter-scales applied to the HF response results in the
     * subsequent higher-order decode generating the same response as a first-
     * order decode.
     */
    for(ALsizei i{0};i < InChannels;i++)
    {
        /* First, split the first-order components into low and high frequency
         * bands.
         */
        mUpSampler[i].XOver.process(mSamples[HF_BAND].data(), mSamples[LF_BAND].data(),
            InSamples[i], SamplesToDo);

        /* Now write each band to the output. */
        MixRowSamples(OutBuffer[i], mUpSampler[i].Gains,
            &reinterpret_cast<ALfloat(&)[BUFFERSIZE]>(mSamples[0]),
            sNumBands, 0, SamplesToDo);
    }
}


void AmbiUpsampler::reset(const ALCdevice *device, const ALfloat w_scale, const ALfloat xyz_scale)
{
    using namespace std::placeholders;

    mXOver[0].init(400.0f / (float)device->Frequency);
    std::fill(std::begin(mXOver)+1, std::end(mXOver), mXOver[0]);

    mGains.fill({});
    if(device->Dry.CoeffCount > 0)
    {
        ALfloat encgains[8][MAX_OUTPUT_CHANNELS];
        for(size_t k{0u};k < COUNTOF(Ambi3DPoints);k++)
        {
            ALfloat coeffs[MAX_AMBI_COEFFS] = { 0.0f };
            CalcDirectionCoeffs(Ambi3DPoints[k], 0.0f, coeffs);
            ComputePanGains(&device->Dry, coeffs, 1.0f, encgains[k]);
        }

        /* Combine the matrices that do the in->virt and virt->out conversions
         * so we get a single in->out conversion. NOTE: the Encoder matrix
         * (encgains) and output are transposed, so the input channels line up
         * with the rows and the output channels line up with the columns.
         */
        for(ALsizei i{0};i < 4;i++)
        {
            for(ALsizei j{0};j < device->Dry.NumChannels;j++)
            {
                ALdouble gain = 0.0;
                for(size_t k{0u};k < COUNTOF(Ambi3DDecoder);k++)
                    gain += (ALdouble)Ambi3DDecoder[k][i] * encgains[k][j];
                mGains[i][j][HF_BAND] = (ALfloat)(gain * Ambi3DDecoderHFScale[i]);
                mGains[i][j][LF_BAND] = (ALfloat)gain;
            }
        }
    }
    else
    {
        for(ALsizei i{0};i < 4;i++)
        {
            ALsizei index = GetChannelForACN(device->Dry, i);
            if(index != INVALID_UPSAMPLE_INDEX)
            {
                ALfloat scale = device->Dry.Ambi.Map[index].Scale;
                mGains[i][index][HF_BAND] = scale * ((i==0) ? w_scale : xyz_scale);
                mGains[i][index][LF_BAND] = scale;
            }
        }
    }
}

void AmbiUpsampler::process(ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], const ALsizei OutChannels, const ALfloat (*RESTRICT InSamples)[BUFFERSIZE], const ALsizei SamplesToDo)
{
    ASSUME(OutChannels > 0);
    ASSUME(SamplesToDo > 0);

    for(ALsizei i{0};i < 4;i++)
    {
        mXOver[i].process(mSamples[HF_BAND], mSamples[LF_BAND], InSamples[i], SamplesToDo);

        for(ALsizei j{0};j < OutChannels;j++)
            MixRowSamples(OutBuffer[j], mGains[i][j].data(), mSamples, sNumBands, 0, SamplesToDo);
    }
}
