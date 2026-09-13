#ifndef CORE_BSINC_DEFS_H
#define CORE_BSINC_DEFS_H

/* The number of distinct scale and phase intervals within the bsinc filter
 * tables.
 */
inline auto constexpr BSincScaleBits = 4u;
inline auto constexpr BSincScaleCount = 1u << BSincScaleBits;
inline auto constexpr BSincPhaseBits = 5u;
inline auto constexpr BSincPhaseCount = 1u << BSincPhaseBits;

#endif /* CORE_BSINC_DEFS_H */
