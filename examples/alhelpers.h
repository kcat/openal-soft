#ifndef ALHELPERS_H
#define ALHELPERS_H

#ifndef _WIN32
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef AL_SOFT_buffer_samples
#define AL_SOFT_buffer_samples 1
/* Sample types */
#define AL_BYTE                                  0x1400
#define AL_UNSIGNED_BYTE                         0x1401
#define AL_SHORT                                 0x1402
#define AL_UNSIGNED_SHORT                        0x1403
#define AL_INT                                   0x1404
#define AL_UNSIGNED_INT                          0x1405
#define AL_FLOAT                                 0x1406
#define AL_DOUBLE                                0x1407
#define AL_BYTE3                                 0x1408
#define AL_UNSIGNED_BYTE3                        0x1409

/* Channel configurations */
#define AL_MONO                                  0x1500
#define AL_STEREO                                0x1501
#define AL_REAR                                  0x1502
#define AL_QUAD                                  0x1503
#define AL_5POINT1                               0x1504
#define AL_6POINT1                               0x1505
#define AL_7POINT1                               0x1506

/* Storage formats */
#define AL_MONO8                                 0x1100
#define AL_MONO16                                0x1101
#define AL_MONO32F                               0x10010
#define AL_STEREO8                               0x1102
#define AL_STEREO16                              0x1103
#define AL_STEREO32F                             0x10011
#define AL_QUAD8                                 0x1204
#define AL_QUAD16                                0x1205
#define AL_QUAD32F                               0x1206
#define AL_REAR8                                 0x1207
#define AL_REAR16                                0x1208
#define AL_REAR32F                               0x1209
#define AL_5POINT1_8                             0x120A
#define AL_5POINT1_16                            0x120B
#define AL_5POINT1_32F                           0x120C
#define AL_6POINT1_8                             0x120D
#define AL_6POINT1_16                            0x120E
#define AL_6POINT1_32F                           0x120F
#define AL_7POINT1_8                             0x1210
#define AL_7POINT1_16                            0x1211
#define AL_7POINT1_32F                           0x1212

/* Buffer attributes */
#define AL_INTERNAL_FORMAT                       0x2008
#define AL_BYTE_LENGTH                           0x2009
#define AL_SAMPLE_LENGTH                         0x200A
#define AL_SEC_LENGTH                            0x200B

typedef void (AL_APIENTRY*LPALBUFFERSAMPLESSOFT)(ALuint,ALuint,ALenum,ALsizei,ALenum,ALenum,const ALvoid*);
typedef void (AL_APIENTRY*LPALBUFFERSUBSAMPLESSOFT)(ALuint,ALsizei,ALsizei,ALenum,ALenum,const ALvoid*);
typedef void (AL_APIENTRY*LPALGETBUFFERSAMPLESSOFT)(ALuint,ALsizei,ALsizei,ALenum,ALenum,ALvoid*);
typedef ALboolean (AL_APIENTRY*LPALISBUFFERFORMATSUPPORTEDSOFT)(ALenum);
#endif


/* Some helper functions to get the name from the channel and type enums. */
const char *ChannelsName(ALenum chans);
const char *TypeName(ALenum type);

/* Helpers to convert frame counts and byte lengths. */
ALsizei FramesToBytes(ALsizei size, ALenum channels, ALenum type);
ALsizei BytesToFrames(ALsizei size, ALenum channels, ALenum type);

/* Retrieves a compatible buffer format given the channel configuration and
 * sample type. Returns 0 if no supported format can be found. */
ALenum GetFormat(ALenum channels, ALenum type);

/* Easy device init/deinit functions. InitAL returns 0 on success. */
int InitAL(void);
void CloseAL(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ALHELPERS_H */
