
#include "config.h"

#include "alstring.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <string>


namespace al {

int case_compare(const std::string_view str0, const std::string_view str1) noexcept
{
    using Traits = std::string_view::traits_type;

    auto ch0 = str0.cbegin();
    auto ch1 = str1.cbegin();
    auto ch1end = ch1 + std::min(str0.size(), str1.size());
    while(ch1 != ch1end)
    {
        const int u0{std::toupper(Traits::to_int_type(*ch0))};
        const int u1{std::toupper(Traits::to_int_type(*ch1))};
        if(const int diff{u0-u1}) return diff;
        ++ch0; ++ch1;
    }

    if(str0.size() < str1.size()) return -1;
    if(str0.size() > str1.size()) return 1;
    return 0;
}

int case_compare(const std::wstring_view str0, const std::wstring_view str1) noexcept
{
    using Traits = std::wstring_view::traits_type;

    auto ch0 = str0.cbegin();
    auto ch1 = str1.cbegin();
    auto ch1end = ch1 + std::min(str0.size(), str1.size());
    while(ch1 != ch1end)
    {
        const auto u0 = std::towupper(Traits::to_int_type(*ch0));
        const auto u1 = std::towupper(Traits::to_int_type(*ch1));
        if(const auto diff = static_cast<int>(u0-u1)) return diff;
        ++ch0; ++ch1;
    }

    if(str0.size() < str1.size()) return -1;
    if(str0.size() > str1.size()) return 1;
    return 0;
}

int strcasecmp(const char *str0, const char *str1) noexcept
{ return case_compare(str0, str1); }

int strncasecmp(const char *str0, const char *str1, std::size_t len) noexcept
{
    return case_compare(std::string_view{str0, std::min(std::strlen(str0), len)},
        std::string_view{str1, std::min(std::strlen(str1), len)});
}

} // namespace al
