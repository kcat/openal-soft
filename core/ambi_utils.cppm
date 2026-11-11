module;

#include "ambi_utils.hpp"

export module core.ambi_utils;

export {
    using ::AmbiChannelsFromOrder;

    using ::MaxAmbiOrder;
    using ::MaxAmbiChannels;

    using ::Ambi0OrderMask;
    using ::Ambi1OrderMask;
    using ::Ambi2OrderMask;
    using ::Ambi3OrderMask;
    using ::Ambi4OrderMask;

    using ::AmbiPeriphonicMask;

    using ::Ambi2DChannelsFromOrder;
    using ::MaxAmbi2DChannels;

    namespace AmbiScale {
        using AmbiScale::FromN3D;
        using AmbiScale::FromSN3D;
        using AmbiScale::FromFuMa;

        using AmbiScale::FirstOrderUp;
        using AmbiScale::FirstOrder2DUp;
        using AmbiScale::SecondOrderUp;
        using AmbiScale::SecondOrder2DUp;
        using AmbiScale::ThirdOrderUp;
        using AmbiScale::ThirdOrder2DUp;
        using AmbiScale::FourthOrder2DUp;

        using AmbiScale::GetHFOrderScales;
    }

    namespace AmbiIndex {
        using AmbiIndex::FromFuMa;
        using AmbiIndex::FromFuMa2D;
        using AmbiIndex::FromACN;
        using AmbiIndex::FromACN2D;
        using AmbiIndex::OrderFromChannel;
        using AmbiIndex::OrderFrom2DChannel;
    }

    using ::CalcAmbiCoeffs;
    using ::CalcDirectionCoeffs;
}
