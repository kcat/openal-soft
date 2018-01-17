#ifndef SAMPLE_CVT_H
#define SAMPLE_CVT_H

#include "AL/al.h"
#include "alBuffer.h"

extern const ALshort muLawDecompressionTable[256];
extern const ALshort aLawDecompressionTable[256];

void ConvertData(ALvoid *dst, enum UserFmtType dstType, const ALvoid *src, enum UserFmtType srcType, ALsizei numchans, ALsizei len, ALsizei align);

#endif /* SAMPLE_CVT_H */
