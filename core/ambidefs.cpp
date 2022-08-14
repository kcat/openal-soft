
#include "config.h"

#include "ambidefs.h"

#include <cassert>


namespace {

constexpr std::array<float,MaxAmbiOrder+1> Ambi3DDecoderHFScale10{{
    1.000000000e+00f, 5.773502692e-01f
}};
constexpr std::array<float,MaxAmbiOrder+1> Ambi3DDecoderHFScale2O{{
    9.128709292e-01f, 7.071067812e-01f, 3.651483717e-01f
}};
constexpr std::array<float,MaxAmbiOrder+1> Ambi3DDecoderHFScale3O{{
    8.340921354e-01f, 7.182670250e-01f, 5.107426573e-01f, 2.541870634e-01f
}};
/*constexpr std::array<float,MaxAmbiOrder+1> Ambi3DDecoderHFScale4O{{
    1.727324867e-02f, 3.238734126e-02f, 8.245277297e-02f, 2.360733547e-01f, 7.127761153e-01f
}};*/

inline auto& GetDecoderHFScales(uint order) noexcept
{
    if(order >= 3) return Ambi3DDecoderHFScale3O;
    if(order == 2) return Ambi3DDecoderHFScale2O;
    return Ambi3DDecoderHFScale10;
}

} // namespace

auto AmbiScale::GetHFOrderScales(const uint in_order, const uint out_order) noexcept
    -> std::array<float,MaxAmbiOrder+1>
{
    std::array<float,MaxAmbiOrder+1> ret{};

    assert(out_order >= in_order);

    const auto &target = GetDecoderHFScales(out_order);
    const auto &input = GetDecoderHFScales(in_order);

    for(size_t i{0};i < in_order+1;++i)
        ret[i] = input[i] / target[i];

    return ret;
}
