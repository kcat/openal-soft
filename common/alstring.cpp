
#include "config.h"

#include "alstring.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <iterator>


namespace {

template<std::input_iterator T, typename F> constexpr
auto find_not_zero(T iter0, T end0, T iter1, T end1, F&& comp) -> int
{
    auto count = std::min(std::distance(iter0, end0), std::distance(iter1, end1));
    while(count > 0)
    {
        if(const auto ret = comp(*iter0, *iter1))
            return ret;
        ++iter0;
        ++iter1;
        --count;
    }
    return 0;
}

} // namespace

namespace al {

int case_compare(const std::string_view str0, const std::string_view str1) noexcept
{
    using Traits = std::string_view::traits_type;

    const auto diff = find_not_zero(str0.cbegin(), str0.cend(), str1.cbegin(), str1.cend(),
        [](const char ch0, const char ch1) -> int
    {
        const auto u0 = std::toupper(Traits::to_int_type(ch0));
        const auto u1 = std::toupper(Traits::to_int_type(ch1));
        return u0 - u1;
    });
    if(diff != 0)
        return diff;

    if(str0.size() < str1.size()) return -1;
    if(str0.size() > str1.size()) return 1;
    return 0;
}

int case_compare(const std::wstring_view str0, const std::wstring_view str1) noexcept
{
    using Traits = std::wstring_view::traits_type;

    const auto diff = find_not_zero(str0.cbegin(), str0.cend(), str1.cbegin(), str1.cend(),
        [](const wchar_t ch0, const wchar_t ch1) -> int
    {
        const auto u0 = std::towupper(Traits::to_int_type(ch0));
        const auto u1 = std::towupper(Traits::to_int_type(ch1));
        return static_cast<int>(u0 - u1);
    });
    if(diff != 0)
        return diff;

    if(str0.size() < str1.size()) return -1;
    if(str0.size() > str1.size()) return 1;
    return 0;
}

} // namespace al
