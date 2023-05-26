#ifndef COMMON_VECMAT_H
#define COMMON_VECMAT_H

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "alspan.h"


namespace alu {

template<typename T>
class VectorR {
    static_assert(std::is_floating_point<T>::value, "Must use floating-point types");
    alignas(16) T mVals[4];

public:
    constexpr VectorR() noexcept = default;
    constexpr VectorR(const VectorR&) noexcept = default;
    constexpr explicit VectorR(T a, T b, T c, T d) noexcept : mVals{a, b, c, d} { }

    constexpr VectorR& operator=(const VectorR&) noexcept = default;

    constexpr T& operator[](size_t idx) noexcept { return mVals[idx]; }
    constexpr const T& operator[](size_t idx) const noexcept { return mVals[idx]; }

    constexpr VectorR& operator+=(const VectorR &rhs) noexcept
    {
        mVals[0] += rhs.mVals[0];
        mVals[1] += rhs.mVals[1];
        mVals[2] += rhs.mVals[2];
        mVals[3] += rhs.mVals[3];
        return *this;
    }

    constexpr VectorR operator-(const VectorR &rhs) const noexcept
    {
        return VectorR{mVals[0] - rhs.mVals[0], mVals[1] - rhs.mVals[1],
            mVals[2] - rhs.mVals[2], mVals[3] - rhs.mVals[3]};
    }

    constexpr T normalize(T limit = std::numeric_limits<T>::epsilon())
    {
        limit = std::max(limit, std::numeric_limits<T>::epsilon());
        const T length_sqr{mVals[0]*mVals[0] + mVals[1]*mVals[1] + mVals[2]*mVals[2]};
        if(length_sqr > limit*limit)
        {
            const T length{std::sqrt(length_sqr)};
            T inv_length{T{1}/length};
            mVals[0] *= inv_length;
            mVals[1] *= inv_length;
            mVals[2] *= inv_length;
            return length;
        }
        mVals[0] = mVals[1] = mVals[2] = T{0};
        return T{0};
    }

    constexpr VectorR cross_product(const alu::VectorR<T> &rhs) const noexcept
    {
        return VectorR{
            mVals[1]*rhs.mVals[2] - mVals[2]*rhs.mVals[1],
            mVals[2]*rhs.mVals[0] - mVals[0]*rhs.mVals[2],
            mVals[0]*rhs.mVals[1] - mVals[1]*rhs.mVals[0],
            T{0}};
    }

    constexpr T dot_product(const alu::VectorR<T> &rhs) const noexcept
    { return mVals[0]*rhs.mVals[0] + mVals[1]*rhs.mVals[1] + mVals[2]*rhs.mVals[2]; }
};
using Vector = VectorR<float>;

template<typename T>
class MatrixR {
    static_assert(std::is_floating_point<T>::value, "Must use floating-point types");
    alignas(16) T mVals[16];

public:
    constexpr MatrixR() noexcept = default;
    constexpr MatrixR(const MatrixR&) noexcept = default;
    constexpr explicit MatrixR(
        T aa, T ab, T ac, T ad,
        T ba, T bb, T bc, T bd,
        T ca, T cb, T cc, T cd,
        T da, T db, T dc, T dd) noexcept
        : mVals{aa,ab,ac,ad, ba,bb,bc,bd, ca,cb,cc,cd, da,db,dc,dd}
    { }

    constexpr MatrixR& operator=(const MatrixR&) noexcept = default;

    constexpr auto operator[](size_t idx) noexcept { return al::span<T,4>{&mVals[idx*4], 4}; }
    constexpr auto operator[](size_t idx) const noexcept
    { return al::span<const T,4>{&mVals[idx*4], 4}; }

    static constexpr MatrixR Identity() noexcept
    {
        return MatrixR{
            T{1}, T{0}, T{0}, T{0},
            T{0}, T{1}, T{0}, T{0},
            T{0}, T{0}, T{1}, T{0},
            T{0}, T{0}, T{0}, T{1}};
    }
};
using Matrix = MatrixR<float>;

template<typename T>
constexpr VectorR<T> operator*(const MatrixR<T> &mtx, const VectorR<T> &vec) noexcept
{
    return VectorR<T>{
        vec[0]*mtx[0][0] + vec[1]*mtx[1][0] + vec[2]*mtx[2][0] + vec[3]*mtx[3][0],
        vec[0]*mtx[0][1] + vec[1]*mtx[1][1] + vec[2]*mtx[2][1] + vec[3]*mtx[3][1],
        vec[0]*mtx[0][2] + vec[1]*mtx[1][2] + vec[2]*mtx[2][2] + vec[3]*mtx[3][2],
        vec[0]*mtx[0][3] + vec[1]*mtx[1][3] + vec[2]*mtx[2][3] + vec[3]*mtx[3][3]};
}

} // namespace alu

#endif /* COMMON_VECMAT_H */
