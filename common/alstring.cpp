
#include "config.h"

#include "alstring.h"

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstring>
#include <cwctype>


namespace al {

auto case_compare(const std::string_view str0, const std::string_view str1) noexcept
    -> std::weak_ordering
{
    return std::lexicographical_compare_three_way(str0.cbegin(), str0.cend(),
        str1.cbegin(), str1.cend(), [](const char ch0, const char ch1) -> std::weak_ordering
    {
        using Traits = std::string_view::traits_type;
        return std::toupper(Traits::to_int_type(ch0)) <=> std::toupper(Traits::to_int_type(ch1));
    });
}

auto case_compare(const std::wstring_view str0, const std::wstring_view str1) noexcept
    -> std::weak_ordering
{
    return std::lexicographical_compare_three_way(str0.cbegin(), str0.cend(),
        str1.cbegin(), str1.cend(), [](const wchar_t ch0, const wchar_t ch1) -> std::weak_ordering
    {
        using Traits = std::wstring_view::traits_type;
        return std::towupper(Traits::to_int_type(ch0)) <=> std::towupper(Traits::to_int_type(ch1));
    });
}

} // namespace al
