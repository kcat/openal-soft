module;

#include "alformat.hpp"
#include "altypes.hpp"

export module format.types;

namespace al {

template<strict_number SelfType, typename CharT>
struct strict_formatter : formatter<typename SelfType::fmttype_t, CharT> {
    using fmttype_t = typename SelfType::fmttype_t;

    auto format(SelfType const &obj, auto& ctx) const
    { return formatter<fmttype_t,CharT>::format(al::convert_to<fmttype_t>(obj.c_val), ctx); }
};

}

export {
    template<typename CharT> struct al::formatter<i8, CharT> : al::strict_formatter<i8, CharT> { };
    template<typename CharT> struct al::formatter<u8, CharT> : al::strict_formatter<u8, CharT> { };
    template<typename CharT> struct al::formatter<i16, CharT> : al::strict_formatter<i16, CharT> { };
    template<typename CharT> struct al::formatter<u16, CharT> : al::strict_formatter<u16, CharT> { };
    template<typename CharT> struct al::formatter<i32, CharT> : al::strict_formatter<i32, CharT> { };
    template<typename CharT> struct al::formatter<u32, CharT> : al::strict_formatter<u32, CharT> { };
    template<typename CharT> struct al::formatter<i64, CharT> : al::strict_formatter<i64, CharT> { };
    template<typename CharT> struct al::formatter<u64, CharT> : al::strict_formatter<u64, CharT> { };
    template<typename CharT> struct al::formatter<f32, CharT> : al::strict_formatter<f32, CharT> { };
    template<typename CharT> struct al::formatter<f64, CharT> : al::strict_formatter<f64, CharT> { };
    template<typename CharT> struct al::formatter<isize, CharT> : al::strict_formatter<isize, CharT> { };
    template<typename CharT> struct al::formatter<usize, CharT> : al::strict_formatter<usize, CharT> { };
}
