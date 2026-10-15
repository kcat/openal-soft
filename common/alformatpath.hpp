#ifndef ALFORMATPATH_HPP
#define ALFORMATPATH_HPP

#include <concepts>
#include <string_view>

#include "alformat.hpp"
#include "filesystem.h"

#if defined(_WIN32)
#include "alstring.h"
#endif


#if !defined(GHC_USE_STD_FS) || !defined(__cpp_lib_format_path) \
    || !(__cpp_lib_format_path >= 202403L)

template<typename CharT>
struct al::formatter<fs::path, CharT> : al::formatter<std::basic_string_view<CharT>, CharT> {
    using fmttype_t = std::basic_string_view<CharT>;

    auto format(fs::path const &path, auto& ctx) const
    {
        if constexpr(std::same_as<fs::path::value_type, CharT>)
            return formatter<fmttype_t, CharT>::format(path.native(), ctx);
#if defined(_WIN32)
        else if constexpr(std::same_as<fs::path::value_type, wchar_t> and std::same_as<char,CharT>)
            return formatter<fmttype_t, CharT>::format(wstr_to_utf8(path.native()), ctx);
#endif
        else
            return formatter<fmttype_t, CharT>::format(path.string<CharT>(), ctx);
    }
};

#endif

#endif /* ALFORMATPATH_HPP */
