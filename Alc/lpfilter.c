/* ----------------- file filterIIR00.c begin ----------------- */
/* 
Resonant low pass filter source code.
By baltrax@hotmail.com (Zxform)
*/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "alMain.h"
#include "alFilter.h"


static void szxform(
    double *a0, double *a1, double *a2,   /* numerator coefficients */
    double *b0, double *b1, double *b2,   /* denominator coefficients */
    double fc,           /* Filter cutoff frequency */
    double fs,           /* sampling rate */
    double *k,           /* overall gain factor */
    float *coef);        /* pointer to 4 iir coefficients */

/*
 * --------------------------------------------------------------------
 *
 * lpFilter - Perform IIR filtering sample by sample on floats
 *
 * Implements cascaded direct form II second order sections.
 * Requires FILTER structure for history and coefficients.
 * The size of the history array is 2*FILTER_SECTIONS.
 * The size of the coefficient array is 4*FILTER_SECTIONS + 1 because
 * the first coefficient is the overall scale factor for the filter.
 * Returns one output sample for each input sample.
 *
 * float lpFilter(FILTER *iir,float input)
 *
 *     FILTER *iir        pointer to FILTER structure
 *     float input        new float input sample
 *
 * Returns float value giving the current output.
 * --------------------------------------------------------------------
 */

/*** moved to ALu.c ***/

/*
 * --------------------------------------------------------------------
 *
 * InitLowPassFilter()
 *
 * Initialize filter coefficients.
 * We create a 4th order filter (24 db/oct rolloff), consisting
 * of two second order sections.
 * --------------------------------------------------------------------
 */
int InitLowPassFilter(ALCcontext *Context, FILTER *iir)
{
    float    *coef;
    double   fs, fc; /* Sampling frequency, cutoff frequency */
    double   Q;      /* Resonance > 1.0 < 1000 */
    unsigned nInd;
    double   a0, a1, a2, b0, b1, b2;
    double   k;      /* overall gain factor */
    struct {
        double a0, a1, a2;       /* numerator coefficients */
        double b0, b1, b2;       /* denominator coefficients */
    } ProtoCoef[FILTER_SECTIONS];      /* Filter prototype coefficients,
                                          1 for each filter section */


    /*
     * Setup filter s-domain coefficients
     */
    /* Section 1 */
    ProtoCoef[0].a0 = 1.0;
    ProtoCoef[0].a1 = 0;
    ProtoCoef[0].a2 = 0;
    ProtoCoef[0].b0 = 1.0;
    ProtoCoef[0].b1 = 0.765367;
    ProtoCoef[0].b2 = 1.0;

    /* Section 2 */
    ProtoCoef[1].a0 = 1.0;
    ProtoCoef[1].a1 = 0;
    ProtoCoef[1].a2 = 0;
    ProtoCoef[1].b0 = 1.0;
    ProtoCoef[1].b1 = 1.847759;
    ProtoCoef[1].b2 = 1.0;

    /*
     * Allocate array of z-domain coefficients for each filter section
     * plus filter gain variable
     */
    iir->coef = (float*)calloc(4*FILTER_SECTIONS + 1, sizeof(float));
    if(!iir->coef)
    {
        AL_PRINT("Unable to allocate coef array\n");
        return 1;
    }

    /* allocate history array */
    iir->history = (float*)calloc(2*FILTER_SECTIONS, sizeof(float));
    if(!iir->history) {
        AL_PRINT("\nUnable to allocate history array\n");
        return 1;
    }


    k = 1.0;              /* Set overall filter gain */
    coef = iir->coef + 1; /* Skip k, or gain */

    Q = 1;                   /* Resonance */
    fc = LOWPASSFREQCUTOFF;  /* Filter cutoff (Hz) */
    fs = Context->Frequency; /* Sampling frequency (Hz) */

    /*
     * Compute z-domain coefficients for each biquad section
     * for new Cutoff Frequency and Resonance
     */
    for (nInd = 0; nInd < FILTER_SECTIONS; nInd++)
    {
        a0 = ProtoCoef[nInd].a0;
        a1 = ProtoCoef[nInd].a1;
        a2 = ProtoCoef[nInd].a2;

        b0 = ProtoCoef[nInd].b0;
        b1 = ProtoCoef[nInd].b1 / Q;      /* Divide by resonance or Q */
        b2 = ProtoCoef[nInd].b2;
        szxform(&a0, &a1, &a2, &b0, &b1, &b2, fc, fs, &k, coef);
        coef += 4;                       /* Point to next filter section */
    }

    /* Update overall filter gain in coef array */
    iir->coef[0] = k;

    return 0;
}
/* ----------------- file filterIIR00.c end ----------------- */


