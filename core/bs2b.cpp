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

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <numbers>
#include <ranges>
#include <span>
#include <stdexcept>

#include "bs2b.h"
#include "gsl/gsl"

namespace {

/* Set up all data. */
void init(Bs2b::bs2b_processor *bs2b)
{
    const auto [Fc_lo, Fc_hi, G_lo, G_hi] = std::invoke([bs2b]
    {
        switch(bs2b->level)
        {
        case Bs2b::LowCLevel: /* Low crossfeed level */
            return std::array{360.0f,  501.0f, 0.398107170553497f, 0.205671765275719f};

        case Bs2b::MiddleCLevel: /* Middle crossfeed level */
            return std::array{500.0f,  711.0f, 0.459726988530872f, 0.228208484414988f};

        case Bs2b::HighCLevel: /* High crossfeed level (virtual speakers are closer to itself) */
            return std::array{700.0f, 1021.0f, 0.530884444230988f, 0.250105790667544f};

        case Bs2b::LowECLevel: /* Low easy crossfeed level */
            return std::array{360.0f,  494.0f, 0.316227766016838f, 0.168236228897329f};

        case Bs2b::MiddleECLevel: /* Middle easy crossfeed level */
            return std::array{500.0f,  689.0f, 0.354813389233575f, 0.187169483835901f};

        case Bs2b::HighECLevel: /* High easy crossfeed level */
        default:
            bs2b->level = Bs2b::HighECLevel;
            return std::array{700.0f,  975.0f, 0.398107170553497f, 0.205671765275719f};
        }
    });

    const auto g = 1.0f / (1.0f - G_hi + G_lo);

    /* $fc = $Fc / $s;
     * $d  = 1 / 2 / pi / $fc;
     * $x  = exp(-1 / $d);
     */
    auto x = std::exp(-std::numbers::pi_v<float>*2.0f*Fc_lo/gsl::narrow_cast<float>(bs2b->srate));
    bs2b->b1_lo = x;
    bs2b->a0_lo = G_lo * (1.0f - x) * g;

    x = std::exp(-std::numbers::pi_v<float>*2.0f*Fc_hi/gsl::narrow_cast<float>(bs2b->srate));
    bs2b->b1_hi = x;
    bs2b->a0_hi = (1.0f - G_hi * (1.0f - x)) * g;
    bs2b->a1_hi = -x * g;
}

} // namespace

/* Exported functions.
 * See descriptions in "bs2b.h"
 */
namespace Bs2b {

void bs2b_processor::set_params(int level_, int srate_)
{
    if(srate_ < 1)
        throw std::runtime_error{"BS2B srate < 1"};

    level = level_;
    srate = srate_;
    init(this);
}

void bs2b_processor::clear()
{
    history.fill(t_last_sample{});
}

void bs2b_processor::cross_feed(const std::span<float> Left, const std::span<float> Right)
{
    const auto a0lo = a0_lo;
    const auto b1lo = b1_lo;
    const auto a0hi = a0_hi;
    const auto a1hi = a1_hi;
    const auto b1hi = b1_hi;
    auto lsamples = Left.first(std::min(Left.size(), Right.size()));
    auto rsamples = Right.first(lsamples.size());
    auto samples = std::array<std::array<float,2>,128>{};

    auto leftio = lsamples.begin();
    auto rightio = rsamples.begin();
    while(auto rem = std::distance(leftio, lsamples.end()))
    {
        const auto todo = std::min<ptrdiff_t>(std::ssize(samples), rem);

        /* Process left input */
        auto z_lo = history[0].lo;
        auto z_hi = history[0].hi;
        std::ranges::transform(std::views::counted(leftio, todo), samples.begin(),
            [a0hi,a1hi,b1hi,a0lo,b1lo,&z_lo,&z_hi](const float x) noexcept
        {
            const auto y0 = a0hi*x + z_hi;
            z_hi = a1hi*x + b1hi*y0;

            const auto y1 = a0lo*x + z_lo;
            z_lo = b1lo*y1;

            return std::array{y0, y1};
        });
        history[0].lo = z_lo;
        history[0].hi = z_hi;

        /* Process right input */
        z_lo = history[1].lo;
        z_hi = history[1].hi;
        std::ranges::transform(std::views::counted(rightio, todo), samples, samples.begin(),
            [a0hi,a1hi,b1hi,a0lo,b1lo,&z_lo,&z_hi](const float x, const std::span<float,2> cur)
        {
            const auto y0 = a0lo*x + z_lo;
            z_lo = b1lo*y0;

            const auto y1 = a0hi*x + z_hi;
            z_hi = a1hi*x + b1hi*y1;

            return std::array{cur[0]+y0, cur[1]+y1};
        });
        history[1].lo = z_lo;
        history[1].hi = z_hi;

        const auto samples_todo = samples | std::views::take(todo);
        leftio = std::ranges::copy(samples_todo | std::views::elements<0>, leftio).out;
        rightio = std::ranges::copy(samples_todo | std::views::elements<1>, rightio).out;
    }
}

} // namespace Bs2b
