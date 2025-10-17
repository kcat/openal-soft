#ifndef CORE_FILTERS_NFC_H
#define CORE_FILTERS_NFC_H

#include <array>
#include <span>

#include "alnumeric.h"


struct NfcFilter1 {
    struct Coefficients {
        f32 a0{1.0f}, a1{}, b1{};
    };
    f32 mBaseGain{1.0f};
    Coefficients mCoeffs;
    std::array<f32, 1> mZ{};

    void process(std::span<f32 const> src, std::span<f32> dst);
};
struct NfcFilter2 {
    struct Coefficients {
        f32 a0{1.0f}, a1{}, a2{}, b1{}, b2{};
    };
    f32 mBaseGain{1.0f};
    Coefficients mCoeffs;
    std::array<f32, 2> mZ{};

    void process(std::span<f32 const> src, std::span<f32> dst);
};
struct NfcFilter3 {
    struct Coefficients {
        f32 a0{1.0f}, a1{}, a2{}, a3{}, b1{}, b2{}, b3{};
    };
    f32 mBaseGain{1.0f};
    Coefficients mCoeffs;
    std::array<f32, 3> mZ{};

    void process(std::span<f32 const> src, std::span<f32> dst);
};
struct NfcFilter4 {
    struct Coefficients {
        f32 a0{1.0f}, a1{}, a2{}, a3{}, a4{}, b1{}, b2{}, b3{}, b4{};
    };
    f32 mBaseGain{1.0f};
    Coefficients mCoeffs;
    std::array<f32, 4> mZ{};

    void process(std::span<f32 const> src, std::span<f32> dst);
};

class NfcFilter {
    NfcFilter1 first;
    NfcFilter2 second;
    NfcFilter3 third;
    NfcFilter4 fourth;

public:
    /* NOTE:
     * w0 = speed_of_sound / (source_distance * sample_rate);
     * w1 = speed_of_sound / (control_distance * sample_rate);
     *
     * Generally speaking, the control distance should be approximately the
     * average speaker distance, or based on the reference delay if outputting
     * NFC-HOA. It must not be negative, 0, or infinite. The source distance
     * should not be too small relative to the control distance.
     */

    void init(f32 w1) noexcept;
    void adjust(f32 w0) noexcept;

    /* Near-field control filter for first-order ambisonic channels (1-3). */
    void process1(std::span<f32 const> const src, std::span<f32> const dst)
    { first.process(src, dst); }

    /* Near-field control filter for second-order ambisonic channels (4-8). */
    void process2(std::span<f32 const> const src, std::span<f32> const dst)
    { second.process(src, dst); }

    /* Near-field control filter for third-order ambisonic channels (9-15). */
    void process3(std::span<f32 const> const src, std::span<f32> const dst)
    { third.process(src, dst); }

    /* Near-field control filter for fourth-order ambisonic channels (16-24). */
    void process4(std::span<f32 const> const src, std::span<f32> const dst)
    { fourth.process(src, dst); }
};

#endif /* CORE_FILTERS_NFC_H */