/* ----------------- file bilinear.c begin ----------------- */
/*
 * ----------------------------------------------------------
 *      bilinear.c
 *
 *      Perform bilinear transformation on s-domain coefficients
 *      of 2nd order biquad section.
 *      First design an analog filter and use s-domain coefficients
 *      as input to szxform() to convert them to z-domain.
 *
 * Here's the butterworth polinomials for 2nd, 4th and 6th order sections.
 *      When we construct a 24 db/oct filter, we take to 2nd order
 *      sections and compute the coefficients separately for each section.
 *
 *      n       Polinomials
 * --------------------------------------------------------------------
 *      2       s^2 + 1.4142s +1
 *      4       (s^2 + 0.765367s + 1) (s^2 + 1.847759s + 1)
 *      6       (s^2 + 0.5176387s + 1) (s^2 + 1.414214 + 1) (s^2 + 1.931852s + 1)
 *
 *      Where n is a filter order.
 *      For n=4, or two second order sections, we have following equasions for each
 *      2nd order stage:
 *
 *      (1 / (s^2 + (1/Q) * 0.765367s + 1)) * (1 / (s^2 + (1/Q) * 1.847759s + 1))
 *
 *      Where Q is filter quality factor in the range of
 *      1 to 1000. The overall filter Q is a product of all
 *      2nd order stages. For example, the 6th order filter
 *      (3 stages, or biquads) with individual Q of 2 will
 *      have filter Q = 2 * 2 * 2 = 8.
 *
 *      The nominator part is just 1.
 *      The denominator coefficients for stage 1 of filter are:
 *      b2 = 1; b1 = 0.765367; b0 = 1;
 *      numerator is
 *      a2 = 0; a1 = 0; a0 = 1;
 *
 *      The denominator coefficients for stage 1 of filter are:
 *      b2 = 1; b1 = 1.847759; b0 = 1;
 *      numerator is
 *      a2 = 0; a1 = 0; a0 = 1;
 *
 *      These coefficients are used directly by the szxform()
 *      and bilinear() functions. For all stages the numerator
 *      is the same and the only thing that is different between
 *      different stages is 1st order coefficient. The rest of
 *      coefficients are the same for any stage and equal to 1.
 *
 *      Any filter could be constructed using this approach.
 *
 *      References:
 *             Van Valkenburg, "Analog Filter Design"
 *             Oxford University Press 1982
 *             ISBN 0-19-510734-9
 *
 *             C Language Algorithms for Digital Signal Processing
 *             Paul Embree, Bruce Kimble
 *             Prentice Hall, 1991
 *             ISBN 0-13-133406-9
 *
 *             Digital Filter Designer's Handbook
 *             With C++ Algorithms
 *             Britton Rorabaugh
 *             McGraw Hill, 1997
 *             ISBN 0-07-053806-9
 * ----------------------------------------------------------
 */

static void prewarp(double *a0, double *a1, double *a2, double fc, double fs);
static void bilinear(
    double a0, double a1, double a2,    /* numerator coefficients */
    double b0, double b1, double b2,    /* denominator coefficients */
    double *k,                          /* overall gain factor */
    double fs,                          /* sampling rate */
    float *coef);                       /* pointer to 4 iir coefficients */


/*
 * ----------------------------------------------------------
 *      Pre-warp the coefficients of a numerator or denominator.
 *      Note that a0 is assumed to be 1, so there is no wrapping
 *      of it.
 * ----------------------------------------------------------
 */
static void prewarp(
    double *a0, double *a1, double *a2,
    double fc, double fs)
{
    double wp, pi;

    pi = 4.0 * atan(1.0);
    wp = 2.0 * fs * tan(pi * fc / fs);

    *a2 = (*a2) / (wp * wp);
    *a1 = (*a1) / wp;
    (void)a0;
}


