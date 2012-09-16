#include "config.h"

#include "alMain.h"
#include "alu.h"
#include "alSource.h"
#include "alAuxEffectSlot.h"


static __inline ALfloat point32(const ALfloat *vals, ALint step, ALint frac)
{ return vals[0]; (void)step; (void)frac; }
static __inline ALfloat lerp32(const ALfloat *vals, ALint step, ALint frac)
{ return lerp(vals[0], vals[step], frac * (1.0f/FRACTIONONE)); }
static __inline ALfloat cubic32(const ALfloat *vals, ALint step, ALint frac)
{ return cubic(vals[-step], vals[0], vals[step], vals[step+step],
               frac * (1.0f/FRACTIONONE)); }

#define DECL_TEMPLATE(Sampler)                                                \
void Resample_##Sampler##_C(const ALfloat *data, ALuint frac,                 \
  ALuint increment, ALuint NumChannels, ALfloat *RESTRICT OutBuffer,          \
  ALuint BufferSize)                                                          \
{                                                                             \
    ALuint pos = 0;                                                           \
    ALfloat value;                                                            \
    ALuint i;                                                                 \
                                                                              \
    for(i = 0;i < BufferSize+1;i++)                                           \
    {                                                                         \
        value = Sampler(data + pos*NumChannels, NumChannels, frac);           \
        OutBuffer[i] = value;                                                 \
                                                                              \
        frac += increment;                                                    \
        pos  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
    }                                                                         \
}

DECL_TEMPLATE(point32)
DECL_TEMPLATE(lerp32)
DECL_TEMPLATE(cubic32)

#undef DECL_TEMPLATE


static __inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                     const ALuint IrSize,
                                     ALfloat (*RESTRICT Coeffs)[2],
                                     ALfloat (*RESTRICT CoeffStep)[2],
                                     ALfloat left, ALfloat right)
{
    ALuint c;
    for(c = 0;c < IrSize;c++)
    {
        const ALuint off = (Offset+c)&HRIR_MASK;
        Values[off][0] += Coeffs[c][0] * left;
        Values[off][1] += Coeffs[c][1] * right;
        Coeffs[c][0] += CoeffStep[c][0];
        Coeffs[c][1] += CoeffStep[c][1];
    }
}

static __inline void ApplyCoeffs(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                 const ALuint IrSize,
                                 ALfloat (*RESTRICT Coeffs)[2],
                                 ALfloat left, ALfloat right)
{
    ALuint c;
    for(c = 0;c < IrSize;c++)
    {
        const ALuint off = (Offset+c)&HRIR_MASK;
        Values[off][0] += Coeffs[c][0] * left;
        Values[off][1] += Coeffs[c][1] * right;
    }
}

#define SUFFIX C
#include "mixer_inc.c"
#undef SUFFIX


void MixDirect_C(ALsource *Source, ALCdevice *Device, DirectParams *params,
  const ALfloat *RESTRICT data, ALuint srcchan,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    ALfloat (*RESTRICT DryBuffer)[BUFFERSIZE] = Device->DryBuffer;
    ALfloat *RESTRICT ClickRemoval = Device->ClickRemoval;
    ALfloat *RESTRICT PendingClicks = Device->PendingClicks;
    ALfloat DrySend[MaxChannels];
    ALuint pos;
    ALuint c;
    (void)Source;

    for(c = 0;c < MaxChannels;c++)
        DrySend[c] = params->Gains[srcchan][c];

    pos = 0;
    if(OutPos == 0)
    {
        for(c = 0;c < MaxChannels;c++)
            ClickRemoval[c] -= data[pos]*DrySend[c];
    }
    for(c = 0;c < MaxChannels;c++)
    {
        for(pos = 0;pos < BufferSize;pos++)
            DryBuffer[c][OutPos+pos] += data[pos]*DrySend[c];
    }
    if(OutPos+pos == SamplesToDo)
    {
        for(c = 0;c < MaxChannels;c++)
            PendingClicks[c] += data[pos]*DrySend[c];
    }
}


void MixSend_C(SendParams *params, const ALfloat *RESTRICT data,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    ALeffectslot *Slot = params->Slot;
    ALfloat *WetBuffer = Slot->WetBuffer;
    ALfloat *WetClickRemoval = Slot->ClickRemoval;
    ALfloat *WetPendingClicks = Slot->PendingClicks;
    ALfloat  WetSend = params->Gain;
    ALuint pos;

    pos = 0;
    if(OutPos == 0)
    {
        WetClickRemoval[0] -= data[pos] * WetSend;
    }
    for(pos = 0;pos < BufferSize;pos++)
    {
        WetBuffer[OutPos] += data[pos] * WetSend;
        OutPos++;
    }
    if(OutPos == SamplesToDo)
    {
        WetPendingClicks[0] += data[pos] * WetSend;
    }
}
