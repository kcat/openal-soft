/**
 * HRTF utility for producing and demonstrating the process of creating an
 * OpenAL Soft compatible HRIR data set.
 *
 * It can currently make use of the 44.1 KHz diffuse and compact KEMAR HRIRs
 * provided by MIT at:
 *
 *   http://sound.media.mit.edu/resources/KEMAR.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "AL/al.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _MSC_VER
static double round(double val)
{
    if(val < 0.0)
        return ceil(val - 0.5);
    return floor(val + 0.5);
}
#endif

// The sample rate of the MIT HRIR data sets.
#define MIT_IR_RATE                  (44100)

// The total number of used impulse responses from the MIT HRIR data sets.
#define MIT_IR_COUNT                 (828)

// The size (in samples) of each HRIR in the MIT data sets.
#define MIT_IR_SIZE                  (128)

// The total number of elevations given a step of 10 degrees.
#define MIT_EV_COUNT                 (19)

// The first elevation that the MIT data sets have HRIRs for.
#define MIT_EV_START                 (5)

// The head radius (in meters) used by the MIT data sets.
#define MIT_RADIUS                   (0.09f)

// The source to listener distance (in meters) used by the MIT data sets.
#define MIT_DISTANCE                 (1.4f)

// The resulting size (in samples) of a mininum-phase reconstructed HRIR.
#define MIN_IR_SIZE                  (32)

// The size (in samples) of the real cepstrum used in reconstruction.  This
// needs to be large enough to reduce inaccuracy.
#define CEP_SIZE                     (8192)

// The OpenAL Soft HRTF format marker.  It stands for minimum-phase head
// response protocol 00.
#define MHR_FORMAT                   ("MinPHR00")

typedef struct ComplexT              ComplexT;
typedef struct HrirDataT             HrirDataT;

// A complex number type.
struct ComplexT {
  float                              mVec [2];
};

// The HRIR data definition.  This can be used to add support for new HRIR
// sources in the future.
struct HrirDataT {
  int                                mIrRate,
                                     mIrCount,
                                     mIrSize,
                                     mEvCount,
                                     mEvStart;
  const int                        * mEvOffset,
                                   * mAzCount;
  float                              mRadius,
                                     mDistance,
                                   * mHrirs,
                                   * mHrtds,
                                     mMaxHrtd;
};

// The linear index of the first HRIR for each elevation of the MIT data set.
static const int                     MIT_EV_OFFSET [MIT_EV_COUNT] = {
  0, 1, 13, 37, 73, 118, 174, 234, 306, 378, 450, 522, 594, 654, 710, 755, 791, 815, 827
},

// The count of distinct azimuth steps for each elevation in the MIT data
// set.
                                     MIT_AZ_COUNT [MIT_EV_COUNT] = {
  1, 12, 24, 36, 45, 56, 60, 72, 72, 72, 72, 72, 60, 56, 45, 36, 24, 12, 1
};

// Performs a forward Fast Fourier Transform.
static void FftProc (int n, const ComplexT * fftIn, ComplexT * fftOut) {
  int m2, rk, k, m;
  float a, b;
  int i;
  float wx, wy;
  int j, km2;
  float tx, ty, wyd;

  // Data copy and bit-reversal ordering.
  m2 = (n >> 1);
  rk = 0;
  for (k = 0; k < n; k ++) {
      fftOut [rk] . mVec [0] = fftIn [k] . mVec [0];
      fftOut [rk] . mVec [1] = fftIn [k] . mVec [1];
      if (k < (n - 1)) {
         m = m2;
         while (rk >= m) {
           rk -= m;
           m >>= 1;
         }
         rk += m;
      }
  }
  // Perform the FFT.
  m2 = 1;
  for (m = 2; m <= n; m <<= 1) {
      a = sin (M_PI / m);
      a = 2.0f * a * a;
      b = sin (2.0f * M_PI / m);
      for (i = 0; i < n; i += m) {
          wx = 1.0f;
          wy = 0.0f;
          for (k = i, j = 0; j < m2; k ++, j ++) {
              km2 = k + m2;
              tx = (wx * fftOut [km2] . mVec [0]) - (wy * fftOut [km2] . mVec [1]);
              ty = (wx * fftOut [km2] . mVec [1]) + (wy * fftOut [km2] . mVec [0]);
              fftOut [km2] . mVec [0] = fftOut [k] . mVec [0] - tx;
              fftOut [km2] . mVec [1] = fftOut [k] . mVec [1] - ty;
              fftOut [k] . mVec [0] += tx;
              fftOut [k] . mVec [1] += ty;
              wyd = (a * wy) - (b * wx);
              wx -= (a * wx) + (b * wy);
              wy -= wyd;
          }
      }
      m2 = m;
  }
}

// Performs an inverse Fast Fourier Transform.
static void FftInvProc (int n, const ComplexT * fftIn, ComplexT * fftOut) {
  int m2, rk, k, m;
  float a, b;
  int i;
  float wx, wy;
  int j, km2;
  float tx, ty, wyd, invn;

  // Data copy and bit-reversal ordering.
  m2 = (n >> 1);
  rk = 0;
  for (k = 0; k < n; k ++) {
      fftOut [rk] . mVec [0] = fftIn [k] . mVec [0];
      fftOut [rk] . mVec [1] = fftIn [k] . mVec [1];
      if (k < (n - 1)) {
         m = m2;
         while (rk >= m) {
           rk -= m;
           m >>= 1;
         }
         rk += m;
      }
  }
  // Perform the IFFT.
  m2 = 1;
  for (m = 2; m <= n; m <<= 1) {
      a = sin (M_PI / m);
      a = 2.0f * a * a;
      b = -sin (2.0f * M_PI / m);
      for (i = 0; i < n; i += m) {
          wx = 1.0f;
          wy = 0.0f;
          for (k = i, j = 0; j < m2; k ++, j ++) {
              km2 = k + m2;
              tx = (wx * fftOut [km2] . mVec [0]) - (wy * fftOut [km2] . mVec [1]);
              ty = (wx * fftOut [km2] . mVec [1]) + (wy * fftOut [km2] . mVec [0]);
              fftOut [km2] . mVec [0] = fftOut [k] . mVec [0] - tx;
              fftOut [km2] . mVec [1] = fftOut [k] . mVec [1] - ty;
              fftOut [k] . mVec [0] += tx;
              fftOut [k] . mVec [1] += ty;
              wyd = (a * wy) - (b * wx);
              wx -= (a * wx) + (b * wy);
              wy -= wyd;
          }
      }
      m2 = m;
  }
  // Normalize the samples.
  invn = 1.0f / n;
  for (i = 0; i < n; i ++) {
      fftOut [i] . mVec [0] *= invn;
      fftOut [i] . mVec [1] *= invn;
  }
}

// Complex absolute value.
static void ComplexAbs (const ComplexT * in, ComplexT * out) {
  out -> mVec [0] = sqrt ((in -> mVec [0] * in -> mVec [0]) + (in -> mVec [1] * in -> mVec [1]));
  out -> mVec [1] = 0.0f;
}

// Complex logarithm.
static void ComplexLog (const ComplexT * in, ComplexT * out) {
  float r, t;

  r = sqrt ((in -> mVec [0] * in -> mVec [0]) + (in -> mVec [1] * in -> mVec [1]));
  t = atan2 (in -> mVec [1], in -> mVec [0]);
  if (t < 0.0f)
     t += 2.0f * M_PI;
  out -> mVec [0] = log (r);
  out -> mVec [1] = t;
}

// Complex exponent.
static void ComplexExp (const ComplexT * in, ComplexT * out) {
  float e;

  e = exp (in -> mVec [0]);
  out -> mVec [0] = e * cos (in -> mVec [1]);
  out -> mVec [1] = e * sin (in -> mVec [1]);
}

// Calculates the real cepstrum of a given impulse response.  It currently
// uses a fixed cepstrum size.  To make this more robust, it should be
// rewritten to handle a variable size cepstrum.
static void RealCepstrum (int irSize, const float * ir, float cep [CEP_SIZE]) {
  ComplexT in [CEP_SIZE], out [CEP_SIZE];
  int index;

  for (index = 0; index < irSize; index ++) {
      in [index] . mVec [0] = ir [index];
      in [index] . mVec [1] = 0.0f;
  }
  for (; index < CEP_SIZE; index ++) {
      in [index] . mVec [0] = 0.0f;
      in [index] . mVec [1] = 0.0f;
  }
  FftProc (CEP_SIZE, in, out);
  for (index = 0; index < CEP_SIZE; index ++) {
      ComplexAbs (& out [index], & out [index]);
      if (out [index] . mVec [0] < 0.000001f)
         out [index] . mVec [0] = 0.000001f;
      ComplexLog (& out [index], & in [index]);
  }
  FftInvProc (CEP_SIZE, in, out);
  for (index = 0; index < CEP_SIZE; index ++)
      cep [index] = out [index] . mVec [0];
}

// Reconstructs the minimum-phase impulse response for a given real cepstrum.
// Like the above function, this should eventually be modified to handle a
// variable size cepstrum.
static void MinimumPhase (const float cep [CEP_SIZE], int irSize, float * mpIr) {
  ComplexT in [CEP_SIZE], out [CEP_SIZE];
  int index;

  in [0] . mVec [0] = cep [0];
  for (index = 1; index < (CEP_SIZE / 2); index ++)
      in [index] . mVec [0] = 2.0f * cep [index];
  if ((CEP_SIZE % 2) != 1) {
     in [index] . mVec [0] = cep [index];
     index ++;
  }
  for (; index < CEP_SIZE; index ++)
      in [index] . mVec [0] = 0.0f;
  for (index = 0; index < CEP_SIZE; index ++)
      in [index] . mVec [1] = 0.0f;
  FftProc (CEP_SIZE, in, out);
  for (index = 0; index < CEP_SIZE; index ++)
      ComplexExp (& out [index], & in [index]);
  FftInvProc (CEP_SIZE, in, out);
  for (index = 0; index < irSize; index ++)
      mpIr [index] = out [index] . mVec [0];
}

// Calculate the left-ear time delay using a spherical head model.
static float CalcLTD (float ev, float az, float rad, float dist) {
  float azp, dlp, l, al;

  azp = asin (cos (ev) * sin (az));
  dlp = sqrt ((dist * dist) + (rad * rad) + (2.0f * dist * rad * sin (azp)));
  l = sqrt ((dist * dist) - (rad * rad));
  al = (0.5f * M_PI) + azp;
  if (dlp > l)
     dlp = l + (rad * (al - acos (rad / dist)));
  return (dlp / 343.3f);
}

// Read a 16-bit little-endian integer from a file and convert it to a 32-bit
// floating-point value in the range of -1.0 to 1.0.
static int ReadInt16LeAsFloat32 (const char * fileName, FILE * fp, float * val) {
  ALubyte vb [2];
  ALushort vw;

  if (fread (vb, 1, sizeof (vb), fp) != sizeof (vb)) {
     fclose (fp);
     fprintf (stderr, "Error reading from file, '%s'.\n", fileName);
     return (0);
  }
  vw = (((unsigned short) vb [1]) << 8) | vb [0];
  (* val) = ((short) vw) / 32768.0f;
  return (1);
}

// Write a string to a file.
static int WriteString (const char * val, const char * fileName, FILE * fp) {
  size_t len;

  len = strlen (val);
  if (fwrite (val, 1, len, fp) != len) {
     fclose (fp);
     fprintf (stderr, "Error writing to file, '%s'.\n", fileName);
     return (0);
  }
  return (1);
}

// Write a 32-bit floating-point value in the range of -1.0 to 1.0 to a file
// as a 16-bit little-endian integer.
static int WriteFloat32AsInt16Le (float val, const char * fileName, FILE * fp) {
  ALshort vw;
  ALubyte vb [2];

  vw = (short) round (32767.0f * val);
  vb [0] =  vw       & 0x00FF;
  vb [1] = (vw >> 8) & 0x00FF;
  if (fwrite (vb, 1, sizeof (vb), fp) != sizeof (vb)) {
     fclose (fp);
     fprintf (stderr, "Error writing to file, '%s'.\n", fileName);
     return (0);
  }
  return (1);
}

// Write a 32-bit little-endian unsigned integer to a file.
static int WriteUInt32Le (ALuint val, const char * fileName, FILE * fp) {
  ALubyte vb [4];

  vb [0] =  val        & 0x000000FF;
  vb [1] = (val >>  8) & 0x000000FF;
  vb [2] = (val >> 16) & 0x000000FF;
  vb [3] = (val >> 24) & 0x000000FF;
  if (fwrite (vb, 1, sizeof (vb), fp) != sizeof (vb)) {
     fclose (fp);
     fprintf (stderr, "Error writing to file, '%s'.\n", fileName);
     return (0);
  }
  return (1);
}

// Write a 16-bit little-endian unsigned integer to a file.
static int WriteUInt16Le (ALushort val, const char * fileName, FILE * fp) {
  ALubyte vb [2];

  vb [0] =  val        & 0x00FF;
  vb [1] = (val >>  8) & 0x00FF;
  if (fwrite (vb, 1, sizeof (vb), fp) != sizeof (vb)) {
     fclose (fp);
     fprintf (stderr, "Error writing to file, '%s'.\n", fileName);
     return (0);
  }
  return (1);
}

// Write an 8-bit unsigned integer to a file.
static int WriteUInt8 (ALubyte val, const char * fileName, FILE * fp) {
  if (fwrite (& val, 1, sizeof (val), fp) != sizeof (val)) {
     fclose (fp);
     fprintf (stderr, "Error writing to file, '%s'.\n", fileName);
     return (0);
  }
  return (1);
}

// Load the MIT HRIRs.  This loads the entire diffuse or compact set starting
// counter-clockwise up at the bottom elevation and clockwise at the forward
// azimuth.
static int LoadMitHrirs (const char * baseName, HrirDataT * hData) {
  const int EV_ANGLE [MIT_EV_COUNT] = {
    -90, -80, -70, -60, -50, -40, -30, -20, -10, 0, 10, 20, 30, 40, 50, 60, 70, 80, 90
  };
  int e, a;
  char fileName [1024];
  FILE * fp = NULL;
  int j0, j1, i;
  float s;

  for (e = MIT_EV_START; e < MIT_EV_COUNT; e ++) {
      for (a = 0; a < MIT_AZ_COUNT [e]; a ++) {
          // The data packs the first 180 degrees in the left channel, and
          // the last 180 degrees in the right channel.
          if (round ((360.0f / MIT_AZ_COUNT [e]) * a) > 180.0f)
             break;
          // Determine which file to open.
          snprintf (fileName, 1023, "%s%d/H%de%03da.wav", baseName, EV_ANGLE [e], EV_ANGLE [e], (int) round ((360.0f / MIT_AZ_COUNT [e]) * a));
          if ((fp = fopen (fileName, "rb")) == NULL) {
             fprintf (stderr, "Could not open file, '%s'.\n", fileName);
             return (0);
          }
          // Assuming they have not changed format, skip the .WAV header.
          fseek (fp, 44, SEEK_SET);
          // Map the left and right channels to their appropriate azimuth
          // offsets.
          j0 = (MIT_EV_OFFSET [e] + a) * MIT_IR_SIZE;
          j1 = (MIT_EV_OFFSET [e] + ((MIT_AZ_COUNT [e] - a) % MIT_AZ_COUNT [e])) * MIT_IR_SIZE;
          // Read in the data, converting it to floating-point.
          for (i = 0; i < MIT_IR_SIZE; i ++) {
              if (! ReadInt16LeAsFloat32 (fileName, fp, & s))
                 return (0);
              hData -> mHrirs [j0 + i] = s;
              if (! ReadInt16LeAsFloat32 (fileName, fp, & s))
                 return (0);
              hData -> mHrirs [j1 + i] = s;
          }
          fclose (fp);
      }
  }
  return (1);
}

// Performs the minimum phase reconstruction for a given HRIR data set.  The
// cepstrum size should be made configureable at some point in the future.
static void ReconstructHrirs (int minIrSize, HrirDataT * hData) {
  int start, end, step, j;
  float cep [CEP_SIZE];

  start = hData -> mEvOffset [hData -> mEvStart];
  end = hData -> mIrCount;
  step = hData -> mIrSize;
  for (j = start; j < end; j ++) {
      RealCepstrum (step, & hData -> mHrirs [j * step], cep);
      MinimumPhase (cep, minIrSize, & hData -> mHrirs [j * minIrSize]);
  }
  hData -> mIrSize = minIrSize;
}

// Renormalize the entire HRIR data set, and attenutate it slightly.
static void RenormalizeHrirs (const HrirDataT * hData) {
  int step, start, end;
  float norm;
  int j, i;

  step = hData -> mIrSize;
  start = hData -> mEvOffset [hData -> mEvStart] * step;
  end = hData -> mIrCount * step;
  norm = 0.0f;
  for (j = start; j < end; j += step) {
      for (i = 0; i < step; i ++) {
          if (fabs (hData -> mHrirs [j + i]) > norm)
             norm = fabs (hData -> mHrirs [j + i]);
      }
  }
  if (norm > 0.000001f)
     norm = 1.0f / norm;
  norm *= 0.95f;
  for (j = start; j < end; j += step) {
      for (i = 0; i < step; i ++)
          hData -> mHrirs [j + i] *= norm;
  }
}

// Given an elevation offset and azimuth, calculates two offsets for
// addressing the HRIRs buffer and their interpolation factor.
static void CalcAzIndices (const HrirDataT * hData, int oi, float az, int * j0, int * j1, float * jf) {
  int ai;

  az = fmod ((2.0f * M_PI) + az, 2.0f * M_PI) * hData -> mAzCount [oi] / (2.0f * M_PI);
  ai = (int) az;
  az -= ai;
  (* j0) = hData -> mEvOffset [oi] + ai;
  (* j1) = hData -> mEvOffset [oi] + ((ai + 1) % hData -> mAzCount [oi]);
  (* jf) = az;
}

// Perform a linear interpolation.
static float Lerp (float a, float b, float f) {
  return (a + (f * (b - a)));
}

// Attempt to synthesize any missing HRIRs at the bottom elevations.  Right
// now this just blends the lowest elevation HRIRs together and applies some
// attenuates and high frequency damping.  It's not a realistic model to use,
// but it is simple.
static void SynthesizeHrirs (HrirDataT * hData) {
  int step, oi, i, a, j, e;
  float of;
  int j0, j1;
  float jf;
  float lp [4], s0, s1;

  if (hData -> mEvStart <= 0)
     return;
  step = hData -> mIrSize;
  oi = hData -> mEvStart;
  for (i = 0; i < step; i ++)
      hData -> mHrirs [i] = 0.0f;
  for (a = 0; a < hData -> mAzCount [oi]; a ++) {
      j = (hData -> mEvOffset [oi] + a) * step;
      for (i = 0; i < step; i ++)
          hData -> mHrirs [i] += hData -> mHrirs [j + i] / hData -> mAzCount [oi];
  }
  for (e = 1; e < hData -> mEvStart; e ++) {
      of = ((float) e) / hData -> mEvStart;
      for (a = 0; a < hData -> mAzCount [e]; a ++) {
          j = (hData -> mEvOffset [e] + a) * step;
          CalcAzIndices (hData, oi, a * 2.0f * M_PI / hData -> mAzCount [e], & j0, & j1, & jf);
          j0 *= step;
          j1 *= step;
          lp [0] = 0.0f;
          lp [1] = 0.0f;
          lp [2] = 0.0f;
          lp [3] = 0.0f;
          for (i = 0; i < step; i ++) {
              s0 = hData -> mHrirs [i];
              s1 = Lerp (hData -> mHrirs [j0 + i], hData -> mHrirs [j1 + i], jf);
              s0 = Lerp (s0, s1, of);
              lp [0] = Lerp (s0, lp [0], 0.15f - (0.15f * of));
              lp [1] = Lerp (lp [0], lp [1], 0.15f - (0.15f * of));
              lp [2] = Lerp (lp [1], lp [2], 0.15f - (0.15f * of));
              lp [3] = Lerp (lp [2], lp [3], 0.15f - (0.15f * of));
              hData -> mHrirs [j + i] = lp [3];
          }
      }
  }
  lp [0] = 0.0f;
  lp [1] = 0.0f;
  lp [2] = 0.0f;
  lp [3] = 0.0f;
  for (i = 0; i < step; i ++) {
      s0 = hData -> mHrirs [i];
      lp [0] = Lerp (s0, lp [0], 0.15f);
      lp [1] = Lerp (lp [0], lp [1], 0.15f);
      lp [2] = Lerp (lp [1], lp [2], 0.15f);
      lp [3] = Lerp (lp [2], lp [3], 0.15f);
      hData -> mHrirs [i] = lp [3];
  }
  hData -> mEvStart = 0;
}

// Calculate the effective head-related time delays for the each HRIR, now
// that they are minimum-phase.
static void CalculateHrtds (HrirDataT * hData) {
  float minHrtd, maxHrtd;
  int e, a, j;
  float t;

  minHrtd = 1000.0f;
  maxHrtd = -1000.0f;
  for (e = 0; e < hData -> mEvCount; e ++) {
      for (a = 0; a < hData -> mAzCount [e]; a ++) {
          j = hData -> mEvOffset [e] + a;
          t = CalcLTD ((-90.0f + (e * 180.0f / (hData -> mEvCount - 1))) * M_PI / 180.0f,
                       (a * 360.0f / hData -> mAzCount [e]) * M_PI / 180.0f,
                       hData -> mRadius, hData -> mDistance);
          hData -> mHrtds [j] = t;
          if (t > maxHrtd)
             maxHrtd = t;
          if (t < minHrtd)
             minHrtd = t;
      }
  }
  maxHrtd -= minHrtd;
  for (j = 0; j < hData -> mIrCount; j ++)
      hData -> mHrtds [j] -= minHrtd;
  hData -> mMaxHrtd = maxHrtd;
}

// Save the OpenAL Soft HRTF data set.
static int SaveMhr (const HrirDataT * hData, const char * fileName) {
  FILE * fp = NULL;
  int e, step, end, j, i;

  if ((fp = fopen (fileName, "wb")) == NULL) {
     fprintf (stderr, "Could not create file, '%s'.\n", fileName);
     return (0);
  }
  if (! WriteString (MHR_FORMAT, fileName, fp))
     return (0);
  if (! WriteUInt32Le ((ALuint) hData -> mIrRate, fileName, fp))
     return (0);
  if (! WriteUInt16Le ((ALushort) hData -> mIrCount, fileName, fp))
     return (0);
  if (! WriteUInt16Le ((ALushort) hData -> mIrSize, fileName, fp))
     return (0);
  if (! WriteUInt8 ((ALubyte) hData -> mEvCount, fileName, fp))
     return (0);
  for (e = 0; e < hData -> mEvCount; e ++) {
      if (! WriteUInt16Le ((ALushort) hData -> mEvOffset [e], fileName, fp))
         return (0);
  }
  step = hData -> mIrSize;
  end = hData -> mIrCount * step;
  for (j = 0; j < end; j += step) {
      for (i = 0; i < step; i ++) {
          if (! WriteFloat32AsInt16Le (hData -> mHrirs [j + i], fileName, fp))
             return (0);
      }
  }
  for (j = 0; j < hData -> mIrCount; j ++) {
      i = (int) round (44100.0f * hData -> mHrtds [j]);
      if (i > 127)
         i = 127;
      if (! WriteUInt8 ((ALubyte) i, fileName, fp))
         return (0);
  }
  fclose (fp);
  return (1);
}

// Save the OpenAL Soft built-in table.
static int SaveTab (const HrirDataT * hData, const char * fileName) {
  FILE * fp = NULL;
  int step, end, j, i;
  char text [16];

  if ((fp = fopen (fileName, "wb")) == NULL) {
     fprintf (stderr, "Could not create file, '%s'.\n", fileName);
     return (0);
  }
  if (! WriteString ("/* This data is Copyright 1994 by the MIT Media Laboratory. It is provided free\n"
                     " * with no restrictions on use, provided the authors are cited when the data is\n"
                     " * used in any research or commercial application. */\n"
                     "/* Bill Gardner <billg@media.mit.edu> and Keith Martin <kdm@media.mit.edu> */\n"
                     "\n"
                     "    /* HRIR Coefficients */\n"
                     "    {\n", fileName, fp))
     return (0);
  step = hData -> mIrSize;
  end = hData -> mIrCount * step;
  for (j = 0; j < end; j += step) {
      if (! WriteString ("        { ", fileName, fp))
         return (0);
      for (i = 0; i < step; i ++) {
          snprintf (text, 15, "%+d, ", (int) round (32767.0f * hData -> mHrirs [j + i]));
          if (! WriteString (text, fileName, fp))
             return (0);
      }
      if (! WriteString ("},\n", fileName, fp))
         return (0);
  }
  if (! WriteString ("    },\n"
                     "\n"
                     "    /* HRIR Delays */\n"
                     "    { ", fileName, fp))
     return (0);
  for (j = 0; j < hData -> mIrCount; j ++) {
      snprintf (text, 15, "%d, ", (int) round (44100.0f * hData -> mHrtds [j]));
      if (! WriteString (text, fileName, fp))
         return (0);
  }
  if (! WriteString ("}\n", fileName, fp))
     return (0);
  fclose (fp);
  return (1);
}

