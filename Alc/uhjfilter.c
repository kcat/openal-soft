
#include "config.h"

#include "alu.h"
#include "uhjfilter.h"

/* This is the maximum number of samples processed for each inner loop
 * iteration. */
#define MAX_UPDATE_SAMPLES  256


static const ALfloat Filter1Coeff[4] = {
    0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f
};
static const ALfloat Filter2Coeff[4] = {
    0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f
};

void EncodeUhj2(Uhj2Encoder *enc, ALfloat (*restrict OutBuffer)[BUFFERSIZE], ALfloat (*restrict InSamples)[BUFFERSIZE], ALuint SamplesToDo)
{
    ALuint base, i, c;

    for(base = 0;base < SamplesToDo;)
    {
        ALfloat D[MAX_UPDATE_SAMPLES], S;
        ALuint todo = minu(SamplesToDo - base, MAX_UPDATE_SAMPLES);

        /* D = 0.6554516*Y */
        for(i = 0;i < todo;i++)
        {
            ALfloat in = 0.6554516f*InSamples[2][base+i];
            for(c = 0;c < 4;c++)
            {
                ALfloat aa = Filter1Coeff[c]*Filter1Coeff[c];
                ALfloat out = aa*(in + enc->Filter1_Y[c].y[1]) - enc->Filter1_Y[c].x[1];
                enc->Filter1_Y[c].x[1] = enc->Filter1_Y[c].x[0];
                enc->Filter1_Y[c].x[0] = in;
                enc->Filter1_Y[c].y[1] = enc->Filter1_Y[c].y[0];
                enc->Filter1_Y[c].y[0] = out;
                in = out;
            }
            /* NOTE: Filter1 requires a 1 sample delay for the base output, so
             * take the sample before the last for output.
             */
            D[i] = enc->Filter1_Y[3].y[1];
        }

        /* D += j(-0.3420201*W' + 0.5098604*X) */
        for(i = 0;i < todo;i++)
        {
            ALfloat in = -0.3420201f*1.414213562f*InSamples[0][base+i] +
                          0.5098604f*InSamples[1][base+i];
            for(c = 0;c < 4;c++)
            {
                ALfloat aa = Filter2Coeff[c]*Filter2Coeff[c];
                ALfloat out = aa*(in + enc->Filter2_WX[c].y[1]) - enc->Filter2_WX[c].x[1];
                enc->Filter2_WX[c].x[1] = enc->Filter2_WX[c].x[0];
                enc->Filter2_WX[c].x[0] = in;
                enc->Filter2_WX[c].y[1] = enc->Filter2_WX[c].y[0];
                enc->Filter2_WX[c].y[0] = out;
                in = out;
            }
            D[i] += enc->Filter2_WX[3].y[0];
        }

        /* S = 0.9396926*W' + 0.1855740*X
         * Left = (S + D)/2.0
         * Right = (S - D)/2.0
         */
        for(i = 0;i < todo;i++)
        {
            ALfloat in = 0.9396926f*1.414213562f*InSamples[0][base+i] +
                         0.1855740f*InSamples[1][base+i];
            for(c = 0;c < 4;c++)
            {
                ALfloat aa = Filter1Coeff[c]*Filter1Coeff[c];
                ALfloat out = aa*(in + enc->Filter1_WX[c].y[1]) - enc->Filter1_WX[c].x[1];
                enc->Filter1_WX[c].x[1] = enc->Filter1_WX[c].x[0];
                enc->Filter1_WX[c].x[0] = in;
                enc->Filter1_WX[c].y[1] = enc->Filter1_WX[c].y[0];
                enc->Filter1_WX[c].y[0] = out;
                in = out;
            }
            S = enc->Filter1_WX[3].y[1];
            OutBuffer[0][base + i] += (S + D[i]) * 0.5f;
            OutBuffer[1][base + i] += (S - D[i]) * 0.5f;
        }

        base += todo;
    }
}
