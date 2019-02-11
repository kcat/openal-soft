#ifndef AL_MAIN_H
#define AL_MAIN_H

#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <limits.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_INTRIN_H
#include <intrin.h>
#endif

#include <array>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "inprogext.h"
#include "atomic.h"
#include "vector.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "threads.h"
#include "ambidefs.h"
#include "opthelpers.h"


template<typename T, size_t N>
constexpr inline size_t countof(const T(&)[N]) noexcept
{ return N; }
#define COUNTOF countof


#ifndef UNUSED
#if defined(__cplusplus)
#define UNUSED(x)
#elif defined(__GNUC__)
#define UNUSED(x) UNUSED_##x __attribute__((unused))
#elif defined(__LCLINT__)
#define UNUSED(x) /*@unused@*/ x
#else
#define UNUSED(x) x
#endif
#endif


/* Define CTZ macros (count trailing zeros), and POPCNT macros (population
 * count/count 1 bits), for 32- and 64-bit integers. The CTZ macros' results
 * are *UNDEFINED* if the value is 0.
 */
#ifdef __GNUC__

#define POPCNT32 __builtin_popcount
#define CTZ32 __builtin_ctz
#if SIZEOF_LONG == 8
#define POPCNT64 __builtin_popcountl
#define CTZ64 __builtin_ctzl
#else
#define POPCNT64 __builtin_popcountll
#define CTZ64 __builtin_ctzll
#endif

#elif defined(HAVE_BITSCANFORWARD64_INTRINSIC)

inline int msvc64_popcnt32(ALuint v)
{ return (int)__popcnt(v); }
#define POPCNT32 msvc64_popcnt32
inline int msvc64_ctz32(ALuint v)
{
    unsigned long idx = 32;
    _BitScanForward(&idx, v);
    return (int)idx;
}
#define CTZ32 msvc64_ctz32

inline int msvc64_popcnt64(uint64_t v)
{ return (int)__popcnt64(v); }
#define POPCNT64 msvc64_popcnt64
inline int msvc64_ctz64(uint64_t v)
{
    unsigned long idx = 64;
    _BitScanForward64(&idx, v);
    return (int)idx;
}
#define CTZ64 msvc64_ctz64

#elif defined(HAVE_BITSCANFORWARD_INTRINSIC)

inline int msvc_popcnt32(ALuint v)
{ return (int)__popcnt(v); }
#define POPCNT32 msvc_popcnt32
inline int msvc_ctz32(ALuint v)
{
    unsigned long idx = 32;
    _BitScanForward(&idx, v);
    return (int)idx;
}
#define CTZ32 msvc_ctz32

inline int msvc_popcnt64(uint64_t v)
{ return (int)(__popcnt((ALuint)v) + __popcnt((ALuint)(v>>32))); }
#define POPCNT64 msvc_popcnt64
inline int msvc_ctz64(uint64_t v)
{
    unsigned long idx = 64;
    if(!_BitScanForward(&idx, v&0xffffffff))
    {
        if(_BitScanForward(&idx, v>>32))
            idx += 32;
    }
    return (int)idx;
}
#define CTZ64 msvc_ctz64

#else

/* There be black magics here. The popcnt method is derived from
 * https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
 * while the ctz-utilizing-popcnt algorithm is shown here
 * http://www.hackersdelight.org/hdcodetxt/ntz.c.txt
 * as the ntz2 variant. These likely aren't the most efficient methods, but
 * they're good enough if the GCC or MSVC intrinsics aren't available.
 */
inline int fallback_popcnt32(ALuint v)
{
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    v = (v + (v >> 4)) & 0x0f0f0f0fu;
    return (int)((v * 0x01010101u) >> 24);
}
#define POPCNT32 fallback_popcnt32
inline int fallback_ctz32(ALuint value)
{ return fallback_popcnt32(~value & (value - 1)); }
#define CTZ32 fallback_ctz32

