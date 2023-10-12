#ifndef INPROGEXT_H
#define INPROGEXT_H

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AL_SOFT_map_buffer
#define AL_SOFT_map_buffer 1
typedef unsigned int ALbitfieldSOFT;
#define AL_MAP_READ_BIT_SOFT                     0x00000001
#define AL_MAP_WRITE_BIT_SOFT                    0x00000002
#define AL_MAP_PERSISTENT_BIT_SOFT               0x00000004
#define AL_PRESERVE_DATA_BIT_SOFT                0x00000008
typedef void (AL_APIENTRY*LPALBUFFERSTORAGESOFT)(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags) AL_API_NOEXCEPT17;
typedef void* (AL_APIENTRY*LPALMAPBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALUNMAPBUFFERSOFT)(ALuint buffer) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALFLUSHMAPPEDBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALBUFFERSTORAGEDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags) AL_API_NOEXCEPT17;
typedef void* (AL_APIENTRY*LPALMAPBUFFERDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALUNMAPBUFFERDIRECTSOFT)(ALCcontext *context, ALuint buffer) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALFLUSHMAPPEDBUFFERDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALsizei offset, ALsizei length) AL_API_NOEXCEPT17;
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alBufferStorageSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags) AL_API_NOEXCEPT;
AL_API void* AL_APIENTRY alMapBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alUnmapBufferSOFT(ALuint buffer) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alFlushMappedBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferStorageDirectSOFT(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags) AL_API_NOEXCEPT;
void* AL_APIENTRY alMapBufferDirectSOFT(ALCcontext *context, ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access) AL_API_NOEXCEPT;
void AL_APIENTRY alUnmapBufferDirectSOFT(ALCcontext *context, ALuint buffer) AL_API_NOEXCEPT;
void AL_APIENTRY alFlushMappedBufferDirectSOFT(ALCcontext *context, ALuint buffer, ALsizei offset, ALsizei length) AL_API_NOEXCEPT;
#endif
#endif

#ifndef AL_SOFT_bformat_hoa
#define AL_SOFT_bformat_hoa
#define AL_UNPACK_AMBISONIC_ORDER_SOFT           0x199D
#endif

#ifndef AL_SOFT_convolution_effect
#define AL_SOFT_convolution_effect
#define AL_EFFECT_CONVOLUTION_SOFT               0xA000
#define AL_CONVOLUTION_ORIENTATION_SOFT          0x100F /* same as AL_ORIENTATION */
#define AL_EFFECTSLOT_STATE_SOFT                 0x199E
typedef void (AL_APIENTRY*LPALAUXILIARYEFFECTSLOTPLAYSOFT)(ALuint slotid) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALAUXILIARYEFFECTSLOTPLAYVSOFT)(ALsizei n, const ALuint *slotids) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALAUXILIARYEFFECTSLOTSTOPSOFT)(ALuint slotid) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALAUXILIARYEFFECTSLOTSTOPVSOFT)(ALsizei n, const ALuint *slotids) AL_API_NOEXCEPT17;
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alAuxiliaryEffectSlotPlaySOFT(ALuint slotid) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alAuxiliaryEffectSlotPlayvSOFT(ALsizei n, const ALuint *slotids) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alAuxiliaryEffectSlotStopSOFT(ALuint slotid) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alAuxiliaryEffectSlotStopvSOFT(ALsizei n, const ALuint *slotids) AL_API_NOEXCEPT;
#endif
#endif

#ifndef AL_SOFT_hold_on_disconnect
#define AL_SOFT_hold_on_disconnect
#define AL_STOP_SOURCES_ON_DISCONNECT_SOFT       0x19AB
#endif


#ifndef AL_EXT_direct_context
#define AL_EXT_direct_context
typedef ALCvoid* (ALC_APIENTRY *LPALCGETPROCADDRESS2)(ALCdevice *device, const ALCchar *funcname) AL_API_NOEXCEPT17;

