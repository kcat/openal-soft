#ifndef LOADDEF_H
#define LOADDEF_H

#include <stdio.h>

#include "makemhr.h"


// Constants for accessing the token reader's ring buffer.
#define TR_RING_BITS                 (16)
#define TR_RING_SIZE                 (1 << TR_RING_BITS)
#define TR_RING_MASK                 (TR_RING_SIZE - 1)


// Token reader state for parsing the data set definition.
struct TokenReaderT {
    FILE *mFile;
    const char *mName;
    uint        mLine;
    uint        mColumn;
    char   mRing[TR_RING_SIZE];
    size_t mIn;
    size_t mOut;
};

void TrSetup(FILE *fp, const char *filename, TokenReaderT *tr);
int ProcessMetrics(TokenReaderT *tr, const uint fftSize, const uint truncSize, HrirDataT *hData);
int ProcessSources(const HeadModelT model, TokenReaderT *tr, HrirDataT *hData);

#endif /* LOADDEF_H */
