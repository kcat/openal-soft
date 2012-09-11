#include "config.h"

#include "AL/alc.h"
#include "AL/al.h"
#include "alMain.h"
#include "alSource.h"
#include "alAuxEffectSlot.h"
#include "mixer_defs.h"

#ifdef __GNUC__
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

#define REAL_MERGE2(a,b) a##b
#define MERGE2(a,b) REAL_MERGE2(a,b)

#define MixDirect_Hrtf MERGE2(MixDirect_Hrtf_,SUFFIX)
#define MixDirect MERGE2(MixDirect_,SUFFIX)
#define MixSend MERGE2(MixSend_,SUFFIX)


static __inline void ApplyCoeffsStep(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                     const ALuint irSize,
                                     ALfloat (*RESTRICT Coeffs)[2],
                                     ALfloat (*RESTRICT CoeffStep)[2],
                                     ALfloat left, ALfloat right);
static __inline void ApplyCoeffs(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                 const ALuint irSize,
                                 ALfloat (*RESTRICT Coeffs)[2],
                                 ALfloat left, ALfloat right);


#ifndef NO_MIXDIRECT_HRTF
void MixDirect_Hrtf(ALsource *Source, ALCdevice *Device, DirectParams *params,
  const ALfloat *RESTRICT data, ALuint srcchan,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    const ALint *RESTRICT DelayStep = params->Hrtf.DelayStep;
    const ALuint IrSize = GetHrtfIrSize(Device->Hrtf);
    ALfloat (*RESTRICT DryBuffer)[MaxChannels];
    ALfloat *RESTRICT ClickRemoval, *RESTRICT PendingClicks;
    ALfloat (*RESTRICT CoeffStep)[2] = params->Hrtf.CoeffStep;
    ALfloat (*RESTRICT TargetCoeffs)[2] = params->Hrtf.Coeffs[srcchan];
    ALuint *RESTRICT TargetDelay = params->Hrtf.Delay[srcchan];
    ALfloat *RESTRICT History = Source->Hrtf.History[srcchan];
    ALfloat (*RESTRICT Values)[2] = Source->Hrtf.Values[srcchan];
    ALint Counter = maxu(Source->Hrtf.Counter, OutPos) - OutPos;
    ALuint Offset = Source->Hrtf.Offset + OutPos;
    ALIGN(16) ALfloat Coeffs[HRIR_LENGTH][2];
    ALuint Delay[2];
    ALfloat left, right;
    ALuint pos;
    ALuint c;

    DryBuffer = Device->DryBuffer;
    ClickRemoval = Device->ClickRemoval;
    PendingClicks = Device->PendingClicks;

    pos = 0;
    for(c = 0;c < IrSize;c++)
    {
        Coeffs[c][0] = TargetCoeffs[c][0] - (CoeffStep[c][0]*Counter);
        Coeffs[c][1] = TargetCoeffs[c][1] - (CoeffStep[c][1]*Counter);
    }

    Delay[0] = TargetDelay[0] - (DelayStep[0]*Counter);
    Delay[1] = TargetDelay[1] - (DelayStep[1]*Counter);

    if(LIKELY(OutPos == 0))
    {
        History[Offset&SRC_HISTORY_MASK] = data[pos];
        left  = lerp(History[(Offset-(Delay[0]>>HRTFDELAY_BITS))&SRC_HISTORY_MASK],
                     History[(Offset-(Delay[0]>>HRTFDELAY_BITS)-1)&SRC_HISTORY_MASK],
                     (Delay[0]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));
        right = lerp(History[(Offset-(Delay[1]>>HRTFDELAY_BITS))&SRC_HISTORY_MASK],
                     History[(Offset-(Delay[1]>>HRTFDELAY_BITS)-1)&SRC_HISTORY_MASK],
                     (Delay[1]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));

        ClickRemoval[FrontLeft]  -= Values[(Offset+1)&HRIR_MASK][0] +
                                    Coeffs[0][0] * left;
        ClickRemoval[FrontRight] -= Values[(Offset+1)&HRIR_MASK][1] +
                                    Coeffs[0][1] * right;
    }
    for(pos = 0;pos < BufferSize && Counter > 0;pos++)
    {
        History[Offset&SRC_HISTORY_MASK] = data[pos];
        left  = lerp(History[(Offset-(Delay[0]>>HRTFDELAY_BITS))&SRC_HISTORY_MASK],
                     History[(Offset-(Delay[0]>>HRTFDELAY_BITS)-1)&SRC_HISTORY_MASK],
                     (Delay[0]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));
        right = lerp(History[(Offset-(Delay[1]>>HRTFDELAY_BITS))&SRC_HISTORY_MASK],
                     History[(Offset-(Delay[1]>>HRTFDELAY_BITS)-1)&SRC_HISTORY_MASK],
                     (Delay[1]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));

        Delay[0] += DelayStep[0];
        Delay[1] += DelayStep[1];

        Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
        Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
        Offset++;

        ApplyCoeffsStep(Offset, Values, IrSize, Coeffs, CoeffStep, left, right);
        DryBuffer[OutPos][FrontLeft]  += Values[Offset&HRIR_MASK][0];
        DryBuffer[OutPos][FrontRight] += Values[Offset&HRIR_MASK][1];

        OutPos++;
        Counter--;
    }

    Delay[0] >>= HRTFDELAY_BITS;
    Delay[1] >>= HRTFDELAY_BITS;
    for(;pos < BufferSize;pos++)
    {
        History[Offset&SRC_HISTORY_MASK] = data[pos];
        left = History[(Offset-Delay[0])&SRC_HISTORY_MASK];
        right = History[(Offset-Delay[1])&SRC_HISTORY_MASK];

        Values[(Offset+IrSize)&HRIR_MASK][0] = 0.0f;
        Values[(Offset+IrSize)&HRIR_MASK][1] = 0.0f;
        Offset++;

        ApplyCoeffs(Offset, Values, IrSize, Coeffs, left, right);
        DryBuffer[OutPos][FrontLeft]  += Values[Offset&HRIR_MASK][0];
        DryBuffer[OutPos][FrontRight] += Values[Offset&HRIR_MASK][1];

        OutPos++;
    }
    if(LIKELY(OutPos == SamplesToDo))
    {
        History[Offset&SRC_HISTORY_MASK] = data[pos];
        left = History[(Offset-Delay[0])&SRC_HISTORY_MASK];
        right = History[(Offset-Delay[1])&SRC_HISTORY_MASK];

        PendingClicks[FrontLeft]  += Values[(Offset+1)&HRIR_MASK][0] +
                                     Coeffs[0][0] * left;
        PendingClicks[FrontRight] += Values[(Offset+1)&HRIR_MASK][1] +
                                     Coeffs[0][1] * right;
    }
}
#endif

