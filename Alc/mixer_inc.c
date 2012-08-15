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
#define REAL_MERGE4(a,b,c,d) a##b##c##d
#define MERGE4(a,b,c,d) REAL_MERGE4(a,b,c,d)


void MERGE4(MixDirect_Hrtf_,SAMPLER,_,SUFFIX)(
  ALsource *Source, ALCdevice *Device, DirectParams *params,
  const ALfloat *RESTRICT data, ALuint srcfrac,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    const ALuint NumChannels = Source->NumChannels;
    const ALint *RESTRICT DelayStep = params->Hrtf.DelayStep;
    ALfloat (*RESTRICT DryBuffer)[MaxChannels];
    ALfloat *RESTRICT ClickRemoval, *RESTRICT PendingClicks;
    ALfloat (*RESTRICT CoeffStep)[2] = params->Hrtf.CoeffStep;
    ALuint pos, frac;
    FILTER *DryFilter;
    ALuint BufferIdx;
    ALuint increment;
    ALfloat value;
    ALuint i,  c;

    increment = Source->Params.Step;

    DryBuffer = Device->DryBuffer;
    ClickRemoval = Device->ClickRemoval;
    PendingClicks = Device->PendingClicks;
    DryFilter = &params->iirFilter;

    for(i = 0;i < NumChannels;i++)
    {
        ALfloat (*RESTRICT TargetCoeffs)[2] = params->Hrtf.Coeffs[i];
        ALuint *RESTRICT TargetDelay = params->Hrtf.Delay[i];
        ALfloat *RESTRICT History = Source->Hrtf.History[i];
        ALfloat (*RESTRICT Values)[2] = Source->Hrtf.Values[i];
        ALint Counter = maxu(Source->Hrtf.Counter, OutPos) - OutPos;
        ALuint Offset = Source->Hrtf.Offset + OutPos;
        ALfloat Coeffs[HRIR_LENGTH][2];
        ALuint Delay[2];
        ALfloat left, right;

        pos = 0;
        frac = srcfrac;

        for(c = 0;c < HRIR_LENGTH;c++)
        {
            Coeffs[c][0] = TargetCoeffs[c][0] - (CoeffStep[c][0]*Counter);
            Coeffs[c][1] = TargetCoeffs[c][1] - (CoeffStep[c][1]*Counter);
        }

        Delay[0] = TargetDelay[0] - (DelayStep[0]*Counter);
        Delay[1] = TargetDelay[1] - (DelayStep[1]*Counter);

        if(LIKELY(OutPos == 0))
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);
            value = lpFilter2PC(DryFilter, i, value);

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
        for(BufferIdx = 0;BufferIdx < BufferSize && Counter > 0;BufferIdx++)
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);
            value = lpFilter2P(DryFilter, i, value);

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

            for(c = 0;c < HRIR_LENGTH;c++)
            {
                const ALuint off = (Offset+c)&HRIR_MASK;
                Values[off][0] += Coeffs[c][0] * left;
                Values[off][1] += Coeffs[c][1] * right;
                Coeffs[c][0] += CoeffStep[c][0];
                Coeffs[c][1] += CoeffStep[c][1];
            }

            DryBuffer[OutPos][FrontLeft]  += Values[Offset&HRIR_MASK][0];
            DryBuffer[OutPos][FrontRight] += Values[Offset&HRIR_MASK][1];

            frac += increment;
            pos  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
            OutPos++;
            Counter--;
        }

        Delay[0] >>= HRTFDELAY_BITS;
        Delay[1] >>= HRTFDELAY_BITS;
        for(;BufferIdx < BufferSize;BufferIdx++)
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);
            value = lpFilter2P(DryFilter, i, value);

            History[Offset&SRC_HISTORY_MASK] = value;
            left = History[(Offset-Delay[0])&SRC_HISTORY_MASK];
            right = History[(Offset-Delay[1])&SRC_HISTORY_MASK];

            Values[Offset&HRIR_MASK][0] = 0.0f;
            Values[Offset&HRIR_MASK][1] = 0.0f;
            Offset++;

            ApplyCoeffs(Offset, Values, Coeffs, left, right);
            DryBuffer[OutPos][FrontLeft]  += Values[Offset&HRIR_MASK][0];
            DryBuffer[OutPos][FrontRight] += Values[Offset&HRIR_MASK][1];

            frac += increment;
            pos  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
            OutPos++;
        }
        if(LIKELY(OutPos == SamplesToDo))
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);
            value = lpFilter2PC(DryFilter, i, value);

            History[Offset&SRC_HISTORY_MASK] = value;
            left = History[(Offset-Delay[0])&SRC_HISTORY_MASK];
            right = History[(Offset-Delay[1])&SRC_HISTORY_MASK];

            PendingClicks[FrontLeft]  += Values[(Offset+1)&HRIR_MASK][0] +
                                         Coeffs[0][0] * left;
            PendingClicks[FrontRight] += Values[(Offset+1)&HRIR_MASK][1] +
                                         Coeffs[0][1] * right;
        }
        OutPos -= BufferSize;
    }
}


