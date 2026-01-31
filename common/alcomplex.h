#ifndef ALCOMPLEX_H
#define ALCOMPLEX_H

#include <complex>
#include <span>

/**
 * Iterative implementation of 2-radix FFT (In-place algorithm). Sign = -1 is
 * FFT and 1 is inverse FFT. Applies the Discrete Fourier Transform (DFT) to
 * the data supplied in the buffer, which MUST BE power of two.
 */
void complex_fft(std::span<std::complex<double>> buffer, double sign);

/**
 * Calculate the frequency-domain response of the time-domain signal in the
 * provided buffer, which MUST BE power of two.
 */
inline void forward_fft(std::span<std::complex<double>> const buffer)
{ complex_fft(buffer, -1.0); }

/**
 * Calculate the time-domain signal of the frequency-domain response in the
 * provided buffer, which MUST BE power of two.
 */
inline void inverse_fft(std::span<std::complex<double>> const buffer)
{ complex_fft(buffer, +1.0); }

/**
 * Calculate the complex helical sequence (discrete-time analytical signal) of
 * the given input using the discrete Hilbert transform (In-place algorithm).
 * Fills the buffer with the discrete-time analytical signal stored in the
 * buffer. The buffer is an array of complex numbers and MUST BE power of two,
 * and the imaginary components should be cleared to 0.
 */
void complex_hilbert(std::span<std::complex<double>> buffer);

#endif /* ALCOMPLEX_H */
