#include "config.h"

#include <assert.h>

#include <limits>

#include "alMain.h"
#include "alu.h"
#include "alSource.h"
#include "alAuxEffectSlot.h"
#include "defs.h"


static inline ALfloat do_point(const InterpState&, const ALfloat *RESTRICT vals, const ALsizei) noexcept
{ return vals[0]; }
static inline ALfloat do_lerp(const InterpState&, const ALfloat *RESTRICT vals, const ALsizei frac) noexcept
{ return lerp(vals[0], vals[1], frac * (1.0f/FRACTIONONE)); }
static inline ALfloat do_cubic(const InterpState&, const ALfloat *RESTRICT vals, const ALsizei frac) noexcept
{ return cubic(vals[0], vals[1], vals[2], vals[3], frac * (1.0f/FRACTIONONE)); }
static inline ALfloat do_bsinc(const InterpState &istate, const ALfloat *RESTRICT vals, const ALsizei frac) noexcept
{
    ASSUME(istate.bsinc.m > 0);

    // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
    const ALsizei pi{frac >> FRAC_PHASE_BITDIFF};
    const ALfloat pf{(frac & ((1<<FRAC_PHASE_BITDIFF)-1)) * (1.0f/(1<<FRAC_PHASE_BITDIFF))};
#undef FRAC_PHASE_BITDIFF

    const ALfloat *fil{istate.bsinc.filter + istate.bsinc.m*pi*4};
    const ALfloat *scd{fil + istate.bsinc.m};
    const ALfloat *phd{scd + istate.bsinc.m};
    const ALfloat *spd{phd + istate.bsinc.m};

    // Apply the scale and phase interpolated filter.
    ALfloat r{0.0f};
    for(ALsizei j_f{0};j_f < istate.bsinc.m;j_f++)
        r += (fil[j_f] + istate.bsinc.sf*scd[j_f] + pf*(phd[j_f] + istate.bsinc.sf*spd[j_f])) * vals[j_f];
    return r;
}

const ALfloat *Resample_copy_C(const InterpState* UNUSED(state),
  const ALfloat *RESTRICT src, ALsizei UNUSED(frac), ALint UNUSED(increment),
  ALfloat *RESTRICT dst, ALsizei numsamples)
{
    ASSUME(numsamples > 0);
#if defined(HAVE_SSE) || defined(HAVE_NEON)
    /* Avoid copying the source data if it's aligned like the destination. */
    if((reinterpret_cast<intptr_t>(src)&15) == (reinterpret_cast<intptr_t>(dst)&15))
        return src;
#endif
    std::copy_n(src, numsamples, dst);
    return dst;
}

template<ALfloat Sampler(const InterpState&, const ALfloat*RESTRICT, const ALsizei) noexcept>
static const ALfloat *DoResample(const InterpState *state, const ALfloat *RESTRICT src,
                                 ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                                 ALsizei numsamples)
{
    ASSUME(numsamples > 0);
    ASSUME(increment > 0);
    ASSUME(frac >= 0);

    const InterpState istate{*state};
    std::generate_n<ALfloat*RESTRICT>(dst, numsamples,
        [&src,&frac,istate,increment]() noexcept -> ALfloat
        {
            ALfloat ret{Sampler(istate, src, frac)};

            frac += increment;
            src  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;

            return ret;
        }
    );
    return dst;
}

const ALfloat *Resample_point_C(const InterpState *state, const ALfloat *RESTRICT src,
                                ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                                ALsizei numsamples)
{ return DoResample<do_point>(state, src, frac, increment, dst, numsamples); }

const ALfloat *Resample_lerp_C(const InterpState *state, const ALfloat *RESTRICT src,
                               ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                               ALsizei numsamples)
{ return DoResample<do_lerp>(state, src, frac, increment, dst, numsamples); }

const ALfloat *Resample_cubic_C(const InterpState *state, const ALfloat *RESTRICT src,
                                ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                                ALsizei numsamples)
{ return DoResample<do_cubic>(state, src-1, frac, increment, dst, numsamples); }

const ALfloat *Resample_bsinc_C(const InterpState *state, const ALfloat *RESTRICT src,
                                ALsizei frac, ALint increment, ALfloat *RESTRICT dst,
                                ALsizei numsamples)
{ return DoResample<do_bsinc>(state, src-state->bsinc.l, frac, increment, dst, numsamples); }


static inline void ApplyCoeffs(ALsizei Offset, ALfloat (&Values)[HRIR_LENGTH][2],
                               const ALsizei IrSize, const ALfloat (&Coeffs)[HRIR_LENGTH][2],
                               const ALfloat left, const ALfloat right)
{
    ASSUME(Offset >= 0 && Offset < HRIR_LENGTH);
    ASSUME(IrSize >= 2);
    ASSUME(&Values != &Coeffs);

    ALsizei count{mini(IrSize, HRIR_LENGTH - Offset)};
    ASSUME(count > 0);
    for(ALsizei c{0};;)
    {
        for(;c < count;++c)
        {
            Values[Offset][0] += Coeffs[c][0] * left;
            Values[Offset][1] += Coeffs[c][1] * right;
            ++Offset;
        }
        if(c >= IrSize)
            break;
        Offset = 0;
        count = IrSize;
    }
}

#define MixHrtf MixHrtf_C
#define MixHrtfBlend MixHrtfBlend_C
#define MixDirectHrtf MixDirectHrtf_C
#include "hrtf_inc.cpp"


void Mix_C(const ALfloat *data, ALsizei OutChans, ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE],
           ALfloat *CurrentGains, const ALfloat *TargetGains, ALsizei Counter, ALsizei OutPos,
           ALsizei BufferSize)
{
    ASSUME(OutChans > 0);
    ASSUME(BufferSize > 0);

    const ALfloat delta{(Counter > 0) ? 1.0f / static_cast<ALfloat>(Counter) : 0.0f};
    for(ALsizei c{0};c < OutChans;c++)
    {
        ALsizei pos{0};
        ALfloat gain{CurrentGains[c]};

        const ALfloat diff{TargetGains[c] - gain};
        if(std::fabs(diff) > std::numeric_limits<float>::epsilon())
        {
            ALsizei minsize{mini(BufferSize, Counter)};
            const ALfloat step{diff * delta};
            ALfloat step_count{0.0f};
            for(;pos < minsize;pos++)
            {
                OutBuffer[c][OutPos+pos] += data[pos] * (gain + step*step_count);
                step_count += 1.0f;
            }
            if(pos == Counter)
                gain = TargetGains[c];
            else
                gain += step*step_count;
            CurrentGains[c] = gain;
        }

        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*gain;
    }
}

/* Basically the inverse of the above. Rather than one input going to multiple
 * outputs (each with its own gain), it's multiple inputs (each with its own
 * gain) going to one output. This applies one row (vs one column) of a matrix
 * transform. And as the matrices are more or less static once set up, no
 * stepping is necessary.
 */
void MixRow_C(ALfloat *OutBuffer, const ALfloat *Gains, const ALfloat (*RESTRICT data)[BUFFERSIZE], ALsizei InChans, ALsizei InPos, ALsizei BufferSize)
{
    ASSUME(InChans > 0);
    ASSUME(BufferSize > 0);

    for(ALsizei c{0};c < InChans;c++)
    {
        const ALfloat gain{Gains[c]};
        if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        for(ALsizei i{0};i < BufferSize;i++)
            OutBuffer[i] += data[c][InPos+i] * gain;
    }
}