void MERGE4(MixDirect_,SAMPLER,_,SUFFIX)(
  ALsource *Source, ALCdevice *Device, DirectParams *params,
  const ALfloat *RESTRICT data, ALuint srcfrac,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    const ALuint NumChannels = Source->NumChannels;
    ALfloat (*RESTRICT DryBuffer)[MaxChannels];
    ALfloat *RESTRICT ClickRemoval, *RESTRICT PendingClicks;
    ALfloat DrySend[MaxChannels];
    FILTER *DryFilter;
    ALuint pos, frac;
    ALuint BufferIdx;
    ALuint increment;
    ALfloat value;
    ALuint i,  c;

    increment = Source->Params.Step;

    DryBuffer = Device->DryBuffer;
    ClickRemoval = Device->ClickRemoval;
    PendingClicks = Device->PendingClicks;
    DryFilter = &params->iirFilter;

    for(i = 0;i < NumChannels;i++)
    {
        for(c = 0;c < MaxChannels;c++)
            DrySend[c] = params->Gains[i][c];

        pos = 0;
        frac = srcfrac;

        if(OutPos == 0)
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);

            value = lpFilter2PC(DryFilter, i, value);
            for(c = 0;c < MaxChannels;c++)
                ClickRemoval[c] -= value*DrySend[c];
        }
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);

            value = lpFilter2P(DryFilter, i, value);
            for(c = 0;c < MaxChannels;c++)
                DryBuffer[OutPos][c] += value*DrySend[c];

            frac += increment;
            pos  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
            OutPos++;
        }
        if(OutPos == SamplesToDo)
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);

            value = lpFilter2PC(DryFilter, i, value);
            for(c = 0;c < MaxChannels;c++)
                PendingClicks[c] += value*DrySend[c];
        }
        OutPos -= BufferSize;
    }
}


void MERGE4(MixSend_,SAMPLER,_,SUFFIX)(
  ALsource *Source, ALuint sendidx, SendParams *params,
  const ALfloat *RESTRICT data, ALuint srcfrac,
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)
{
    const ALuint NumChannels = Source->NumChannels;
    ALeffectslot *Slot;
    ALfloat  WetSend;
    ALfloat *WetBuffer;
    ALfloat *WetClickRemoval;
    ALfloat *WetPendingClicks;
    FILTER  *WetFilter;
    ALuint pos, frac;
    ALuint BufferIdx;
    ALuint increment;
    ALfloat value;
    ALuint i;

    increment = Source->Params.Step;

    Slot = Source->Params.Slot[sendidx];
    WetBuffer = Slot->WetBuffer;
    WetClickRemoval = Slot->ClickRemoval;
    WetPendingClicks = Slot->PendingClicks;
    WetFilter = &params->iirFilter;
    WetSend = params->Gain;

    for(i = 0;i < NumChannels;i++)
    {
        pos = 0;
        frac = srcfrac;

        if(OutPos == 0)
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);

            value = lpFilter2PC(WetFilter, i, value);
            WetClickRemoval[0] -= value * WetSend;
        }
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);

            value = lpFilter2P(WetFilter, i, value);
            WetBuffer[OutPos] += value * WetSend;

            frac += increment;
            pos  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
            OutPos++;
        }
        if(OutPos == SamplesToDo)
        {
            value = SAMPLER(data + pos*NumChannels + i, NumChannels, frac);

            value = lpFilter2PC(WetFilter, i, value);
            WetPendingClicks[0] += value * WetSend;
        }
        OutPos -= BufferSize;
    }
}

#undef MERGE4
#undef REAL_MERGE4
#undef MERGE2
#undef REAL_MERGE2

#undef UNLIKELY
#undef LIKELY
