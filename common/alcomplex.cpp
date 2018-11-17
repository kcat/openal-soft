
#include "config.h"

#include "alcomplex.h"

#include <cmath>

namespace {

constexpr double Pi{3.141592653589793238462643383279502884};

} // namespace

void complex_fft(std::complex<double> *FFTBuffer, int FFTSize, double Sign)
{
    /* Bit-reversal permutation applied to a sequence of FFTSize items */
    for(int i{1};i < FFTSize-1;i++)
    {
        int j{0};
        for(int mask{1};mask < FFTSize;mask <<= 1)
        {
            if((i&mask) != 0)
                j++;
            j <<= 1;
        }
        j >>= 1;

        if(i < j)
            std::swap(FFTBuffer[i], FFTBuffer[j]);
    }

    /* Iterative form of DanielsonLanczos lemma */
    int step{2};
    for(int i{1};i < FFTSize;i<<=1, step<<=1)
    {
        int step2{step >> 1};
        double arg{Pi / step2};

        std::complex<double> w{std::cos(arg), std::sin(arg)*Sign};
        std::complex<double> u{1.0, 0.0};
        for(int j{0};j < step2;j++)
        {
            for(int k{j};k < FFTSize;k+=step)
            {
                std::complex<double> temp{FFTBuffer[k+step2] * u};
                FFTBuffer[k+step2] = FFTBuffer[k] - temp;
                FFTBuffer[k] += temp;
            }

            u *= w;
        }
    }
}

void complex_hilbert(std::complex<double> *Buffer, int size)
{
    const double inverse_size = 1.0/(double)size;

    for(int i{0};i < size;i++)
        Buffer[i].imag(0.0);

    complex_fft(Buffer, size, 1.0);

    int todo{size>>1};
    int i{0};

    Buffer[i++] *= inverse_size;
    while(i < todo)
        Buffer[i++] *= 2.0*inverse_size;
    Buffer[i++] *= inverse_size;

    for(;i < size;i++)
        Buffer[i] = std::complex<double>{};

    complex_fft(Buffer, size, -1.0);
}
