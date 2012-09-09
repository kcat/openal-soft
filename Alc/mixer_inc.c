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
                                     ALfloat (*RESTRICT Coeffs)[2],
                                     ALfloat (*RESTRICT CoeffStep)[2],
                                     ALfloat left, ALfloat right);
static __inline void ApplyCoeffs(ALuint Offset, ALfloat (*RESTRICT Values)[2],
                                 ALfloat (*RESTRICT Coeffs)[2],
                                 ALfloat left, ALfloat right);


#ifndef NO_MIXDIRECT_HRTF
void MixDirect_Hrtf(ALsource *Source, ALCdevice *Device, DirectParams *params,
  const ALfloat *RESTRICT data, ALuint srcchan,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    const ALint *RESTRICT DelayStep = params->Hrtf.DelayStep;
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
    FILTER *DryFilter;
    ALfloat value;
    ALuint pos;
    ALuint c;

    DryBuffer = Device->DryBuffer;
    ClickRemoval = Device->ClickRemoval;
    PendingClicks = Device->PendingClicks;
    DryFilter = &params->iirFilter;

    pos = 0;
    for(c = 0;c < HRIR_LENGTH;c++)
    {
        Coeffs[c][0] = TargetCoeffs[c][0] - (CoeffStep[c][0]*Counter);
        Coeffs[c][1] = TargetCoeffs[c][1] - (CoeffStep[c][1]*Counter);
    }

    Delay[0] = TargetDelay[0] - (DelayStep[0]*Counter);
    Delay[1] = TargetDelay[1] - (DelayStep[1]*Counter);

    if(LIKELY(OutPos == 0))
    {
        value = lpFilter2PC(DryFilter, srcchan, data[pos]);

        History[Offset&SRC_HISTORY_MASK] = value;
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
        value = lpFilter2P(DryFilter, srcchan, data[pos]);

        History[Offset&SRC_HISTORY_MASK] = value;
        left  = lerp(History[(Offset-(Delay[0]>>HRTFDELAY_BITS))&SRC_HISTORY_MASK],
                     History[(Offset-(Delay[0]>>HRTFDELAY_BITS)-1)&SRC_HISTORY_MASK],
                     (Delay[0]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));
        right = lerp(History[(Offset-(Delay[1]>>HRTFDELAY_BITS))&SRC_HISTORY_MASK],
                     History[(Offset-(Delay[1]>>HRTFDELAY_BITS)-1)&SRC_HISTORY_MASK],
                     (Delay[1]&HRTFDELAY_MASK)*(1.0f/HRTFDELAY_FRACONE));

        Delay[0] += DelayStep[0];
        Delay[1] += DelayStep[1];

        Values[Offset&HRIR_MASK][0] = 0.0f;
        Values[Offset&HRIR_MASK][1] = 0.0f;
        Offset++;

        ApplyCoeffsStep(Offset, Values, Coeffs, CoeffStep, left, right);
        DryBuffer[OutPos][FrontLeft]  += Values[Offset&HRIR_MASK][0];
        DryBuffer[OutPos][FrontRight] += Values[Offset&HRIR_MASK][1];

        OutPos++;
        Counter--;
    }

    Delay[0] >>= HRTFDELAY_BITS;
    Delay[1] >>= HRTFDELAY_BITS;
    for(;pos < BufferSize;pos++)
    {
        value = lpFilter2P(DryFilter, srcchan, data[pos]);

        History[Offset&SRC_HISTORY_MASK] = value;
        left = History[(Offset-Delay[0])&SRC_HISTORY_MASK];
        right = History[(Offset-Delay[1])&SRC_HISTORY_MASK];

        Values[Offset&HRIR_MASK][0] = 0.0f;
        Values[Offset&HRIR_MASK][1] = 0.0f;
        Offset++;

        ApplyCoeffs(Offset, Values, Coeffs, left, right);
        DryBuffer[OutPos][FrontLeft]  += Values[Offset&HRIR_MASK][0];
        DryBuffer[OutPos][FrontRight] += Values[Offset&HRIR_MASK][1];

        OutPos++;
    }
    if(LIKELY(OutPos == SamplesToDo))
    {
        value = lpFilter2PC(DryFilter, srcchan, data[pos]);

        History[Offset&SRC_HISTORY_MASK] = value;
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
    FILTER *DryFilter;
    ALuint pos;
    ALfloat value;
    ALuint c;
    (void)Source;

    DryBuffer = Device->DryBuffer;
    ClickRemoval = Device->ClickRemoval;
    PendingClicks = Device->PendingClicks;
    DryFilter = &params->iirFilter;

    for(c = 0;c < MaxChannels;c++)
        DrySend[c] = params->Gains[srcchan][c];

    pos = 0;
    if(OutPos == 0)
    {
        value = lpFilter2PC(DryFilter, srcchan, data[pos]);
        for(c = 0;c < MaxChannels;c++)
            ClickRemoval[c] -= value*DrySend[c];
    }
    for(pos = 0;pos < BufferSize;pos++)
    {
        value = lpFilter2P(DryFilter, srcchan, data[pos]);
        for(c = 0;c < MaxChannels;c++)
            DryBuffer[OutPos][c] += value*DrySend[c];
        OutPos++;
    }
    if(OutPos == SamplesToDo)
    {
        value = lpFilter2PC(DryFilter, srcchan, data[pos]);
        for(c = 0;c < MaxChannels;c++)
            PendingClicks[c] += value*DrySend[c];
    }
}
#endif

#ifndef NO_MIXSEND
void MixSend(SendParams *params, const ALfloat *RESTRICT data, ALuint srcchan,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    ALeffectslot *Slot;
    ALfloat  WetSend;
    ALfloat *WetBuffer;
    ALfloat *WetClickRemoval;
    ALfloat *WetPendingClicks;
    FILTER  *WetFilter;
    ALuint pos;
    ALfloat value;

    Slot = params->Slot;
    WetBuffer = Slot->WetBuffer;
    WetClickRemoval = Slot->ClickRemoval;
    WetPendingClicks = Slot->PendingClicks;
    WetFilter = &params->iirFilter;
    WetSend = params->Gain;

    pos = 0;
    if(OutPos == 0)
    {
        value = lpFilter2PC(WetFilter, srcchan, data[pos]);
        WetClickRemoval[0] -= value * WetSend;
    }
    for(pos = 0;pos < BufferSize;pos++)
    {
        value = lpFilter2P(WetFilter, srcchan, data[pos]);
        WetBuffer[OutPos] += value * WetSend;
        OutPos++;
    }
    if(OutPos == SamplesToDo)
    {
        value = lpFilter2PC(WetFilter, srcchan, data[pos]);
        WetPendingClicks[0] += value * WetSend;
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