typedef void (AL_APIENTRY *LPALENABLEDIRECT)(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALDISABLEDIRECT)(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT17;
typedef ALboolean (AL_APIENTRY *LPALISENABLEDDIRECT)(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALDOPPLERFACTORDIRECT)(ALCcontext *context, ALfloat value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSPEEDOFSOUNDDIRECT)(ALCcontext *context, ALfloat value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALDISTANCEMODELDIRECT)(ALCcontext *context, ALenum distanceModel) AL_API_NOEXCEPT17;
typedef const ALchar* (AL_APIENTRY *LPALGETSTRINGDIRECT)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBOOLEANVDIRECT)(ALCcontext *context, ALenum param, ALboolean *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETINTEGERVDIRECT)(ALCcontext *context, ALenum param, ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETFLOATVDIRECT)(ALCcontext *context, ALenum param, ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETDOUBLEVDIRECT)(ALCcontext *context, ALenum param, ALdouble *values) AL_API_NOEXCEPT17;
typedef ALboolean (AL_APIENTRY *LPALGETBOOLEANDIRECT)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT17;
typedef ALint (AL_APIENTRY *LPALGETINTEGERDIRECT)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT17;
typedef ALfloat (AL_APIENTRY *LPALGETFLOATDIRECT)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT17;
typedef ALdouble (AL_APIENTRY *LPALGETDOUBLEDIRECT)(ALCcontext *context, ALenum param) AL_API_NOEXCEPT17;
typedef ALenum (AL_APIENTRY *LPALGETERRORDIRECT)(ALCcontext *context) AL_API_NOEXCEPT17;
typedef ALboolean (AL_APIENTRY *LPALISEXTENSIONPRESENTDIRECT)(ALCcontext *context, const ALchar *extname) AL_API_NOEXCEPT17;
typedef void* (AL_APIENTRY *LPALGETPROCADDRESSDIRECT)(ALCcontext *context, const ALchar *fname) AL_API_NOEXCEPT17;
typedef ALenum (AL_APIENTRY *LPALGETENUMVALUEDIRECT)(ALCcontext *context, const ALchar *ename) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALLISTENERFDIRECT)(ALCcontext *context, ALenum param, ALfloat value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALLISTENER3FDIRECT)(ALCcontext *context, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALLISTENERFVDIRECT)(ALCcontext *context, ALenum param, const ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALLISTENERIDIRECT)(ALCcontext *context, ALenum param, ALint value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALLISTENER3IDIRECT)(ALCcontext *context, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALLISTENERIVDIRECT)(ALCcontext *context, ALenum param, const ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETLISTENERFDIRECT)(ALCcontext *context, ALenum param, ALfloat *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETLISTENER3FDIRECT)(ALCcontext *context, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETLISTENERFVDIRECT)(ALCcontext *context, ALenum param, ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETLISTENERIDIRECT)(ALCcontext *context, ALenum param, ALint *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETLISTENER3IDIRECT)(ALCcontext *context, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETLISTENERIVDIRECT)(ALCcontext *context, ALenum param, ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGENSOURCESDIRECT)(ALCcontext *context, ALsizei n, ALuint *sources) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALDELETESOURCESDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT17;
typedef ALboolean (AL_APIENTRY *LPALISSOURCEDIRECT)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEFDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCE3FDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEFVDIRECT)(ALCcontext *context, ALuint source, ALenum param, const ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEIDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCE3IDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEIVDIRECT)(ALCcontext *context, ALuint source, ALenum param, const ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCEFDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCE3FDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCEFVDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCEIDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCE3IDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCEIVDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEPLAYVDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCESTOPVDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEREWINDVDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEPAUSEVDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEPLAYDIRECT)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCESTOPDIRECT)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEREWINDDIRECT)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEPAUSEDIRECT)(ALCcontext *context, ALuint source) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEQUEUEBUFFERSDIRECT)(ALCcontext *context, ALuint source, ALsizei nb, const ALuint *buffers) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEUNQUEUEBUFFERSDIRECT)(ALCcontext *context, ALuint source, ALsizei nb, ALuint *buffers) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGENBUFFERSDIRECT)(ALCcontext *context, ALsizei n, ALuint *buffers) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALDELETEBUFFERSDIRECT)(ALCcontext *context, ALsizei n, const ALuint *buffers) AL_API_NOEXCEPT17;
typedef ALboolean (AL_APIENTRY *LPALISBUFFERDIRECT)(ALCcontext *context, ALuint buffer) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALBUFFERDATADIRECT)(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei samplerate) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALBUFFERFDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALBUFFER3FDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALBUFFERFVDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, const ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALBUFFERIDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALBUFFER3IDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALBUFFERIVDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, const ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBUFFERFDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBUFFER3FDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBUFFERFVDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBUFFERIDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBUFFER3IDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBUFFERIVDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint *values) AL_API_NOEXCEPT17;
/* ALC_EXT_EFX */
typedef void (AL_APIENTRY *LPALGENEFFECTSDIRECT)(ALCcontext *context, ALsizei n, ALuint *effects) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALDELETEEFFECTSDIRECT)(ALCcontext *context, ALsizei n, const ALuint *effects) AL_API_NOEXCEPT17;
typedef ALboolean (AL_APIENTRY *LPALISEFFECTDIRECT)(ALCcontext *context, ALuint effect) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALEFFECTIDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALint value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALEFFECTIVDIRECT)(ALCcontext *context, ALuint effect, ALenum param, const ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALEFFECTFDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALfloat value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALEFFECTFVDIRECT)(ALCcontext *context, ALuint effect, ALenum param, const ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETEFFECTIDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALint *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETEFFECTIVDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETEFFECTFDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALfloat *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETEFFECTFVDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGENFILTERSDIRECT)(ALCcontext *context, ALsizei n, ALuint *filters) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALDELETEFILTERSDIRECT)(ALCcontext *context, ALsizei n, const ALuint *filters) AL_API_NOEXCEPT17;
typedef ALboolean (AL_APIENTRY *LPALISFILTERDIRECT)(ALCcontext *context, ALuint filter) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALFILTERIDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALint value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALFILTERIVDIRECT)(ALCcontext *context, ALuint filter, ALenum param, const ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALFILTERFDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALfloat value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALFILTERFVDIRECT)(ALCcontext *context, ALuint filter, ALenum param, const ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETFILTERIDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALint *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETFILTERIVDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETFILTERFDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALfloat *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETFILTERFVDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGENAUXILIARYEFFECTSLOTSDIRECT)(ALCcontext *context, ALsizei n, ALuint *effectslots) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALDELETEAUXILIARYEFFECTSLOTSDIRECT)(ALCcontext *context, ALsizei n, const ALuint *effectslots) AL_API_NOEXCEPT17;
typedef ALboolean (AL_APIENTRY *LPALISAUXILIARYEFFECTSLOTDIRECT)(ALCcontext *context, ALuint effectslot) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTIDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALint value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTIVDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, const ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTFDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTFVDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, const ALfloat *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTIDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALint *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTIVDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALint *values) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTFDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTFVDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *values) AL_API_NOEXCEPT17;
/* AL_EXT_BUFFER_DATA_STATIC */
typedef void (AL_APIENTRY *LPALBUFFERDATASTATICDIRECT)(ALCcontext *context, ALuint buffer, ALenum format, ALvoid *data, ALsizei size, ALsizei freq) AL_API_NOEXCEPT17;
/* AL_EXT_debug */
typedef void (AL_APIENTRY*LPALDEBUGMESSAGECALLBACKDIRECTEXT)(ALCcontext *context, ALDEBUGPROCEXT callback, void *userParam) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALDEBUGMESSAGEINSERTDIRECTEXT)(ALCcontext *context, ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALDEBUGMESSAGECONTROLDIRECTEXT)(ALCcontext *context, ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALPUSHDEBUGGROUPDIRECTEXT)(ALCcontext *context, ALenum source, ALuint id, ALsizei length, const ALchar *message) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALPOPDEBUGGROUPDIRECTEXT)(ALCcontext *context) AL_API_NOEXCEPT17;
typedef ALuint (AL_APIENTRY*LPALGETDEBUGMESSAGELOGDIRECTEXT)(ALCcontext *context, ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALOBJECTLABELDIRECTEXT)(ALCcontext *context, ALenum identifier, ALuint name, ALsizei length, const ALchar *label) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY*LPALGETOBJECTLABELDIRECTEXT)(ALCcontext *context, ALenum identifier, ALuint name, ALsizei bufSize, ALsizei *length, ALchar *label) AL_API_NOEXCEPT17;
/* AL_EXT_FOLDBACK */
typedef void (AL_APIENTRY *LPALREQUESTFOLDBACKSTARTDIRECT)(ALCcontext *context, ALenum mode, ALsizei count, ALsizei length, ALfloat *mem, LPALFOLDBACKCALLBACK callback) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALREQUESTFOLDBACKSTOPDIRECT)(ALCcontext *context) AL_API_NOEXCEPT17;
/* AL_SOFT_buffer_sub_data */
typedef void (AL_APIENTRY *LPALBUFFERSUBDATADIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length) AL_API_NOEXCEPT17;
/* AL_SOFT_source_latency */
typedef void (AL_APIENTRY *LPALSOURCEDDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCE3DDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble,ALdouble,ALdouble) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEDVDIRECTSOFT)(ALCcontext*,ALuint,ALenum,const ALdouble*) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCEDDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble*) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCE3DDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble*,ALdouble*,ALdouble*) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCEDVDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble*) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEI64DIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCE3I64DIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT,ALint64SOFT,ALint64SOFT) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEI64VDIRECTSOFT)(ALCcontext*,ALuint,ALenum,const ALint64SOFT*) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCEI64DIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT*) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCE3I64DIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT*,ALint64SOFT*,ALint64SOFT*) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETSOURCEI64VDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT*) AL_API_NOEXCEPT17;
/* AL_SOFT_deferred_updates */
typedef void (AL_APIENTRY *LPALDEFERUPDATESDIRECTSOFT)(ALCcontext *context) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALPROCESSUPDATESDIRECTSOFT)(ALCcontext *context) AL_API_NOEXCEPT17;
/* AL_SOFT_source_resampler */
typedef const ALchar* (AL_APIENTRY *LPALGETSTRINGIDIRECTSOFT)(ALCcontext *context, ALenum pname, ALsizei index) AL_API_NOEXCEPT17;
/* AL_SOFT_events */
typedef void (AL_APIENTRY *LPALEVENTCONTROLDIRECTSOFT)(ALCcontext *context, ALsizei count, const ALenum *types, ALboolean enable) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALEVENTCALLBACKDIRECTSOFT)(ALCcontext *context, ALEVENTPROCSOFT callback, void *userParam) AL_API_NOEXCEPT17;
typedef void* (AL_APIENTRY *LPALGETPOINTERDIRECTSOFT)(ALCcontext *context, ALenum pname) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETPOINTERVDIRECTSOFT)(ALCcontext *context, ALenum pname, void **values) AL_API_NOEXCEPT17;
/* AL_SOFT_callback_buffer */
typedef void (AL_APIENTRY *LPALBUFFERCALLBACKDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum format, ALsizei freq, ALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBUFFERPTRDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **value) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBUFFER3PTRDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **value1, ALvoid **value2, ALvoid **value3) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALGETBUFFERPTRVDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **values) AL_API_NOEXCEPT17;
/* AL_SOFT_source_start_delay */
typedef void (AL_APIENTRY *LPALSOURCEPLAYATTIMEDIRECTSOFT)(ALCcontext *context, ALuint source, ALint64SOFT start_time) AL_API_NOEXCEPT17;
typedef void (AL_APIENTRY *LPALSOURCEPLAYATTIMEVDIRECTSOFT)(ALCcontext *context, ALsizei n, const ALuint *sources, ALint64SOFT start_time) AL_API_NOEXCEPT17;
/* EAX */
typedef ALenum (AL_APIENTRY *LPEAXSETDIRECT)(ALCcontext *context, const struct _GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid *property_buffer, ALuint property_size) AL_API_NOEXCEPT17;
typedef ALenum (AL_APIENTRY *LPEAXGETDIRECT)(ALCcontext *context, const struct _GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid *property_value, ALuint property_value_size) AL_API_NOEXCEPT17;
typedef ALboolean (AL_APIENTRY *LPEAXSETBUFFERMODEDIRECT)(ALCcontext *context, ALsizei n, const ALuint *buffers, ALint value) AL_API_NOEXCEPT17;
typedef ALenum (AL_APIENTRY *LPEAXGETBUFFERMODEDIRECT)(ALCcontext *context, ALuint buffer, ALint *pReserved) AL_API_NOEXCEPT17;
#ifdef AL_ALEXT_PROTOTYPES
ALCvoid* AL_APIENTRY alcGetProcAddress2(ALCdevice *device, const ALchar *funcName) AL_API_NOEXCEPT;

