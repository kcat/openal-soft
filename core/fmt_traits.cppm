module;

#include "core/fmt_traits.hpp"

export module core.fmt_traits;

export {
    using ::muLawDecompressionTable;
    using ::aLawDecompressionTable;

    using ::MulawSample;
    using ::AlawSample;
    using ::IMA4Data;
    using ::MSADPCMData;

    using ::SampleInfo;
}
