#ifndef CORE_AMBIDEFS_H
#define CORE_AMBIDEFS_H

#include <cstddef>

/* The maximum number of Ambisonics channels. For a given order (o), the size
 * needed will be (o+1)**2, thus zero-order has 1, first-order has 4, second-
 * order has 9, third-order has 16, and fourth-order has 25.
 */
constexpr auto AmbiChannelsFromOrder(std::size_t const order) noexcept -> std::size_t
{ return (order+1) * (order+1); }

inline constexpr auto MaxAmbiOrder = std::size_t{4};
inline constexpr auto MaxAmbiChannels = AmbiChannelsFromOrder(MaxAmbiOrder);

/* A bitmask of ambisonic channels for 0 to 4th order. This only specifies up
 * to 4th order, which is the highest order a 32-bit mask value can specify (a
 * 64-bit mask could handle up to 7th order).
 */
inline constexpr auto Ambi0OrderMask = 0x00000001u;
inline constexpr auto Ambi1OrderMask = 0x0000000fu;
inline constexpr auto Ambi2OrderMask = 0x000001ffu;
inline constexpr auto Ambi3OrderMask = 0x0000ffffu;
inline constexpr auto Ambi4OrderMask = 0x01ffffffu;

/* A bitmask of ambisonic channels with height information. If none of these
 * channels are used/needed, there's no height (e.g. with most surround sound
 * speaker setups). This is ACN ordering, with bit 0 being ACN 0, etc.
 */
inline constexpr auto AmbiPeriphonicMask = 0xfe7ce4u;

/* The maximum number of ambisonic channels for 2D (non-periphonic)
 * representation. This is 2 per each order above zero-order, plus 1 for zero-
 * order. Or simply, o*2 + 1.
 */
constexpr auto Ambi2DChannelsFromOrder(std::size_t const order) noexcept -> std::size_t
{ return order*2 + 1; }
inline constexpr auto MaxAmbi2DChannels = Ambi2DChannelsFromOrder(MaxAmbiOrder);

#endif /* CORE_AMBIDEFS_H */
