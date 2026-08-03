/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#ifndef OAL_dynapi_dlopennote_h
#define OAL_dynapi_dlopennote_h

#define OAL_ELF_NOTE_DLOPEN_PRIORITY_REQUIRED    "required"
#define OAL_ELF_NOTE_DLOPEN_PRIORITY_RECOMMENDED "recommended"
#define OAL_ELF_NOTE_DLOPEN_PRIORITY_SUGGESTED   "suggested"

#if defined(__ELF__) && defined(HAVE_DLOPEN_NOTES)

/* Modified here to avoid unsafe C arrays and use proper C++ types. */
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <string_view>
#include <type_traits>

namespace NoteDlOpen {

inline constexpr auto Vendor = std::to_array("FDO");
inline constexpr auto Type = 0x407C0C0Au;

template<std::size_t json_len>
struct [[gnu::aligned(4)]] Structure {
    struct {
        std::uint32_t n_namesz{sizeof(Structure::name)};
        std::uint32_t n_descsz{sizeof(Structure::dlopen_json)};
        std::uint32_t n_type{Type};
    } nhdr;
    std::array<char, 4> name{Vendor};
    std::array<char, json_len> dlopen_json;

    explicit constexpr
    Structure(std::array<char, json_len> const &json) : dlopen_json{json} { }
};

template<std::size_t N>
Structure(std::array<char, N> const&) -> Structure<N>;

namespace detail_ {

    /* Gets the size of a C-style array type (e.g. char[N]) or the size of a
     * tuple-like type (e.g. std::array<char, N>). None of .size(), std::size,
     * or std::ranges::size support getting the size of a std::array in a
     * constexpr context, despite all calls being constexpr, and no type trait
     * supports getting the size of both C-style array and std::array types.
     */
    template<typename T> [[nodiscard]] consteval
    auto get_size() noexcept -> std::size_t
    {
        if constexpr(std::is_bounded_array_v<std::remove_reference_t<T>>)
            return std::extent_v<std::remove_reference_t<T>>;
        else
            return std::tuple_size_v<std::remove_reference_t<T>>;
    }

    /* Gets a string_view for the given range, excluding the final nul char. */
    [[nodiscard]] consteval
    auto get_strview(std::ranges::contiguous_range auto&& str) noexcept
    {
        if(std::ranges::size(str) == 0) std::abort();
        if(*(std::ranges::end(str)-1) != '\0') std::abort();
        return std::string_view{std::ranges::begin(str), std::ranges::size(str)-1};
    }

} // namespace detail_

/* Concatenates a set of char arrays (excluding their final nul chars) into a
 * single nul-terminated char array.
 */
template<std::ranges::contiguous_range ...Args> [[nodiscard]] consteval
auto Concat(Args&& ...args) noexcept
{
    constexpr auto tmplen = (... + (detail_::get_size<Args>()-1)) + 1;
    auto arr = std::array<char, tmplen>{};
    auto oiter = arr.begin();
    auto do_concat = [&oiter](std::string_view const str)
    {
        oiter = std::ranges::copy(str, oiter).out;
    };
    (..., do_concat(detail_::get_strview(std::forward<Args>(args))));
    return arr;
}

/* Combines a set of char arrays (excluding their final nul chars) into a
 * single nul-terminated char array, with each element quoted and separated by
 * commas. e.g. calling QuotedList("foo", "bar") will return an array
 * containing the string "[\"foo\",\"bar\"]".
 */
template<std::ranges::contiguous_range S1, std::ranges::contiguous_range ...Args>
[[nodiscard]] consteval auto QuotedList(S1&& s1, Args&& ...more) noexcept
{
    constexpr auto tmplen = ((detail_::get_size<S1>()-1) + ... + (detail_::get_size<Args>()-1))
        + 3*sizeof...(Args) + 5;
    auto arr = std::array<char, tmplen>{};
    auto oiter = std::ranges::copy(detail_::get_strview("[\""), arr.begin()).out;
    oiter = std::ranges::copy(detail_::get_strview(std::forward<S1>(s1)), oiter).out;
    auto do_concat = [sep=detail_::get_strview("\",\""), &oiter](std::string_view const str)
    {
        oiter = std::ranges::copy(std::array{sep, str} | std::views::join, oiter).out;
    };
    (..., do_concat(detail_::get_strview(std::forward<Args>(more))));
    std::ranges::copy(detail_::get_strview("\"]"), oiter);
    return arr;
}


template<typename FT, typename DT, typename PT, typename ...Args> [[nodiscard]] consteval
auto MakeNote(FT&& feature, DT&& description, PT&& priority, Args&& ...sonames) noexcept
{
    auto const json = Concat(
        "[{\"feature\":\"", std::forward<FT>(feature),
        "\",\"description\":\"", std::forward<DT>(description),
        "\",\"priority\":\"", std::forward<PT>(priority),
        "\",\"soname\":", QuotedList(std::forward<Args>(sonames)...), "}]");
    return Structure{json};
}

} // namespace DlOpen

/* Create "unique" variable name using __LINE__,
 * so creating elf notes on the same line is not supported
 * C++26 would allow using _ as an anonymous variable without needing to
 * generate a unique name, since it's not accessed after being defined.
 */
#define OAL_ELF_NOTE_JOIN2(A,B) A##B
#define OAL_ELF_NOTE_JOIN(A,B) OAL_ELF_NOTE_JOIN2(A,B)
#define OAL_ELF_NOTE_UNIQUE_NAME OAL_ELF_NOTE_JOIN(s_dlopen_note_, __LINE__)

#define OAL_ELF_NOTE_DLOPEN(feature, description, priority, ...)    \
    [[gnu::used, gnu::section(".note.dlopen")]]                     \
    constexpr auto OAL_ELF_NOTE_UNIQUE_NAME = NoteDlOpen::MakeNote( \
        feature, description, priority, __VA_ARGS__)

#else

#define OAL_ELF_NOTE_DLOPEN(...)

#endif

#endif
