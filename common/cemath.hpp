#ifndef AL_CEMATH_HPP
#define AL_CEMATH_HPP

#include <cmath>
#include <concepts>


namespace ce {

template<typename T> requires(std::signed_integral<T> or std::floating_point<T>) [[nodiscard]]
    constexpr
auto abs(T const x) noexcept -> T { return x < 0 ? -x : x; }

template<std::unsigned_integral T> [[nodiscard]] constexpr
auto abs(T const x) noexcept -> T { return x; }

template<std::floating_point T> [[nodiscard]] constexpr
auto sqrt(T const x) noexcept -> T
{
    if(std::is_constant_evaluated())
    {
        if(!(x >= T{0} && x < std::numeric_limits<T>::infinity()))
            return std::numeric_limits<T>::quiet_NaN();

        auto prev = T{0};
        auto curr = x;
        while(curr != prev)
        {
            prev = curr;
            curr = T{0.5} * (curr + x/curr);
        }
        return curr;
    }
    else
        return std::sqrt(x);
}

template<std::floating_point T> [[nodiscard]] constexpr
auto cos(T const x) noexcept -> T
{
    if(std::is_constant_evaluated())
    {
        auto const nx2 = -(x*x);
        auto fact = T{2};
        auto acc = 3llu;
        auto tmp = nx2;
        auto result = T{1} + tmp/fact;

        auto last_result = result;
        do {
            tmp *= nx2;
            fact *= static_cast<T>(acc * (acc+1));
            acc += 2;

            last_result = result;
            result += tmp / fact;
        } while(result != last_result);
        return result;
    }
    else
        return std::cos(x);
}

template<std::floating_point T> [[nodiscard]] constexpr
auto sin(T const x) noexcept -> T
{
    if(std::is_constant_evaluated())
    {
        auto const nx2 = -(x*x);
        auto fact = T{6};
        auto acc = 4llu;
        auto tmp = nx2*x;
        auto result = x + tmp/fact;

        auto last_result = result;
        do {
            tmp *= nx2;
            fact *= static_cast<T>(acc * (acc+1));
            acc += 2;

            last_result = result;
            result += tmp / fact;
        } while(result != last_result);
        return result;
    }
    else
        return std::sin(x);
}

}

#endif /* AL_CEMATH_HPP */
