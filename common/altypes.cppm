module;

#include "alformat.hpp"
#include "altypes.hpp"
#if USING_STD_FORMAT
#include "fmt/format.h"
#endif

export module types;

export {

    using ::i8;
    using ::u8;
    using ::i16;
    using ::u16;
    using ::i32;
    using ::u32;
    using ::i64;
    using ::u64;
    using ::f32;
    using ::f64;
    using ::isize;
    using ::usize;

    using ::operator""_i8;
    using ::operator""_u8;
    using ::operator""_i16;
    using ::operator""_u16;
    using ::operator""_i32;
    using ::operator""_u32;
    using ::operator""_i64;
    using ::operator""_u64;
    using ::operator""_f32;
    using ::operator""_f64;
    using ::operator""_isize;
    using ::operator""_usize;
    using ::operator""_z;
    using ::operator""_uz;
    using ::operator""_zu;

    inline namespace altypeops {
        using altypeops::operator++;
        using altypeops::operator--;
        using altypeops::operator+;
        using altypeops::operator-;
        using altypeops::operator~;
        using altypeops::operator+;
        using altypeops::operator-;
        using altypeops::operator*;
        using altypeops::operator/;
        using altypeops::operator%;
        using altypeops::operator|;
        using altypeops::operator&;
        using altypeops::operator^;
        using altypeops::operator+;
        using altypeops::operator-;
        using altypeops::operator~;
        using altypeops::operator<<;
        using altypeops::operator>>;
        using altypeops::operator+=;
        using altypeops::operator-=;
        using altypeops::operator*=;
        using altypeops::operator/=;
        using altypeops::operator%=;
        using altypeops::operator|=;
        using altypeops::operator&=;
        using altypeops::operator^=;
        using altypeops::operator<<=;
        using altypeops::operator>>=;
        using altypeops::operator<=>;
        using altypeops::operator==;

        using altypeops::popcount;
        using altypeops::countl_zero;
        using altypeops::countr_zero;
        using altypeops::abs;
        using altypeops::ceil;
        using altypeops::floor;
        using altypeops::sqrt;
        using altypeops::cbrt;
        using altypeops::sin;
        using altypeops::asin;
        using altypeops::cos;
        using altypeops::acos;
        using altypeops::atan2;
        using altypeops::pow;
        using altypeops::log;
        using altypeops::log2;
        using altypeops::log10;
        using altypeops::exp;
        using altypeops::exp2;
        using altypeops::round;
        using altypeops::lerp;
        using altypeops::lerpf;
    }

    namespace al {
        using al::number_base;
        using al::narrowing_error;
        using al::strict_number;
        using al::strict_integral;
        using al::strict_signed_integral;
        using al::strict_unsigned_integral;
        using al::strict_floating_point;
    }

    namespace std {
        using std::common_type;
    }

    template<al::strict_number SelfType, typename CharT>
    struct al::formatter<SelfType, CharT> : formatter<typename SelfType::fmttype_t, CharT> {
        using fmttype_t = typename SelfType::fmttype_t;

        auto format(SelfType const &obj, auto& ctx) const
        { return formatter<fmttype_t,CharT>::format(al::convert_to<fmttype_t>(obj.c_val), ctx); }
    };

#if USING_STD_FORMAT
    template<al::strict_number SelfType, typename CharT>
    struct fmt::formatter<SelfType, CharT> : formatter<typename SelfType::fmttype_t, CharT> {
        using fmttype_t = typename SelfType::fmttype_t;

        auto format(SelfType const &obj, auto& ctx) const
        { return formatter<fmttype_t,CharT>::format(al::convert_to<fmttype_t>(obj.c_val), ctx); }
    };
#endif

}
