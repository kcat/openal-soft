
#include "config.h"

#include "sample_cvt.h"

#include "AL/al.h"
#include "alu.h"
#include "alBuffer.h"


namespace {

/* IMA ADPCM Stepsize table */
constexpr int IMAStep_size[89] = {
       7,    8,    9,   10,   11,   12,   13,   14,   16,   17,   19,
      21,   23,   25,   28,   31,   34,   37,   41,   45,   50,   55,
      60,   66,   73,   80,   88,   97,  107,  118,  130,  143,  157,
     173,  190,  209,  230,  253,  279,  307,  337,  371,  408,  449,
     494,  544,  598,  658,  724,  796,  876,  963, 1060, 1166, 1282,
    1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,10442,
   11487,12635,13899,15289,16818,18500,20350,22358,24633,27086,29794,
   32767
};

/* IMA4 ADPCM Codeword decode table */
constexpr int IMA4Codeword[16] = {
    1, 3, 5, 7, 9, 11, 13, 15,
   -1,-3,-5,-7,-9,-11,-13,-15,
};

/* IMA4 ADPCM Step index adjust decode table */
constexpr int IMA4Index_adjust[16] = {
   -1,-1,-1,-1, 2, 4, 6, 8,
   -1,-1,-1,-1, 2, 4, 6, 8
};


/* MSADPCM Adaption table */
constexpr int MSADPCMAdaption[16] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};

/* MSADPCM Adaption Coefficient tables */
constexpr int MSADPCMAdaptionCoeff[7][2] = {
    { 256,    0 },
    { 512, -256 },
    {   0,    0 },
    { 192,   64 },
    { 240,    0 },
    { 460, -208 },
    { 392, -232 }
};

void DecodeIMA4Block(ALshort *dst, const ALubyte *src, ALint numchans, ALsizei align)
{
    ALint sample[MAX_INPUT_CHANNELS]{};
    ALint index[MAX_INPUT_CHANNELS]{};
    ALuint code[MAX_INPUT_CHANNELS]{};

    for(int c{0};c < numchans;c++)
    {
        sample[c]  = *(src++);
        sample[c] |= *(src++) << 8;
        sample[c]  = (sample[c]^0x8000) - 32768;
        index[c]  = *(src++);
        index[c] |= *(src++) << 8;
        index[c]  = (index[c]^0x8000) - 32768;

        index[c] = clampi(index[c], 0, 88);

        dst[c] = sample[c];
    }

    for(int i{1};i < align;i++)
    {
        if((i&7) == 1)
        {
            for(int c{0};c < numchans;c++)
            {
                code[c]  = *(src++);
                code[c] |= *(src++) << 8;
                code[c] |= *(src++) << 16;
                code[c] |= *(src++) << 24;
            }
        }

        for(int c{0};c < numchans;c++)
        {
            int nibble = code[c]&0xf;
            code[c] >>= 4;

            sample[c] += IMA4Codeword[nibble] * IMAStep_size[index[c]] / 8;
            sample[c] = clampi(sample[c], -32768, 32767);

            index[c] += IMA4Index_adjust[nibble];
            index[c] = clampi(index[c], 0, 88);

            *(dst++) = sample[c];
        }
    }
}

void DecodeMSADPCMBlock(ALshort *dst, const ALubyte *src, ALint numchans, ALsizei align)
{
    ALubyte blockpred[MAX_INPUT_CHANNELS]{};
    ALint delta[MAX_INPUT_CHANNELS]{};
    ALshort samples[MAX_INPUT_CHANNELS][2]{};

    for(int c{0};c < numchans;c++)
    {
        blockpred[c] = *(src++);
        blockpred[c] = minu(blockpred[c], 6);
    }
    for(int c{0};c < numchans;c++)
    {
        delta[c]  = *(src++);
        delta[c] |= *(src++) << 8;
        delta[c]  = (delta[c]^0x8000) - 32768;
    }
    for(int c{0};c < numchans;c++)
    {
        samples[c][0]  = *(src++);
        samples[c][0] |= *(src++) << 8;
        samples[c][0]  = (samples[c][0]^0x8000) - 32768;
    }
    for(int c{0};c < numchans;c++)
    {
        samples[c][1]  = *(src++);
        samples[c][1] |= *(src++) << 8;
        samples[c][1]  = (samples[c][1]^0x8000) - 0x8000;
    }

    /* Second sample is written first. */
    for(int c{0};c < numchans;c++)
        *(dst++) = samples[c][1];
    for(int c{0};c < numchans;c++)
        *(dst++) = samples[c][0];

    for(int i{2};i < align;i++)
    {
        for(int c{0};c < numchans;c++)
        {
            const ALint num{(i*numchans) + c};
            ALint nibble, pred;

            /* Read the nibble (first is in the upper bits). */
            if(!(num&1))
                nibble = (*src>>4)&0x0f;
            else
                nibble = (*(src++))&0x0f;

            pred  = (samples[c][0]*MSADPCMAdaptionCoeff[blockpred[c]][0] +
                     samples[c][1]*MSADPCMAdaptionCoeff[blockpred[c]][1]) / 256;
            pred += ((nibble^0x08) - 0x08) * delta[c];
            pred  = clampi(pred, -32768, 32767);

            samples[c][1] = samples[c][0];
            samples[c][0] = pred;

            delta[c] = (MSADPCMAdaption[nibble] * delta[c]) / 256;
            delta[c] = maxi(16, delta[c]);

            *(dst++) = pred;
        }
    }
}

} // namespace

void Convert_ALshort_ALima4(ALshort *dst, const ALubyte *src, ALsizei numchans, ALsizei len,
                            ALsizei align)
{
    const ALsizei byte_align{((align-1)/2 + 4) * numchans};

    len /= align;
    while(len--)
    {
        DecodeIMA4Block(dst, src, numchans, align);
        src += byte_align;
        dst += align*numchans;
    }
}

void Convert_ALshort_ALmsadpcm(ALshort *dst, const ALubyte *src, ALsizei numchans, ALsizei len,
                               ALsizei align)
{
    const ALsizei byte_align{((align-2)/2 + 7) * numchans};

    len /= align;
    while(len--)
    {
        DecodeMSADPCMBlock(dst, src, numchans, align);
        src += byte_align;
        dst += align*numchans;
    }
}