inline int fallback_popcnt64(uint64_t v)
{
    v = v - ((v >> 1) & 0x5555555555555555_u64);
    v = (v & 0x3333333333333333_u64) + ((v >> 2) & 0x3333333333333333_u64);
    v = (v + (v >> 4)) & 0x0f0f0f0f0f0f0f0f_u64;
    return (int)((v * 0x0101010101010101_u64) >> 56);
}
#define POPCNT64 fallback_popcnt64
inline int fallback_ctz64(uint64_t value)
{ return fallback_popcnt64(~value & (value - 1)); }
#define CTZ64 fallback_ctz64
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#define IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#else
static const union {
    ALuint u;
    ALubyte b[sizeof(ALuint)];
} EndianTest = { 1 };
#define IS_LITTLE_ENDIAN (EndianTest.b[0] == 1)
#endif


struct HrtfEntry;
struct HrtfHandle;
struct EnumeratedHrtf;
struct DirectHrtfState;
struct FrontStablizer;
struct Compressor;
struct BackendBase;
struct ALbuffer;
struct ALeffect;
struct ALfilter;
struct EffectState;
struct Uhj2Encoder;
class BFormatDec;
class AmbiUpsampler;
struct bs2b;


#define DEFAULT_UPDATE_SIZE  (1024)
#define DEFAULT_NUM_UPDATES  (3)
#define DEFAULT_OUTPUT_RATE  (44100)
#define MIN_OUTPUT_RATE      (8000)


/* Fast float-to-int conversion. No particular rounding mode is assumed; the
 * IEEE-754 default is round-to-nearest with ties-to-even, though an app could
 * change it on its own threads. On some systems, a truncating conversion may
 * always be the fastest method.
 */
inline ALint fastf2i(ALfloat f) noexcept
{
#if defined(HAVE_INTRIN_H) && ((defined(_M_IX86_FP) && (_M_IX86_FP > 0)) || defined(_M_X64))
    return _mm_cvt_ss2si(_mm_set1_ps(f));

#elif defined(_MSC_VER) && defined(_M_IX86_FP)

    ALint i;
    __asm fld f
    __asm fistp i
    return i;

#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))

    ALint i;
#ifdef __SSE_MATH__
    __asm__("cvtss2si %1, %0" : "=r"(i) : "x"(f));
#else
    __asm__ __volatile__("fistpl %0" : "=m"(i) : "t"(f) : "st");
#endif
    return i;

    /* On GCC when compiling with -fno-math-errno, lrintf can be inlined to
     * some simple instructions. Clang does not inline it, always generating a
     * libc call, while MSVC's implementation is horribly slow, so always fall
     * back to a normal integer conversion for them.
     */
#elif !defined(_MSC_VER) && !defined(__clang__)

    return lrintf(f);

#else

    return (ALint)f;
#endif
}

/* Converts float-to-int using standard behavior (truncation). */
inline int float2int(float f) noexcept
{
#if ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)) && \
     !defined(__SSE_MATH__)) || (defined(_MSC_VER) && defined(_M_IX86_FP) && _M_IX86_FP == 0)
    ALint sign, shift, mant;
    union {
        ALfloat f;
        ALint i;
    } conv;

    conv.f = f;
    sign = (conv.i>>31) | 1;
    shift = ((conv.i>>23)&0xff) - (127+23);

    /* Over/underflow */
    if(UNLIKELY(shift >= 31 || shift < -23))
        return 0;

    mant = (conv.i&0x7fffff) | 0x800000;
    if(LIKELY(shift < 0))
        return (mant >> -shift) * sign;
    return (mant << shift) * sign;

#else

    return static_cast<ALint>(f);
#endif
}

/* Rounds a float to the nearest integral value, according to the current
 * rounding mode. This is essentially an inlined version of rintf, although
 * makes fewer promises (e.g. -0 or -0.25 rounded to 0 may result in +0).
 */
inline float fast_roundf(float f) noexcept
{
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)) && \
    !defined(__SSE_MATH__)

    float out;
    __asm__ __volatile__("frndint" : "=t"(out) : "0"(f));
    return out;

#else

    /* Integral limit, where sub-integral precision is not available for
     * floats.
     */
    static const float ilim[2] = {
         8388608.0f /*  0x1.0p+23 */,
        -8388608.0f /* -0x1.0p+23 */
    };
    ALuint sign, expo;
    union {
        ALfloat f;
        ALuint i;
    } conv;

    conv.f = f;
    sign = (conv.i>>31)&0x01;
    expo = (conv.i>>23)&0xff;

    if(UNLIKELY(expo >= 150/*+23*/))
    {
        /* An exponent (base-2) of 23 or higher is incapable of sub-integral
         * precision, so it's already an integral value. We don't need to worry
         * about infinity or NaN here.
         */
        return f;
    }
    /* Adding the integral limit to the value (with a matching sign) forces a
     * result that has no sub-integral precision, and is consequently forced to
     * round to an integral value. Removing the integral limit then restores
     * the initial value rounded to the integral. The compiler should not
     * optimize this out because of non-associative rules on floating-point
     * math (as long as you don't use -fassociative-math,
     * -funsafe-math-optimizations, -ffast-math, or -Ofast, in which case this
     * may break).
     */
    f += ilim[sign];
    return f - ilim[sign];
