#ifndef ALCOMPLEX_H
#define ALCOMPLEX_H

#include "AL/al.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALcomplex {
    ALdouble Real;
    ALdouble Imag;
} ALcomplex;

/**
 * Iterative implementation of 2-radix FFT (In-place algorithm). Sign = -1 is
 * FFT and 1 is iFFT (inverse). Fills FFTBuffer[0...FFTSize-1] with the
 * Discrete Fourier Transform (DFT) of the time domain data stored in
 * FFTBuffer[0...FFTSize-1]. FFTBuffer is an array of complex numbers, FFTSize
 * MUST BE power of two.
 */
void complex_fft(ALcomplex *FFTBuffer, ALsizei FFTSize, ALdouble Sign);

/**
 * Calculate the complex helical sequence (discrete-time analytical signal) of
 * the given input using the discrete Hilbert transform (In-place algorithm).
 * Fills Buffer[0...size-1] with the discrete-time analytical signal stored in
 * Buffer[0...size-1]. Buffer is an array of complex numbers, size MUST BE
 * power of two.
 */
void complex_hilbert(ALcomplex *Buffer, ALsizei size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* ALCOMPLEX_H */
