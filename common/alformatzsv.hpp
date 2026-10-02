#ifndef AL_FORMATZSV_HPP
#define AL_FORMATZSV_HPP

#include <type_traits>

#include "alformat.hpp"
#include "instance_of.hpp"
#include "zstring_view.hpp"
#if USING_STD_FORMAT
#include "fmt/format.h"
#endif

template<al::instance_of<al::basic_zstring_view> T, typename CharT>
struct al::formatter<T, CharT> : formatter<typename T::underlying_type, CharT> {
    using fmttype_t = typename T::underlying_type;

    auto format(T const &zsv, auto& ctx) const
    { return formatter<fmttype_t,CharT>::format(zsv, ctx); }
};

#if USING_STD_FORMAT
template<al::instance_of<al::basic_zstring_view> T, typename CharT>
struct fmt::formatter<T, CharT> : formatter<typename T::underlying_type, CharT> {
    using fmttype_t = typename T::underlying_type;

    auto format(T const &zsv, auto& ctx) const
    { return formatter<fmttype_t,CharT>::format(zsv, ctx); }
};
#endif

#endif /* AL_FORMATZSV_HPP */