#endif
}


enum DevProbe {
    ALL_DEVICE_PROBE,
    CAPTURE_DEVICE_PROBE
};


enum Channel {
    FrontLeft = 0,
    FrontRight,
    FrontCenter,
    LFE,
    BackLeft,
    BackRight,
    BackCenter,
    SideLeft,
    SideRight,

    UpperFrontLeft,
    UpperFrontRight,
    UpperBackLeft,
    UpperBackRight,
    LowerFrontLeft,
    LowerFrontRight,
    LowerBackLeft,
    LowerBackRight,

    Aux0,
    Aux1,
    Aux2,
    Aux3,
    Aux4,
    Aux5,
    Aux6,
    Aux7,
    Aux8,
    Aux9,
    Aux10,
    Aux11,
    Aux12,
    Aux13,
    Aux14,
    Aux15,

    InvalidChannel
};


/* Device formats */
enum DevFmtType {
    DevFmtByte   = ALC_BYTE_SOFT,
    DevFmtUByte  = ALC_UNSIGNED_BYTE_SOFT,
    DevFmtShort  = ALC_SHORT_SOFT,
    DevFmtUShort = ALC_UNSIGNED_SHORT_SOFT,
    DevFmtInt    = ALC_INT_SOFT,
    DevFmtUInt   = ALC_UNSIGNED_INT_SOFT,
    DevFmtFloat  = ALC_FLOAT_SOFT,

    DevFmtTypeDefault = DevFmtFloat
};
enum DevFmtChannels {
    DevFmtMono   = ALC_MONO_SOFT,
    DevFmtStereo = ALC_STEREO_SOFT,
    DevFmtQuad   = ALC_QUAD_SOFT,
    DevFmtX51    = ALC_5POINT1_SOFT,
    DevFmtX61    = ALC_6POINT1_SOFT,
    DevFmtX71    = ALC_7POINT1_SOFT,
    DevFmtAmbi3D = ALC_BFORMAT3D_SOFT,

    /* Similar to 5.1, except using rear channels instead of sides */
    DevFmtX51Rear = 0x80000000,

    DevFmtChannelsDefault = DevFmtStereo
};
#define MAX_OUTPUT_CHANNELS  (16)

/* DevFmtType traits, providing the type, etc given a DevFmtType. */
template<DevFmtType T>
struct DevFmtTypeTraits { };

template<>
struct DevFmtTypeTraits<DevFmtByte> { using Type = ALbyte; };
template<>
struct DevFmtTypeTraits<DevFmtUByte> { using Type = ALubyte; };
template<>
struct DevFmtTypeTraits<DevFmtShort> { using Type = ALshort; };
template<>
struct DevFmtTypeTraits<DevFmtUShort> { using Type = ALushort; };
template<>
struct DevFmtTypeTraits<DevFmtInt> { using Type = ALint; };
template<>
struct DevFmtTypeTraits<DevFmtUInt> { using Type = ALuint; };
template<>
struct DevFmtTypeTraits<DevFmtFloat> { using Type = ALfloat; };


ALsizei BytesFromDevFmt(DevFmtType type) noexcept;
ALsizei ChannelsFromDevFmt(DevFmtChannels chans, ALsizei ambiorder) noexcept;
inline ALsizei FrameSizeFromDevFmt(DevFmtChannels chans, DevFmtType type, ALsizei ambiorder) noexcept
{ return ChannelsFromDevFmt(chans, ambiorder) * BytesFromDevFmt(type); }

enum class AmbiLayout {
    FuMa = ALC_FUMA_SOFT, /* FuMa channel order */
    ACN = ALC_ACN_SOFT,   /* ACN channel order */

    Default = ACN
};

