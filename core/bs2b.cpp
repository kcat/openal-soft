/*-
 * Copyright (c) 2005 Boris Mikhaylov
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <iterator>
#include <ranges>
#include <span>

#include "opthelper_macros.hpp"

#if HAVE_CXXMODULES
import common.std;

import core.bs2b;
import gsl;

#else

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <stdexcept>

#include "core/bs2b.hpp"
#include "gsl/gsl"
#endif


namespace Bs2b {

/* Set up all data. */
auto bs2b_processor::init() -> void
{
    auto const [Fc_lo, Fc_hi, G_lo, G_hi] = std::invoke([this]
    {
        switch(mLevel)
        {
        case Level::LowCLevel: /* Low crossfeed level */
            return std::array{360.0f,  501.0f, 0.398107170553497f, 0.205671765275719f};

        case Level::MiddleCLevel: /* Middle crossfeed level */
            return std::array{500.0f,  711.0f, 0.459726988530872f, 0.228208484414988f};

        case Level::HighCLevel: /* High crossfeed level (virtual speakers are closer to itself) */
            return std::array{700.0f, 1021.0f, 0.530884444230988f, 0.250105790667544f};

        case Level::LowECLevel: /* Low easy crossfeed level */
            return std::array{360.0f,  494.0f, 0.316227766016838f, 0.168236228897329f};

        case Level::MiddleECLevel: /* Middle easy crossfeed level */
            return std::array{500.0f,  689.0f, 0.354813389233575f, 0.187169483835901f};

        case Level::HighECLevel: /* High easy crossfeed level */
        default:
            mLevel = Level::HighECLevel;
            return std::array{700.0f,  975.0f, 0.398107170553497f, 0.205671765275719f};
        }
    });

    auto const g = 1.0f / (1.0f - G_hi + G_lo);

    /* $fc = $Fc / $s;
     * $d  = 1 / 2 / pi / $fc;
     * $x  = exp(-1 / $d);
     */
    auto x = std::exp(-std::numbers::pi_v<float>*2.0f * Fc_lo
        / gsl::narrow_cast<float>(mSampleRate));
    b1_lo = x;
    a0_lo = G_lo * (1.0f - x) * g;

    x = std::exp(-std::numbers::pi_v<float>*2.0f * Fc_hi / gsl::narrow_cast<float>(mSampleRate));
    b1_hi = x;
    a0_hi = (1.0f - G_hi * (1.0f - x)) * g;
    a1_hi = -x * g;
}

void bs2b_processor::set_params(int const level, int const srate)
{
    if(srate < 1)
        throw std::runtime_error{"BS2B srate < 1"};

    mLevel = level;
    mSampleRate = srate;
    init();
}

void bs2b_processor::clear()
{
    history.fill(t_last_sample{});
}

void bs2b_processor::cross_feed(std::span<float> const Left, std::span<float> const Right) noexcept
    NONBLOCKING
{
    auto const a0lo = a0_lo;
    auto const b1lo = b1_lo;
    auto const a0hi = a0_hi;
    auto const a1hi = a1_hi;
    auto const b1hi = b1_hi;
    auto lsamples = Left.first(std::min(Left.size(), Right.size()));
    auto rsamples = Right.first(lsamples.size());
    auto samples = std::array<std::array<float, 2>, 128>{};

    auto leftio = lsamples.begin();
    auto rightio = rsamples.begin();
    while(auto rem = std::distance(leftio, lsamples.end()))
    {
        auto const todo = std::min<ptrdiff_t>(std::ssize(samples), rem);

        /* Process left input */
        auto z_lo = history[0].lo;
        auto z_hi = history[0].hi;
        std::ranges::transform(std::views::counted(leftio, todo), samples.begin(),
            [a0hi, a1hi, b1hi, a0lo, b1lo, &z_lo, &z_hi](float const x) noexcept
        {
            auto const y0 = a0hi*x + z_hi;
            z_hi = a1hi*x + b1hi*y0;

            auto const y1 = a0lo*x + z_lo;
            z_lo = b1lo*y1;

            return std::array{y0, y1};
        });
        history[0].lo = z_lo;
        history[0].hi = z_hi;

        /* Process right input */
        z_lo = history[1].lo;
        z_hi = history[1].hi;
        std::ranges::transform(std::views::counted(rightio, todo), samples, samples.begin(),
            [a0hi,a1hi,b1hi,a0lo,b1lo,&z_lo,&z_hi](float const x, const std::span<float, 2> cur)
        {
            auto const y0 = a0lo*x + z_lo;
            z_lo = b1lo*y0;

            auto const y1 = a0hi*x + z_hi;
            z_hi = a1hi*x + b1hi*y1;

            return std::array{cur[0]+y0, cur[1]+y1};
        });
        history[1].lo = z_lo;
        history[1].hi = z_hi;

        auto const samples_todo = samples | std::views::take(todo);
        leftio = std::ranges::copy(samples_todo | std::views::elements<0>, leftio).out;
        rightio = std::ranges::copy(samples_todo | std::views::elements<1>, rightio).out;
    }
}

} // namespace Bs2b
