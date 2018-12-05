
#include "config.h"

#include "splitter.h"

#include <cmath>
#include <algorithm>

#include "math_defs.h"


void bandsplit_init(BandSplitter *splitter, float f0norm)
{
    float w = f0norm * F_TAU;
    float cw = std::cos(w);
    if(cw > FLT_EPSILON)
        splitter->coeff = (std::sin(w) - 1.0f) / cw;
    else
        splitter->coeff = cw * -0.5f;

    splitter->lp_z1 = 0.0f;
    splitter->lp_z2 = 0.0f;
    splitter->hp_z1 = 0.0f;
}

void bandsplit_clear(BandSplitter *splitter)
{
    splitter->lp_z1 = 0.0f;
    splitter->lp_z2 = 0.0f;
    splitter->hp_z1 = 0.0f;
}

void bandsplit_process(BandSplitter *splitter, float *RESTRICT hpout, float *RESTRICT lpout,
                       const ALfloat *input, int count)
{
    ASSUME(count > 0);

    const float ap_coeff{splitter->coeff};
    const float lp_coeff{splitter->coeff*0.5f + 0.5f};
    float lp_z1{splitter->lp_z1};
    float lp_z2{splitter->lp_z2};
    float ap_z1{splitter->hp_z1};
    auto proc_sample = [ap_coeff,lp_coeff,&lp_z1,&lp_z2,&ap_z1,&lpout](const float in) noexcept -> float
    {
        /* Low-pass sample processing. */
        float d{(in - lp_z1) * lp_coeff};
        float lp_y{lp_z1 + d};
        lp_z1 = lp_y + d;

        d = (lp_y - lp_z2) * lp_coeff;
        lp_y = lp_z2 + d;
        lp_z2 = lp_y + d;

        *(lpout++) = lp_y;

        /* All-pass sample processing. */
        float ap_y{in*ap_coeff + ap_z1};
        ap_z1 = in - ap_y*ap_coeff;

        /* High-pass generated from removing low-passed output. */
        return ap_y - lp_y;
    };
    std::transform<const float*RESTRICT>(input, input+count, hpout, proc_sample);
    splitter->lp_z1 = lp_z1;
    splitter->lp_z2 = lp_z2;
    splitter->hp_z1 = ap_z1;
}


void splitterap_init(SplitterAllpass *splitter, float f0norm)
{
    float w = f0norm * F_TAU;
    float cw = std::cos(w);
    if(cw > FLT_EPSILON)
        splitter->coeff = (std::sin(w) - 1.0f) / cw;
    else
        splitter->coeff = cw * -0.5f;

    splitter->z1 = 0.0f;
}

void splitterap_clear(SplitterAllpass *splitter)
{
    splitter->z1 = 0.0f;
}

void splitterap_process(SplitterAllpass *splitter, float *RESTRICT samples, int count)
{
    ASSUME(count > 0);

    const float coeff{splitter->coeff};
    float z1{splitter->z1};
    auto proc_sample = [coeff,&z1](const float in) noexcept -> float
    {
        float out{in*coeff + z1};
        z1 = in - out*coeff;
        return out;
    };
    std::transform(samples, samples+count, samples, proc_sample);
    splitter->z1 = z1;
}