enum class AmbiNorm {
    FuMa = ALC_FUMA_SOFT, /* FuMa normalization */
    SN3D = ALC_SN3D_SOFT, /* SN3D normalization */
    N3D = ALC_N3D_SOFT,   /* N3D normalization */

    Default = SN3D
};


enum DeviceType {
    Playback,
    Capture,
    Loopback
};


enum RenderMode {
    NormalRender,
    StereoPair,
    HrtfRender
};


struct BufferSubList {
    uint64_t FreeMask{~0_u64};
    ALbuffer *Buffers{nullptr}; /* 64 */

    BufferSubList() noexcept = default;
    BufferSubList(const BufferSubList&) = delete;
    BufferSubList(BufferSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Buffers{rhs.Buffers}
    { rhs.FreeMask = ~0_u64; rhs.Buffers = nullptr; }
    ~BufferSubList();

    BufferSubList& operator=(const BufferSubList&) = delete;
    BufferSubList& operator=(BufferSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Buffers, rhs.Buffers); return *this; }
};

struct EffectSubList {
    uint64_t FreeMask{~0_u64};
    ALeffect *Effects{nullptr}; /* 64 */

    EffectSubList() noexcept = default;
    EffectSubList(const EffectSubList&) = delete;
    EffectSubList(EffectSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Effects{rhs.Effects}
    { rhs.FreeMask = ~0_u64; rhs.Effects = nullptr; }
    ~EffectSubList();

    EffectSubList& operator=(const EffectSubList&) = delete;
    EffectSubList& operator=(EffectSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Effects, rhs.Effects); return *this; }
};

struct FilterSubList {
    uint64_t FreeMask{~0_u64};
    ALfilter *Filters{nullptr}; /* 64 */

    FilterSubList() noexcept = default;
    FilterSubList(const FilterSubList&) = delete;
    FilterSubList(FilterSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Filters{rhs.Filters}
    { rhs.FreeMask = ~0_u64; rhs.Filters = nullptr; }
    ~FilterSubList();

    FilterSubList& operator=(const FilterSubList&) = delete;
    FilterSubList& operator=(FilterSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Filters, rhs.Filters); return *this; }
};


/* Maximum delay in samples for speaker distance compensation. */
#define MAX_DELAY_LENGTH 1024

class DistanceComp {
public:
    struct DistData {
        ALfloat Gain{1.0f};
        ALsizei Length{0}; /* Valid range is [0...MAX_DELAY_LENGTH). */
        ALfloat *Buffer{nullptr};
    };

private:
    DistData mChannel[MAX_OUTPUT_CHANNELS];
    al::vector<ALfloat,16> mSamples;

public:
    void resize(size_t new_size) { mSamples.resize(new_size); }
    void shrink_to_fit() { mSamples.shrink_to_fit(); }
    void clear() noexcept
    {
        for(auto &chan : mChannel)
        {
            chan.Gain = 1.0f;
            chan.Length = 0;
            chan.Buffer = nullptr;
        }
        mSamples.clear();
    }

    DistData *begin() noexcept { return std::begin(mChannel); }
    const DistData *begin() const noexcept { return std::begin(mChannel); }
    const DistData *cbegin() const noexcept { return std::begin(mChannel); }
    DistData *end() noexcept { return std::end(mChannel); }
    const DistData *end() const noexcept { return std::end(mChannel); }
    const DistData *cend() const noexcept { return std::end(mChannel); }

    ALfloat *data() noexcept { return mSamples.data(); }
    const ALfloat *data() const noexcept { return mSamples.data(); }

    DistData& operator[](size_t o) noexcept { return mChannel[o]; }
    const DistData& operator[](size_t o) const noexcept { return mChannel[o]; }
};

struct BFChannelConfig {
    ALfloat Scale;
    ALsizei Index;
};

/* Size for temporary storage of buffer data, in ALfloats. Larger values need
 * more memory, while smaller values may need more iterations. The value needs
 * to be a sensible size, however, as it constrains the max stepping value used
 * for mixing, as well as the maximum number of samples per mixing iteration.
 */
#define BUFFERSIZE 2048

struct MixParams {
    /* Coefficient channel mapping for mixing to the buffer. */
    std::array<BFChannelConfig,MAX_OUTPUT_CHANNELS> AmbiMap;

