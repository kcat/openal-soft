#ifndef AL_STRING_H
#define AL_STRING_H

#include <algorithm>
#include <cctype>
#include <compare>
#include <concepts>
#include <cwctype>
#include <string_view>

#include "opthelpers.h"

#if defined(_WIN32)
#include <string>

auto wstr_to_utf8(std::wstring_view wstr) -> std::string;
auto utf8_to_wstr(std::string_view str) -> std::wstring;
#endif

namespace al {

[[nodiscard]] constexpr
auto contains(std::string_view const& str0, std::string_view const& str1) noexcept -> bool
{ return str0.find(str1) != std::string_view::npos; }

namespace detail {
    template<typename CharT, typename Traits> NOINLINE [[nodiscard]]
    auto case_compare(std::basic_string_view<CharT, Traits> const& str0,
        std::basic_string_view<CharT, Traits> const& str1) noexcept -> std::weak_ordering
    {
        return std::lexicographical_compare_three_way(str0.cbegin(), str0.cend(),
            str1.cbegin(), str1.cend(), [](CharT const ch0, CharT const ch1) -> std::weak_ordering
        {
            /* Treat char8_t as normal char for comparison purposes. We
             * basically do this anyway when storing UTF-8 in char strings, or
             * use u8_as_char to reinterpret char8_t strings as char strings.
             */
            if constexpr(std::same_as<char8_t, CharT> or std::same_as<char, CharT>)
                return std::toupper(static_cast<int>(Traits::to_int_type(ch0))) <=>
                    std::toupper(static_cast<int>(Traits::to_int_type(ch1)));
            else if constexpr(std::same_as<wchar_t, CharT>)
                return std::towupper(Traits::to_int_type(ch0)) <=>
                    std::towupper(Traits::to_int_type(ch1));
            else
            {
                static_assert(al::dependent_false<CharT>, "Unhandled character type");
                return std::weak_ordering::equivalent;
            }
        });
    }
}

[[nodiscard]] inline
auto case_compare(std::string_view const& str0, std::string_view const& str1) noexcept
    -> std::weak_ordering
{ return detail::case_compare(str0, str1); }

[[nodiscard]] inline
auto case_compare(std::u8string_view const& str0, std::u8string_view const& str1) noexcept
    -> std::weak_ordering
{ return detail::case_compare(str0, str1); }

[[nodiscard]] inline
auto case_compare(std::wstring_view const& str0, std::wstring_view const& str1) noexcept
    -> std::weak_ordering
{ return detail::case_compare(str0, str1); }


/* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
 * C++20 changes path::u8string() to return a string using a new/distinct
 * char8_t type for UTF-8 strings, and deprecates u8path in favor of using
 * fs::path(char8_t*). However, support for this type with standard string
 * functions is totally inadequate, and we already hold UTF-8 with plain char*
 * strings. So these functions are used to reinterpret between char and char8_t
 * string views.
 */
[[nodiscard]] inline
auto char_as_u8(std::string_view const str LIFETIMEBOUND) -> std::u8string_view
{ return std::u8string_view{reinterpret_cast<const char8_t*>(str.data()), str.size()}; }

[[nodiscard]] inline
auto u8_as_char(std::u8string_view const str LIFETIMEBOUND) -> std::string_view
{ return std::string_view{reinterpret_cast<const char*>(str.data()), str.size()}; }
/* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

} // namespace al

#endif /* AL_STRING_H */
