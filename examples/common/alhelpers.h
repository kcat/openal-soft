#ifndef ALHELPERS_H
#define ALHELPERS_H

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ALC_EXT_EFX */
extern LPALGENFILTERS palGenFilters;
extern LPALDELETEFILTERS palDeleteFilters;
extern LPALISFILTER palIsFilter;
extern LPALFILTERI palFilteri;
extern LPALFILTERIV palFilteriv;
extern LPALFILTERF palFilterf;
extern LPALFILTERFV palFilterfv;
extern LPALGETFILTERI palGetFilteri;
extern LPALGETFILTERIV palGetFilteriv;
extern LPALGETFILTERF palGetFilterf;
extern LPALGETFILTERFV palGetFilterfv;
extern LPALGENEFFECTS palGenEffects;
extern LPALDELETEEFFECTS palDeleteEffects;
extern LPALISEFFECT palIsEffect;
extern LPALEFFECTI palEffecti;
extern LPALEFFECTIV palEffectiv;
extern LPALEFFECTF palEffectf;
extern LPALEFFECTFV palEffectfv;
extern LPALGETEFFECTI palGetEffecti;
extern LPALGETEFFECTIV palGetEffectiv;
extern LPALGETEFFECTF palGetEffectf;
extern LPALGETEFFECTFV palGetEffectfv;
extern LPALGENAUXILIARYEFFECTSLOTS palGenAuxiliaryEffectSlots;
extern LPALDELETEAUXILIARYEFFECTSLOTS palDeleteAuxiliaryEffectSlots;
extern LPALISAUXILIARYEFFECTSLOT palIsAuxiliaryEffectSlot;
extern LPALAUXILIARYEFFECTSLOTI palAuxiliaryEffectSloti;
extern LPALAUXILIARYEFFECTSLOTIV palAuxiliaryEffectSlotiv;
extern LPALAUXILIARYEFFECTSLOTF palAuxiliaryEffectSlotf;
extern LPALAUXILIARYEFFECTSLOTFV palAuxiliaryEffectSlotfv;
extern LPALGETAUXILIARYEFFECTSLOTI palGetAuxiliaryEffectSloti;
extern LPALGETAUXILIARYEFFECTSLOTIV palGetAuxiliaryEffectSlotiv;
extern LPALGETAUXILIARYEFFECTSLOTF palGetAuxiliaryEffectSlotf;
extern LPALGETAUXILIARYEFFECTSLOTFV palGetAuxiliaryEffectSlotfv;

/* AL_EXT_debug */
extern LPALDEBUGMESSAGECALLBACKEXT palDebugMessageCallbackEXT;
extern LPALDEBUGMESSAGEINSERTEXT palDebugMessageInsertEXT;
extern LPALDEBUGMESSAGECONTROLEXT palDebugMessageControlEXT;
extern LPALPUSHDEBUGGROUPEXT palPushDebugGroupEXT;
extern LPALPOPDEBUGGROUPEXT palPopDebugGroupEXT;
extern LPALGETDEBUGMESSAGELOGEXT palGetDebugMessageLogEXT;
extern LPALOBJECTLABELEXT palObjectLabelEXT;
extern LPALGETOBJECTLABELEXT palGetObjectLabelEXT;
extern LPALGETPOINTEREXT palGetPointerEXT;
extern LPALGETPOINTERVEXT palGetPointervEXT;

/* AL_SOFT_source_latency */
extern LPALSOURCEDSOFT palSourcedSOFT;
extern LPALSOURCE3DSOFT palSource3dSOFT;
extern LPALSOURCEDVSOFT palSourcedvSOFT;
extern LPALGETSOURCEDSOFT palGetSourcedSOFT;
extern LPALGETSOURCE3DSOFT palGetSource3dSOFT;
extern LPALGETSOURCEDVSOFT palGetSourcedvSOFT;
extern LPALSOURCEI64SOFT palSourcei64SOFT;
extern LPALSOURCE3I64SOFT palSource3i64SOFT;
extern LPALSOURCEI64VSOFT palSourcei64vSOFT;
extern LPALGETSOURCEI64SOFT palGetSourcei64SOFT;
extern LPALGETSOURCE3I64SOFT palGetSource3i64SOFT;
extern LPALGETSOURCEI64VSOFT palGetSourcei64vSOFT;

/* AL_SOFT_events */
extern LPALEVENTCONTROLSOFT palEventControlSOFT;
extern LPALEVENTCALLBACKSOFT palEventCallbackSOFT;

/* AL_SOFT_callback_buffer */
extern LPALBUFFERCALLBACKSOFT palBufferCallbackSOFT;


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