#ifndef NO_MIXDIRECT
void MixDirect(ALsource *Source, ALCdevice *Device, DirectParams *params,
  const ALfloat *RESTRICT data, ALuint srcchan,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    ALfloat (*RESTRICT DryBuffer)[MaxChannels];
    ALfloat *RESTRICT ClickRemoval, *RESTRICT PendingClicks;
    ALIGN(16) ALfloat DrySend[MaxChannels];
    ALuint pos;
    ALuint c;
    (void)Source;

    DryBuffer = Device->DryBuffer;
    ClickRemoval = Device->ClickRemoval;
    PendingClicks = Device->PendingClicks;

    for(c = 0;c < MaxChannels;c++)
        DrySend[c] = params->Gains[srcchan][c];

    pos = 0;
    if(OutPos == 0)
    {
        for(c = 0;c < MaxChannels;c++)
            ClickRemoval[c] -= data[pos]*DrySend[c];
    }
    for(pos = 0;pos < BufferSize;pos++)
    {
        for(c = 0;c < MaxChannels;c++)
            DryBuffer[OutPos][c] += data[pos]*DrySend[c];
        OutPos++;
    }
    if(OutPos == SamplesToDo)
    {
        for(c = 0;c < MaxChannels;c++)
            PendingClicks[c] += data[pos]*DrySend[c];
    }
}
#endif

#ifndef NO_MIXSEND
void MixSend(SendParams *params, const ALfloat *RESTRICT data,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    ALeffectslot *Slot;
    ALfloat  WetSend;
    ALfloat *WetBuffer;
    ALfloat *WetClickRemoval;
    ALfloat *WetPendingClicks;
    ALuint pos;

    Slot = params->Slot;
    WetBuffer = Slot->WetBuffer;
    WetClickRemoval = Slot->ClickRemoval;
    WetPendingClicks = Slot->PendingClicks;
    WetSend = params->Gain;

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
#endif


#undef MixSend
#undef MixDirect
#undef MixDirect_Hrtf

#undef MERGE2
#undef REAL_MERGE2

#undef UNLIKELY
#undef LIKELY
