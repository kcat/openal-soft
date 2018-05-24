
#include "config.h"

#include "alcomplex.h"
#include "math_defs.h"


extern inline ALcomplex complex_add(ALcomplex a, ALcomplex b);
extern inline ALcomplex complex_sub(ALcomplex a, ALcomplex b);
extern inline ALcomplex complex_mult(ALcomplex a, ALcomplex b);

void complex_fft(ALcomplex *FFTBuffer, ALsizei FFTSize, ALdouble Sign)
{
    ALsizei i, j, k, mask, step, step2;
    ALcomplex temp, u, w;
    ALdouble arg;

    /* Bit-reversal permutation applied to a sequence of FFTSize items */
    for(i = 1;i < FFTSize-1;i++)
    {
        for(mask = 0x1, j = 0;mask < FFTSize;mask <<= 1)
        {
            if((i&mask) != 0)
                j++;
            j <<= 1;
        }
        j >>= 1;

        if(i < j)
        {
            temp         = FFTBuffer[i];
            FFTBuffer[i] = FFTBuffer[j];
            FFTBuffer[j] = temp;
        }
    }

    /* Iterative form of DanielsonLanczos lemma */
    for(i = 1, step = 2;i < FFTSize;i<<=1, step<<=1)
    {
        step2 = step >> 1;
        arg   = M_PI / step2;

        w.Real = cos(arg);
        w.Imag = sin(arg) * Sign;

        u.Real = 1.0;
        u.Imag = 0.0;

        for(j = 0;j < step2;j++)
        {
            for(k = j;k < FFTSize;k+=step)
            {
                temp               = complex_mult(FFTBuffer[k+step2], u);
                FFTBuffer[k+step2] = complex_sub(FFTBuffer[k], temp);
                FFTBuffer[k]       = complex_add(FFTBuffer[k], temp);
            }

            u = complex_mult(u, w);
        }
    }
}

void complex_hilbert(ALcomplex *Buffer, ALsizei size)
{
    const ALdouble inverse_size = 1.0/(ALdouble)size;
    ALsizei todo, i;

    for(i = 0;i < size;i++)
        Buffer[i].Imag = 0.0;

    complex_fft(Buffer, size, 1.0);

    todo = size >> 1;
    Buffer[0].Real *= inverse_size;
    Buffer[0].Imag *= inverse_size;
    for(i = 1;i < todo;i++)
    {
        Buffer[i].Real *= 2.0*inverse_size;
        Buffer[i].Imag *= 2.0*inverse_size;
    }
    Buffer[i].Real *= inverse_size;
    Buffer[i].Imag *= inverse_size;
    i++;

    for(;i < size;i++)
    {
        Buffer[i].Real = 0.0;
        Buffer[i].Imag = 0.0;
    }

    complex_fft(Buffer, size, -1.0);
}
