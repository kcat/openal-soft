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
import alsoft.gsl;
import openal.alc;

#else

#include "AL/alc.h"

#include "gsl/gsl"
#endif


[[nodiscard]]
inline auto InitAL(std::span<std::string_view> &args, const ALCint *attribs=nullptr)
{
    struct Handle {
        ALCdevice *device{};
        ALCcontext *context{};

        Handle() = default;
        Handle(const Handle&) = delete;
        Handle(Handle&& rhs) noexcept
            : device{std::exchange(rhs.device, nullptr)}
            , context{std::exchange(rhs.context, nullptr)}
        { }
        ~Handle()
        {
            if(context)
                alcDestroyContext(context);
            if(device)
                alcCloseDevice(device);
        }
        auto operator=(const Handle&) -> Handle& = delete;
        auto operator=(Handle&&) -> Handle& = delete;

        auto close() -> void
        {
            if(context)
                alcDestroyContext(context);
            context = nullptr;
            if(device)
                alcCloseDevice(device);
            device = nullptr;
        }

        auto printName() const -> void
        {
            auto *name = gsl::czstring{};
            if(alcIsExtensionPresent(device, "ALC_ENUMERATE_ALL_EXT"))
                name = alcGetString(device, ALC_ALL_DEVICES_SPECIFIER);
            if(!name || alcGetError(device) != ALC_NO_ERROR)
                name = alcGetString(device, ALC_DEVICE_SPECIFIER);
            fmt::println("Opened \"{}\"", name);
        }
    };
    auto hdl = Handle{};

    /* Open and initialize a device */
    if(args.size() > 1 && args[0] == "-device")
    {
        hdl.device = alcOpenDevice(std::string{args[1]}.c_str());
        if(!hdl.device)
            fmt::println(std::cerr, "Failed to open \"{}\", trying default", args[1]);
        args = args.subspan(2);
    }
    if(!hdl.device)
        hdl.device = alcOpenDevice(nullptr);
    if(!hdl.device)
    {
        fmt::println(std::cerr, "Could not open a device");
        throw std::runtime_error{"Failed to open a device"};
    }

    hdl.context = alcCreateContext(hdl.device, attribs);
    if(!hdl.context || alcMakeContextCurrent(hdl.context) == ALC_FALSE)
    {
        fmt::println(std::cerr, "Could not set a context");
        throw std::runtime_error{"Failed to initialize an OpenAL context"};
    }

    return hdl;
}

#endif /* ALHELPERS_HPP */