/*
 * ----------------------------------------------------------
 * bilinear()
 *
 * Transform the numerator and denominator coefficients
 * of s-domain biquad section into corresponding
 * z-domain coefficients.
 *
 *      Store the 4 IIR coefficients in array pointed by coef
 *      in following order:
 *             beta1, beta2    (denominator)
 *             alpha1, alpha2  (numerator)
 *
 * Arguments:
 *             a0-a2   - s-domain numerator coefficients
 *             b0-b2   - s-domain denominator coefficients
 *             k       - filter gain factor. initially set to 1
 *                       and modified by each biquad section in such
 *                       a way, as to make it the coefficient by
 *                       which to multiply the overall filter gain
 *                       in order to achieve a desired overall filter gain,
 *                       specified in initial value of k.
 *             fs      - sampling rate (Hz)
 *             coef    - array of z-domain coefficients to be filled in.
 *
 * Return:
 *             On return, set coef z-domain coefficients
 * ----------------------------------------------------------
 */
static void bilinear(
    double a0, double a1, double a2,    /* numerator coefficients */
    double b0, double b1, double b2,    /* denominator coefficients */
    double *k,                          /* overall gain factor */
    double fs,                          /* sampling rate */
    float *coef                         /* pointer to 4 iir coefficients */
)
{
    double ad, bd;

    /* alpha (Numerator in s-domain) */
    ad = 4. * a2 * fs * fs + 2. * a1 * fs + a0;
    /* beta (Denominator in s-domain) */
    bd = 4. * b2 * fs * fs + 2. * b1* fs + b0;

    /* update gain constant for this section */
    *k *= ad/bd;

    /* Denominator */
    *coef++ = (2.*b0 - 8.*b2*fs*fs) / bd;         /* beta1 */
    *coef++ = (4.*b2*fs*fs - 2.*b1*fs + b0) / bd; /* beta2 */

    /* Nominator */
    *coef++ = (2.*a0 - 8.*a2*fs*fs) / ad;         /* alpha1 */
    *coef = (4.*a2*fs*fs - 2.*a1*fs + a0) / ad;   /* alpha2 */
}


/*
 * ----------------------------------------------------------
 * Transform from s to z domain using bilinear transform
 * with prewarp.
 *
 * Arguments:
 *      For argument description look at bilinear()
 *
 *      coef - pointer to array of floating point coefficients,
 *                     corresponding to output of bilinear transofrm
 *                     (z domain).
 *
 * Note: frequencies are in Hz.
 * ----------------------------------------------------------
 */
static void szxform(
    double *a0, double *a1, double *a2, /* numerator coefficients */
    double *b0, double *b1, double *b2, /* denominator coefficients */
    double fc,                          /* Filter cutoff frequency */
    double fs,                          /* sampling rate */
    double *k,                          /* overall gain factor */
    float *coef)                        /* pointer to 4 iir coefficients */
{
    /* Calculate a1 and a2 and overwrite the original values */
    prewarp(a0, a1, a2, fc, fs);
    prewarp(b0, b1, b2, fc, fs);
    bilinear(*a0, *a1, *a2, *b0, *b1, *b2, k, fs, coef);
}


/* ----------------- file bilinear.c end ----------------- */

