module;

#include <type_traits>

#include "alformat.hpp"
#include "instance_of.hpp"
#include "zstring_view.hpp"

export module zstring_view;

#if USING_STD_FORMAT
import fmtlib;
#endif

export namespace al {
    using al::basic_zstring_view;
    using al::zstring_view;
    using al::u8zstring_view;
    using al::u16zstring_view;
    using al::u32zstring_view;
    using al::wzstring_view;

    inline namespace literals
    {
        inline namespace zstring_view_literals
        {
            using zstring_view_literals::operator ""_zsv;
        }
    }
}

export {

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

} /* export */
