#ifndef CORE_DEVFMTTRAITS_HPP
#define CORE_DEVFMTTRAITS_HPP

#include <cstdint>

#include "devformat.h"

namespace detail {

template<DevFmtType Type> [[nodiscard]] consteval
auto detect_fmttype() noexcept
{
    if constexpr(Type == DevFmtByte) return std::int8_t{};
    else if constexpr(Type == DevFmtUByte) return std::uint8_t{};
    else if constexpr(Type == DevFmtShort) return std::int16_t{};
    else if constexpr(Type == DevFmtUShort) return std::uint16_t{};
    else if constexpr(Type == DevFmtInt) return std::int32_t{};
    else if constexpr(Type == DevFmtUInt) return std::uint32_t{};
    else if constexpr(Type == DevFmtFloat) return float{};
    else static_assert(Type == DevFmtFloat, "Unexpected DevFmtType");
}

}

/* DevFmtType trait, providing the sample type of a DevFmtType. */
template<DevFmtType T>
using DevFmtType_t = decltype(detail::detect_fmttype<T>());

#endif /* CORE_DEVFMTTRAITS_HPP */
