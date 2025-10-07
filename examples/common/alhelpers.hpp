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

#else

#include "AL/alc.h"

#include "gsl/gsl"
#endif


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
