#ifndef SAMPLE_CVT_H
#define SAMPLE_CVT_H

#include "AL/al.h"
#include "alBuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const ALshort muLawDecompressionTable[256];
extern const ALshort aLawDecompressionTable[256];

void Convert_ALshort_ALima4(ALshort *dst, const ALubyte *src, ALsizei numchans, ALsizei len,
                            ALsizei align);
void Convert_ALshort_ALmsadpcm(ALshort *dst, const ALubyte *src, ALsizei numchans, ALsizei len,
                               ALsizei align);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* SAMPLE_CVT_H */
