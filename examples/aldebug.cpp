/*
 * OpenAL Debug Context Example
 *
 * Copyright (c) 2024 by Chris Robinson <chris.kcat@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* This file contains an example for using the debug extension. */

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "alnumeric.h"
#include "alspan.h"
#include "fmt/core.h"

#include "win_main_utf8.h"

namespace {

using namespace std::string_view_literals;

struct DeviceCloser {
    void operator()(ALCdevice *device) const noexcept { alcCloseDevice(device); }
};
using DevicePtr = std::unique_ptr<ALCdevice,DeviceCloser>;

struct ContextDestroyer {
    void operator()(ALCcontext *context) const noexcept { alcDestroyContext(context); }
};
using ContextPtr = std::unique_ptr<ALCcontext,ContextDestroyer>;


constexpr auto GetDebugSourceName(ALenum source) noexcept -> std::string_view
{
    switch(source)
    {
    case AL_DEBUG_SOURCE_API_EXT: return "API"sv;
    case AL_DEBUG_SOURCE_AUDIO_SYSTEM_EXT: return "Audio System"sv;
    case AL_DEBUG_SOURCE_THIRD_PARTY_EXT: return "Third Party"sv;
    case AL_DEBUG_SOURCE_APPLICATION_EXT: return "Application"sv;
    case AL_DEBUG_SOURCE_OTHER_EXT: return "Other"sv;
    }
    return "<invalid source>"sv;
}

constexpr auto GetDebugTypeName(ALenum type) noexcept -> std::string_view
{
    switch(type)
    {
    case AL_DEBUG_TYPE_ERROR_EXT: return "Error"sv;
    case AL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_EXT: return "Deprecated Behavior"sv;
    case AL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_EXT: return "Undefined Behavior"sv;
    case AL_DEBUG_TYPE_PORTABILITY_EXT: return "Portability"sv;
    case AL_DEBUG_TYPE_PERFORMANCE_EXT: return "Performance"sv;
    case AL_DEBUG_TYPE_MARKER_EXT: return "Marker"sv;
    case AL_DEBUG_TYPE_PUSH_GROUP_EXT: return "Push Group"sv;
    case AL_DEBUG_TYPE_POP_GROUP_EXT: return "Pop Group"sv;
    case AL_DEBUG_TYPE_OTHER_EXT: return "Other"sv;
    }
    return "<invalid type>"sv;
}

constexpr auto GetDebugSeverityName(ALenum severity) noexcept -> std::string_view
{
    switch(severity)
    {
    case AL_DEBUG_SEVERITY_HIGH_EXT: return "High"sv;
    case AL_DEBUG_SEVERITY_MEDIUM_EXT: return "Medium"sv;
    case AL_DEBUG_SEVERITY_LOW_EXT: return "Low"sv;
    case AL_DEBUG_SEVERITY_NOTIFICATION_EXT: return "Notification"sv;
    }
    return "<invalid severity>"sv;
}

auto alDebugMessageCallbackEXT = LPALDEBUGMESSAGECALLBACKEXT{};
auto alDebugMessageInsertEXT = LPALDEBUGMESSAGEINSERTEXT{};
auto alDebugMessageControlEXT = LPALDEBUGMESSAGECONTROLEXT{};
auto alPushDebugGroupEXT = LPALPUSHDEBUGGROUPEXT{};
auto alPopDebugGroupEXT = LPALPOPDEBUGGROUPEXT{};
auto alGetDebugMessageLogEXT = LPALGETDEBUGMESSAGELOGEXT{};
auto alObjectLabelEXT = LPALOBJECTLABELEXT{};
auto alGetObjectLabelEXT = LPALGETOBJECTLABELEXT{};
auto alGetPointerEXT = LPALGETPOINTEREXT{};
auto alGetPointervEXT = LPALGETPOINTERVEXT{};


int main(al::span<std::string_view> args)
{
    /* Print out usage if -h was specified */
    if(args.size() > 1 && (args[1] == "-h" || args[1] == "--help"))
    {
        fmt::println(stderr, "Usage: {} [-device <name>] [-nodebug]", args[0]);
        return 1;
    }

    /* Initialize OpenAL. */
    args = args.subspan(1);

    auto device = DevicePtr{};
    if(args.size() > 1 && args[0] == "-device")
    {
        device = DevicePtr{alcOpenDevice(std::string{args[1]}.c_str())};
        if(!device)
            fmt::println(stderr, "Failed to open \"{}\", trying default", args[1]);
        args = args.subspan(2);
    }
    if(!device)
        device = DevicePtr{alcOpenDevice(nullptr)};
    if(!device)
    {
        fmt::println(stderr, "Could not open a device!");
        return 1;
    }

    if(!alcIsExtensionPresent(device.get(), "ALC_EXT_debug"))
    {
        fmt::println(stderr, "ALC_EXT_debug not supported on device");
        return 1;
    }

    /* Load the Debug API functions we're using. */
#define LOAD_PROC(N) N = reinterpret_cast<decltype(N)>(alcGetProcAddress(device.get(), #N))
    LOAD_PROC(alDebugMessageCallbackEXT);
    LOAD_PROC(alDebugMessageInsertEXT);
    LOAD_PROC(alDebugMessageControlEXT);
    LOAD_PROC(alPushDebugGroupEXT);
    LOAD_PROC(alPopDebugGroupEXT);
    LOAD_PROC(alGetDebugMessageLogEXT);
    LOAD_PROC(alObjectLabelEXT);
    LOAD_PROC(alGetObjectLabelEXT);
    LOAD_PROC(alGetPointerEXT);
    LOAD_PROC(alGetPointervEXT);
#undef LOAD_PROC

    /* Create a debug context and set it as current. If -nodebug was specified,
     * create a non-debug context (to see how debug messages react).
     */
    auto flags = ALCint{ALC_CONTEXT_DEBUG_BIT_EXT};
    if(!args.empty() && args[0] == "-nodebug")
        flags &= ~ALC_CONTEXT_DEBUG_BIT_EXT;

    const auto attribs = std::array<ALCint,3>{{
        ALC_CONTEXT_FLAGS_EXT, flags,
        0 /* end-of-list */
    }};
    auto context = ContextPtr{alcCreateContext(device.get(), attribs.data())};
    if(!context || alcMakeContextCurrent(context.get()) == ALC_FALSE)
    {
        fmt::println(stderr, "Could not create and set a context!");
        return 1;
    }

    /* Enable low-severity debug messages, which are disabled by default. */
    alDebugMessageControlEXT(AL_DONT_CARE_EXT, AL_DONT_CARE_EXT, AL_DEBUG_SEVERITY_LOW_EXT, 0,
        nullptr, AL_TRUE);

    fmt::println("Context flags: {:#010x}", as_unsigned(alGetInteger(AL_CONTEXT_FLAGS_EXT)));

    /* A debug context has debug output enabled by default. But in case this
     * isn't a debug context, explicitly enable it (probably won't get much, if
     * anything, in that case).
     */
    fmt::println("Default debug state is: {}",
        alIsEnabled(AL_DEBUG_OUTPUT_EXT) ? "enabled"sv : "disabled"sv);
    alEnable(AL_DEBUG_OUTPUT_EXT);

    /* The max debug message length property will allow us to define message
     * storage of sufficient length. This includes space for the null
     * terminator.
     */
    const auto maxloglength = alGetInteger(AL_MAX_DEBUG_MESSAGE_LENGTH_EXT);
    fmt::println("Max debug message length: {}", maxloglength);

    fmt::println("");

    /* Doppler Velocity is deprecated since AL 1.1, so this should generate a
     * deprecation debug message. We'll first handle debug messages through the
     * message log, meaning we'll query for and read it afterward.
     */
    fmt::println("Calling alDopplerVelocity(0.5f)...");
    alDopplerVelocity(0.5f);

    for(auto numlogs = alGetInteger(AL_DEBUG_LOGGED_MESSAGES_EXT);numlogs > 0;--numlogs)
    {
        auto message = std::vector<char>(static_cast<ALuint>(maxloglength), '\0');
        auto source = ALenum{};
        auto type = ALenum{};
        auto id = ALuint{};
        auto severity = ALenum{};
        auto msglength = ALsizei{};

        /* Getting the message removes it from the log. */
        const auto read = alGetDebugMessageLogEXT(1, maxloglength, &source, &type, &id, &severity,
            &msglength, message.data());
        if(read != 1)
        {
            fmt::println(stderr, "Read {} debug messages, expected to read 1", read);
            break;
        }

        /* The message lengths returned by alGetDebugMessageLogEXT include the
         * null terminator, so subtract one for the string_view length. If we
         * read more than one message at a time, the length could be used as
         * the offset to the next message.
         */
        const auto msgstr = std::string_view{message.data(),
            static_cast<ALuint>(msglength ? msglength-1 : 0)};
        fmt::println("Got message from log:\n"
            "  Source: {}\n"
            "  Type: {}\n"
            "  ID: {}\n"
            "  Severity: {}\n"
            "  Message: \"{}\"", GetDebugSourceName(source), GetDebugTypeName(type), id,
            GetDebugSeverityName(severity), msgstr);
    }
    fmt::println("");

    /* Now set up a callback function. This lets us print the debug messages as
     * they happen without having to explicitly query and get them.
     */
    static constexpr auto debug_callback = [](ALenum source, ALenum type, ALuint id,
        ALenum severity, ALsizei length, const ALchar *message, void *userParam [[maybe_unused]])
        noexcept -> void
    {
        /* The message length provided to the callback does not include the
         * null terminator.
         */
        const auto msgstr = std::string_view{message, static_cast<ALuint>(length)};
        fmt::println("Got message from callback:\n"
            "  Source: {}\n"
            "  Type: {}\n"
            "  ID: {}\n"
            "  Severity: {}\n"
            "  Message: \"{}\"", GetDebugSourceName(source), GetDebugTypeName(type), id,
            GetDebugSeverityName(severity), msgstr);
    };
    alDebugMessageCallbackEXT(debug_callback, nullptr);

    if(const auto numlogs = alGetInteger(AL_DEBUG_LOGGED_MESSAGES_EXT))
        fmt::println(stderr, "{} left over logged message{}!", numlogs, (numlogs==1)?"":"s");

    /* This should also generate a deprecation debug message, which will now go
     * through the callback.
     */
    fmt::println("Calling alGetInteger(AL_DOPPLER_VELOCITY)...");
    auto dv [[maybe_unused]] = alGetInteger(AL_DOPPLER_VELOCITY);
    fmt::println("");

    /* These functions are notoriously unreliable for their behavior, they will
     * likely generate portability debug messages.
     */
    fmt::println("Calling alcSuspendContext and alcProcessContext...");
    alcSuspendContext(context.get());
    alcProcessContext(context.get());
    fputs("\n", stdout);

    fmt::println("Pushing a debug group, making some invalid calls, and popping the debug group...");
    alPushDebugGroupEXT(AL_DEBUG_SOURCE_APPLICATION_EXT, 0, -1, "Error test group");
    alSpeedOfSound(0.0f);
    /* Can't set the label of the null buffer. */
    alObjectLabelEXT(AL_BUFFER, 0, -1, "The null buffer");
    alPopDebugGroupEXT();
    fmt::println("");

    /* All done, insert a custom message and unset the callback. The context
     * and device will clean themselves up.
     */
    alDebugMessageInsertEXT(AL_DEBUG_SOURCE_APPLICATION_EXT, AL_DEBUG_TYPE_MARKER_EXT, 0,
        AL_DEBUG_SEVERITY_NOTIFICATION_EXT, -1, "End of run, cleaning up");
    alDebugMessageCallbackEXT(nullptr, nullptr);

    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    assert(argc >= 0);
    auto args = std::vector<std::string_view>(static_cast<unsigned int>(argc));
    std::copy_n(argv, args.size(), args.begin());
    return main(al::span{args});
}