    ALfloat (*Buffer)[BUFFERSIZE]{nullptr};
    ALsizei NumChannels{0};
};

struct RealMixParams {
    Channel ChannelName[MAX_OUTPUT_CHANNELS]{};

    ALfloat (*Buffer)[BUFFERSIZE]{nullptr};
    ALsizei NumChannels{0};
};

using POSTPROCESS = void(*)(ALCdevice *device, const ALsizei SamplesToDo);

struct ALCdevice {
    RefCount ref{1u};

    std::atomic<bool> Connected{true};
    const DeviceType Type{};

    ALuint Frequency{};
    ALuint UpdateSize{};
    ALuint NumUpdates{};
    DevFmtChannels FmtChans{};
    DevFmtType     FmtType{};
    ALboolean IsHeadphones{AL_FALSE};
    ALsizei mAmbiOrder{0};
    /* For DevFmtAmbi* output only, specifies the channel order and
     * normalization.
     */
    AmbiLayout mAmbiLayout{AmbiLayout::Default};
    AmbiNorm   mAmbiScale{AmbiNorm::Default};

    ALCenum LimiterState{ALC_DONT_CARE_SOFT};

    std::string DeviceName;

    // Device flags
    ALuint Flags{0u};

    std::string HrtfName;
    al::vector<EnumeratedHrtf> HrtfList;
    ALCenum HrtfStatus{ALC_FALSE};

    std::atomic<ALCenum> LastError{ALC_NO_ERROR};

    // Maximum number of sources that can be created
    ALuint SourcesMax{};
    // Maximum number of slots that can be created
    ALuint AuxiliaryEffectSlotMax{};

    ALCuint NumMonoSources{};
    ALCuint NumStereoSources{};
    ALsizei NumAuxSends{};

    // Map of Buffers for this device
    std::mutex BufferLock;
    al::vector<BufferSubList> BufferList;

    // Map of Effects for this device
    std::mutex EffectLock;
    al::vector<EffectSubList> EffectList;

    // Map of Filters for this device
    std::mutex FilterLock;
    al::vector<FilterSubList> FilterList;

    /* Rendering mode. */
    RenderMode mRenderMode{NormalRender};

    /* The average speaker distance as determined by the ambdec configuration
     * (or alternatively, by the NFC-HOA reference delay). Only used for NFC.
     */
    ALfloat AvgSpeakerDist{0.0f};

    ALuint SamplesDone{0u};
    std::chrono::nanoseconds ClockBase{0};
    std::chrono::nanoseconds FixedLatency{0};

    /* Temp storage used for mixer processing. */
    alignas(16) ALfloat TempBuffer[4][BUFFERSIZE];

    /* Mixing buffer used by the Dry mix, FOAOut, and Real out. */
    al::vector<std::array<ALfloat,BUFFERSIZE>, 16> MixBuffer;

    /* The "dry" path corresponds to the main output. */
    MixParams Dry;
    ALsizei NumChannelsPerOrder[MAX_AMBI_ORDER+1]{};

    /* First-order ambisonics output, to be upsampled to the dry buffer if different. */
    MixParams FOAOut;

    /* "Real" output, which will be written to the device buffer. May alias the
     * dry buffer.
     */
    RealMixParams RealOut;

    /* HRTF state and info */
    std::unique_ptr<DirectHrtfState> mHrtfState;
    HrtfEntry *mHrtf{nullptr};

    /* UHJ encoder state */
    std::unique_ptr<Uhj2Encoder> Uhj_Encoder;

    /* High quality Ambisonic decoder */
    std::unique_ptr<BFormatDec> AmbiDecoder;

    /* Stereo-to-binaural filter */
    std::unique_ptr<bs2b> Bs2b;

    /* First-order ambisonic upsampler for higher-order output */
    std::unique_ptr<AmbiUpsampler> AmbiUp;

    POSTPROCESS PostProcess{};

    std::unique_ptr<FrontStablizer> Stablizer;

    std::unique_ptr<Compressor> Limiter;

    /* Delay buffers used to compensate for speaker distances. */
    DistanceComp ChannelDelay;

    /* Dithering control. */
    ALfloat DitherDepth{0.0f};
    ALuint DitherSeed{0u};

