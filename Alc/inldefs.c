
#include "config.h"

#include "alMain.h"
#include "alu.h"
#include "filters/defs.h"
#include "mixer/defs.h"
#include "alBuffer.h"
#include "alEffect.h"
#include "alstring.h"

#include "backends/base.h"

/* This is a place to dump inline function instantiations, to generate function
 * bodies for calls that can't be inlined. C++ does not have a way to do this
 * explicitly, so as long as there is C code calling inline functions, a body
 * must be explicitly instantiated in case of non-inlined calls.
 *
 * This just makes it easier to keep track of everything, while things are
 * converted to C++.
 */

extern inline ALsizei FrameSizeFromDevFmt(enum DevFmtChannels chans, enum DevFmtType type, ALsizei ambiorder);

extern inline ALint GetChannelIndex(const enum Channel names[MAX_OUTPUT_CHANNELS], enum Channel chan);
extern inline ALint GetChannelIdxByName(const RealMixParams *real, enum Channel chan);

extern inline void LockBufferList(ALCdevice *device);
extern inline void UnlockBufferList(ALCdevice *device);
extern inline ALsizei FrameSizeFromUserFmt(enum UserFmtChannels chans, enum UserFmtType type);
extern inline ALsizei FrameSizeFromFmt(enum FmtChannels chans, enum FmtType type);

extern inline ALuint64 GetDeviceClockTime(ALCdevice *device);


extern inline void alstr_reset(al_string *str);
extern inline size_t alstr_length(const_al_string str);
extern inline ALboolean alstr_empty(const_al_string str);
extern inline const al_string_char_type *alstr_get_cstr(const_al_string str);


extern inline ALuint NextPowerOf2(ALuint value);
extern inline size_t RoundUp(size_t value, size_t r);
extern inline ALint fastf2i(ALfloat f);
extern inline int float2int(float f);
extern inline float fast_roundf(float f);
#ifndef __GNUC__
#if defined(HAVE_BITSCANFORWARD64_INTRINSIC)
extern inline int msvc64_ctz64(ALuint64 v);
#elif defined(HAVE_BITSCANFORWARD_INTRINSIC)
extern inline int msvc_ctz64(ALuint64 v);
#else
extern inline int fallback_popcnt64(ALuint64 v);
extern inline int fallback_ctz64(ALuint64 value);
#endif
#endif

extern inline ALfloat minf(ALfloat a, ALfloat b);
extern inline ALfloat maxf(ALfloat a, ALfloat b);
extern inline ALfloat clampf(ALfloat val, ALfloat min, ALfloat max);

extern inline ALdouble mind(ALdouble a, ALdouble b);
extern inline ALdouble maxd(ALdouble a, ALdouble b);
extern inline ALdouble clampd(ALdouble val, ALdouble min, ALdouble max);

extern inline ALuint minu(ALuint a, ALuint b);
extern inline ALuint maxu(ALuint a, ALuint b);
extern inline ALuint clampu(ALuint val, ALuint min, ALuint max);

extern inline ALint mini(ALint a, ALint b);
extern inline ALint maxi(ALint a, ALint b);
extern inline ALint clampi(ALint val, ALint min, ALint max);

extern inline ALint64 mini64(ALint64 a, ALint64 b);
extern inline ALint64 maxi64(ALint64 a, ALint64 b);
extern inline ALint64 clampi64(ALint64 val, ALint64 min, ALint64 max);

extern inline ALuint64 minu64(ALuint64 a, ALuint64 b);
extern inline ALuint64 maxu64(ALuint64 a, ALuint64 b);
extern inline ALuint64 clampu64(ALuint64 val, ALuint64 min, ALuint64 max);

extern inline size_t minz(size_t a, size_t b);
extern inline size_t maxz(size_t a, size_t b);
extern inline size_t clampz(size_t val, size_t min, size_t max);

extern inline ALfloat lerp(ALfloat val1, ALfloat val2, ALfloat mu);
extern inline ALfloat cubic(ALfloat val1, ALfloat val2, ALfloat val3, ALfloat val4, ALfloat mu);

extern inline void aluVectorSet(aluVector *vector, ALfloat x, ALfloat y, ALfloat z, ALfloat w);

extern inline void aluMatrixfSetRow(aluMatrixf *matrix, ALuint row,
                                    ALfloat m0, ALfloat m1, ALfloat m2, ALfloat m3);
extern inline void aluMatrixfSet(aluMatrixf *matrix,
                                 ALfloat m00, ALfloat m01, ALfloat m02, ALfloat m03,
                                 ALfloat m10, ALfloat m11, ALfloat m12, ALfloat m13,
                                 ALfloat m20, ALfloat m21, ALfloat m22, ALfloat m23,
                                 ALfloat m30, ALfloat m31, ALfloat m32, ALfloat m33);

extern inline void CalcDirectionCoeffs(const ALfloat dir[3], ALfloat spread, ALfloat coeffs[MAX_AMBI_COEFFS]);
extern inline void CalcAngleCoeffs(ALfloat azimuth, ALfloat elevation, ALfloat spread, ALfloat coeffs[MAX_AMBI_COEFFS]);
extern inline float ScaleAzimuthFront(float azimuth, float scale);
extern inline void ComputePanGains(const MixParams *dry, const ALfloat*RESTRICT coeffs, ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS]);

extern inline void BiquadFilter_clear(BiquadFilter *filter);
extern inline void BiquadFilter_copyParams(BiquadFilter *RESTRICT dst, const BiquadFilter *RESTRICT src);
extern inline void BiquadFilter_passthru(BiquadFilter *filter, ALsizei numsamples);
extern inline ALfloat calc_rcpQ_from_slope(ALfloat gain, ALfloat slope);
extern inline ALfloat calc_rcpQ_from_bandwidth(ALfloat f0norm, ALfloat bandwidth);


extern inline void LockFilterList(ALCdevice *device);
extern inline void UnlockFilterList(ALCdevice *device);

extern inline void LockEffectList(ALCdevice *device);
extern inline void UnlockEffectList(ALCdevice *device);
extern inline ALboolean IsReverbEffect(ALenum type);

extern inline void LockEffectSlotList(ALCcontext *context);
extern inline void UnlockEffectSlotList(ALCcontext *context);


extern inline void InitiatePositionArrays(ALsizei frac, ALint increment, ALsizei *RESTRICT frac_arr, ALsizei *RESTRICT pos_arr, ALsizei size);
