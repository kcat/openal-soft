
#ifdef __cplusplus
extern "C" {
#endif

#ifndef M_PI
#define M_PI                         (3.14159265358979323846)
#endif

#ifndef HUGE_VAL
#define HUGE_VAL                     (1.0 / 0.0)
#endif

// The epsilon used to maintain signal stability.
#define EPSILON                      (1e-15)

// Constants for accessing the token reader's ring buffer.
#define TR_RING_BITS                 (16)
#define TR_RING_SIZE                 (1 << TR_RING_BITS)
#define TR_RING_MASK                 (TR_RING_SIZE - 1)

// The token reader's load interval in bytes.
#define TR_LOAD_SIZE                 (TR_RING_SIZE >> 2)

// The maximum identifier length used when processing the data set
// definition.
#define MAX_IDENT_LEN                (16)

// The maximum path length used when processing filenames.
#define MAX_PATH_LEN                 (256)

// The limits for the sample 'rate' metric in the data set definition and for
// resampling.
#define MIN_RATE                     (32000)
#define MAX_RATE                     (96000)

// The limits for the HRIR 'points' metric in the data set definition.
#define MIN_POINTS                   (16)
#define MAX_POINTS                   (8192)

// The limits to the number of 'azimuths' listed in the data set definition.
#define MIN_EV_COUNT                 (5)
#define MAX_EV_COUNT                 (256)

// The limits for each of the 'azimuths' listed in the data set definition.
#define MIN_AZ_COUNT                 (1)
#define MAX_AZ_COUNT                 (512)

// The limits for the listener's head 'radius' in the data set definition.
#define MIN_RADIUS                   (0.05)
#define MAX_RADIUS                   (0.15)

// The limits for the 'distance' from source to listener in the definition
// file.
#define MIN_DISTANCE                 (0.5)
#define MAX_DISTANCE                 (2.5)

// The maximum number of channels that can be addressed for a WAVE file
// source listed in the data set definition.
#define MAX_WAVE_CHANNELS            (65535)

// The limits to the byte size for a binary source listed in the definition
// file.
#define MIN_BIN_SIZE                 (2)
#define MAX_BIN_SIZE                 (4)

// The minimum number of significant bits for binary sources listed in the
// data set definition.  The maximum is calculated from the byte size.
#define MIN_BIN_BITS                 (16)

// The limits to the number of significant bits for an ASCII source listed in
// the data set definition.
#define MIN_ASCII_BITS               (16)
#define MAX_ASCII_BITS               (32)

// The limits to the FFT window size override on the command line.
#define MIN_FFTSIZE                  (512)
#define MAX_FFTSIZE                  (16384)

// The limits to the equalization range limit on the command line.
#define MIN_LIMIT                    (2.0)
#define MAX_LIMIT                    (120.0)

// The limits to the truncation window size on the command line.
#define MIN_TRUNCSIZE                (8)
#define MAX_TRUNCSIZE                (128)

// The limits to the custom head radius on the command line.
#define MIN_CUSTOM_RADIUS            (0.05)
#define MAX_CUSTOM_RADIUS            (0.15)

// The truncation window size must be a multiple of the below value to allow
// for vectorized convolution.
#define MOD_TRUNCSIZE                (8)

// The defaults for the command line options.
#define DEFAULT_EQUALIZE             (1)
#define DEFAULT_SURFACE              (1)
#define DEFAULT_LIMIT                (24.0)
#define DEFAULT_TRUNCSIZE            (32)
#define DEFAULT_HEAD_MODEL           (HM_DATASET)
#define DEFAULT_CUSTOM_RADIUS        (0.0)

// The four-character-codes for RIFF/RIFX WAVE file chunks.
#define FOURCC_RIFF                  (0x46464952) // 'RIFF'
#define FOURCC_RIFX                  (0x58464952) // 'RIFX'
#define FOURCC_WAVE                  (0x45564157) // 'WAVE'
#define FOURCC_FMT                   (0x20746D66) // 'fmt '
#define FOURCC_DATA                  (0x61746164) // 'data'
#define FOURCC_LIST                  (0x5453494C) // 'LIST'
#define FOURCC_WAVL                  (0x6C766177) // 'wavl'
#define FOURCC_SLNT                  (0x746E6C73) // 'slnt'

// The supported wave formats.
#define WAVE_FORMAT_PCM              (0x0001)
#define WAVE_FORMAT_IEEE_FLOAT       (0x0003)
#define WAVE_FORMAT_EXTENSIBLE       (0xFFFE)

// The maximum propagation delay value supported by OpenAL Soft.
#define MAX_HRTD                     (63.0)

// The OpenAL Soft HRTF format marker.  It stands for minimum-phase head
// response protocol 01.
#define MHR_FORMAT                   ("MinPHR01")

// Byte order for the serialization routines.
typedef enum ByteOrderT {
    BO_NONE,
    BO_LITTLE,
    BO_BIG
} ByteOrderT;

// Source format for the references listed in the data set definition.
typedef enum SourceFormatT {
    SF_NONE,
    SF_WAVE,   // RIFF/RIFX WAVE file.
    SF_BIN_LE, // Little-endian binary file.
    SF_BIN_BE, // Big-endian binary file.
    SF_ASCII   // ASCII text file.
} SourceFormatT;

// Element types for the references listed in the data set definition.
typedef enum ElementTypeT {
    ET_NONE,
    ET_INT,   // Integer elements.
    ET_FP    // Floating-point elements.
} ElementTypeT;

// Head model used for calculating the impulse delays.
typedef enum HeadModelT {
    HM_NONE,
    HM_DATASET, // Measure the onset from the dataset.
    HM_SPHERE   // Calculate the onset using a spherical head model.
} HeadModelT;

// Desired output format from the command line.
typedef enum OutputFormatT {
    OF_NONE,
    OF_MHR   // OpenAL Soft MHR data set file.
} OutputFormatT;

// Unsigned integer type.
typedef unsigned int uint;

// Serialization types.  The trailing digit indicates the number of bits.
typedef ALubyte      uint8;
typedef ALint        int32;
typedef ALuint       uint32;
typedef ALuint64SOFT uint64;

// Token reader state for parsing the data set definition.
typedef struct TokenReaderT {
    FILE *mFile;
    const char *mName;
    uint        mLine;
    uint        mColumn;
    char   mRing[TR_RING_SIZE];
    size_t mIn;
    size_t mOut;
} TokenReaderT;

// Source reference state used when loading sources.
typedef struct SourceRefT {
    SourceFormatT mFormat;
    ElementTypeT  mType;
    uint mSize;
    int  mBits;
    uint mChannel;
    uint mSkip;
    uint mOffset;
    char mPath[MAX_PATH_LEN+1];
} SourceRefT;

// The HRIR metrics and data set used when loading, processing, and storing
// the resulting HRTF.
typedef struct HrirDataT {
    uint mIrRate;
    uint mIrCount;
    uint mIrSize;
    uint mIrPoints;
    uint mFftSize;
    uint mEvCount;
    uint mEvStart;
    uint mAzCount[MAX_EV_COUNT];
    uint mEvOffset[MAX_EV_COUNT];
    uint mStereo;
    double mRadius;
    double mDistance;
    double *mHrirs;
    double *mHrtds;
    double  mMaxHrtd;
} HrirDataT;

// The resampler metrics and FIR filter.
typedef struct ResamplerT {
    uint mP, mQ, mM, mL;
    double *mF;
} ResamplerT;

double *CreateArray(size_t n);
void DestroyArray(double *a);
int hrtfPostProcessing(const uint outRate, const int equalize, const int surface, const double limit, const uint truncSize, 
	const HeadModelT model, const double radius, const OutputFormatT outFormat, const char *outName, HrirDataT *hData);
void AverageHrirOnset(const double *hrir, const double f, const uint ei, const uint ai, const HrirDataT *hData);
void AverageHrirMagnitude(const double *hrir, const double f, const uint ei, const uint ai, const HrirDataT *hData);

#ifdef __cplusplus
}
#endif

