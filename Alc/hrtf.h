#ifndef ALC_HRTF_H
#define ALC_HRTF_H

#include "AL/al.h"
#include "AL/alc.h"

#include "alstring.h"

enum DevFmtChannels;

struct Hrtf;

typedef struct HrtfEntry {
    al_string name;
    al_string filename;

    const struct Hrtf *hrtf;
} HrtfEntry;
TYPEDEF_VECTOR(HrtfEntry, vector_HrtfEntry)

#define HRIR_BITS        (7)
#define HRIR_LENGTH      (1<<HRIR_BITS)
#define HRIR_MASK        (HRIR_LENGTH-1)
#define HRTFDELAY_BITS    (20)
#define HRTFDELAY_FRACONE (1<<HRTFDELAY_BITS)
#define HRTFDELAY_MASK    (HRTFDELAY_FRACONE-1)

void FreeHrtfs(void);

vector_HrtfEntry EnumerateHrtf(const_al_string devname);
void FreeHrtfList(vector_HrtfEntry *list);

ALuint GetHrtfSampleRate(const struct Hrtf *Hrtf);
ALuint GetHrtfIrSize(const struct Hrtf *Hrtf);

void GetLerpedHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat dirfact, ALfloat gain, ALfloat (*coeffs)[2], ALuint *delays);
ALuint GetMovingHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat dirfact, ALfloat gain, ALfloat delta, ALint counter, ALfloat (*coeffs)[2], ALuint *delays, ALfloat (*coeffStep)[2], ALint *delayStep);
void GetBFormatHrtfCoeffs(const struct Hrtf *Hrtf, const ALuint num_chans, ALfloat (**coeffs_list)[2], ALuint **delay_list);

#endif /* ALC_HRTF_H */
