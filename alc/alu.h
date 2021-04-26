#ifndef ALU_H
#define ALU_H

#include <string>

#include "aloptional.h"

struct ALCcontext;
struct ALCdevice;
struct EffectSlot;


#define MAX_SENDS  6


constexpr float GainMixMax{1000.0f}; /* +60dB */

constexpr float SpeedOfSoundMetersPerSec{343.3f};
constexpr float AirAbsorbGainHF{0.99426f}; /* -0.05dB */


enum HrtfRequestMode {
    Hrtf_Default = 0,
    Hrtf_Enable = 1,
    Hrtf_Disable = 2,
};

void aluInit(void);

void aluInitMixer(al::optional<std::string> resampler);

/* aluInitRenderer
 *
 * Set up the appropriate panning method and mixing method given the device
 * properties.
 */
void aluInitRenderer(ALCdevice *device, int hrtf_id, HrtfRequestMode hrtf_appreq,
    HrtfRequestMode hrtf_userreq);

void aluInitEffectPanning(EffectSlot *slot, ALCcontext *context);


extern const float ConeScale;
extern const float ZScale;

#endif
