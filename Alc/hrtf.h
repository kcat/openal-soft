#ifndef ALC_HRTF_H
#define ALC_HRTF_H

#include "AL/al.h"
#include "AL/alc.h"

enum DevFmtChannels;

struct Hrtf;

#define HRIR_BITS        (7)
#define HRIR_LENGTH      (1<<HRIR_BITS)
#define HRIR_MASK        (HRIR_LENGTH-1)
#define HRTFDELAY_BITS    (20)
#define HRTFDELAY_FRACONE (1<<HRTFDELAY_BITS)
#define HRTFDELAY_MASK    (HRTFDELAY_FRACONE-1)

const struct Hrtf *GetHrtf(enum DevFmtChannels chans, ALCuint srate);
ALCboolean FindHrtfFormat(enum DevFmtChannels *chans, ALCuint *srate);

void FreeHrtfs(void);

ALuint GetHrtfIrSize(const struct Hrtf *Hrtf);
void GetLerpedHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat (*coeffs)[2], ALuint *delays);

#endif /* ALC_HRTF_H */
