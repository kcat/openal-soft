#ifndef LOADDEF_H
#define LOADDEF_H

#include <stdio.h>

#include "makemhr.h"


bool LoadDefInput(FILE *fp, const char *startbytes, size_t startbytecount, const char *filename,
    const uint fftSize, const uint truncSize, const ChannelModeT chanMode, HrirDataT *hData);

#endif /* LOADDEF_H */