// Loads and processes an MIT data set.  At present, the HRIR and HRTD data
// is loaded and processed in a static buffer.  That should change to using
// heap allocated memory in the future.  A cleanup function will then be
// required.
static int MakeMit(const char *baseInName, HrirDataT *hData)
{
    static float hrirs[MIT_IR_COUNT * MIT_IR_SIZE];
    static float hrtds[MIT_IR_COUNT];

    hData->mIrRate = MIT_IR_RATE;
    hData->mIrCount = MIT_IR_COUNT;
    hData->mIrSize = MIT_IR_SIZE;
    hData->mEvCount = MIT_EV_COUNT;
    hData->mEvStart = MIT_EV_START;
    hData->mEvOffset = MIT_EV_OFFSET;
    hData->mAzCount = MIT_AZ_COUNT;
    hData->mRadius = MIT_RADIUS;
    hData->mDistance = MIT_DISTANCE;
    hData->mHrirs = hrirs;
    hData->mHrtds = hrtds;
    fprintf(stderr, "Loading base HRIR data...\n");
    if(!LoadMitHrirs(baseInName, hData))
        return 0;
    fprintf(stderr, "Performing minimum phase reconstruction and truncation...\n");
    ReconstructHrirs(MIN_IR_SIZE, hData);
    fprintf(stderr, "Renormalizing minimum phase HRIR data...\n");
    RenormalizeHrirs(hData);
    fprintf(stderr, "Synthesizing missing elevations...\n");
    SynthesizeHrirs(hData);
    fprintf(stderr, "Calculating impulse delays...\n");
    CalculateHrtds(hData);
    return 1;
}