void AL_APIENTRY alEnableDirect(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT;
void AL_APIENTRY alDisableDirect(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT;
ALboolean AL_APIENTRY alIsEnabledDirect(ALCcontext *context, ALenum capability) AL_API_NOEXCEPT;

void AL_APIENTRY alDopplerFactorDirect(ALCcontext *context, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alSpeedOfSoundDirect(ALCcontext *context, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alDistanceModelDirect(ALCcontext *context, ALenum distanceModel) AL_API_NOEXCEPT;

const ALchar* AL_APIENTRY alGetStringDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBooleanvDirect(ALCcontext *context, ALenum param, ALboolean *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetIntegervDirect(ALCcontext *context, ALenum param, ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFloatvDirect(ALCcontext *context, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetDoublevDirect(ALCcontext *context, ALenum param, ALdouble *values) AL_API_NOEXCEPT;
ALboolean AL_APIENTRY alGetBooleanDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT;
ALint AL_APIENTRY alGetIntegerDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT;
ALfloat AL_APIENTRY alGetFloatDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT;
ALdouble AL_APIENTRY alGetDoubleDirect(ALCcontext *context, ALenum param) AL_API_NOEXCEPT;

ALenum AL_APIENTRY alGetErrorDirect(ALCcontext *context) AL_API_NOEXCEPT;
ALboolean AL_APIENTRY alIsExtensionPresentDirect(ALCcontext *context, const ALchar *extname) AL_API_NOEXCEPT;
void* AL_APIENTRY alGetProcAddressDirect(ALCcontext *context, const ALchar *fname) AL_API_NOEXCEPT;
ALenum AL_APIENTRY alGetEnumValueDirect(ALCcontext *context, const ALchar *ename) AL_API_NOEXCEPT;

void AL_APIENTRY alListenerfDirect(ALCcontext *context, ALenum param, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alListener3fDirect(ALCcontext *context, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
void AL_APIENTRY alListenerfvDirect(ALCcontext *context, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alListeneriDirect(ALCcontext *context, ALenum param, ALint value) AL_API_NOEXCEPT;
void AL_APIENTRY alListener3iDirect(ALCcontext *context, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
void AL_APIENTRY alListenerivDirect(ALCcontext *context, ALenum param, const ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListenerfDirect(ALCcontext *context, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListener3fDirect(ALCcontext *context, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListenerfvDirect(ALCcontext *context, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListeneriDirect(ALCcontext *context, ALenum param, ALint *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListener3iDirect(ALCcontext *context, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetListenerivDirect(ALCcontext *context, ALenum param, ALint *values) AL_API_NOEXCEPT;

void AL_APIENTRY alGenSourcesDirect(ALCcontext *context, ALsizei n, ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteSourcesDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
ALboolean AL_APIENTRY alIsSourceDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcefDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alSource3fDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcefvDirect(ALCcontext *context, ALuint source, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceiDirect(ALCcontext *context, ALuint source, ALenum param, ALint value) AL_API_NOEXCEPT;
void AL_APIENTRY alSource3iDirect(ALCcontext *context, ALuint source, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceivDirect(ALCcontext *context, ALuint source, ALenum param, const ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcefDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSource3fDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcefvDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourceiDirect(ALCcontext *context, ALuint source,  ALenum param, ALint *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSource3iDirect(ALCcontext *context, ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourceivDirect(ALCcontext *context, ALuint source,  ALenum param, ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePlayDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceStopDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceRewindDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePauseDirect(ALCcontext *context, ALuint source) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePlayvDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceStopvDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceRewindvDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePausevDirect(ALCcontext *context, ALsizei n, const ALuint *sources) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceQueueBuffersDirect(ALCcontext *context, ALuint source, ALsizei nb, const ALuint *buffers) AL_API_NOEXCEPT;
void AL_APIENTRY alSourceUnqueueBuffersDirect(ALCcontext *context, ALuint source, ALsizei nb, ALuint *buffers) AL_API_NOEXCEPT;

void AL_APIENTRY alGenBuffersDirect(ALCcontext *context, ALsizei n, ALuint *buffers) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteBuffersDirect(ALCcontext *context, ALsizei n, const ALuint *buffers) AL_API_NOEXCEPT;
ALboolean AL_APIENTRY alIsBufferDirect(ALCcontext *context, ALuint buffer) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferDataDirect(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei samplerate) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferfDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value) AL_API_NOEXCEPT;
void AL_APIENTRY alBuffer3fDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferfvDirect(ALCcontext *context, ALuint buffer, ALenum param, const ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferiDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint value) AL_API_NOEXCEPT;
void AL_APIENTRY alBuffer3iDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3) AL_API_NOEXCEPT;
void AL_APIENTRY alBufferivDirect(ALCcontext *context, ALuint buffer, ALenum param, const ALint *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferfDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBuffer3fDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferfvDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferiDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBuffer3iDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferivDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint *values) AL_API_NOEXCEPT;

void AL_APIENTRY alGenEffectsDirect(ALCcontext *context, ALsizei n, ALuint *effects) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteEffectsDirect(ALCcontext *context, ALsizei n, const ALuint *effects) AL_API_NOEXCEPT;
ALboolean AL_APIENTRY alIsEffectDirect(ALCcontext *context, ALuint effect) AL_API_NOEXCEPT;
void AL_APIENTRY alEffectiDirect(ALCcontext *context, ALuint effect, ALenum param, ALint iValue) AL_API_NOEXCEPT;
void AL_APIENTRY alEffectivDirect(ALCcontext *context, ALuint effect, ALenum param, const ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alEffectfDirect(ALCcontext *context, ALuint effect, ALenum param, ALfloat flValue) AL_API_NOEXCEPT;
void AL_APIENTRY alEffectfvDirect(ALCcontext *context, ALuint effect, ALenum param, const ALfloat *pflValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetEffectiDirect(ALCcontext *context, ALuint effect, ALenum param, ALint *piValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetEffectivDirect(ALCcontext *context, ALuint effect, ALenum param, ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetEffectfDirect(ALCcontext *context, ALuint effect, ALenum param, ALfloat *pflValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetEffectfvDirect(ALCcontext *context, ALuint effect, ALenum param, ALfloat *pflValues) AL_API_NOEXCEPT;

void AL_APIENTRY alGenFiltersDirect(ALCcontext *context, ALsizei n, ALuint *filters) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteFiltersDirect(ALCcontext *context, ALsizei n, const ALuint *filters) AL_API_NOEXCEPT;
ALboolean AL_APIENTRY alIsFilterDirect(ALCcontext *context, ALuint filter) AL_API_NOEXCEPT;
void AL_APIENTRY alFilteriDirect(ALCcontext *context, ALuint filter, ALenum param, ALint iValue) AL_API_NOEXCEPT;
void AL_APIENTRY alFilterivDirect(ALCcontext *context, ALuint filter, ALenum param, const ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alFilterfDirect(ALCcontext *context, ALuint filter, ALenum param, ALfloat flValue) AL_API_NOEXCEPT;
void AL_APIENTRY alFilterfvDirect(ALCcontext *context, ALuint filter, ALenum param, const ALfloat *pflValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFilteriDirect(ALCcontext *context, ALuint filter, ALenum param, ALint *piValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFilterivDirect(ALCcontext *context, ALuint filter, ALenum param, ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFilterfDirect(ALCcontext *context, ALuint filter, ALenum param, ALfloat *pflValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetFilterfvDirect(ALCcontext *context, ALuint filter, ALenum param, ALfloat *pflValues) AL_API_NOEXCEPT;

void AL_APIENTRY alGenAuxiliaryEffectSlotsDirect(ALCcontext *context, ALsizei n, ALuint *effectslots) AL_API_NOEXCEPT;
void AL_APIENTRY alDeleteAuxiliaryEffectSlotsDirect(ALCcontext *context, ALsizei n, const ALuint *effectslots) AL_API_NOEXCEPT;
ALboolean AL_APIENTRY alIsAuxiliaryEffectSlotDirect(ALCcontext *context, ALuint effectslot) AL_API_NOEXCEPT;
void AL_APIENTRY alAuxiliaryEffectSlotiDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALint iValue) AL_API_NOEXCEPT;
void AL_APIENTRY alAuxiliaryEffectSlotivDirect(ALCcontext *context, ALuint effectslot, ALenum param, const ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alAuxiliaryEffectSlotfDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat flValue) AL_API_NOEXCEPT;
void AL_APIENTRY alAuxiliaryEffectSlotfvDirect(ALCcontext *context, ALuint effectslot, ALenum param, const ALfloat *pflValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetAuxiliaryEffectSlotiDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALint *piValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetAuxiliaryEffectSlotivDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALint *piValues) AL_API_NOEXCEPT;
void AL_APIENTRY alGetAuxiliaryEffectSlotfDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *pflValue) AL_API_NOEXCEPT;
void AL_APIENTRY alGetAuxiliaryEffectSlotfvDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *pflValues) AL_API_NOEXCEPT;

void AL_APIENTRY alBufferDataStaticDirect(ALCcontext *context, ALuint buffer, ALenum format, ALvoid *data, ALsizei size, ALsizei freq) AL_API_NOEXCEPT;

void AL_APIENTRY alDebugMessageCallbackDirectEXT(ALCcontext *context, ALDEBUGPROCEXT callback, void *userParam)  AL_API_NOEXCEPT;
void AL_APIENTRY alDebugMessageInsertDirectEXT(ALCcontext *context, ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message)  AL_API_NOEXCEPT;
void AL_APIENTRY alDebugMessageControlDirectEXT(ALCcontext *context, ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) AL_API_NOEXCEPT;
void AL_APIENTRY alPushDebugGroupDirectEXT(ALCcontext *context, ALenum source, ALuint id, ALsizei length, const ALchar *message) AL_API_NOEXCEPT;
void AL_APIENTRY alPopDebugGroupDirectEXT(ALCcontext *context) AL_API_NOEXCEPT;
ALuint AL_APIENTRY alGetDebugMessageLogDirectEXT(ALCcontext *context, ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf) AL_API_NOEXCEPT;
void AL_APIENTRY alObjectLabelDirectEXT(ALCcontext *context, ALenum identifier, ALuint name, ALsizei length, const ALchar *label) AL_API_NOEXCEPT;
void AL_APIENTRY alGetObjectLabelDirectEXT(ALCcontext *context, ALenum identifier, ALuint name, ALsizei bufSize, ALsizei *length, ALchar *label) AL_API_NOEXCEPT;

void AL_APIENTRY alRequestFoldbackStartDirect(ALCcontext *context, ALenum mode, ALsizei count, ALsizei length, ALfloat *mem, LPALFOLDBACKCALLBACK callback) AL_API_NOEXCEPT;
void AL_APIENTRY alRequestFoldbackStopDirect(ALCcontext *context) AL_API_NOEXCEPT;

void AL_APIENTRY alBufferSubDataDirectSOFT(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length) AL_API_NOEXCEPT;

void AL_APIENTRY alSourcedDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble value) AL_API_NOEXCEPT;
void AL_APIENTRY alSource3dDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble value1, ALdouble value2, ALdouble value3) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcedvDirectSOFT(ALCcontext *context, ALuint source, ALenum param, const ALdouble *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcedDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSource3dDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble *value1, ALdouble *value2, ALdouble *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcedvDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble *values) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcei64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT value) AL_API_NOEXCEPT;
void AL_APIENTRY alSource3i64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT value1, ALint64SOFT value2, ALint64SOFT value3) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcei64vDirectSOFT(ALCcontext *context, ALuint source, ALenum param, const ALint64SOFT *values) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcei64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT *value) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSource3i64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT *value1, ALint64SOFT *value2, ALint64SOFT *value3) AL_API_NOEXCEPT;
void AL_APIENTRY alGetSourcei64vDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT *values) AL_API_NOEXCEPT;

void AL_APIENTRY alDeferUpdatesDirectSOFT(ALCcontext *context) AL_API_NOEXCEPT;
void AL_APIENTRY alProcessUpdatesDirectSOFT(ALCcontext *context) AL_API_NOEXCEPT;

const ALchar* AL_APIENTRY alGetStringiDirectSOFT(ALCcontext *context, ALenum pname, ALsizei index) AL_API_NOEXCEPT;

void AL_APIENTRY alEventControlDirectSOFT(ALCcontext *context, ALsizei count, const ALenum *types, ALboolean enable) AL_API_NOEXCEPT;
void AL_APIENTRY alEventCallbackDirectSOFT(ALCcontext *context, ALEVENTPROCSOFT callback, void *userParam) AL_API_NOEXCEPT;
void* AL_APIENTRY alGetPointerDirectSOFT(ALCcontext *context, ALenum pname) AL_API_NOEXCEPT;
void AL_APIENTRY alGetPointervDirectSOFT(ALCcontext *context, ALenum pname, void **values) AL_API_NOEXCEPT;

void AL_APIENTRY alBufferCallbackDirectSOFT(ALCcontext *context, ALuint buffer, ALenum format, ALsizei freq, ALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferPtrDirectSOFT(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **ptr) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBuffer3PtrDirectSOFT(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **ptr0, ALvoid **ptr1, ALvoid **ptr2) AL_API_NOEXCEPT;
void AL_APIENTRY alGetBufferPtrvDirectSOFT(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **ptr) AL_API_NOEXCEPT;

void AL_APIENTRY alSourcePlayAtTimeDirectSOFT(ALCcontext *context, ALuint source, ALint64SOFT start_time) AL_API_NOEXCEPT;
void AL_APIENTRY alSourcePlayAtTimevDirectSOFT(ALCcontext *context, ALsizei n, const ALuint *sources, ALint64SOFT start_time) AL_API_NOEXCEPT;

ALenum AL_APIENTRY EAXSetDirect(ALCcontext *context, const struct _GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid *property_value, ALuint property_value_size) AL_API_NOEXCEPT;
ALenum AL_APIENTRY EAXGetDirect(ALCcontext *context, const struct _GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid *property_value, ALuint property_value_size) AL_API_NOEXCEPT;
ALboolean AL_APIENTRY EAXSetBufferModeDirect(ALCcontext *context, ALsizei n, const ALuint *buffers, ALint value) AL_API_NOEXCEPT;
ALenum AL_APIENTRY EAXGetBufferModeDirect(ALCcontext *context, ALuint buffer, ALint *pReserved) AL_API_NOEXCEPT;
#endif
#endif

/* Non-standard exports. Not part of any extension. */
AL_API const ALchar* AL_APIENTRY alsoft_get_version(void) noexcept;

typedef void (ALC_APIENTRY*LPALSOFTLOGCALLBACK)(void *userptr, char level, const char *message, int length) noexcept;
void ALC_APIENTRY alsoft_set_log_callback(LPALSOFTLOGCALLBACK callback, void *userptr) noexcept;

/* Functions from abandoned extensions. Only here for binary compatibility. */
AL_API void AL_APIENTRY alSourceQueueBufferLayersSOFT(ALuint src, ALsizei nb,
    const ALuint *buffers) noexcept;

AL_API ALint64SOFT AL_APIENTRY alGetInteger64SOFT(ALenum pname) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetInteger64vSOFT(ALenum pname, ALint64SOFT *values) AL_API_NOEXCEPT;
ALint64SOFT AL_APIENTRY alGetInteger64DirectSOFT(ALCcontext *context, ALenum pname) AL_API_NOEXCEPT;
void AL_APIENTRY alGetInteger64vDirectSOFT(ALCcontext *context, ALenum pname, ALint64SOFT *values) AL_API_NOEXCEPT;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* INPROGEXT_H */
