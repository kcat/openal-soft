#include "config.h"

#ifdef HAVE_ARM_NEON_H
#include <arm_neon.h>
#endif

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


void MixDirect_Neon(DirectParams *params, const ALfloat *restrict data, ALuint srcchan,
  ALuint OutPos, ALuint BufferSize)
{
    ALfloat (*restrict OutBuffer)[BUFFERSIZE] = params->OutBuffer;
    ALuint Counter = maxu(params->Counter, OutPos) - OutPos;
    ALfloat DrySend, Step;
    float32x4_t gain;
    ALuint c;

    for(c = 0;c < MaxChannels;c++)
    {
        ALuint pos = 0;
        Step = params->Mix.Gains.Step[srcchan][c];
        if(Step != 1.0f && Counter > 0)
        {
            DrySend = params->Mix.Gains.Current[srcchan][c];
            for(;BufferSize-pos > 3 && Counter-pos > 3;pos+=4)
            {
                OutBuffer[c][OutPos+pos  ] += data[pos  ]*DrySend;
                DrySend *= Step;
                OutBuffer[c][OutPos+pos+1] += data[pos+1]*DrySend;
                DrySend *= Step;
                OutBuffer[c][OutPos+pos+2] += data[pos+2]*DrySend;
                DrySend *= Step;
                OutBuffer[c][OutPos+pos+4] += data[pos+3]*DrySend;
                DrySend *= Step;
            }
            if(!(BufferSize-pos > 3))
            {
                for(;pos < BufferSize && pos < Counter;pos++)
                {
                    OutBuffer[c][OutPos+pos] += data[pos]*DrySend;
                    DrySend *= Step;
                }
            }
            params->Mix.Gains.Current[srcchan][c] = DrySend;
        }

        DrySend = params->Mix.Gains.Target[srcchan][c];
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


void MixSend_Neon(SendParams *params, const ALfloat *restrict data,
  ALuint OutPos, ALuint UNUSED(SamplesToDo), ALuint BufferSize)
{
    ALfloat (*restrict OutBuffer)[BUFFERSIZE] = params->OutBuffer;
    ALuint Counter = maxu(params->Counter, OutPos) - OutPos;
    ALfloat WetGain, Step;
    float32x4_t gain;

    {
        ALuint pos = 0;
        Step = params->Gain.Step;
        if(Step != 1.0f && Counter > 0)
        {
            WetGain = params->Gain.Current;
            for(;BufferSize-pos > 3 && Counter-pos > 3;pos+=4)
            {
                OutBuffer[0][OutPos+pos  ] += data[pos  ]*WetGain;
                WetGain *= Step;
                OutBuffer[0][OutPos+pos+1] += data[pos+1]*WetGain;
                WetGain *= Step;
                OutBuffer[0][OutPos+pos+2] += data[pos+2]*WetGain;
                WetGain *= Step;
                OutBuffer[0][OutPos+pos+4] += data[pos+3]*WetGain;
                WetGain *= Step;
            }
            if(!(BufferSize-pos > 3))
            {
                for(;pos < BufferSize && pos < Counter;pos++)
                {
                    OutBuffer[0][OutPos+pos] += data[pos]*WetGain;
                    WetGain *= Step;
                }
            }
            params->Gain.Current = WetGain;
        }

        WetGain = params->Gain.Target;
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
