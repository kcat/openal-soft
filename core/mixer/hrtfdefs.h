#ifndef CORE_MIXER_HRTFDEFS_H
#define CORE_MIXER_HRTFDEFS_H

#include <array>

#include "core/ambidefs.h"
#include "core/bufferline.h"
#include "core/filters/splitter.h"


#define HRTF_HISTORY_BITS   6
#define HRTF_HISTORY_LENGTH (1<<HRTF_HISTORY_BITS)
#define HRTF_HISTORY_MASK   (HRTF_HISTORY_LENGTH-1)

#define HRIR_BITS   7
#define HRIR_LENGTH (1<<HRIR_BITS)
#define HRIR_MASK   (HRIR_LENGTH-1)

#define MIN_IR_LENGTH 8

#define HRTF_DIRECT_DELAY 256

using float2 = std::array<float,2>;
using HrirArray = std::array<float2,HRIR_LENGTH>;
using ubyte = unsigned char;
using ubyte2 = std::array<ubyte,2>;
using ushort = unsigned short;
using uint = unsigned int;


struct MixHrtfFilter {
    const HrirArray *Coeffs;
    std::array<uint,2> Delay;
    float Gain;
    float GainStep;
};

struct HrtfFilter {
    alignas(16) HrirArray Coeffs;
    std::array<uint,2> Delay;
    float Gain;
};


struct HrtfChannelState {
    std::array<float,HRTF_DIRECT_DELAY> mDelay{};
    BandSplitter mSplitter;
    float mHfScale{};
    alignas(16) HrirArray mCoeffs{};
};

#endif /* CORE_MIXER_HRTFDEFS_H */
