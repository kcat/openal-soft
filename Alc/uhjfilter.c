
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

/* NOTE: There seems to be a bit of an inconsistency in how this encoding is
 * supposed to work. Some references, such as
 *
 * http://members.tripod.com/martin_leese/Ambisonic/UHJ_file_format.html
 *
 * specify a pre-scaling of sqrt(2) on the W channel input, while other
 * references, such as
 *
 * https://en.wikipedia.org/wiki/Ambisonic_UHJ_format#Encoding.5B1.5D
 * and
 * https://wiki.xiph.org/Ambisonics#UHJ_format
 *
 * do not. The sqrt(2) scaling is in line with B-Format decoder coefficients
 * which include such a scaling for the W channel input, however the original
 * source for this equation is a 1985 paper by Michael Gerzon, which does not
 * apparently include the scaling. Applying the extra scaling creates a louder
 * result with a narrower stereo image compared to not scaling, and I don't
 * know which is the intended result.
 */

void EncodeUhj2(Uhj2Encoder *enc, ALfloat *restrict LeftOut, ALfloat *restrict RightOut, ALfloat (*restrict InSamples)[BUFFERSIZE], ALuint SamplesToDo)
{
    ALuint base, i, c;

    for(base = 0;base < SamplesToDo;)
    {
        ALfloat D[MAX_UPDATE_SAMPLES/2], S[MAX_UPDATE_SAMPLES/2];
        ALuint todo = minu(SamplesToDo - base, MAX_UPDATE_SAMPLES/2);

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

        /* D += j(-0.3420201*W + 0.5098604*X) */
        for(i = 0;i < todo;i++)
        {
            ALfloat in = -0.3420201f*InSamples[0][base+i] +
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

        /* S = 0.9396926*W + 0.1855740*X */
        for(i = 0;i < todo;i++)
        {
            ALfloat in = 0.9396926f*InSamples[0][base+i] +
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
            S[i] = enc->Filter1_WX[3].y[1];
        }

        /* Left = (S + D)/2.0 */
        for(i = 0;i < todo;i++)
            *(LeftOut++) += (S[i] + D[i]) * 0.5f;
        /* Right = (S - D)/2.0 */
        for(i = 0;i < todo;i++)
            *(RightOut++) += (S[i] - D[i]) * 0.5f;

        base += todo;
    }
}
