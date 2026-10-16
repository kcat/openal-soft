#ifndef ALFORMATPATH_HPP
#define ALFORMATPATH_HPP

#include "alformat.hpp"
#include "filesystem.h"
#if !USING_STD_FORMAT
#include "fmt/std.h"
#endif


namespace fs::detail {

template<typename CharT>
auto get_path_as(fs::path const& path) -> decltype(auto)
{
    if constexpr(std::same_as<fs::path::value_type, CharT>)
        return path.native();
#if defined(_WIN32)
    else if constexpr(std::same_as<fs::path::value_type, wchar_t> and std::same_as<char, CharT>)
        return wstr_to_utf8(path.native());
#endif
    else
        return path.string<CharT>();
}

}

#if USING_STD_FORMAT && (!defined(__cpp_lib_format_path) || !(__cpp_lib_format_path >= 202403L) \
        || !defined(GHC_USE_STD_FS))

#include <concepts>
#include <string_view>

#if defined(_WIN32)
#include "alstring.h"
#endif

template<typename CharT>
struct std::formatter<fs::path, CharT> : std::formatter<std::basic_string_view<CharT>, CharT> {
    using fmttype_t = std::basic_string_view<CharT>;

    auto format(fs::path const &path, auto& ctx) const
    { return formatter<fmttype_t, CharT>::format(fs::detail::get_path_as<CharT>(path), ctx); }
};

#endif

#if !USING_STD_FORMAT && !defined(GHC_USE_STD_FS)

#include <concepts>
#include <string_view>

#if defined(_WIN32)
#include "alstring.h"
#endif

template<typename CharT>
struct fmt::formatter<fs::path, CharT> : fmt::formatter<std::basic_string_view<CharT>, CharT> {
    using fmttype_t = std::basic_string_view<CharT>;

    auto format(fs::path const &path, auto& ctx) const
    { return formatter<fmttype_t, CharT>::format(fs::detail::get_path_as<CharT>(path), ctx); }
};

#endif

#endif /* ALFORMATPATH_HPP */
