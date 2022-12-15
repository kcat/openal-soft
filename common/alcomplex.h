#ifndef ALCOMPLEX_H
#define ALCOMPLEX_H

#include <complex>
#include <type_traits>

#include "alspan.h"

/**
 * Iterative implementation of 2-radix FFT (In-place algorithm). Sign = -1 is
 * FFT and 1 is inverse FFT. Applies the Discrete Fourier Transform (DFT) to
 * the data supplied in the buffer, which MUST BE power of two.
 */
template<typename Real>
std::enable_if_t<std::is_floating_point<Real>::value>
complex_fft(const al::span<std::complex<Real>> buffer, const al::type_identity_t<Real> sign);

/**
 * Calculate the frequency-domain response of the time-domain signal in the
 * provided buffer, which MUST BE power of two.
 */
template<typename Real, size_t N>
std::enable_if_t<std::is_floating_point<Real>::value>
forward_fft(const al::span<std::complex<Real>,N> buffer)
{ complex_fft(buffer.subspan(0), -1); }

/**
 * Calculate the time-domain signal of the frequency-domain response in the
 * provided buffer, which MUST BE power of two.
 */
template<typename Real, size_t N>
std::enable_if_t<std::is_floating_point<Real>::value>
inverse_fft(const al::span<std::complex<Real>,N> buffer)
{ complex_fft(buffer.subspan(0), 1); }

/**
 * Calculate the complex helical sequence (discrete-time analytical signal) of
 * the given input using the discrete Hilbert transform (In-place algorithm).
 * Fills the buffer with the discrete-time analytical signal stored in the
 * buffer. The buffer is an array of complex numbers and MUST BE power of two,
 * and the imaginary components should be cleared to 0.
 */
void complex_hilbert(const al::span<std::complex<double>> buffer);

#endif /* ALCOMPLEX_H */
