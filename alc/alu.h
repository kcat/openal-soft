#ifndef ALU_H
#define ALU_H

#include <bitset>
#include <cstdint>
#include <optional>

struct ALCcontext;
struct ALCdevice;
struct EffectSlot;

enum class StereoEncoding : std::uint8_t;

namespace al {
struct Device;
} // namespace al

constexpr float GainMixMax{1000.0f}; /* +60dB */


enum CompatFlags : std::uint8_t {
    ReverseX,
    ReverseY,
    ReverseZ,

    Count
};
using CompatFlagBitset = std::bitset<CompatFlags::Count>;

void aluInit(CompatFlagBitset flags, const float nfcscale);

/* aluInitRenderer
 *
 * Set up the appropriate panning method and mixing method given the device
 * properties.
 */
void aluInitRenderer(al::Device *device, int hrtf_id, std::optional<StereoEncoding> stereomode);

void aluInitEffectPanning(EffectSlot *slot, ALCcontext *context);

#endif
