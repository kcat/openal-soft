#ifndef ALC_HRTF_H
#define ALC_HRTF_H

#include "AL/al.h"
#include "AL/alc.h"

#include "alMain.h"
#include "atomic.h"
#include "vector.h"


#define HRTF_HISTORY_BITS   (6)
#define HRTF_HISTORY_LENGTH (1<<HRTF_HISTORY_BITS)
#define HRTF_HISTORY_MASK   (HRTF_HISTORY_LENGTH-1)

#define HRIR_BITS        (7)
#define HRIR_LENGTH      (1<<HRIR_BITS)
#define HRIR_MASK        (HRIR_LENGTH-1)


struct HrtfEntry;

struct Hrtf {
    RefCount ref;

    ALuint sampleRate;
    ALsizei irSize;

    ALfloat distance;
    ALubyte evCount;

    const ALubyte *azCount;
    const ALushort *evOffset;
    const ALfloat (*coeffs)[2];
    const ALubyte (*delays)[2];
};


typedef struct HrtfState {
    alignas(16) ALfloat History[HRTF_HISTORY_LENGTH];
    alignas(16) ALfloat Values[HRIR_LENGTH][2];
} HrtfState;

typedef struct HrtfParams {
    alignas(16) ALfloat Coeffs[HRIR_LENGTH][2];
    ALsizei Delay[2];
    ALfloat Gain;
} HrtfParams;

typedef struct DirectHrtfState {
    /* HRTF filter state for dry buffer content */
    ALsizei Offset;
    ALsizei IrSize;
    struct {
        alignas(16) ALfloat Values[HRIR_LENGTH][2];
        alignas(16) ALfloat Coeffs[HRIR_LENGTH][2];
    } Chan[];
} DirectHrtfState;

struct AngularPoint {
    ALfloat Elev;
    ALfloat Azim;
};


void FreeHrtfs(void);

al::vector<EnumeratedHrtf> EnumerateHrtf(const char *devname);
void FreeHrtfList(al::vector<EnumeratedHrtf> &list);
struct Hrtf *GetLoadedHrtf(struct HrtfEntry *entry);
void Hrtf_IncRef(struct Hrtf *hrtf);
void Hrtf_DecRef(struct Hrtf *hrtf);

void GetHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat spread, ALfloat (*RESTRICT coeffs)[2], ALsizei *delays);

/**
 * Produces HRTF filter coefficients for decoding B-Format, given a set of
 * virtual speaker positions, a matching decoding matrix, and per-order high-
 * frequency gains for the decoder. The calculated impulse responses are
 * ordered and scaled according to the matrix input.
 */
void BuildBFormatHrtf(const struct Hrtf *Hrtf, DirectHrtfState *state, ALsizei NumChannels, const struct AngularPoint *AmbiPoints, const ALfloat (*RESTRICT AmbiMatrix)[MAX_AMBI_COEFFS], ALsizei AmbiCount, const ALfloat *RESTRICT AmbiOrderHFGain);

#endif /* ALC_HRTF_H */
