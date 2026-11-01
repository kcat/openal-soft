module;

#include "numeric.hpp"

export module numeric;

export namespace al {
    using al::saturating_add;
    using al::saturating_cast;
}

export namespace std {
    using std::accumulate;
    using std::gcd;
    using std::lcm;
    using std::partial_sum;
    using std::reduce;
}

export {
    using ::as_signed;
    using ::as_unsigned;

    using ::GetCounterSuffix;

    using ::lerpf;

    using ::RoundFromZero;
    using ::RoundToZero;

    using ::fastf2i;
    using ::fastf2u;

    using ::fast_roundf;

    using ::level_mb_to_gain;
    using ::gain_to_level_mb;
}
