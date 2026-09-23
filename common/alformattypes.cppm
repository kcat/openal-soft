module;

#include "alformat.hpp"
#include "altypes.hpp"
#if USING_STD_FORMAT
#include "fmt/format.h"
#endif

export module format.types;

export {

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
