#include "config.h"

#ifdef HAVE_ARM_NEON_H
#include <arm_neon.h>
#endif

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"


static inline void ApplyCoeffsStep(const ALuint IrSize,
                                   ALfloat (*restrict Coeffs)[2],
                                   const ALfloat (*restrict CoeffStep)[2])
{
    float32x4_t coeffs, deltas;
    ALuint c;

    for(c = 0;c < IrSize;c += 2)
    {
        coeffs = vld1q_f32(&Coeffs[c][0]);
        deltas = vld1q_f32(&CoeffStep[c][0]);
        coeffs = vaddq_f32(coeffs, deltas);
        vst1q_f32(&Coeffs[c][0], coeffs);
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


void MixDirect_Neon(const DirectParams *params, const ALfloat *restrict data, ALuint srcchan,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    ALfloat (*restrict OutBuffer)[BUFFERSIZE] = params->OutBuffer;
    ALfloat *restrict ClickRemoval = params->ClickRemoval;
    ALfloat *restrict PendingClicks = params->PendingClicks;
    ALfloat DrySend;
    float32x4_t gain;
    ALuint pos;
    ALuint c;

    for(c = 0;c < MaxChannels;c++)
    {
        DrySend = params->Gains[srcchan][c];
        if(!(DrySend > GAIN_SILENCE_THRESHOLD))
            continue;

        if(OutPos == 0)
            ClickRemoval[c] -= data[0]*DrySend;

        gain = vdupq_n_f32(DrySend);
        for(pos = 0;BufferSize-pos > 3;pos += 4)
        {
            const float32x4_t val4 = vld1q_f32(&data[pos]);
            float32x4_t dry4 = vld1q_f32(&OutBuffer[c][OutPos+pos]);
            dry4 = vaddq_f32(dry4, vmulq_f32(val4, gain));
            vst1q_f32(&OutBuffer[c][OutPos+pos], dry4);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*DrySend;

        if(OutPos+pos == SamplesToDo)
            PendingClicks[c] += data[pos]*DrySend;
    }
}


void MixSend_Neon(const SendParams *params, const ALfloat *restrict data,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    ALfloat (*restrict OutBuffer)[BUFFERSIZE] = params->OutBuffer;
    ALfloat *restrict ClickRemoval = params->ClickRemoval;
    ALfloat *restrict PendingClicks = params->PendingClicks;
    ALfloat WetGain;
    float32x4_t gain;
    ALuint pos;

    WetGain = params->Gain;
    if(!(WetGain > GAIN_SILENCE_THRESHOLD))
        return;

    if(OutPos == 0)
        ClickRemoval[0] -= data[0] * WetGain;

    gain = vdupq_n_f32(WetGain);
    for(pos = 0;BufferSize-pos > 3;pos += 4)
    {
        const float32x4_t val4 = vld1q_f32(&data[pos]);
        float32x4_t wet4 = vld1q_f32(&OutBuffer[0][OutPos+pos]);
        wet4 = vaddq_f32(wet4, vmulq_f32(val4, gain));
        vst1q_f32(&OutBuffer[0][OutPos+pos], wet4);
    }
    for(;pos < BufferSize;pos++)
        OutBuffer[0][OutPos+pos] += data[pos] * WetGain;

    if(OutPos+pos == SamplesToDo)
        PendingClicks[0] += data[pos] * WetGain;
}
