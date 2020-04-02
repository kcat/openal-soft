#ifndef BSINC_DEFS_H
#define BSINC_DEFS_H

/* The number of distinct scale and phase intervals within the filter table. */
#define BSINC_SCALE_BITS  4
#define BSINC_SCALE_COUNT (1<<BSINC_SCALE_BITS)
#define BSINC_PHASE_BITS  5
#define BSINC_PHASE_COUNT (1<<BSINC_PHASE_BITS)

/* The maximum number of sample points for the bsinc filters. */
#define BSINC_POINTS_MAX 48

#endif /* BSINC_DEFS_H */
