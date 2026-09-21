#ifndef AL_FORMATTYPES_HPP
#define AL_FORMATTYPES_HPP

#include "alformat.hpp"
#include "altypes.hpp"


template<al::strict_number SelfType, typename CharT>
struct al::formatter<SelfType, CharT> : formatter<typename SelfType::fmttype_t, CharT> {
    using fmttype_t = typename SelfType::fmttype_t;

    auto format(SelfType const &obj, auto& ctx) const
    { return formatter<fmttype_t,CharT>::format(al::convert_to<fmttype_t>(obj.c_val), ctx); }
};

#endif /* AL_FORMATTYPES_HPP */
