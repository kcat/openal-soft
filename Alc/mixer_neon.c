#include "config.h"

#include <arm_neon.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"
#include "hrtf.h"


static inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*restrict Values)[2],
                                   const ALuint IrSize,
                                   ALfloat (*restrict Coeffs)[2],
                                   const ALfloat (*restrict CoeffStep)[2],
                                   ALfloat left, ALfloat right)
{
    ALuint c;
    float32x4_t leftright4;
    {
        float32x2_t leftright2 = vdup_n_f32(0.0);
        leftright2 = vset_lane_f32(left, leftright2, 0);
        leftright2 = vset_lane_f32(right, leftright2, 1);
        leftright4 = vcombine_f32(leftright2, leftright2);
    }
    for(c = 0;c < IrSize;c += 2)
    {
        const ALuint o0 = (Offset+c)&HRIR_MASK;
        const ALuint o1 = (o0+1)&HRIR_MASK;
        float32x4_t vals = vcombine_f32(vld1_f32((float32_t*)&Values[o0][0]),
                                        vld1_f32((float32_t*)&Values[o1][0]));
        float32x4_t coefs = vld1q_f32((float32_t*)&Coeffs[c][0]);
        float32x4_t deltas = vld1q_f32(&CoeffStep[c][0]);

        vals = vmlaq_f32(vals, coefs, leftright4);
        coefs = vaddq_f32(coefs, deltas);

        vst1_f32((float32_t*)&Values[o0][0], vget_low_f32(vals));
        vst1_f32((float32_t*)&Values[o1][0], vget_high_f32(vals));
        vst1q_f32(&Coeffs[c][0], coefs);
    }
}

static inline void ApplyCoeffs(ALuint Offset, ALfloat (*restrict Values)[2],
                               const ALuint IrSize,
                               ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right)
{
    ALuint c;
    float32x4_t leftright4;
    {
        float32x2_t leftright2 = vdup_n_f32(0.0);
        leftright2 = vset_lane_f32(left, leftright2, 0);
        leftright2 = vset_lane_f32(right, leftright2, 1);
        leftright4 = vcombine_f32(leftright2, leftright2);
    }
    for(c = 0;c < IrSize;c += 2)
    {
        const ALuint o0 = (Offset+c)&HRIR_MASK;
        const ALuint o1 = (o0+1)&HRIR_MASK;
        float32x4_t vals = vcombine_f32(vld1_f32((float32_t*)&Values[o0][0]),
                                        vld1_f32((float32_t*)&Values[o1][0]));
        float32x4_t coefs = vld1q_f32((float32_t*)&Coeffs[c][0]);

        vals = vmlaq_f32(vals, coefs, leftright4);

        vst1_f32((float32_t*)&Values[o0][0], vget_low_f32(vals));
        vst1_f32((float32_t*)&Values[o1][0], vget_high_f32(vals));
    }
}


#define SUFFIX Neon
#include "mixer_inc.c"
#undef SUFFIX


void MixDirect_Neon(ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *data,
                    MixGains *Gains, ALuint Counter, ALuint OutPos, ALuint BufferSize)
{
    ALfloat DrySend, Step;
    float32x4_t gain;
    ALuint c;

    for(c = 0;c < MaxChannels;c++)
    {
        ALuint pos = 0;
        DrySend = Gains->Current[c];
        Step = Gains->Step[c];
        if(Step != 1.0f && Counter > 0)
        {
            for(;pos < BufferSize && pos < Counter;pos++)
            {
                OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
                DrySend *= Step;
            }
            if(pos == Counter)
                DrySend = Gains->Target[c];
            Gains->Current[c] = DrySend;
            /* Mix until pos is aligned with 4 or the mix is done. */
            for(;pos < BufferSize && (pos&3) != 0;pos++)
                OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
        }

        if(!(DrySend > GAIN_SILENCE_THRESHOLD))
            continue;
        gain = vdupq_n_f32(DrySend);
        for(;BufferSize-pos > 3;pos += 4)
        {
            const float32x4_t val4 = vld1q_f32(&data[pos]);
            float32x4_t dry4 = vld1q_f32(&OutBuffer[c][OutPos+pos]);
            dry4 = vaddq_f32(dry4, vmulq_f32(val4, gain));
            vst1q_f32(&OutBuffer[c][OutPos+pos], dry4);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
    }
}


void MixSend_Neon(ALfloat (*restrict OutBuffer)[BUFFERSIZE], const ALfloat *data,
                  MixGainMono *Gain, ALuint Counter, ALuint OutPos, ALuint BufferSize)
{
    ALfloat WetGain, Step;
    float32x4_t gain;

    {
        ALuint pos = 0;
        WetGain = Gain->Current;
        Step = Gain->Step;
        if(Step != 1.0f && Counter > 0)
        {
            for(;pos < BufferSize && pos < Counter;pos++)
            {
                OutBuffer[0][OutPos+pos] += data[pos]*WetGain;
                WetGain *= Step;
            }
            if(pos == Counter)
                WetGain = Gain->Target;
            Gain->Current = WetGain;
            for(;pos < BufferSize && (pos&3) != 0;pos++)
                OutBuffer[0][OutPos+pos] += data[pos]*WetGain;
        }

        if(!(WetGain > GAIN_SILENCE_THRESHOLD))
            return;
        gain = vdupq_n_f32(WetGain);
        for(;BufferSize-pos > 3;pos += 4)
        {
            const float32x4_t val4 = vld1q_f32(&data[pos]);
            float32x4_t wet4 = vld1q_f32(&OutBuffer[0][OutPos+pos]);
            wet4 = vaddq_f32(wet4, vmulq_f32(val4, gain));
            vst1q_f32(&OutBuffer[0][OutPos+pos], wet4);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[0][OutPos+pos] += data[pos] * WetGain;
    }
}