/* ----------------- file filter.txt begin -----------------
How to construct a kewl low pass resonant filter?

Lets assume we want to create a filter for analog synth.
The filter rolloff is 24 db/oct, which corresponds to 4th
order filter. Filter of first order is equivalent to RC circuit
and has max rolloff of 6 db/oct.

We will use classical Butterworth IIR filter design, as it
exactly corresponds to our requirements.

A common practice is to chain several 2nd order sections,
or biquads, as they commonly called, in order to achive a higher
order filter. Each 2nd order section is a 2nd order filter, which
has 12 db/oct roloff. So, we need 2 of those sections in series.

To compute those sections, we use standard Butterworth polinomials,
or so called s-domain representation and convert it into z-domain,
or digital domain. The reason we need to do this is because
the filter theory exists for analog filters for a long time
and there exist no theory of working in digital domain directly.
So the common practice is to take standard analog filter design
and use so called bilinear transform to convert the butterworth
equasion coefficients into z-domain.

Once we compute the z-domain coefficients, we can use them in
a very simple transfer function, such as iir_filter() in our
C source code, in order to perform the filtering function.
The filter itself is the simpliest thing in the world.
The most complicated thing is computing the coefficients
for z-domain.

Ok, lets look at butterworth polynomials, arranged as a series
of 2nd order sections:

 * Note: n is filter order.
 *
 *      n       Polynomials
 *      --------------------------------------------------------------------
 *      2       s^2 + 1.4142s +1
 *      4       (s^2 + 0.765367s + 1) * (s^2 + 1.847759s + 1)
 *      6       (s^2 + 0.5176387s + 1) * (s^2 + 1.414214 + 1) * (s^2 + 1.931852s + 1)
 *
 * For n=4 we have following equasion for the filter transfer function:
 *
 *                     1                              1
 * T(s) = --------------------------- * ----------------------------
 *        s^2 + (1/Q) * 0.765367s + 1   s^2 + (1/Q) * 1.847759s + 1
 *

The filter consists of two 2nd order secions since highest s power is 2.
Now we can take the coefficients, or the numbers by which s is multiplied
and plug them into a standard formula to be used by bilinear transform.

Our standard form for each 2nd order secion is:

       a2 * s^2 + a1 * s + a0
H(s) = ----------------------
       b2 * s^2 + b1 * s + b0

Note that butterworth nominator is 1 for all filter sections,
which means s^2 = 0 and s^1 = 0

Lets convert standard butterworth polinomials into this form:

       0 + 0 + 1                  0 + 0 + 1
-------------------------- * --------------------------
1 + ((1/Q) * 0.765367) + 1   1 + ((1/Q) * 1.847759) + 1

Section 1:
a2 = 0; a1 = 0; a0 = 1;
b2 = 1; b1 = 0.5176387; b0 = 1;

Section 2:
a2 = 0; a1 = 0; a0 = 1;
b2 = 1; b1 = 1.847759; b0 = 1;

That Q is filter quality factor or resonance, in the range of
1 to 1000. The overall filter Q is a product of all 2nd order stages.
For example, the 6th order filter (3 stages, or biquads)
with individual Q of 2 will have filter Q = 2 * 2 * 2 = 8.

These a and b coefficients are used directly by the szxform()
and bilinear() functions.

The transfer function for z-domain is:

       1 + alpha1 * z^(-1) + alpha2 * z^(-2)
H(z) = -------------------------------------
       1 + beta1 * z^(-1) + beta2 * z^(-2)

When you need to change the filter frequency cutoff or resonance,
or Q, you call the szxform() function with proper a and b
coefficients and the new filter cutoff frequency or resonance.
You also need to supply the sampling rate and filter gain you want
to achive. For our purposes the gain = 1.

We call szxform() function 2 times becase we have 2 filter sections.
Each call provides different coefficients.

The gain argument to szxform() is a pointer to desired filter
gain variable.

double k = 1.0;      // overall gain factor

Upon return from each call, the k argument will be set to a value,
by which to multiply our actual signal in order for the gain
to be one. On second call to szxform() we provide k that was
changed by the previous section. During actual audio filtering
function iir_filter() will use this k

Summary:

Our filter is pretty close to ideal in terms of all relevant
parameters and filter stability even with extremely large values
of resonance. This filter design has been verified under all
variations of parameters and it all appears to work as advertized.

Good luck with it.
If you ever make a directX wrapper for it, post it to comp.dsp.


 *
 * ----------------------------------------------------------
 *References:
 *Van Valkenburg, "Analog Filter Design"
 *Oxford University Press 1982
 *ISBN 0-19-510734-9
 *
 *C Language Algorithms for Digital Signal Processing
 *Paul Embree, Bruce Kimble
 *Prentice Hall, 1991
 *ISBN 0-13-133406-9
 *
 *Digital Filter Designer's Handbook
 *With C++ Algorithms
 *Britton Rorabaugh
 *McGraw Hill, 1997
 *ISBN 0-07-053806-9
 * ----------------------------------------------------------



// ----------------- file filter.txt end ----------------- */