// Simple dispatch.  Provided a command, the path to the MIT set of choice,
// and an optional output filename, this will produce an OpenAL Soft
// compatible HRTF set in the chosen format.
int main(int argc, char *argv[])
{
    char baseName[1024];
    const char *outName = NULL;
    HrirDataT hData;

    if(argc < 3 || strcmp(argv [1], "-h") == 0 || strcmp (argv [1], "--help") == 0)
    {
        fprintf(stderr, "Usage:  %s <command> <path of MIT set> [ <output file> ]\n\n", argv[0]);
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, " -m, --make-mhr   Makes an OpenAL Soft compatible HRTF data set.\n");
        fprintf(stderr, "                  Defaults output to:  ./oal_soft_hrtf_44100.mhr\n");
        fprintf(stderr, " -t, --make-tab   Makes the built-in table used when compiling OpenAL Soft.\n");
        fprintf(stderr, "                  Defaults output to:  ./hrtf_tables.inc\n");
        fprintf(stderr, " -h, --help       Displays this help information.\n");
        return 0;
    }

    snprintf(baseName, sizeof(baseName), "%s/elev", argv[2]);
    if(strcmp(argv[1], "-m") == 0 || strcmp(argv[1], "--make-mhr") == 0)
    {
        if(argc > 3)
            outName = argv[3];
        else
            outName = "./oal_soft_hrtf_44100.mhr";
        if(!MakeMit(baseName, &hData))
            return -1;
        fprintf(stderr, "Creating data set file...\n");
        if(!SaveMhr(&hData, outName))
            return -1;
    }
    else if(strcmp(argv[1], "-t") == 0 || strcmp(argv[1], "--make-tab") == 0)
    {
        if(argc > 3)
            outName = argv[3];
        else
            outName = "./hrtf_tables.inc";
        if(!MakeMit(baseName, &hData))
            return -1;
        fprintf(stderr, "Creating table file...\n");
        if(!SaveTab(&hData, outName))
            return -1;
    }
    else
    {
        fprintf(stderr, "Invalid command '%s'\n", argv[1]);
        return -1;
    }
    fprintf(stderr, "Done.\n");
    return 0;
}
