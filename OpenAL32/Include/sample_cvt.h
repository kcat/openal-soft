#ifndef SAMPLE_CVT_H
#define SAMPLE_CVT_H

#include "AL/al.h"
#include "albyte.h"


void Convert_ALshort_ALima4(ALshort *dst, const al::byte *src, ALsizei numchans, ALsizei len,
                            ALsizei align);
void Convert_ALshort_ALmsadpcm(ALshort *dst, const al::byte *src, ALsizei numchans, ALsizei len,
                               ALsizei align);

#endif /* SAMPLE_CVT_H */