    /* Running count of the mixer invocations, in 31.1 fixed point. This
     * actually increments *twice* when mixing, first at the start and then at
     * the end, so the bottom bit indicates if the device is currently mixing
     * and the upper bits indicates how many mixes have been done.
     */
    RefCount MixCount{0u};

    // Contexts created on this device
    std::atomic<ALCcontext*> ContextList{nullptr};

    /* This lock protects the device state (format, update size, etc) from
     * being from being changed in multiple threads, or being accessed while
     * being changed. It's also used to serialize calls to the backend.
     */
    std::mutex StateLock;
    std::unique_ptr<BackendBase> Backend;

    std::atomic<ALCdevice*> next{nullptr};


    ALCdevice(DeviceType type);
    ALCdevice(const ALCdevice&) = delete;
    ALCdevice& operator=(const ALCdevice&) = delete;
    ~ALCdevice();

    ALsizei bytesFromFmt() const noexcept { return BytesFromDevFmt(FmtType); }
    ALsizei channelsFromFmt() const noexcept { return ChannelsFromDevFmt(FmtChans, mAmbiOrder); }
    ALsizei frameSizeFromFmt() const noexcept { return bytesFromFmt() * channelsFromFmt(); }

    static constexpr inline const char *CurrentPrefix() noexcept { return "ALCdevice::"; }
    DEF_NEWDEL(ALCdevice)
};

// Frequency was requested by the app or config file
#define DEVICE_FREQUENCY_REQUEST                 (1u<<1)
// Channel configuration was requested by the config file
#define DEVICE_CHANNELS_REQUEST                  (1u<<2)
// Sample type was requested by the config file
#define DEVICE_SAMPLE_TYPE_REQUEST               (1u<<3)

// Specifies if the DSP is paused at user request
#define DEVICE_PAUSED                            (1u<<30)

// Specifies if the device is currently running
#define DEVICE_RUNNING                           (1u<<31)


/* Must be less than 15 characters (16 including terminating null) for
 * compatibility with pthread_setname_np limitations. */
#define MIXER_THREAD_NAME "alsoft-mixer"

#define RECORD_THREAD_NAME "alsoft-record"


enum {
    /* End event thread processing. */
    EventType_KillThread = 0,

    /* User event types. */
    EventType_SourceStateChange = 1<<0,
    EventType_BufferCompleted   = 1<<1,
    EventType_Error             = 1<<2,
    EventType_Performance       = 1<<3,
    EventType_Deprecated        = 1<<4,
    EventType_Disconnected      = 1<<5,

    /* Internal events. */
    EventType_ReleaseEffectState = 65536,
};

struct AsyncEvent {
    unsigned int EnumType{0u};
    union {
        char dummy;
        struct {
            ALuint id;
            ALenum state;
        } srcstate;
        struct {
            ALuint id;
            ALsizei count;
        } bufcomp;
        struct {
            ALenum type;
            ALuint id;
            ALuint param;
            ALchar msg[1008];
        } user;
        EffectState *mEffectState;
    } u{};

    AsyncEvent() noexcept = default;
    constexpr AsyncEvent(unsigned int type) noexcept : EnumType{type} { }
};


void AllocateVoices(ALCcontext *context, ALsizei num_voices, ALsizei old_sends);


extern ALint RTPrioLevel;
void SetRTPriority(void);

void SetDefaultChannelOrder(ALCdevice *device);
void SetDefaultWFXChannelOrder(ALCdevice *device);

const ALCchar *DevFmtTypeString(DevFmtType type) noexcept;
const ALCchar *DevFmtChannelsString(DevFmtChannels chans) noexcept;

inline ALint GetChannelIndex(const Channel (&names)[MAX_OUTPUT_CHANNELS], Channel chan)
{
    auto iter = std::find(std::begin(names), std::end(names), chan);
    if(iter == std::end(names)) return -1;
    return static_cast<ALint>(std::distance(std::begin(names), iter));
}
/**
 * GetChannelIdxByName
 *
 * Returns the index for the given channel name (e.g. FrontCenter), or -1 if it
 * doesn't exist.
 */
inline ALint GetChannelIdxByName(const RealMixParams &real, Channel chan)
{ return GetChannelIndex(real.ChannelName, chan); }


void StartEventThrd(ALCcontext *ctx);
void StopEventThrd(ALCcontext *ctx);


al::vector<std::string> SearchDataFiles(const char *match, const char *subdir);

#endif
