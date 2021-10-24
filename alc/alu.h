#ifndef ALU_H
#define ALU_H

#include "aloptional.h"

struct ALCcontext;
struct ALCdevice;
struct EffectSlot;

enum class StereoEncoding : unsigned char;


constexpr float GainMixMax{1000.0f}; /* +60dB */

constexpr float AirAbsorbGainHF{0.99426f}; /* -0.05dB */


void aluInit(void);

/* aluInitRenderer
 *
 * Set up the appropriate panning method and mixing method given the device
 * properties.
 */
void aluInitRenderer(ALCdevice *device, int hrtf_id, al::optional<StereoEncoding> stereomode);

void aluInitEffectPanning(EffectSlot *slot, ALCcontext *context);


extern const float ConeScale;
extern const float ZScale;

#endif
