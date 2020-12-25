
#include "config.h"

#include "ambidefs.h"


constexpr std::array<float,MaxAmbiChannels> AmbiScale::FromN3D;
constexpr std::array<float,MaxAmbiChannels> AmbiScale::FromSN3D;
constexpr std::array<float,MaxAmbiChannels> AmbiScale::FromFuMa;
constexpr std::array<uint8_t,MaxAmbiChannels> AmbiIndex::FromFuMa;
constexpr std::array<uint8_t,MaxAmbi2DChannels> AmbiIndex::FromFuMa2D;
constexpr std::array<uint8_t,MaxAmbiChannels> AmbiIndex::FromACN;
constexpr std::array<uint8_t,MaxAmbi2DChannels> AmbiIndex::FromACN2D;
constexpr std::array<uint8_t,MaxAmbiChannels> AmbiIndex::OrderFromChannel;
constexpr std::array<uint8_t,MaxAmbi2DChannels> AmbiIndex::OrderFrom2DChannel;
