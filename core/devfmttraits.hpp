#ifndef CORE_DEVFMTTRAITS_HPP
#define CORE_DEVFMTTRAITS_HPP

#include "altypes.hpp"
#include "devformat.h"

namespace detail {

template<DevFmtType Type> [[nodiscard]] consteval
auto detect_fmttype() noexcept
{
    if constexpr(Type == DevFmtByte) return i8{};
    else if constexpr(Type == DevFmtUByte) return u8{};
    else if constexpr(Type == DevFmtShort) return i16{};
    else if constexpr(Type == DevFmtUShort) return u16{};
    else if constexpr(Type == DevFmtInt) return i32{};
    else if constexpr(Type == DevFmtUInt) return u32{};
    else if constexpr(Type == DevFmtFloat) return f32{};
    else static_assert(Type == DevFmtFloat, "Unexpected DevFmtType");
}

}

/* DevFmtType trait, providing the sample type of a DevFmtType. */
template<DevFmtType T>
using DevFmtType_t = decltype(detail::detect_fmttype<T>());

#endif /* CORE_DEVFMTTRAITS_HPP */
