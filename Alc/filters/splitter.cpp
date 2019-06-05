
#include "config.h"

#include "splitter.h"

#include <cmath>
#include <limits>
#include <algorithm>

#include "math_defs.h"

template<typename Real>
void BandSplitterR<Real>::init(Real f0norm)
{
    const Real w{f0norm * al::MathDefs<Real>::Tau()};
    const Real cw{std::cos(w)};
    if(cw > std::numeric_limits<float>::epsilon())
        coeff = (std::sin(w) - 1.0f) / cw;
    else
        coeff = cw * -0.5f;

    lp_z1 = 0.0f;
    lp_z2 = 0.0f;
    ap_z1 = 0.0f;
}

template<typename Real>
void BandSplitterR<Real>::process(Real *hpout, Real *lpout, const Real *input, const int count)
{
    ASSUME(count > 0);

    const Real ap_coeff{this->coeff};
    const Real lp_coeff{this->coeff*0.5f + 0.5f};
    Real lp_z1{this->lp_z1};
    Real lp_z2{this->lp_z2};
    Real ap_z1{this->ap_z1};
    auto proc_sample = [ap_coeff,lp_coeff,&lp_z1,&lp_z2,&ap_z1,&lpout](const Real in) noexcept -> Real
    {
        /* Low-pass sample processing. */
        Real d{(in - lp_z1) * lp_coeff};
        Real lp_y{lp_z1 + d};
        lp_z1 = lp_y + d;

        d = (lp_y - lp_z2) * lp_coeff;
        lp_y = lp_z2 + d;
        lp_z2 = lp_y + d;

        *(lpout++) = lp_y;

        /* All-pass sample processing. */
        Real ap_y{in*ap_coeff + ap_z1};
        ap_z1 = in - ap_y*ap_coeff;

        /* High-pass generated from removing low-passed output. */
        return ap_y - lp_y;
    };
    std::transform(input, input+count, hpout, proc_sample);
    this->lp_z1 = lp_z1;
    this->lp_z2 = lp_z2;
    this->ap_z1 = ap_z1;
}

template<typename Real>
void BandSplitterR<Real>::applyHfScale(Real *samples, const Real hfscale, const int count)
{
    ASSUME(count > 0);

    const Real ap_coeff{this->coeff};
    const Real lp_coeff{this->coeff*0.5f + 0.5f};
    Real lp_z1{this->lp_z1};
    Real lp_z2{this->lp_z2};
    Real ap_z1{this->ap_z1};
    auto proc_sample = [hfscale,ap_coeff,lp_coeff,&lp_z1,&lp_z2,&ap_z1](const Real in) noexcept -> Real
    {
        /* Low-pass sample processing. */
        Real d{(in - lp_z1) * lp_coeff};
        Real lp_y{lp_z1 + d};
        lp_z1 = lp_y + d;

        d = (lp_y - lp_z2) * lp_coeff;
        lp_y = lp_z2 + d;
        lp_z2 = lp_y + d;

        /* All-pass sample processing. */
        Real ap_y{in*ap_coeff + ap_z1};
        ap_z1 = in - ap_y*ap_coeff;

        /* High-pass generated from removing low-passed output. */
        return (ap_y-lp_y)*hfscale + lp_y;
    };
    std::transform(samples, samples+count, samples, proc_sample);
    this->lp_z1 = lp_z1;
    this->lp_z2 = lp_z2;
    this->ap_z1 = ap_z1;
}

template<typename Real>
void BandSplitterR<Real>::applyAllpass(Real *samples, const int count) const
{
    ASSUME(count > 0);

    const Real coeff{this->coeff};
    Real z1{0.0f};
    auto proc_sample = [coeff,&z1](const Real in) noexcept -> Real
    {
        const Real out{in*coeff + z1};
        z1 = in - out*coeff;
        return out;
    };
    std::transform(samples, samples+count, samples, proc_sample);
}


template class BandSplitterR<float>;
template class BandSplitterR<double>;
