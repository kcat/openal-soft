#ifndef AL_FORMATZSV_HPP
#define AL_FORMATZSV_HPP

#include <type_traits>

#include "alformat.hpp"
#include "zstring_view.hpp"

namespace al::detail_ {
    template<typename T, template<typename...> typename U>
    inline constexpr auto is_instance_of_v = false;

    template<template<typename...> typename U, typename... Vs>
    inline constexpr auto is_instance_of_v<U<Vs...>, U> = true;

    template<typename T>
    concept zstring_view_type = detail_::is_instance_of_v<std::remove_cvref_t<T>, basic_zstring_view>;
}

template<al::detail_::zstring_view_type T, typename CharT>
struct al::formatter<T, CharT> : formatter<typename T::underlying_type, CharT> {
    using fmttype_t = typename T::underlying_type;

    auto format(T const &zsv, auto& ctx) const
    { return formatter<fmttype_t,CharT>::format(zsv, ctx); }
};

#endif /* AL_FORMATZSV_HPP */
