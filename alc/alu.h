#ifndef ALU_H
#define ALU_H

#include <cstdint>
#include <optional>

#include "bitset.hpp"

struct EffectSlotBase;

enum class StereoEncoding : std::uint8_t;

namespace al {
struct Context;
struct Device;
} // namespace al

constexpr inline auto GainMixMax = 1000.0f; /* +60dB */


enum class CompatFlags : std::uint8_t {
    ReverseX,
    ReverseY,
    ReverseZ,

    MaxValue = ReverseZ
};
using CompatFlagBitset = al::bitset<CompatFlags>;

void aluInit(CompatFlagBitset flags, float nfcscale);

/* aluInitRenderer
 *
 * Set up the appropriate panning method and mixing method given the device
 * properties.
 */
void aluInitRenderer(al::Device *device, int hrtf_id, std::optional<StereoEncoding> stereomode);

void aluInitEffectPanning(EffectSlotBase *slot, al::Context *context);

#endif
