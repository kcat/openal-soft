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
