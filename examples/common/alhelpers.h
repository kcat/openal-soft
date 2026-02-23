#ifndef ALHELPERS_H
#define ALHELPERS_H

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ALC_EXT_EFX */
extern LPALGENFILTERS alGenFilters;
extern LPALDELETEFILTERS alDeleteFilters;
extern LPALISFILTER alIsFilter;
extern LPALFILTERI alFilteri;
extern LPALFILTERIV alFilteriv;
extern LPALFILTERF alFilterf;
extern LPALFILTERFV alFilterfv;
extern LPALGETFILTERI alGetFilteri;
extern LPALGETFILTERIV alGetFilteriv;
extern LPALGETFILTERF alGetFilterf;
extern LPALGETFILTERFV alGetFilterfv;
extern LPALGENEFFECTS alGenEffects;
extern LPALDELETEEFFECTS alDeleteEffects;
extern LPALISEFFECT alIsEffect;
extern LPALEFFECTI alEffecti;
extern LPALEFFECTIV alEffectiv;
extern LPALEFFECTF alEffectf;
extern LPALEFFECTFV alEffectfv;
extern LPALGETEFFECTI alGetEffecti;
extern LPALGETEFFECTIV alGetEffectiv;
extern LPALGETEFFECTF alGetEffectf;
extern LPALGETEFFECTFV alGetEffectfv;
extern LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
extern LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
extern LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
extern LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
extern LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
extern LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
extern LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
extern LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
extern LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
extern LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
extern LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;

/* AL_EXT_debug */
extern LPALDEBUGMESSAGECALLBACKEXT alDebugMessageCallbackEXT;
extern LPALDEBUGMESSAGEINSERTEXT alDebugMessageInsertEXT;
extern LPALDEBUGMESSAGECONTROLEXT alDebugMessageControlEXT;
extern LPALPUSHDEBUGGROUPEXT alPushDebugGroupEXT;
extern LPALPOPDEBUGGROUPEXT alPopDebugGroupEXT;
extern LPALGETDEBUGMESSAGELOGEXT alGetDebugMessageLogEXT;
extern LPALOBJECTLABELEXT alObjectLabelEXT;
extern LPALGETOBJECTLABELEXT alGetObjectLabelEXT;
extern LPALGETPOINTEREXT alGetPointerEXT;
extern LPALGETPOINTERVEXT alGetPointervEXT;

/* AL_SOFT_source_latency */
extern LPALSOURCEDSOFT alSourcedSOFT;
extern LPALSOURCE3DSOFT alSource3dSOFT;
extern LPALSOURCEDVSOFT alSourcedvSOFT;
extern LPALGETSOURCEDSOFT alGetSourcedSOFT;
extern LPALGETSOURCE3DSOFT alGetSource3dSOFT;
extern LPALGETSOURCEDVSOFT alGetSourcedvSOFT;
extern LPALSOURCEI64SOFT alSourcei64SOFT;
extern LPALSOURCE3I64SOFT alSource3i64SOFT;
extern LPALSOURCEI64VSOFT alSourcei64vSOFT;
extern LPALGETSOURCEI64SOFT alGetSourcei64SOFT;
extern LPALGETSOURCE3I64SOFT alGetSource3i64SOFT;
extern LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT;

/* AL_SOFT_events */
extern LPALEVENTCONTROLSOFT alEventControlSOFT;
extern LPALEVENTCALLBACKSOFT alEventCallbackSOFT;

/* AL_SOFT_callback_buffer */
extern LPALBUFFERCALLBACKSOFT alBufferCallbackSOFT;


/* Some helper functions to get the name from the format enums. */
const char *FormatName(ALenum format);

/* Easy device init/deinit functions. InitAL returns 0 on success. */
int InitAL(char ***argv, int *argc);
void CloseAL(void);

/* Load AL extension functions for the current context. */
void LoadALExtensions(void);

/* Cross-platform timeget and sleep functions. */
int altime_get(void);
void al_nssleep(unsigned long nsec);

/* C doesn't allow casting between function and non-function pointer types, so
 * with C99 we need to use a union to reinterpret the pointer type. Pre-C99
 * still needs to use a normal cast and live with the warning (C++ is fine with
 * a regular reinterpret_cast).
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define FUNCTION_CAST(T, ptr) (union{void *p; T f;}){ptr}.f
#elif defined(__cplusplus)
#define FUNCTION_CAST(T, ptr) reinterpret_cast<T>(ptr)
#else
#define FUNCTION_CAST(T, ptr) (T)(ptr)
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* ALHELPERS_H */
