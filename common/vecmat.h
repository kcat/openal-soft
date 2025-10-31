#ifndef COMMON_VECMAT_H
#define COMMON_VECMAT_H

#include <array>
#include <cmath>
#include <limits>
#include <span>

#include "altypes.hpp"
#include "opthelpers.h"


namespace al {

class Vector {
    alignas(16) std::array<f32, 4> mVals{};

public:
    constexpr Vector() noexcept = default;
    constexpr Vector(const Vector&) noexcept = default;
    constexpr Vector(Vector&&) noexcept = default;
    constexpr explicit Vector(f32 const a, f32 const b, f32 const c, f32 const d) noexcept
        : mVals{{a,b,c,d}}
    { }

    constexpr auto operator=(const Vector&) & noexcept LIFETIMEBOUND -> Vector& = default;
    constexpr auto operator=(Vector&&) & noexcept LIFETIMEBOUND -> Vector& = default;

    [[nodiscard]] constexpr
    auto operator[](usize const idx) noexcept LIFETIMEBOUND -> f32& { return mVals[idx]; }
    [[nodiscard]] constexpr
    auto operator[](usize const idx) const noexcept LIFETIMEBOUND -> f32 const& { return mVals[idx]; }

    constexpr auto operator+=(const Vector &rhs) & noexcept -> Vector&
    {
        mVals[0] += rhs.mVals[0];
        mVals[1] += rhs.mVals[1];
        mVals[2] += rhs.mVals[2];
        mVals[3] += rhs.mVals[3];
        return *this;
    }

    [[nodiscard]] constexpr
    auto operator-(const Vector &rhs) const noexcept -> Vector
    {
        return Vector{mVals[0] - rhs.mVals[0], mVals[1] - rhs.mVals[1],
            mVals[2] - rhs.mVals[2], mVals[3] - rhs.mVals[3]};
    }

    constexpr auto normalize() -> f32
    {
        auto const length_sqr = f32{mVals[0]*mVals[0] + mVals[1]*mVals[1] + mVals[2]*mVals[2]};
        if(length_sqr > std::numeric_limits<f32>::epsilon())
        {
            auto const length = std::sqrt(length_sqr);
            auto const inv_length = 1.0f / length;
            mVals[0] *= inv_length;
            mVals[1] *= inv_length;
            mVals[2] *= inv_length;
            return length;
        }
        mVals[0] = mVals[1] = mVals[2] = 0.0f;
        return 0.0f;
    }

    [[nodiscard]] constexpr auto cross_product(const Vector &rhs) const noexcept -> Vector
    {
        return Vector{
            mVals[1]*rhs.mVals[2] - mVals[2]*rhs.mVals[1],
            mVals[2]*rhs.mVals[0] - mVals[0]*rhs.mVals[2],
            mVals[0]*rhs.mVals[1] - mVals[1]*rhs.mVals[0],
            0.0f};
    }

    [[nodiscard]] constexpr auto dot_product(const Vector &rhs) const noexcept -> f32
    { return mVals[0]*rhs.mVals[0] + mVals[1]*rhs.mVals[1] + mVals[2]*rhs.mVals[2]; }
};

class Matrix {
    alignas(16) std::array<f32, 16> mVals{};

public:
    constexpr Matrix() noexcept = default;
    constexpr Matrix(const Matrix&) noexcept = default;
    constexpr Matrix(Matrix&&) noexcept = default;
    constexpr explicit Matrix(
        f32 const aa, f32 const ab, f32 const ac, f32 const ad,
        f32 const ba, f32 const bb, f32 const bc, f32 const bd,
        f32 const ca, f32 const cb, f32 const cc, f32 const cd,
        f32 const da, f32 const db, f32 const dc, f32 const dd) noexcept
        : mVals{{aa,ab,ac,ad, ba,bb,bc,bd, ca,cb,cc,cd, da,db,dc,dd}}
    { }

    constexpr auto operator=(const Matrix&) & noexcept LIFETIMEBOUND -> Matrix& = default;
    constexpr auto operator=(Matrix&&) & noexcept LIFETIMEBOUND -> Matrix& = default;

    [[nodiscard]] constexpr auto operator[](usize const idx) noexcept LIFETIMEBOUND
    { return std::span<f32, 4>{&mVals[idx*4], 4}; }
    [[nodiscard]] constexpr auto operator[](usize const idx) const noexcept LIFETIMEBOUND
    { return std::span<f32 const, 4>{&mVals[idx*4], 4}; }

    [[nodiscard]] static constexpr auto Identity() noexcept -> Matrix
    {
        return Matrix{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
    }

    [[nodiscard]] friend constexpr
    auto operator*(const Matrix &mtx, const Vector &vec) noexcept -> Vector
    {
        return Vector{
            vec[0]*mtx[0][0] + vec[1]*mtx[1][0] + vec[2]*mtx[2][0] + vec[3]*mtx[3][0],
            vec[0]*mtx[0][1] + vec[1]*mtx[1][1] + vec[2]*mtx[2][1] + vec[3]*mtx[3][1],
            vec[0]*mtx[0][2] + vec[1]*mtx[1][2] + vec[2]*mtx[2][2] + vec[3]*mtx[3][2],
            vec[0]*mtx[0][3] + vec[1]*mtx[1][3] + vec[2]*mtx[2][3] + vec[3]*mtx[3][3]};
    }
};

} // namespace al

#endif /* COMMON_VECMAT_H */
