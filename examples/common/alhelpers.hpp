#ifndef ALHELPERS_HPP
#define ALHELPERS_HPP

#include "config.h"

#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "fmt/base.h"
#include "fmt/ostream.h"

#if HAVE_CXXMODULES
import gsl;
import openal.alc;
import openal.ext;

#else

#include "AL/alc.h"
#include "AL/alext.h"

#include "gsl/gsl"
#endif

extern "C" {

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

/* Load AL extension functions for the current context. */
void LoadALExtensions();

} /* extern "C" */


[[nodiscard]]
inline auto InitAL(std::span<std::string_view> &args, const ALCint *attribs=nullptr)
{
    struct Handle {
        ALCdevice *mDevice{};
        ALCcontext *mContext{};

        Handle() = default;
        Handle(const Handle&) = delete;
        Handle(Handle&& rhs) noexcept
            : mDevice{std::exchange(rhs.mDevice, nullptr)}
            , mContext{std::exchange(rhs.mContext, nullptr)}
        { }
        ~Handle()
        {
            if(mContext)
                alcDestroyContext(mContext);
            if(mDevice)
                alcCloseDevice(mDevice);
        }
        auto operator=(const Handle&) -> Handle& = delete;
        auto operator=(Handle&&) -> Handle& = delete;

        auto close() -> void
        {
            if(mContext)
                alcDestroyContext(mContext);
            mContext = nullptr;
            if(mDevice)
                alcCloseDevice(mDevice);
            mDevice = nullptr;
        }

        auto printName() const -> void
        {
            auto *name = gsl::czstring{};
            if(alcIsExtensionPresent(mDevice, "ALC_ENUMERATE_ALL_EXT"))
                name = alcGetString(mDevice, ALC_ALL_DEVICES_SPECIFIER);
            if(!name || alcGetError(mDevice) != ALC_NO_ERROR)
                name = alcGetString(mDevice, ALC_DEVICE_SPECIFIER);
            fmt::println("Opened \"{}\"", name);
        }
    };
    auto hdl = Handle{};

    /* Open and initialize a device */
    if(args.size() > 1 && args[0] == "-device")
    {
        hdl.mDevice = alcOpenDevice(std::string{args[1]}.c_str());
        if(!hdl.mDevice)
            fmt::println(std::cerr, "Failed to open \"{}\", trying default", args[1]);
        args = args.subspan(2);
    }
    if(!hdl.mDevice)
        hdl.mDevice = alcOpenDevice(nullptr);
    if(!hdl.mDevice)
    {
        fmt::println(std::cerr, "Could not open a device");
        throw std::runtime_error{"Failed to open a device"};
    }

    hdl.mContext = alcCreateContext(hdl.mDevice, attribs);
    if(!hdl.mContext || alcMakeContextCurrent(hdl.mContext) == ALC_FALSE)
    {
        fmt::println(std::cerr, "Could not set a context");
        throw std::runtime_error{"Failed to initialize an OpenAL context"};
    }

    return hdl;
}

#endif /* ALHELPERS_HPP */
